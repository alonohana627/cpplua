# my_lua

Every Lua program written while developing lua2, kept as a regression test, in a folder per
topic:

| Folder            | Programs                                                              |
| ----------------- | --------------------------------------------------------------------- |
| `parser/`         | syntax: statements, expressions, scopes, shebang lines, syntax errors |
| `vm/`             | the VM and collector of stage 5 (`vm_*`)                              |
| `runtime_errors/` | errors of operations: arithmetic, calls, comparisons, `for`, ... (`err_*`) |
| `tables/`         | tables (`table_*`) and indexing errors (`err_index_*`)                |
| `metatables/`     | metamethods (`meta_*`)                                                |
| `close/`          | to-be-closed variables (`close_*`, `err_close*`)                      |
| `gc/`             | finalizers and weak tables (`gc_*`)                                   |
| `errors/`         | `error`, `pcall`, `xpcall`, `assert` (`error_*`, `err_error_*`, `err_assert*`) |
| `stdlib/`         | the standard library: `base_*`, `string_*`, `table_*`, `math_*`, `utf8_*`, `io_*`, `os_*`, and their errors (`err_*`) |

A program and its golden files stay together in one folder:

| File         | Contents                                                     |
| ------------ | ------------------------------------------------------------ |
| `name.lua`   | the program                                                  |
| `name.ast`   | the expected `lua2 --ast name.lua` output, if it parses      |
| `name.error` | the expected error message, if it does not (one line)        |
| `name.dot`   | optional: the expected `lua2 --ast=dot name.lua` output      |
| `name.bytecode` | optional: the expected `lua2 --bytecode name.lua` output, which is the real `luac -l -l -p` listing without its memory addresses |
| `name.out`   | optional: what running it prints, then what it returns, then what finalizers print when the interpreter closes, as `lua2 name.lua` writes it (from the real Lua: `lua_ref -e "print(dofile('name.lua'))"`) |
| `name.runerror` | optional: the runtime error it stops with (the real Lua's message) |

`tests/my_lua_test.cpp` (tag `[my_lua]`) finds every `.lua` file in these folders, parses it and compares the result with
its golden file. Every program that parses must also compile; one with a `.bytecode` file must
compile to exactly that listing; one with `.out` or `.runerror` is run in both collector modes,
and again under both GC stress modes, and must give that result every time. Error messages use the file name as the chunk name: `too_deep.lua:1: ...`.
Like `lua2 --ast`, it loads them as files, so a `#` first line (`#!/usr/bin/env lua`) is skipped.

## Adding a program

1. Write `name.lua` in the folder of its topic (or a new folder).
2. Run `lua2 --ast name.lua` from that folder and **read the output carefully**: a golden file
   only protects behavior that was checked to be right.
3. Save the output as `name.ast`, or the error message (without the `lua2: ` prefix) as
   `name.error`. Keep Unix line endings; `.gitattributes` keeps these files byte for byte.
4. For a program that tests the graph view, also save `lua2 --ast=dot name.lua` as `name.dot`.
5. For a program that runs, save what the real Lua makes of it: `lua_ref -e "print(dofile('name.lua'))"`
   as `name.out` (or the error message, without the program's name in front, as `name.runerror`),
   and `luac_ref -l -l -p name.lua` without the memory addresses as `name.bytecode`. Check that
   `lua2` agrees. Programs can use the language and the base library ([../../../docs/final_docs/stdlib.md](../../../docs/final_docs/stdlib.md)),
   but nothing that depends on addresses, memory sizes or the order `pairs` visits string keys,
   and programs with `__gc` or weak tables should call `collectgarbage("stop")` first: the tests
   also run them with a collection at every possible point.
