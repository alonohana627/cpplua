# Tables plan

Roadmap stage 6. Rule ids such as `T3.1` point to [../spec/tables.md](../spec/tables.md). How the
finished code works is in [../final_docs/tables.md](../final_docs/tables.md); the background is
in [../learn/tables_metatables_errors.md](../learn/tables_metatables_errors.md).

## Status: done (0.0.6, released together with stages 7 and 8 as 0.0.8)

The user asked for stages 6, 7 and 8 in one go (2026-10-17: "Implement in one shot steps 6 7 8.
Still create the proper docs for them. Care about performance only after you implement these and
after they work properly."), so the three stages were built together, then tested against the
real Lua, then tuned. The parts below are the order the work went in.

## Decisions

- **A port of `ltable.c`, not a new design.** Its array/hash split, its rehash sizes and its `#`
  (with the `alimit` hint) decide what `#` returns on tables with holes and in what order `next`
  visits integer keys; following it function by function makes lua2 answer as the real Lua does
  (T3.2), which the golden files check.
- **Split storage in the array part** (stage 4's decision for stored values): payloads and tags
  in two arrays of one block, 9 bytes a slot.
- **Flags in the object header's padding**, as Lua keeps them after its `CommonHeader`: a table is
  64 bytes (measured: the size did not change the benchmarks much, but it costs nothing).
- **Fast paths inline, everything else in `Index` and `NewIndex`**, which try the common slow
  cases (a new key without `__newindex`, `__index` chains of tables) before the general
  `luaV_finishget` and `luaV_finishset`.
- **The minimal string-keys-only globals table of stage 5 is gone**: `_ENV` is an ordinary table.

## Parts

1. **The table**: `Table` with every `ltable.c` operation (`get_int`, `get_short_string`, `get`,
   `new_key` with Brent's variation, `rehash`, `resize`, `length`, `next_entry`), checked by
   `tests/table_test.cpp` and by the collector traversing it.
2. **The VM**: the table instructions, `NEWTABLE` and `SETLIST`, `LEN`; `_ENV` as a table.
3. **The library**: `next`, `pairs`, `ipairs`, `select`, the `raw*` functions (in
   `lib/std/base.cpp`).
4. **Golden programs** `table_*.lua` and `err_index_*.lua` with the real Lua's output.
5. **Speed** (after stages 7 and 8 worked): measured on `bench/tables_array.lua`,
   `tables_hash.lua` and `sort.lua`, then the fast paths of [../final_docs/tables.md](../final_docs/tables.md)
   ("Speed").
