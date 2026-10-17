# Conformance

The rules for roadmap stage 11: lua2 passes the official Lua 5.4.9 test suite, and has what
the suite needs that the earlier stages left out: binary chunks, the stand-alone `lua`
program, and the locale. Behavior and messages are the reference implementation's (`ldump.c`,
`lundump.c`, `lua.c`, `lvm.c`'s `l_strcmp`, `lobject.c`'s `l_str2d` and `tostringbuff`).

How the code works is in [../final_docs/conformance.md](../final_docs/conformance.md),
[../final_docs/dump.md](../final_docs/dump.md) and
[../final_docs/standalone.md](../final_docs/standalone.md).

---

## P1. The official test suite

- **P1.1** `tests/fixtures/lua-5.4.9-tests/` is the suite as lua.org distributes it, unchanged.
  `lua2 -e "_port=true" all.lua`, run in that folder, prints `final OK !!!`. The CTest test
  `lua_5_4_test_suite` runs it in every build (debug, release, asan, the `switch` dispatch).
- **P1.2** `_port=true` skips what depends on a POSIX system (`io.popen`, `os.execute` with
  shell commands, the locales `ptb` and `pt_BR`), as the real Lua needs on Windows too. That
  includes all of `main.lua`, which runs the interpreter through a Unix shell: on Windows it
  stops at the same line in the real Lua and in lua2. The
  parts for Lua's internal test library (`T`, `ltests.c`) are skipped because lua2 has none,
  as in a normal build of the real Lua.

## P2. Binary chunks

- **P2.1** `string.dump(f, strip)` gives a Lua 5.4 binary chunk, byte for byte what the real
  Lua's `string.dump` and `luac` give for the same function. `strip` leaves out the line
  information, the names of locals and upvalues and the source name.
- **P2.2** `load`, `loadfile`, `dofile` and `lua2 file` load binary chunks (`"\27Lua"`) as well
  as text: the real Lua's and lua2's. The mode argument works as in Lua (`"t"` refuses binary,
  `"b"` refuses text). A chunk that is not Lua 5.4's (another version, format, number size) is
  refused with `lundump.c`'s messages, such as `bad binary format (truncated chunk)` or
  `version mismatch`.
- **P2.3** As in Lua, binary chunks are not checked beyond their header: a crafted one can make
  the VM read or write outside its stack. Code that loads chunks it does not trust uses mode
  `"t"`.
- **P2.4** A loaded binary chunk's first upvalue is the global table, like a text chunk's; the
  others start as `nil` (upvalues are not saved).

## P3. The stand-alone program

- **P3.1** `lua2` is the real `lua` program (a port of `lua.c`): `lua2 [options] [script
  [args]]` with `-e stat`, `-l mod`, `-l g=mod`, `-i`, `-v`, `-E`, `-W`, `--` and `-`; the
  global `arg` with the script at index 0; `LUA_INIT_5_4` or `LUA_INIT` (a chunk, or `@file`)
  unless `-E`; and the interactive loop (`_PROMPT`, `_PROMPT2`, a line starting with an
  expression prints its values) when there is no script and the input is a terminal. Errors go
  to the error output as `lua2: message` and a traceback, and give exit status 1.
- **P3.2** lua2's own options come before those, alone: `--tokens`, `--ast`, `--ast=dot` and
  `--bytecode` print a debug view of a file; `--incremental` and `--generational` choose the
  collector's mode for the rest of the command line (generational is the default, as `lua.c`
  sets it); `--version` is `-v`.
- **P3.3** Ctrl-C stops the running Lua code with the error `interrupted!` (H4.1).
- **P3.4** C++ programs get the same program through `lua2::run_standalone(argv)`
  (`include/lua2/standalone.hpp`).

## P4. The locale

Programs start in the `"C"` locale; `os.setlocale` changes the C library's, and these follow
it, as in Lua:

- **P4.1** Strings compare with `strcoll` when the collation (`LC_COLLATE`) is not `"C"` or
  `"POSIX"`, piece by piece between embedded zeros (`l_strcmp`); otherwise byte by byte.
- **P4.2** A numeral may use the locale's decimal point (`LC_NUMERIC`) as well as the dot:
  `tonumber("3,5")` in a locale with a comma, as `l_str2d` retries. So may `io.read("n")`.
- **P4.3** Floats are written with the locale's decimal point by `tostring`, `print`,
  concatenation, `io.write` and `string.format`, as the C library's `printf` writes them.
  `string.format("%q")` always writes a dot, so its output loads back in any locale.
- **P4.4** Pattern classes (`%a`, `%l`, `%u`, ...), `string.upper` and `string.lower` use the
  locale's character classes (`LC_CTYPE`) when it is not `"C"`.
