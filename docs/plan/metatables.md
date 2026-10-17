# Metatables plan

Roadmap stage 7. Rule ids such as `M3.4` point to [../spec/metatables.md](../spec/metatables.md).
How the finished code works is in [../final_docs/metatables.md](../final_docs/metatables.md); the
background is in [../learn/tables_metatables_errors.md](../learn/tables_metatables_errors.md).

## Status: done (0.0.7, released together with stages 6 and 8 as 0.0.8)

Built in one go with stages 6 and 8, at the user's request (see [tables.md](tables.md)).

## Decisions

- **Metamethods are calls through C++** (`call()`), as in Lua (`luaT_callTMres` uses
  `luaD_call`): they count toward the C-call limit, and a deep recursion through `__index`
  functions ends in `C stack overflow`, as in Lua.
- **Protect before every call**, as Lua's `Protect` does: save the pc and put the top above the
  frame's registers; reload the registers afterwards. Every slow path that may call is in a cold
  (or warm) handler, so the hot ones stay frameless.
- **Strings stand in for the string library's metamethods** until stage 9 gives strings a
  metatable: the arithmetic slow path converts strings itself, in the same order of tries as
  `lstrlib.c`'s `arith` and `trymt`.
- **`LUA_COMPAT_5_3`'s `__le` fallback is in**, because the reference Lua is built with it (as
  Lua's own makefile builds it), and the golden files come from it.
- **`lgc.c`'s weak tables and finalizers, ported whole**: the extra lists (`finobj`, `tobefnz`,
  their generational pointers, the weak gray lists), the atomic phase in Lua's order, and the
  extra sweep phases, so finalizer order and weak-table clearing match Lua's.
- **To-be-closed variables in a side list** (`State::to_be_closed`, slot numbers) instead of
  Lua's deltas stored in the stack slots: the stack's two arrays have no room for them.
- **Closing the interpreter runs finalizers** (`Heap::close`, also from the destructor), so a
  program's output is complete when `lua2` exits, as with `lua_close`.

## Parts

1. **Lookups**: `Event`, the names, `metatable_of`, `metamethod_in` with the absence cache.
2. **Indexing**: `finish_get`, `finish_set`, `__index`/`__newindex` chains (M2).
3. **Operators**: arithmetic, bitwise, unary, `..`, `#`, `==`, `<`, `<=` (M3); `__call` (M4).
4. **`tostring` and `print`** with `__tostring` and `__name` (M5).
5. **To-be-closed variables**: `TBC`, `CLOSE`, `RETURN` with `k`, `TFORPREP`, closing on errors
   (M6).
6. **The collector**: finalizers and weak tables (M7), `collectgarbage`.
7. **Golden programs** `meta_*.lua`, `close_*.lua`, `gc_*.lua`, and the rule tests
   (`tests/meta_test.cpp`).
8. **Speed**: `bench/methods.lua` and `metamethods.lua`.
