# Benchmarks

Lua programs that time the VM and the garbage collector (docs/plan/vm.md), then tables,
metatables and errors (stages 6 to 8), then the standard library (stage 9, `lib_*.lua`). Each
returns its result, so lua2 and the reference Lua can run the same file and must print the same.

| File                | What it stresses                                         |
| ------------------- | -------------------------------------------------------- |
| `loop_integers.lua` | integer arithmetic and comparisons in a numeric `for`    |
| `loop_floats.lua`   | float arithmetic                                         |
| `fib.lua`           | recursive calls and returns                              |
| `closures.lua`      | making closures, upvalue reads and writes                |
| `strings.lua`       | short concatenations, number to string, string garbage   |
| `varargs.lua`       | varargs and multiple results                             |
| `tables_array.lua`  | filling and reading arrays, `#`, `ipairs`                |
| `tables_hash.lua`   | small records from constructors, string keys, `pairs`    |
| `sort.lua`          | quicksort in Lua over integer arrays                     |
| `methods.lua`       | classes through `__index` chains, method calls           |
| `metamethods.lua`   | `__add`, `__mul`, `__eq`, `__len`, an `__index` function |
| `pcall.lua`         | `pcall` that succeeds and fails, to-be-closed variables  |
| `lib_string.lua`    | the string library: `gmatch`, `gsub`, `find`, `match`, `format`, methods |
| `lib_table.lua`     | `table.sort` with and without an order function, `insert`, `remove`, `concat`, `unpack`, `move` |
| `lib_math.lua`      | `math.random` and the math functions                     |

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