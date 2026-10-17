# Standard library

**Status:** stage 9 is done (version 0.0.9): the whole Lua 5.4 standard library. Stage 10
added `coroutine` and the rest of `debug` ([coroutines.md](coroutines.md), [debug.md](debug.md)),
and stage 11 binary chunks ([dump.md](dump.md)). Every function is C++;
the Lua files in `std/` are only a reference. The rules are in
[../spec/stdlib.md](../spec/stdlib.md), the plan in [../plan/stdlib.md](../plan/stdlib.md), and
the background in [../learn/stdlib.md](../learn/stdlib.md).

## What exists

| Library | Functions | Port of |
| ------- | --------- | ------- |
| base | `assert`, `collectgarbage`, `dofile`, `error`, `getmetatable`, `ipairs`, `load`, `loadfile`, `next`, `pairs`, `pcall`, `print`, `rawequal`, `rawget`, `rawlen`, `rawset`, `select`, `setmetatable`, `tonumber`, `tostring`, `type`, `warn`, `xpcall`, `_G`, `_VERSION` | `lbaselib.c` |
| `coroutine` | `close`, `create`, `isyieldable`, `resume`, `running`, `status`, `wrap`, `yield` | `lcorolib.c` |
| `package` | `require`, `package.loadlib`, `searchpath`, `path`, `cpath`, `config`, `loaded`, `preload`, `searchers` | `loadlib.c` |
| `string` | `byte`, `char`, `dump`, `find`, `format`, `gmatch`, `gsub`, `len`, `lower`, `match`, `pack`, `packsize`, `rep`, `reverse`, `sub`, `unpack`, `upper`, and the strings' metatable | `lstrlib.c` |
| `table` | `concat`, `insert`, `move`, `pack`, `remove`, `sort`, `unpack` | `ltablib.c` |
| `math` | all of 5.4 (`random` is xoshiro256\*\*), and 5.3's `atan2`, `cosh`, `sinh`, `tanh`, `pow`, `frexp`, `ldexp`, `log10` | `lmathlib.c` |
| `utf8` | `char`, `charpattern`, `codepoint`, `codes`, `len`, `offset` | `lutf8lib.c` |
| `io` | `close`, `flush`, `input`, `lines`, `open`, `output`, `popen`, `read`, `tmpfile`, `type`, `write`, `stdin`, `stdout`, `stderr`, and the file methods | `liolib.c` |
| `os` | `clock`, `date`, `difftime`, `execute`, `exit`, `getenv`, `remove`, `rename`, `setlocale`, `time`, `tmpname` | `loslib.c` |
| `debug` | all of 5.4's ([debug.md](debug.md)) | `ldblib.c` |

Where lua2 differs from the real Lua (all in [../spec/stdlib.md](../spec/stdlib.md)):

- no C libraries: `package.loadlib` and the C searchers answer as a Lua without dynamic
  libraries does (S9.2); C++ adds functions through the C++ API instead;
- `io` and `os` use only the C++ standard library (S6.1, S7.1): so no `io.popen` (S6.4),
  and `os.rename` replaces an existing file (S7.2);
- a reader function given to `load` is read to the end before parsing (S8.1);
- `%a` in `string.format` takes flags and widths, as C99 does (S2.2);
- a function with two names in a library is named the same way every run (S1.3).

## Where it lives

| File | Contents |
| ---- | -------- |
| `lib/internal/std/libs.hpp`, `lib/std/libs.cpp` | an `open_` function per library, and `open_libraries` |
| `lib/std/base.cpp` (`lib/internal/std/base.hpp`) | the base library |
| `lib/std/coroutine.cpp`, `lib/std/debug.cpp` | `coroutine` and `debug` (stage 10) |
| `lib/std/string.cpp` | patterns, `format`, `pack`, the strings' metatable |
| `lib/std/table.cpp`, `math.cpp`, `utf8.cpp`, `io.cpp`, `os.cpp`, `package.cpp` | one library each |
| `lib/internal/std/auxlib.hpp`, `lib/std/auxlib.cpp` | the helpers of `lauxlib.c`: arguments, errors, tables, files, tracebacks |
| `std/*.lua` | the reference: every function as an empty Lua function with LuaLS annotations |

`lua2::Interpreter` calls `open_libraries`; the fixture tests do too. `tests/stdlib_test.cpp`
checks that `std/*.lua` declares exactly the functions and fields the libraries have (S1.2), so
adding a function means adding its stub.

