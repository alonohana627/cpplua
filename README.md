# cpplua

A Lua 5.4 interpreter written from scratch in C++26, built step by step.
The plan is in [docs/ROADMAP.md](docs/ROADMAP.md); background reading for each step is in [docs/learn/](docs/learn/), and the exact rules we implement are in [docs/spec/](docs/spec/). To call C++ from Lua and Lua from C++ (functions, classes, callbacks), see [docs/how_to_put_cpp_functions_and_classes_into_lua.md](docs/how_to_put_cpp_functions_and_classes_into_lua.md).

## Requirements

- clang++ with libc++ (C++26, `-std=c++2c`). CI uses clang and libc++ 23; libc++ 18 is too old
  (it has no floating-point `std::from_chars` or `std::atomic_ref`)
- CMake 3.30+ (the first to know `cxx_std_26`) and Ninja
- Python 3 (for `run-clang-tidy`)
- clang-format and clang-tidy

Catch2 is downloaded by CMake on first configure.

## Layout

| Folder              | Contents                                                           |
| ------------------- | ------------------------------------------------------------------ |
| `include/cpplua/`   | Public headers, the embedding API (`#include <cpplua/...>`)        |
| `lib/`              | The embeddable interpreter library (`cpplua::cpplua`), `.cpp` only |
| `lib/internal/`     | The library's private headers, not visible to embedders            |
| `src/`              | The `cpplua` binary that runs Lua, built on `lib/`                 |
| `tests/`            | Catch2 tests                                                       |
| `bench/`            | Benchmark programs and their runner (`bench/README.md`)            |
| `std/`              | The standard library's reference: every function as an empty Lua function with its documentation (the code is C++, in `lib/std/`) |
| `original_lua_src/` | The original Lua 5.4.9 distribution from lua.org, unchanged, built as the reference Lua. Not in Git: configuring downloads it when missing |

## Build

```sh
cmake --preset debug             # configure (once, or after editing CMake files)
cmake --build --preset debug     # build
./build/debug/cpplua             # run
```

Use `release` instead of `debug` for an optimized build, `asan` for a debug build with
AddressSanitizer, and `sanitize` for AddressSanitizer and UndefinedBehaviorSanitizer together
(what CI runs on Linux). On Windows the sanitized programs get an 8 MB stack, since sanitized
frames are larger.

## Install

cpplua installs as two parts: the `cpplua` interpreter, and the library for embedding
(`libcpplua.a`, `include/cpplua/`, a CMake package and a pkg-config file). A program that
embeds cpplua must be built with clang, libc++ and C++26, like cpplua itself.

A project that adds cpplua with `add_subdirectory` or FetchContent gets the library only; set
`CPPLUA_BUILD_CLI=ON` to build the `cpplua` program too.

From a release build:

```sh
cmake --install build/release --prefix <dir>                  # everything
cmake --install build/release --prefix <dir> --component lib  # the library only
```

Packages, made in the build folder (`.zip` on Windows, `.tar.gz` and `.deb` on Linux):

```sh
cd build/release
cpack                                # cpplua-<version>-<system>: everything
cpack --config CPackLibConfig.cmake  # libcpplua-<version>-<system>: the library only
sudo apt install ./cpplua_*.deb      # Linux: the interpreter and the library, under /usr
sudo apt install ./libcpplua-dev_*.deb   # Linux: the library only
```

Using the library from CMake, once installed (add `<dir>` to `CMAKE_PREFIX_PATH`):

```cmake
find_package(cpplua 5.4 CONFIG REQUIRED)
target_link_libraries(my_program PRIVATE cpplua::cpplua)
```

Or without installing, from the source (cpplua then builds only the library and the
interpreter, no tests):

```cmake
include(FetchContent)
FetchContent_Declare(cpplua GIT_REPOSITORY https://github.com/alonohana627/cpplua.git GIT_TAG v5.4.9-beta)
FetchContent_MakeAvailable(cpplua)
target_link_libraries(my_program PRIVATE cpplua::cpplua)
```

Without CMake: `clang++ -std=c++2c -stdlib=libc++ main.cpp $(pkg-config --cflags --libs cpplua)`.
`tests/install/` is such a program, and CI builds it against every kind of install.

## CI

`.github/workflows/ci.yml` builds a release on Windows (MSYS2 CLANG64) and Ubuntu (x86-64,
clang and libc++ from apt.llvm.org), runs every test, and checks every install above
(`.github/scripts/verify_install.sh`); the packages are kept as the run's artifacts. On Ubuntu it
also runs the tests with the `sanitize` preset (ASan + UBSan) and fails on any clang-format or
clang-tidy finding. Pushing a
tag `v<version>` (such as `v5.4.9-beta`) also publishes them as a GitHub release. To check the
installs locally, after a release build:

```sh
.github/scripts/verify_install.sh build/release
```

## Run

`cpplua` is the real `lua` program (a port of `lua.c`), with the same options
([docs/final_docs/standalone.md](docs/final_docs/standalone.md)):

```sh
./build/debug/cpplua file.lua a b       # run a file, with arg[1] = "a", arg[2] = "b"
./build/debug/cpplua                    # the interactive loop (Ctrl-C stops running code)
./build/debug/cpplua -e "print(1+1)"    # run a string (-e and -l may repeat, in order)
./build/debug/cpplua -l mod file.lua    # require "mod" into the global mod first (-l m=mod: into m)
./build/debug/cpplua -i file.lua        # run a file, then the interactive loop
./build/debug/cpplua -v                 # print the version (also --version)
./build/debug/cpplua -E file.lua        # ignore LUA_INIT, LUA_PATH and LUA_CPATH
./build/debug/cpplua -W file.lua        # turn warnings on
./build/debug/cpplua - < file.lua       # run the standard input
./build/debug/cpplua file.luac          # binary chunks load too (string.dump, luac_ref -o)
./build/debug/cpplua --incremental file.lua  # the incremental collector (generational is the default)
```

