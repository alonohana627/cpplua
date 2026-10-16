# lua2

A Lua 5.4 interpreter written from scratch in C++26, built step by step.
The plan is in [docs/ROADMAP.md](docs/ROADMAP.md).

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
| `lib/`          | The embeddable interpreter library (`lua2::lua2`)            |
| `src/`          | The standalone `lua2` command-line program                   |
| `tests/`        | Catch2 tests                                                 |

## Build

```sh
cmake --preset debug             # configure (once, or after editing CMake files)
cmake --build --preset debug     # build
./build/debug/lua2               # run
```

Use `release` instead of `debug` for an optimized build.

## Test

```sh
ctest --preset debug                         # run all tests
./build/debug/lua2_tests                     # run Catch2 directly
./build/debug/lua2_tests "[some-tag]"        # run tests with a tag
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
```

## Version

The version lives in `project(... VERSION ...)` in `CMakeLists.txt`.
It goes up by 0.0.1 per roadmap step until the release, which becomes 5.4.
