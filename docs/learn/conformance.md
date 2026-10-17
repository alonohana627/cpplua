# Conformance: background

The ideas behind roadmap stage 11, for reading the code. How lua2 does each is in
[../final_docs/conformance.md](../final_docs/conformance.md).

## 1. The official test suite

Lua's authors publish the tests they run on every release (`lua-5.4.x-tests` on lua.org). Each
file tests one area (`strings.lua`, `coroutine.lua`, `gc.lua`, ...) with plain `assert`s, so the
first failure stops the file with a line number. `all.lua` runs them all, and prints `final OK
!!!` at the end. Some parts depend on the system (a Unix shell for `main.lua`, `io.popen`,
locales by their POSIX names): setting `_port=true` skips them. Others need Lua's internal test
library (`ltests.c`, the global `T`), compiled only into a special debug build: they skip
themselves when `T` is missing. The tests check corner cases the manual leaves unsaid (error
messages, limits, the order of evaluation), so passing them means behaving like the real Lua,
not only like the manual.

## 2. Binary chunks

`string.dump` and `luac` turn a compiled function into bytes: a header that says which Lua
built it (version, format, the sizes of an instruction, an integer and a float, and a known
integer and float to check byte order and representation), then the function: its
instructions, constants, upvalue descriptions, nested functions and the debug information
(lines, local variable names). Loading it skips the compiler entirely. The format is not
portable between Lua versions, nor checked for safety: a crafted chunk can make the VM misbehave,
which is why `load` has a mode argument.

Since lua2's compiler emits the real Lua's bytecode, its chunks can be the real Lua's, byte for
byte, which is the simplest compatible choice: one format, and `luac` output runs in lua2.

## 3. The stand-alone program

The `lua` program is itself a small C program (`lua.c`) on top of Lua's API: it parses options,
creates the `arg` table, runs `LUA_INIT`, then the script, or a read-eval-print loop. It runs
everything inside one protected call, so even an error in option handling is reported, and
installs a `SIGINT` handler that sets a hook to stop the running code at its next instruction
(a signal handler cannot safely do much else).

## 4. The locale

The C library's locale decides things Lua inherits: how strings sort (`strcoll` under
`LC_COLLATE`), which characters are letters or upper case (`isalpha`, `toupper` under
`LC_CTYPE`), and the decimal point `printf` and `strtod` use (`LC_NUMERIC`). Programs start in
the `"C"` locale, where all of these are plain ASCII rules; `os.setlocale` changes them for the
whole program. Lua's source follows the locale everywhere it calls the C library, and the test
suite checks it, so lua2 asks the C library in the same places, but only when the locale is
not `"C"`: the fast byte-by-byte paths stay for the usual case.
