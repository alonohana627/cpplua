# Standard library plan

Roadmap stage 9. Rule ids such as `S6.1` point to [../spec/stdlib.md](../spec/stdlib.md). How the
finished code works is in [../final_docs/stdlib.md](../final_docs/stdlib.md); the background is
in [../learn/stdlib.md](../learn/stdlib.md).

## Status: done (0.0.9)

## Decisions

- **Everything in C++** (the user's request): every library function is C++, a port of the
  reference implementation's C file. The Lua side is a reference only: `std/*.lua` declares each
  function as an empty function with its documentation, like Python's stubs for modules written
  in C, and a test keeps those files and the C++ in step (S1.2).
- **Ports, not rewrites.** Each function follows its C original step by step, so results,
  corner cases and error messages match without guessing; the comments give Lua's names.
  Errors are return values, as in the rest of lua2: where Lua would `longjmp` from deep inside
  (the pattern matcher, `string.pack`'s option parser), the code records the error and returns.
- **The reference Lua decides.** Behavior is checked against `lua_ref` (built with
  `LUA_COMPAT_5_3`, so the 5.3 math functions are there too) by 48 golden programs.
- **Where lua2 cannot do what Lua does**, it answers as a Lua built without that feature: no
  binary chunks (`string.dump`; stage 11 added them), no C libraries (`package.loadlib`, C
  searchers).
- **The standard streams go through the interpreter's output functions**, so an embedding
  program captures `print`, `io.write` and warnings alike.
- **Speed after correctness**: the libraries were made to work first, then measured with three
  new benchmarks and tuned where they were slower than the real Lua.

## Parts

1. **Helpers** in `auxlib`: argument checks for views, options and functions, library tables
   (`open_library`), table access with metamethods, `luaL_fileresult`, `luaL_execresult`.
2. **`string`**: the matcher, `find`, `match`, `gmatch`, `gsub`, `format`, `pack`, the strings'
   metatable.
3. **`table`**, **`math`**, **`utf8`**.
4. **The rest of the base library**: `load`, `loadfile`, `dofile`, `warn`.
5. **`io`** and **`os`**.
6. **`package`** and `require`.
7. **The reference files** `std/*.lua` and the test that checks them.
8. **Golden programs** in `tests/fixtures/my_lua/stdlib/` and the rule tests
   (`tests/stdlib_test.cpp`); benchmarks `bench/lib_*.lua`.
