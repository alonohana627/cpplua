cmake_minimum_required(VERSION 3.28)
# Pre-release: bump by 0.0.1 per step. The release becomes 5.4.
project(lua2 VERSION 0.0.8 LANGUAGES CXX)

# C++26 (clang spells it -std=c++2c), no compiler extensions.
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Warnings applied to our own targets only (not to Catch2).
add_library(lua2_warnings INTERFACE)
target_compile_options(lua2_warnings INTERFACE
  -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)

# Layout:
#   include/lua2/  public headers, the API embedders see (#include <lua2/...>)
#   lib/           the embeddable interpreter library (.cpp files)
#   lib/internal/  the library's private headers, kept apart from the sources
#   src/           the `lua2` binary that runs Lua, built on lib

# The interpreter library. Embedders link lua2::lua2.
add_library(lua2_lib
  lib/version.cpp
  lib/syntax_error.cpp
  lib/debug.cpp
  lib/source_file.cpp
  lib/text_format.cpp
  lib/numeral.cpp
  lib/lexer/lexer.cpp
  lib/lexer/token.cpp
  lib/parser/ast.cpp
  lib/parser/ast_printer.cpp
  lib/parser/parser.cpp
  lib/parser/scopes.cpp
  lib/value/value.cpp
  lib/value/arith.cpp
  lib/value/bitwise.cpp
  lib/value/compare.cpp
  lib/value/convert.cpp
  lib/value/string.cpp
  lib/gc/pool.cpp
  lib/gc/heap.cpp
  lib/gc/collector.cpp
  lib/vm/proto.cpp
  lib/vm/state.cpp
  lib/vm/compiler.cpp
  lib/vm/debug_info.cpp
  lib/vm/execute.cpp
  lib/vm/run.cpp
  lib/vm/metamethods.cpp
  lib/value/table.cpp
  lib/std/base.cpp
  lib/std/auxlib.cpp
  lib/api.cpp
)
add_library(lua2::lua2 ALIAS lua2_lib)
target_include_directories(lua2_lib
  PUBLIC  include
  PRIVATE lib/internal)
target_link_libraries(lua2_lib PRIVATE lua2_warnings)
# The single source of the version string is project() above.
target_compile_definitions(lua2_lib PRIVATE LUA2_VERSION="${PROJECT_VERSION}")

# How the VM goes from one instruction to the next (lib/vm/execute.cpp):
# tail calls between instruction functions (clang only, the fastest), or a
# switch in a loop.
set(LUA2_DISPATCH "tailcall" CACHE STRING "VM dispatch: tailcall or switch")
set_property(CACHE LUA2_DISPATCH PROPERTY STRINGS tailcall switch)
if(LUA2_DISPATCH STREQUAL "switch")
  target_compile_definitions(lua2_lib PRIVATE LUA2_DISPATCH_SWITCH)
endif()

# The command-line front end, built on the public API only.
add_executable(lua2 src/main.cpp)
target_link_libraries(lua2 PRIVATE lua2::lua2 lua2_warnings)
# Optimized builds link the C++ runtime in: loading libc++.dll costs a
# millisecond or two at every start (measured against the reference Lua,
# which needs no such DLL).
if(MINGW AND CMAKE_BUILD_TYPE STREQUAL "Release")
  target_link_options(lua2 PRIVATE -static)
endif()

# Tests
include(CTest)
if(BUILD_TESTING)
  include(FetchContent)
  FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.9.1
    GIT_SHALLOW    TRUE
    SYSTEM)  # treat Catch2's headers as system headers: no warnings from its macros
  FetchContent_MakeAvailable(Catch2)

  add_executable(lua2_tests
    tests/version_test.cpp
    tests/lexer_test.cpp
    tests/syntax_error_test.cpp
    tests/debug_test.cpp
    tests/conformance_test.cpp
    tests/parser_test.cpp
    tests/my_lua_test.cpp
    tests/source_file_test.cpp
    tests/value_test.cpp
    tests/arith_test.cpp
    tests/bitwise_test.cpp
    tests/compare_test.cpp
    tests/string_test.cpp
    tests/convert_test.cpp
    tests/coerce_test.cpp
    tests/values_conformance_test.cpp
    tests/opcodes_test.cpp
    tests/gc_test.cpp
    tests/vm_test.cpp
    tests/table_test.cpp
    tests/meta_test.cpp
    tests/error_test.cpp
  )
  target_link_libraries(lua2_tests PRIVATE lua2::lua2 lua2_warnings Catch2::Catch2WithMain)
  # Tests may include internal headers from lib/internal/ to test internals directly.
  target_include_directories(lua2_tests PRIVATE lib/internal)
  target_compile_definitions(lua2_tests PRIVATE
    LUA2_VERSION="${PROJECT_VERSION}"
    LUA2_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures")

  # The public API's tests, which see only include/lua2 (no lib/internal):
  # what a program embedding lua2 can use.
  add_executable(lua2_api_tests tests/embedding_test.cpp)
  target_link_libraries(lua2_api_tests PRIVATE lua2::lua2 lua2_warnings Catch2::Catch2WithMain)

  include(Catch)
  catch_discover_tests(lua2_tests)
  catch_discover_tests(lua2_api_tests)
endif()

# The benchmark runner (bench/README.md).
add_executable(lua2_bench bench/runner.cpp)
target_link_libraries(lua2_bench PRIVATE lua2_warnings)

# The real Lua, from original_lua_src/, to measure and compare against
# (cmake/ReferenceLua.cmake).
option(LUA2_REFERENCE_LUA "Build the reference Lua 5.4.9 (lua_ref, luac_ref)" ON)
if(LUA2_REFERENCE_LUA)
  include(cmake/ReferenceLua.cmake)
endif()

# format / format-check / tidy targets. Last, so it can see our targets.
include(cmake/Tooling.cmake)
