# lua2

A Lua 5.4 interpreter written from scratch in C++26, built step by step.
The plan is in [docs/ROADMAP.md](docs/ROADMAP.md); background reading for each step is in [docs/learn/](docs/learn/), and the exact rules we implement are in [docs/spec/](docs/spec/).

## Requirements

- clang++ with libc++ (C++26, `-std=c++2c`)
- CMake 3.28+ and Ninja
- Python 3 (for `run-clang-tidy`)
- clang-format and clang-tidy

Catch2 is downloaded by CMake on first configure.

## Layout

| Folder          | Contents                                                     |
| --------------- | ------------------------------------------------------------ |
| `include/lua2/` | Public headers, the embedding API (`#include <lua2/...>`)    |
| `lib/`          | The embeddable interpreter library (`lua2::lua2`), `.cpp` only |
| `lib/internal/` | The library's private headers, not visible to embedders      |
| `src/`          | The `lua2` binary that runs Lua, built on `lib/`             |
| `tests/`        | Catch2 tests                                                 |
| `bench/`        | Benchmark programs and their runner (`bench/README.md`)      |
| `original_lua_src/` | The original Lua 5.4.9 distribution from lua.org, unchanged, built as the reference Lua. Not in Git: configuring downloads it when missing |

## Build

```sh
cmake --preset debug             # configure (once, or after editing CMake files)
cmake --build --preset debug     # build
./build/debug/lua2               # run
```

Use `release` instead of `debug` for an optimized build, and `asan` for a debug build with
AddressSanitizer (it gives the programs an 8 MB stack, since sanitized frames are larger).

## Run

```sh
./build/debug/lua2 file.lua             # run a file; then prints what it returns, if anything
./build/debug/lua2 --generational file.lua   # the same, with the generational collector
./build/debug/lua2                      # print the version (also -v, --version)
./build/debug/lua2 --tokens file.lua    # print every token of a file (lexer debugging)
./build/debug/lua2 --ast file.lua       # print the syntax tree of a file (parser debugging)
./build/debug/lua2 --ast=dot file.lua > tree.dot   # the tree as a Graphviz graph
dot -Tsvg tree.dot -o tree.svg          # draw it (needs Graphviz: pacman -S mingw-w64-clang-x86_64-graphviz)
./build/debug/lua2 --bytecode file.lua  # the compiled bytecode, listed like `luac -l -l`
```

The standard library so far is `print`; the rest comes in stage 9. Globals work (they live in
the globals table), but until tables (stage 6) that is the only table: `{}` is an error, and so
is a key that is not a string. Errors go to stderr as `lua2: file.lua:3: message`.

## Test

```sh
ctest --preset debug                         # run all tests
./build/debug/lua2_tests                     # run Catch2 directly
./build/debug/lua2_tests "[some-tag]"        # run tests with a tag
./build/debug/lua2_tests "[conformance]"     # only the official Lua test suite checks
cmake --preset asan && cmake --build --preset asan && ctest --preset asan   # under AddressSanitizer
```

Lua programs used as tests live in `tests/fixtures/`: the official Lua 5.4.9 suite, and
`my_lua/`, where every Lua program written while developing lua2 is kept with its expected
`--ast` output, and optionally its `--ast=dot` and `--bytecode` output and what running it
returns or the error it stops with (see `tests/fixtures/my_lua/README.md`).

```sh
./build/debug/lua2_tests "[vm]"              # the compiler and the VM
./build/debug/lua2_tests "[gc]"              # the garbage collector (and GC stress runs)
./build/debug/lua2_tests "[my_lua]"          # the my_lua programs: parse, compile, run, stress
```

## Benchmark

```sh
cmake --build --preset release
./build/release/lua2_bench bench ./build/release/lua2                          # lua2 alone
./build/release/lua2_bench bench ./build/release/lua2 ./build/release/lua_ref  # against real Lua
```

The runner prints the best of 5 runs of each program in `bench/` and the ratio to the
reference (results in `docs/final_docs/vm.md`). The reference is the real Lua 5.4.9, built from
`original_lua_src/` with the rest of the project: `lua_ref` (the interpreter) and `luac_ref`
(the compiler, for bytecode listings). The folder is only a reference, so Git ignores it; when
it is missing, `cmake --preset ...` downloads `lua-5.4.9.tar.gz` from lua.org, checks its
SHA-256 and extracts it there. Without a network (or with a wrong checksum) it warns and builds
lua2 without them.

```sh
./build/release/lua_ref file.lua            # run a file with the real Lua
./build/release/lua_ref -e "print(dofile('file.lua'))"   # print what it returns, like lua2 does
./build/release/luac_ref -l -l -p file.lua  # the real Lua's bytecode for a file
cmake --preset release -DLUA2_REFERENCE_LUA=OFF   # build without them (and never download)
rm -rf original_lua_src && cmake --preset release # download a fresh copy
```

The VM's dispatch is a build option: tail calls between instruction functions (the default,
fastest, needs clang) or a `switch` in a loop:

```sh
cmake --preset release -B build/release-switch -DLUA2_DISPATCH=switch
cmake --build build/release-switch
```

## Format

```sh
cmake --build --preset debug --target format        # rewrite files in place
cmake --build --preset debug --target format-check  # fail if anything is unformatted
```

Style is in `.clang-format`.

## Lint and static analysis

```sh
cmake --build --preset debug --target tidy   # clang-tidy + Clang Static Analyzer
```

Checks are in `.clang-tidy` (tests add exceptions in `tests/.clang-tidy`).
To run clang-tidy on every compile instead:

```sh
cmake --preset debug -DLUA2_CLANG_TIDY=ON
```

Editors using clangd pick up the same checks through `.clangd`.

## Before committing

```sh
cmake --build --preset debug --target format
cmake --build --preset debug
ctest --preset debug
cmake --build --preset debug --target tidy
ctest --preset asan        # after building the asan preset, for changes to memory handling
```

## Version

The version lives in `project(... VERSION ...)` in `CMakeLists.txt`.
It goes up by 0.0.1 per roadmap step until the release, which becomes 5.4.
