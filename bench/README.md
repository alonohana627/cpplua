# Benchmarks

Lua programs that time the VM and the garbage collector (docs/plan/vm.md). Each uses only what
stage 5 builds: locals, arithmetic, strings, functions and closures (no tables, no standard
library), and returns its result, so lua2 and the reference Lua can run the same file.

| File                | What it stresses                                         |
| ------------------- | -------------------------------------------------------- |
| `loop_integers.lua` | integer arithmetic and comparisons in a numeric `for`    |
| `loop_floats.lua`   | float arithmetic                                         |
| `fib.lua`           | recursive calls and returns                              |
| `closures.lua`      | making closures, upvalue reads and writes                |
| `strings.lua`       | short concatenations, number to string, string garbage   |
| `varargs.lua`       | varargs and multiple results                             |

Run them with the runner, built with the project:

```sh
./build/release/lua2_bench bench ./build/release/lua2 ./build/release/lua_ref
```

It prints the best of 5 wall-clock runs for each interpreter and the ratio
(lua2 time / reference time; below 1 means lua2 is faster). Use the release build. The latest
results, and where the time goes, are in `docs/final_docs/vm.md` ("Benchmarks").

To compare generational collection, run each program by hand:

```sh
./build/release/lua2 --generational bench/strings.lua
./build/release/lua_ref -e "collectgarbage('generational')" bench/strings.lua
```