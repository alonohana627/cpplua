# Tables

How lua2's tables work (roadmap stage 6). This page describes the code as it is. The rules are
in [../spec/tables.md](../spec/tables.md), how the stage was planned in
[../plan/tables.md](../plan/tables.md), and the background in
[../learn/tables_metatables_errors.md](../learn/tables_metatables_errors.md). Metatables are in
[metatables.md](metatables.md).

**Status:** stage 6 is done (version 0.0.6, built together with stages 7 and 8 at 0.0.8). Tables
take keys of every type, constructors, `#`, `next`, `pairs`, `ipairs` and the raw functions
work, and `#` answers as the real Lua's does for the same operations.

## Where it lives

| File                                                  | Contents                                   |
| ----------------------------------------------------- | ------------------------------------------ |
| `lib/internal/value/table.hpp`, `lib/value/table.cpp` | `Table`: a port of Lua 5.4.9's `ltable.c`  |
| `lib/vm/handlers.inc`                                 | the table instructions' fast paths         |
| `lib/vm/execute.cpp`                                  | `finish_get`, `finish_set`: the slow paths |
| `lib/std/base.cpp`                                    | `next`, `pairs`, `ipairs`, `select`, `raw*` |
| `tests/table_test.cpp`, `tests/fixtures/my_lua/tables/` | tests: `[table]`, golden programs     |

## The layout

A `Table` is a `GcObject` of 64 bytes, like Lua's 56 plus the array's tag pointer:

- **The array part** holds the values of the keys 1..n. Like the stack, it keeps tags and payloads
  apart: one block, the 8-byte payloads first, then one tag byte per slot (`array_values()`,
  `array_tags()`), 9 bytes a slot instead of Lua's 16.
- **The hash part** is a power of two of `Node`s (24 bytes: value, key, their tags, a dead-key
  flag, and the offset to the next node of the chain), or, when empty, a shared read-only dummy
  node, as in Lua. It is a chained scatter table with Brent's variation (`new_key`): a key whose
  main position is taken by a key that is not in *its* main position takes that node, and moves
  the other one to a free node; free nodes are found by scanning down from `last_free_`.
- **The header's spare bytes** hold what Lua keeps after its `CommonHeader`: `alimit` (the array
  size, or a hint below it, see below), the log2 of the node count, and the flags: the metamethod
  cache ([metatables.md](metatables.md)) and Lua's `BITRAS`, "alimit is not the real size". They
  live in `GcObject`'s padding (`own_flags`, `own_byte`, `own_word`), so the table stays at 64
  bytes.

Reads return a `Slot`, two pointers to a value's tag and payload, or empty pointers for an
absent key (Lua's `absentkey`). A present slot may hold nil: an array slot not set yet, or a
node whose value was removed. Storing into a present slot needs no new key.

## Operations (the `ltable.c` functions, one by one)

- `get_int` (`luaH_getint`): the array part when `key - 1 < alimit`, inline; otherwise the
  "Xmilia trick" for keys between a lowered `alimit` and the real size; otherwise the hash chain
  from `key % ((nodes - 1) | 1)`.
- `get_short_string` (`luaH_getshortstr`): the chain from `hash & (nodes - 1)`, comparing
  pointers (short strings are interned); inline, for `GETFIELD`, `GETTABUP` and `SELF`.
- `get` (`luaH_get`): by key type; a float with an integral value is looked up as the integer
  (T1.3), nil is never present.
- `finish_set`, `set`, `set_int` (`luaH_finishset`, `luaH_set`, `luaH_setint`): store into the
  slot or make a new key. Every store goes through the backward barrier (`luaC_barrierback`) and
  resets the metamethod cache.
- `new_key` (`luaH_newkey`): integral floats become integers, nil values are not stored, and a
  full hash part is **rehashed**: `rehash` counts the integer keys by powers of two
  (`numusearray`, `numusehash`), picks the largest array size that is more than half full
  (`computesizes`), and `resize` rebuilds both parts.
- `length` (`luaH_getn`): a border (T3). It starts from `alimit`, checks the common cases (the
  last slot is nil but the one before is not; the slot after is nil), binary-searches the array,
  and only then the hash part (`hash_search`, doubling then bisecting). Like Lua, it may lower
  `alimit` to the border it found, as a hint for the next `#`, so `#` gives exactly the real
  Lua's answer after the same operations, even on tables with holes.
- `next_entry` (`luaH_next`): the array part in order, then the nodes in order. The key given is
  found again first (`findindex`); a key whose node was cleared still matches its dead key by
  address (T4.2); any other unknown key is `invalid key to 'next'`.

`check_key` gives `table index is nil` and `table index is NaN` (T1.4); the callers check before
storing, so `Table` itself never fails.

## In the VM

The table instructions (`GETTABUP`, `GETTABLE`, `GETI`, `GETFIELD`, `SELF`, and the `SET`
versions) do the common case inline: a table, a key of the expected type (an integer, a short
string constant), and a present value. Stores also take a present nil slot when the table has no
metatable (no `__newindex` can apply). Everything else jumps to `Index` or `NewIndex`, which
first try tables without the metamethod (a new key, an `__index` chain of tables, the common
cases of classes) and then the full `luaV_finishget` and `luaV_finishset` (`finish_get`,
`finish_set` in `execute.cpp`, M2).

- `NEWTABLE` sizes the table from the constructor (`B` is log2 of the hash size plus one, `C` the
  array size, plus `EXTRAARG` for big ones) and checks the collector.
- `SETLIST` resizes the array part once for the whole batch and writes into it directly, then
  the barrier per value (T2).
- `LEN` on a table without `__len` is `length()`.
- A generic `for` over `pairs` or `ipairs` of a plain table runs the iterator inside the VM
  (`ForIterator`): the same results as `next` and `ipairs`'s step function, without a call.

## Garbage collection

The collector traverses tables like `traversetable` ([metatables.md](metatables.md) has the weak
modes): the metatable, every array value, every node's key and value; a node whose value is nil
lets its key die (`clear_key`), so a removed key's object can be freed while `next` still finds
the node. Freeing a table gives back its array, its nodes and itself.

## Speed

On the benchmarks (`bench/`, best of 5, release build) against the real Lua 5.4.9:
`tables_array` 0.60x, `tables_hash` 0.96x, `sort` 0.84x (lua2's time over Lua's). What made the
difference, in order: present nil slots stored inline; integer and short string keys inline in
`GETTABLE` and `SETTABLE`; `pairs` and `ipairs` iterators run in the VM; the new-key path without
metamethods first; the slow paths that some programs run all the time kept with the hot code
instead of the cold section.
