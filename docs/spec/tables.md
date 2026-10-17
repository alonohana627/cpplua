# Lua 5.4 tables

The rules our tables implement (roadmap stage 6): keys, values, constructors, length and
traversal. It is based on the Lua 5.4 Reference Manual §2.1 (values and types), §3.4.7 (the
length operator), §3.4.9 (table constructors) and §6.1 (`next`, `pairs`, `ipairs`, `rawget`,
`rawset`, `rawequal`, `rawlen`, `select`), restated in our own words. Where the manual is
silent, the reference implementation (`ltable.c`, `lvm.c`, `lbaselib.c` in Lua 5.4.9) is the
tie-breaker, and those rules are marked **(impl)**.

Metamethods (`__index`, `__newindex`, `__len`, `__pairs`) are in [metatables.md](metatables.md);
this page describes tables without a metatable, and the raw operations.

Every rule has an id like `T3.1`. Tests should name the rule they check, for example
`TEST_CASE("T3.1 the length of a sequence is its size", "[table]")`.

---

## T1. Keys and values

- **T1.1** A table maps keys to values. Any value can be a key except `nil` and NaN; any value
  can be stored except `nil`. A key whose value is `nil` is absent: storing `nil` removes the
  entry, and reading an absent key gives `nil`.
- **T1.2** Tables are objects: a variable holds a reference to a table, and `{}` makes a new,
  distinct table every time it runs (E2.1).
- **T1.3** Keys are compared with raw equality (V7, without metamethods). Integer and float keys
  with the same mathematical value are the same key: `t[1]` and `t[1.0]` are one entry, and a
  float key with an integral value is stored as an integer, so `next` returns `1`, not `1.0`.
  **(impl: `luaH_newkey` and `luaH_get` convert with `luaV_flttointeger`)** Strings with the
  same bytes are the same key, whether short or long.
- **T1.4** Storing with a `nil` key is the error `table index is nil`; with a NaN key, `table
  index is NaN`. Reading `t[nil]` or `t[0/0]` is not an error: it gives `nil`. **(impl: the
  messages of `luaH_newkey` in 5.4.9, with the position of E8.1)**
- **T1.5** Indexing a value that is not a table (reading or writing a field, calling a method)
  is an error, `attempt to index a X value`, with variable information (E8.2), unless a
  metamethod handles it (M2).

## T2. Constructors

- **T2.1** `{}` evaluates its fields left to right. Positional fields (`{a, b}`) get the integer
  keys 1, 2, 3, ... in order; `name = v` stores under the string `"name"`; `[k] = v` stores under
  the value of `k`, with T1.4's errors.
- **T2.2** If the last field is a call or `...` without parentheses, all its values become
  positional fields (E3.2); anywhere else it gives one value.
- **T2.3** A positional `nil` leaves a hole: `{1, nil, 3}` has keys 1 and 3.
- **T2.4** When a positional field and a keyed field use the same key, the positional one wins
  if it is stored later. **(impl: positional values are stored in batches of 50
  (`LFIELDS_PER_FLUSH`) after the batch is evaluated, so `{[1] = "a", "b"}` has `t[1] == "b"`)**

## T3. Length

- **T3.1** `#t` on a table without `__len` is a *border*: a non-negative integer `n` with
  `t[n] ~= nil` (or `n == 0`) and `t[n + 1] == nil`. For a sequence (positive integer keys
  exactly 1..n) it is `n`; for `{}` it is 0.
- **T3.2** A table with holes may have several borders, and `#` may return any of them.
  **(impl: lua2 follows `luaH_getn` step for step, including its `alimit` hint, so `#` answers
  as the real Lua does after the same operations)**
- **T3.3** `#` of a string is its length in bytes (V9.3); of any other value without `__len` it is
  the error `attempt to get length of a X value` (E8.2).

## T4. Traversal

- **T4.1** `next(t, k)` returns the key that follows `k` in the table and its value, or `nil`
  at the end; `next(t)` and `next(t, nil)` return the first entry, or `nil` for an empty table.
  The order is unspecified, and each key appears once. **(impl: the integer keys of the array
  part first, in order, then the hash part in node order)**
- **T4.2** During a traversal the program may assign to existing fields, including `nil` to
  clear them; adding new keys makes the rest of the traversal undefined. **(impl: a cleared
  key stays usable by `next` even after the collector frees its object: a *dead key*)**
- **T4.3** `next` with a key that is not in the table is the error `invalid key to 'next'`
  (raised inside `next`, so without a position).
- **T4.4** `pairs(t)` returns `next, t, nil` (unless `__pairs`, M2.6). `ipairs(t)` returns an
  iterator that gives `1, t[1]`, `2, t[2]`, ... and stops at the first `nil`; it reads with
  metamethods (M2.1).
- **T4.5** `select('#', ...)` is the number of its extra arguments; `select(n, ...)` returns its
  arguments from the `n`-th on, and a negative `n` counts from the end (`-1` is the last). `n`
  out of range is the error `bad argument #1 to 'select' (index out of range)`.

## T5. Raw access

- **T5.1** `rawget(t, k)`, `rawset(t, k, v)` (which returns `t`), `rawequal(a, b)` and
  `rawlen(v)` do what indexing, assignment, `==` and `#` do without metamethods. `rawlen` takes
  a table or a string; `rawget` and `rawset` need a table: `bad argument #1 to 'rawget' (table
  expected, got nil)`.

## T6. Memory

- **T6.1** Tables are collected like every other object (E9): an unreachable table and what only
  it refers to are freed.
- **T6.2** A table grows and shrinks on its own; its size never shows except through speed and
  memory. **(impl: an array part for integer keys 1..n, sized to the largest power of two that
  is more than half full, and a hash part of a power of two nodes, chained scatter with Brent's
  variation; both resized together when a new key finds no free node (`rehash`))**
