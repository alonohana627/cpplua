# my_lua

Every Lua program written while developing lua2, kept as a regression test.

| File         | Contents                                                     |
| ------------ | ------------------------------------------------------------ |
| `name.lua`   | the program                                                  |
| `name.ast`   | the expected `lua2 --ast name.lua` output, if it parses      |
| `name.error` | the expected error message, if it does not (one line)        |
| `name.dot`   | optional: the expected `lua2 --ast=dot name.lua` output      |
| `name.bytecode` | optional: the expected `lua2 --bytecode name.lua` output, which is the real `luac -l -l -p` listing without its memory addresses |
| `name.out`   | optional: what running it prints, then what it returns, as `lua2 name.lua` writes it (from the real Lua: `lua_ref -e "print(dofile('name.lua'))"`) |
| `name.runerror` | optional: the runtime error it stops with (the real Lua's message) |

`tests/my_lua_test.cpp` (tag `[my_lua]`) parses each `.lua` file and compares the result with
its golden file. Every program that parses must also compile; one with a `.bytecode` file must
compile to exactly that listing; one with `.out` or `.runerror` is run in both collector modes,
and again under both GC stress modes, and must give that result every time. Error messages use the file name as the chunk name: `too_deep.lua:1: ...`.
Like `lua2 --ast`, it loads them as files, so a `#` first line (`#!/usr/bin/env lua`) is skipped.

## Adding a program

1. Write `name.lua` here.
2. Run `lua2 --ast name.lua` from this folder and **read the output carefully**: a golden file
   only protects behavior that was checked to be right.
3. Save the output as `name.ast`, or the error message (without the `lua2: ` prefix) as
   `name.error`. Keep Unix line endings; `.gitattributes` keeps these files byte for byte.
4. For a program that tests the graph view, also save `lua2 --ast=dot name.lua` as `name.dot`.
5. For a program that runs, save what the real Lua makes of it: `lua_ref -e "print(dofile('name.lua'))"`
   as `name.out` (or the error message, without the program's name in front, as `name.runerror`),
   and `luac_ref -l -l -p name.lua` without the memory addresses as `name.bytecode`. Check that
   `lua2` agrees. Programs that use only locals can run until tables and the standard library exist.