cpplua's own debug views come first on the command line:

```sh
./build/debug/cpplua --tokens file.lua  # print every token of a file (lexer debugging)
./build/debug/cpplua --ast file.lua     # print the syntax tree of a file (parser debugging)
./build/debug/cpplua --ast=dot file.lua > tree.dot # the tree as a Graphviz graph
dot -Tsvg tree.dot -o tree.svg          # draw it (needs Graphviz: pacman -S mingw-w64-clang-x86_64-graphviz)
./build/debug/cpplua --bytecode file.lua # the compiled bytecode, listed like `luac -l -l`
```

The whole language runs, with tables, metatables, `<close>`, finalizers, weak tables and
coroutines, and the whole standard library: the base library, `require` and `package`,
`coroutine`, `string`, `table`, `math`, `utf8`, `io`, `os` and `debug`, all written in C++
([docs/final_docs/stdlib.md](docs/final_docs/stdlib.md)). It passes the official Lua 5.4.9 test
suite ([docs/final_docs/conformance.md](docs/final_docs/conformance.md)). The Lua files in `std/` describe every
library function for readers and editors: point lua-language-server at them
(`"workspace.library": ["std"]`) for completion and hover help. An uncaught error goes to stderr as
`cpplua: file.lua:3: message` followed by a stack traceback, as the real `lua` prints it.

## Test

```sh
ctest --preset debug                         # run all tests
./build/debug/cpplua_tests                   # run Catch2 directly
./build/debug/cpplua_tests "[some-tag]"      # run tests with a tag
./build/debug/cpplua_tests "[conformance]"   # every file of the official suite lexes, parses and compiles
ctest --preset debug -R lua_5_4_test_suite   # the official Lua 5.4.9 test suite, whole (all.lua)
cd tests/fixtures/lua-5.4.9-tests && ../../../build/release/cpplua -e "_port=true" all.lua # by hand
cmake --preset asan && cmake --build --preset asan && ctest --preset asan   # under AddressSanitizer
cmake --preset sanitize && cmake --build --preset sanitize && ctest --preset sanitize   # ASan + UBSan
```

Lua programs used as tests live in `tests/fixtures/`: the official Lua 5.4.9 suite (unchanged
from lua.org; `_port=true` skips its Unix-only parts), and
`my_lua/`, where every Lua program written while developing cpplua is kept with its expected
`--ast` output, and optionally its `--ast=dot` and `--bytecode` output and what running it
returns or the error it stops with (see `tests/fixtures/my_lua/README.md`).

```sh
./build/debug/cpplua_tests "[vm]"            # the compiler and the VM
./build/debug/cpplua_tests "[gc]"            # the garbage collector (and GC stress runs)
./build/debug/cpplua_tests "[table]"         # tables (docs/spec/tables.md)
./build/debug/cpplua_tests "[meta]"          # metatables, <close>, finalizers, weak tables
./build/debug/cpplua_tests "[error]"         # error, pcall, xpcall, tracebacks
./build/debug/cpplua_tests "[stdlib]"        # the standard library, and std/*.lua against it
./build/debug/cpplua_api_tests               # the public C++ API, with the embedding guide's examples
./build/debug/cpplua_tests "[my_lua]"        # the my_lua programs: parse, compile, run, stress
```

## Benchmark

```sh
cmake --build --preset release
./build/release/cpplua_bench bench ./build/release/cpplua                      # cpplua alone
./build/release/cpplua_bench bench ./build/release/cpplua ./build/release/lua_ref # against real Lua
```

The runner prints the best of 5 runs of each program in `bench/` and the ratio to the
reference (results in `docs/final_docs/vm.md`). The reference is the real Lua 5.4.9, built from
`original_lua_src/` with the rest of the project: `lua_ref` (the interpreter) and `luac_ref`
(the compiler, for bytecode listings). The folder is only a reference, so Git ignores it; when
it is missing, `cmake --preset ...` downloads `lua-5.4.9.tar.gz` from lua.org, checks its
SHA-256 and extracts it there. Without a network (or with a wrong checksum) it warns and builds
cpplua without them.

```sh
./build/release/lua_ref file.lua            # run a file with the real Lua
./build/release/lua_ref -e "print(dofile('file.lua'))"   # print what it returns, like cpplua does
./build/release/luac_ref -l -l -p file.lua  # the real Lua's bytecode for a file
./build/release/luac_ref -o file.luac file.lua   # a binary chunk, which cpplua runs too
cmake --preset release -DCPPLUA_REFERENCE_LUA=OFF   # build without them (and never download)
rm -rf original_lua_src && cmake --preset release # download a fresh copy
```

The VM's dispatch is a build option: tail calls between instruction functions (the default,
fastest, needs clang) or a `switch` in a loop:

```sh
cmake --preset release -B build/release-switch -DCPPLUA_DISPATCH=switch
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
cmake --preset debug -DCPPLUA_CLANG_TIDY=ON
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

The version lives in `project(... VERSION ...)` in `CMakeLists.txt`, which follows the Lua
version cpplua implements (5.4.9), and `CPPLUA_VERSION_SUFFIX` next to it (`beta` until
installing is verified by hand; empty for the release).
