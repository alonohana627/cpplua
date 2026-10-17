# The stand-alone program

**Status:** stage 11 (version 0.0.11): `lua2` is the real `lua` program, a port of Lua 5.4.9's
`lua.c`, with lua2's debug views in front. The rules are P3 in
[../spec/conformance.md](../spec/conformance.md).

## Using it

```sh
lua2 script.lua a b c        # run a script; arg[0] = "script.lua", arg[1] = "a", ...
lua2 -e "print(1+1)"         # run a string
lua2 -l json script.lua      # require "json" into the global json first
lua2 -l j=json script.lua    # require "json" into the global j
lua2 -i script.lua           # run the script, then the interactive loop
lua2 -v                      # the version (also --version)
lua2 -E script.lua           # ignore LUA_INIT, LUA_PATH and LUA_CPATH
lua2 -W script.lua           # warnings on
lua2 - < script.lua          # run the standard input
lua2                         # the interactive loop (or stdin, when it is not a terminal)
lua2 --incremental script.lua   # the incremental collector (generational is the default)
lua2 --tokens script.lua     # lua2's debug views: tokens, --ast, --ast=dot, --bytecode
```

Before the options, `LUA_INIT_5_4` (or else `LUA_INIT`) runs: a chunk, or `@file` for a file.
In the interactive loop a line that is an expression prints its values (`1+1` prints `2`), an
incomplete statement asks for more with `>>`, and `_PROMPT` and `_PROMPT2` change the prompts.
Ctrl-C stops the running code with `interrupted!`; a second Ctrl-C ends the program.

An error prints `lua2: message` and a stack traceback on the error output, and the exit status
is 1, as with `lua`.

## Where it lives

| File | Contents |
| ---- | -------- |
| `include/lua2/standalone.hpp` | `int run_standalone(std::span<char* const> argv, bool generational = true)` |
| `lib/standalone.cpp` | the port: `collect_options`, `run_init`, `run_arguments`, `run_script`, `repl`, `docall` with the traceback handler, the `SIGINT` handler |
| `src/main.cpp` | `lua2`'s `main`: its own options (`--tokens`, `--ast`, `--ast=dot`, `--bytecode`, `--incremental`, `--generational`, `--version`), then `run_standalone` |

## How it works

`run_standalone` makes a heap and runs everything inside a C++ function called in protected
mode, as `lua.c`'s `pmain` does (so tracebacks end with `[C]: in ?`, as `lua`'s do). The steps
follow `lua.c` in its order: read the options and stop at the first that is wrong (printing the
usage), print the version for `-v`, set `LUA_NOENV` in the registry for `-E`, open the
libraries, make the `arg` table, switch the collector to generational mode, run `LUA_INIT`,
run `-e` and `-l` in their order, run the script with its arguments, then the interactive loop
for `-i`, or when nothing else was given.

Each chunk runs through `docall`: a protected call with `traceback_handler` as the message
handler, with the `SIGINT` handler installed while it runs (H4). An embedding program can
call `run_standalone` to offer the same command line.
