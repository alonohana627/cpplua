# my_lua

Every Lua program written while developing lua2, kept as a regression test.

| File         | Contents                                                     |
| ------------ | ------------------------------------------------------------ |
| `name.lua`   | the program                                                  |
| `name.ast`   | the expected `lua2 --ast name.lua` output, if it parses      |
| `name.error` | the expected error message, if it does not (one line)        |

`tests/my_lua_test.cpp` (tag `[my_lua]`) parses each `.lua` file and compares the result with
its golden file. Error messages use the file name as the chunk name: `too_deep.lua:1: ...`.

## Adding a program

1. Write `name.lua` here.
2. Run `lua2 --ast name.lua` from this folder and **read the output carefully**: a golden file
   only protects behavior that was checked to be right.
3. Save the output as `name.ast`, or the error message (without the `lua2: ` prefix) as
   `name.error`. Keep Unix line endings; `.gitattributes` keeps these files byte for byte.