## How the library is written

Each library is a port of its C file, function by function, with Lua's names in the comments.
What changes is how errors leave: Lua's functions `longjmp` out of the middle of anything;
lua2's return `c_function_error` (see [errors.md](errors.md)). The checks of `auxlib.hpp`
(`check_integer`, `check_view`, `opt_view`, `check_option`, ...) raise the error and return
false, so a function goes `if (!check_...) return c_function_error;`. Deep code returns an error
flag instead: the pattern matcher records an error in its `MatchState` and every level returns
`nullptr` until the top raises it, where `lstrlib.c` would `longjmp`.

Library tables come from `open_library(heap, name, functions)` (a global and `package.loaded`
entry, like `luaL_requiref`), with the functions in a `constexpr std::array<LibraryFunction, N>`.
State that Lua keeps in upvalues stays in upvalues: the random generator's state is a userdata
upvalue of `math.random` and `math.randomseed`, a `gmatch` iterator's state is a userdata
upvalue of its closure, `require` and the searchers have the `package` table. What Lua keeps in
the registry (the files' metatable, the default input and output) is in the heap's references
table under light-userdata keys. Files are userdata with a `type_tag`, which is how `io` knows
its own, holding a `Stream`: a `std::filebuf` (owned) or the standard input's buffer, read and
written through the `std::streambuf` interface (`sgetc` takes the place of Lua's `getc` and
`ungetc` look-ahead when reading numbers). The heap's output goes to `std::cout` and its error
output to `std::cerr`.

Values a function holds must be on the stack before it calls anything that can run the
collector (a call into Lua): collection only happens at those points, never inside an
allocation. `table.sort` keeps its pivot and the two values it compares in stack slots, as Lua
does, so an order function that runs the collector cannot free them.

Strings take arguments as views (`check_view`): a number argument is converted to a string in
its stack slot first, as `lua_tolstring` does, so the view lives as long as the call.

### Speed

`bench/lib_string.lua`, `lib_table.lua` and `lib_math.lua` time the libraries (release, best of
5, the same machine as [vm.md](vm.md)):

| Benchmark | lua2 | Lua 5.4.9 | Ratio |
| --------- | ---: | --------: | ----: |
| `lib_math` | 76 ms | 77 ms | 0.99x |
| `lib_string` | 23 ms | 23 ms | 1.00x |
| `lib_table` | 142 ms | 311 ms | 0.46x |

What made the difference:

- **`table.sort` of a plain array of numbers or strings** without an order function sorts a
  copy with the same steps, then writes it back (also when it fails midway, so the table is
  left as Lua leaves it), instead of reading and writing the table at every step.
- **`table.insert` and `table.remove` in the middle** of a table without a metatable move the
  block of the array part with `memmove` instead of an element at a time.
- **Pattern character classes** come from a table of the C locale's classes, not from calls to
  the C library's `isalpha` and the rest.
- **`gsub`** copies the text between matches in runs, not a character at a time, and the matcher
  tests a `bool` for errors.

## Writing a C++ function

This section is about the library's own functions, written against lua2's internals. Programs
that embed lua2 use the public API instead (`lua2::Interpreter`, `lua2::Context`, automatic
conversions, classes), described in
[../how_to_put_cpp_functions_and_classes_into_lua.md](../how_to_put_cpp_functions_and_classes_into_lua.md).

A function is a `CFunction`, `int (*)(State&)`. Its arguments are in its frame, from
`ci->func + 1` up to the top: `arg_count`, `arg(state, n)` (nil past the last), `arg_is_none`.
It pushes its results (`push`, `push_string`; the frame has 20 free slots) and returns how
many, or `c_function_error` after raising an error: `raise_error` (`luaL_error`, with the
caller's position), `arg_error` and `arg_type_error` (`luaL_argerror`, `luaL_typeerror`), and
the checks. Tables are read and written with `get_index` and `set_index` (`lua_geti`,
`lua_seti`), `index_value` and `assign_value` (`lua_gettable`, `lua_settable`), compared with
`compare_values` and `equal_values` (`lua_compare`), measured with `length_integer`
(`luaL_len`). To call Lua, push the function and its arguments and use `call()` (errors pass
up) or `protected_call()`.

The VM knows two functions by address, `base_next` and `base_ipairs_step`: a generic `for` over
`pairs` or `ipairs` of a table without a metatable runs them without a call (see
[tables.md](tables.md)).
