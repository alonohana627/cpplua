# Standard library

What lua2's standard library has so far, and how its C++ functions are written. The full library
is roadmap stage 9; stages 5 to 8 brought what they needed: the base library and
`debug.traceback`.

## What exists

| Function | Notes |
| -------- | ----- |
| `print(...)` | writes each argument as `tostring` does, tabs between, a newline at the end |
| `type(v)`, `tostring(v)`, `tonumber(v [, base])` | `tostring` uses `__tostring` and `__name` (M5.1) |
| `next`, `pairs`, `ipairs`, `select` | `pairs` uses `__pairs`, `ipairs` reads with `__index` (T4) |
| `rawget`, `rawset`, `rawequal`, `rawlen` | no metamethods (T5.1) |
| `setmetatable`, `getmetatable` | `__metatable` protects (M1) |
| `error`, `assert`, `pcall`, `xpcall` | [errors.md](errors.md) |
| `collectgarbage(opt, ...)` | `collect`, `count`, `step`, `stop`, `restart`, `isrunning`, `incremental`, `generational`, `setpause`, `setstepmul` (M7.6) |
| `_G`, `_VERSION` | the globals table, and `"Lua 5.4"` |
| `debug.traceback([msg [, level]])` | the only function of the debug library yet (R4) |

Missing until stage 9: `load`, `loadfile`, `dofile`, `require`, `warn`, and the `string`, `table`,
`math`, `utf8`, `os`, `io` and `coroutine` libraries; strings have no metatable yet, so
`("x"):upper()` is an error.

## Where it lives

| File | Contents |
| ---- | -------- |
| `lib/internal/std/base.hpp`, `lib/std/base.cpp` | the functions, `open_base`, `open_debug` (`lbaselib.c`) |
| `lib/internal/std/auxlib.hpp`, `lib/std/auxlib.cpp` | the helpers: arguments, errors, `tostring`, tracebacks (`lauxlib.c`) |

`lua2::Interpreter` opens both; the fixture tests do too.

## Writing a C++ function

This section is about the library's own functions, written against lua2's internals. Programs
that embed lua2 use the public API instead (`lua2::Interpreter`, `lua2::Context`, automatic
conversions, classes), described in
[../how_to_put_cpp_functions_and_classes_into_lua.md](../how_to_put_cpp_functions_and_classes_into_lua.md).

A function is a `CFunction`, `int (*)(State&)`. Its arguments are in its frame, from
`ci->func + 1` up to the top: `arg_count`, `arg(state, n)` (nil past the last), `arg_is_none`.
It pushes its results (`push`; the frame has 20 free slots) and returns how many, or
`c_function_error` after raising an error: `raise_error` (`luaL_error`, with the caller's
position), `arg_error` and `arg_type_error` (`luaL_argerror`, `luaL_typeerror`), and the checks
`check_any`, `check_table`, `check_number`, `check_integer`, `opt_integer`, `check_string`, which raise and return
false. To call Lua, push the function and its arguments and use `call()` (errors pass up) or
`protected_call()`.

Values a function makes must be on the stack before it calls anything that can run the
collector (a call into Lua): collection only happens at those points, never inside an
allocation.

The VM knows two functions by address, `base_next` and `base_ipairs_step`: a generic `for` over
`pairs` or `ipairs` of a table without a metatable runs them without a call (see
[tables.md](tables.md)).
