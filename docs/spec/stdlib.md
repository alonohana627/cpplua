# Lua 5.4 standard library

The rules for lua2's standard library (roadmap stage 9). The library is the one of the Lua 5.4
Reference Manual §6 (the base library, `package`, `string`, `utf8`, `table`, `math`, `io` and
`os`; `coroutine` and `debug` came in stage 10, with their rules in
[coroutines.md](coroutines.md)), with every function's behavior, results and error messages as
in the reference implementation (Lua 5.4.9's `lbaselib.c`, `lstrlib.c`, `ltablib.c`,
`lmathlib.c`, `lutf8lib.c`, `liolib.c`, `loslib.c`, `loadlib.c`, `lauxlib.c`),
built with `LUA_COMPAT_5_3` like the reference Lua (`lua_ref`). The manual is not restated
here: these rules name what lua2 adds, leaves out or decides where Lua leaves a choice open.

The my_lua programs in `tests/fixtures/my_lua/stdlib/` check the library against the real Lua's
output. Tests of the rules below are in `tests/stdlib_test.cpp` and name their rule, for example
`TEST_CASE("S6.1 files: ...", "[stdlib]")`.

---

## S1. The libraries

- **S1.1** Every function is C++ (`lib/std/`). `lua2::Interpreter` opens all the libraries, in
  `luaL_openlibs`'s order: base, `package`, `coroutine`, `table`, `io`, `os`, `string`, `math`,
  `utf8`, `debug`. Each is a global and an entry of `package.loaded`.
- **S1.2** `std/*.lua` declares every library function as an empty Lua function with its
  documentation, and every field as an assignment (for readers and editors, like Python's stubs
  for modules written in C). They are never run. The set of names declared there is exactly the
  set of functions and fields the libraries have.
- **S1.3** Errors in arguments name the function as Lua's `luaL_argerror` does: by how the
  caller calls it, else by its name in a loaded module (`'string.rep'`). Where a function has
  two such names (`math.atan` and `math.atan2`), Lua takes whichever its table traversal finds
  first, which changes from run to run; lua2 takes the shortest, then the first in byte order.

## S2. Strings

- **S2.1** Patterns are Lua's, matched by a port of `lstrlib.c`'s matcher, with its limits (32
  captures, 200 levels of `match` recursion: `pattern too complex`).
- **S2.2** `string.format` formats numbers with the C library's `printf`; on MinGW it uses
  MinGW's own C99 one, so `nan` and `%a` print as on other systems (and as the reference Lua
  does). `%a` takes flags, width and precision, as C99 does (the reference Lua on Windows
  rejects them).
- **S2.3** `string.pack`, `string.unpack` and `string.packsize` are Lua's, with the machine's
  native byte order and alignment taken as little-endian.
- **S2.4** Strings have a metatable whose `__index` is the `string` table and whose arithmetic
  metamethods convert strings to numbers (`"10" + 1`); bitwise operators do not convert strings.
- **S2.5** `string.dump` makes a Lua 5.4 binary chunk, byte for byte the real Lua's (stage 11,
  P2.1 in [conformance.md](conformance.md)); a C++ function is `unable to dump given
  function`.

## S3. Tables

- **S3.1** The table functions read and write through `__index`, `__newindex` and `__len` as
  Lua's do. A table with no metatable may be changed directly (sorting a copy, moving a block
  of its array part): nothing can tell the difference, the result included when a sort fails
  midway.

## S4. Math

- **S4.1** `math.random` is Lua's xoshiro256\*\*: after the same `math.randomseed(n)`, the same
  numbers as the real Lua. Without a seed, the state starts from the time and an address.
- **S4.2** The functions of `LUA_COMPAT_MATHLIB` are there: `atan2`, `cosh`, `sinh`, `tanh`,
  `pow`, `frexp`, `ldexp`, `log10`.

## S5. UTF-8

- **S5.1** The `utf8` library is Lua's, with its strict and lax decoding.

## S6. Input and output

- **S6.1** The io library uses only the C++ standard library (the user's decision): a file is a
  userdata of type `FILE*` holding a `std::filebuf` (the standard input: `std::cin`'s buffer),
  opened with the open mode of each `fopen` mode. The functions and methods are Lua's, with the
  same messages (`attempt to use a closed file`, `cannot close standard file`, `default input
  file is closed`) and `errno` texts. Files left open are closed by their `__gc`.
- **S6.4** `io.popen` checks its arguments, then raises `'popen' not supported`, as a Lua built
  without `popen` does: the C++ standard library cannot start a process with a pipe.
- **S6.5** `io.tmpfile` makes a new file in `std::filesystem::temp_directory_path()`, open for
  update, and removes it when the file is closed (or collected).
- **S6.2** What goes to the standard output (`print`, `io.write`, `io.stdout`) goes through the
  interpreter's output function, and what goes to the standard error (`io.stderr`, warnings)
  through its error output function: both default to the C streams, and a program embedding
  lua2 can take them (`Interpreter::set_output`, `set_error_output`).
- **S6.3** `io.input` and `io.output` set the default files, by name or by file.

## S7. Operating system

- **S7.1** The os library uses only the C++ standard library: the current time from
  `std::chrono::system_clock`, and the calendar (`os.date`, `os.time` with a table) from
  `<ctime>`'s `std::localtime`, `std::gmtime`, `std::mktime` and `std::strftime`, since C++ has
  no time zone database on every system. `os.date` accepts the conversions Lua accepts on the
  system (on Windows the C89 set and `#` flags).
- **S7.2** `os.remove` and `os.rename` use `std::filesystem` (so `os.rename` replaces an
  existing file, as POSIX `rename` does), and report failures with the `errno` value and text.
  `os.tmpname` creates the file it names in the temporary folder, as POSIX Lua's `mkstemp` does.
  `os.getenv`, `os.execute`, `os.exit`, `os.clock` and `os.setlocale` are `<cstdlib>`'s,
  `<ctime>`'s and `<clocale>`'s functions, as in Lua. What the locale changes (string order,
  numerals, character classes) follows `os.setlocale` as in Lua (P4 in
  [conformance.md](conformance.md)).

## S8. Loading chunks

- **S8.1** `load` takes a string or a reader function, a chunk name, a mode and an environment.
  A reader is called until it returns `nil` or an empty string, and then the pieces are parsed
  (Lua parses while it reads). Binary chunks load too (P2.2 in [conformance.md](conformance.md));
  a mode without `b` refuses them, and a mode without `t` refuses text.
- **S8.2** `loadfile` and `dofile` read files as `lua2 file.lua` does (a byte order mark and a
  first line starting with `#` are skipped); no name means stdin.

## S9. Modules

- **S9.1** `require` and `package` are Lua's: `package.path` from `LUA_PATH_5_4` or `LUA_PATH`
  (with `;;` for the default), `package.preload`, `package.searchers`, `package.searchpath`,
  `package.config`; a module is loaded once and kept in `package.loaded`.
- **S9.2** lua2 loads no C libraries (C++ code adds functions through the C++ API):
  `package.loadlib` returns `nil`, `dynamic libraries not enabled; check your Lua installation`,
  `absent`, and a module found on `package.cpath` is that error, as in a Lua built without
  dynamic libraries.

## S10. Warnings

- **S10.1** `warn` writes `Lua warning: ` and its arguments to the error output, when warnings
  are on. They start off, as in the `lua` program; `warn("@on")` and `warn("@off")` switch them.
