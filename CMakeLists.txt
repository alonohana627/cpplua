cmake_minimum_required(VERSION 3.28)
# The release line follows the Lua version it implements (5.4.9). The suffix
# marks a pre-release; drop it once installing has been verified by hand.
project(cpplua VERSION 5.4.9 LANGUAGES CXX
  DESCRIPTION "Lua 5.4 in C++26, embeddable through a C++ API"
  HOMEPAGE_URL "https://github.com/alonohana627/cpplua")
set(CPPLUA_VERSION_SUFFIX "beta")
set(CPPLUA_VERSION_STRING "${PROJECT_VERSION}")
if(CPPLUA_VERSION_SUFFIX)
  string(APPEND CPPLUA_VERSION_STRING "-${CPPLUA_VERSION_SUFFIX}")
endif()

# C++26 (clang spells it -std=c++2c), no compiler extensions.
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(PROJECT_IS_TOP_LEVEL)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

# Tests, the benchmark runner, the reference Lua and the tooling targets are
# for working on cpplua itself: a project that pulls cpplua in with
# add_subdirectory or FetchContent gets only the library and the binary.
option(CPPLUA_BUILD_TESTS "Build cpplua's tests" ${PROJECT_IS_TOP_LEVEL})
option(CPPLUA_INSTALL "Generate cpplua's install rules" ${PROJECT_IS_TOP_LEVEL})

# Warnings applied to our own targets only (not to Catch2).
add_library(cpplua_warnings INTERFACE)
target_compile_options(cpplua_warnings INTERFACE
  -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)

# Layout:
#   include/cpplua/  public headers, the API embedders see (#include <cpplua/...>)
#   lib/           the embeddable interpreter library (.cpp files)
#   lib/internal/  the library's private headers, kept apart from the sources
#   src/           the `cpplua` binary that runs Lua, built on lib

# The interpreter library. Embedders link cpplua::cpplua.
add_library(cpplua_lib
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
  lib/value/locale.cpp
  lib/value/string.cpp
  lib/gc/pool.cpp
  lib/gc/heap.cpp
  lib/gc/collector.cpp
  lib/vm/proto.cpp
  lib/vm/state.cpp
  lib/vm/compiler.cpp
  lib/vm/debug_info.cpp
  lib/vm/dump.cpp
  lib/vm/execute.cpp
  lib/vm/run.cpp
  lib/vm/metamethods.cpp
  lib/value/table.cpp
  lib/std/base.cpp
  lib/std/auxlib.cpp
  lib/std/coroutine.cpp
  lib/std/debug.cpp
  lib/std/io.cpp
  lib/std/libs.cpp
  lib/std/math.cpp
  lib/std/os.cpp
  lib/std/package.cpp
  lib/std/string.cpp
  lib/std/table.cpp
  lib/std/utf8.cpp
  lib/api.cpp
  lib/standalone.cpp
)
add_library(cpplua::cpplua ALIAS cpplua_lib)
# The public headers. A file set is what installs them, and it puts include/
# on the include path both here and in an installed copy.
target_sources(cpplua_lib PUBLIC
  FILE_SET HEADERS
  BASE_DIRS include
  FILES
    include/cpplua/api.hpp
    include/cpplua/debug.hpp
    include/cpplua/interpreter.hpp
    include/cpplua/source_file.hpp
    include/cpplua/standalone.hpp
    include/cpplua/type.hpp
    include/cpplua/version.hpp)
target_include_directories(cpplua_lib PRIVATE lib/internal)
# Whoever includes <cpplua/...> needs C++26 too.
target_compile_features(cpplua_lib PUBLIC cxx_std_26)
# BUILD_INTERFACE: the warnings are ours to build with, not part of what is
# installed (a static library's private links are exported otherwise).
target_link_libraries(cpplua_lib PRIVATE $<BUILD_INTERFACE:cpplua_warnings>)
# libcpplua.a / cpplua.lib, linked as cpplua::cpplua.
set_target_properties(cpplua_lib PROPERTIES OUTPUT_NAME cpplua EXPORT_NAME cpplua)
# The single source of the version string is project() above.
target_compile_definitions(cpplua_lib PRIVATE CPPLUA_VERSION="${CPPLUA_VERSION_STRING}")

# How the VM goes from one instruction to the next (lib/vm/execute.cpp):
# tail calls between instruction functions (clang only, the fastest), or a
# switch in a loop.
set(CPPLUA_DISPATCH "tailcall" CACHE STRING "VM dispatch: tailcall or switch")
set_property(CACHE CPPLUA_DISPATCH PROPERTY STRINGS tailcall switch)
if(CPPLUA_DISPATCH STREQUAL "switch")
  target_compile_definitions(cpplua_lib PRIVATE CPPLUA_DISPATCH_SWITCH)
endif()

# The command-line front end, built on the public API only.
add_executable(cpplua src/main.cpp)
target_link_libraries(cpplua PRIVATE cpplua::cpplua cpplua_warnings)
# Optimized builds link the C++ runtime in: loading libc++.dll costs a
# millisecond or two at every start (measured against the reference Lua,
# which needs no such DLL).
if(MINGW AND CMAKE_BUILD_TYPE STREQUAL "Release")
  target_link_options(cpplua PRIVATE -static)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_BUILD_TYPE STREQUAL "Release")
  # Linux distributions rarely ship libc++, so the installed binary carries
  # it (and libc++abi, which -static-libstdc++ would leave out) inside and
  # needs only the C library.
  target_link_options(cpplua PRIVATE -nostdlib++)
  target_link_libraries(cpplua PRIVATE -Wl,-Bstatic c++ c++abi -Wl,-Bdynamic m)
endif()

# Tests
if(CPPLUA_BUILD_TESTS)
  enable_testing()
  include(FetchContent)
  FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.9.1
    GIT_SHALLOW    TRUE
    SYSTEM)  # treat Catch2's headers as system headers: no warnings from its macros
  FetchContent_MakeAvailable(Catch2)

  add_executable(cpplua_tests
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
    tests/stdlib_test.cpp
  )
  target_link_libraries(cpplua_tests PRIVATE cpplua::cpplua cpplua_warnings Catch2::Catch2WithMain)
  # Tests may include internal headers from lib/internal/ to test internals directly.
  target_include_directories(cpplua_tests PRIVATE lib/internal)
  target_compile_definitions(cpplua_tests PRIVATE
    CPPLUA_VERSION="${CPPLUA_VERSION_STRING}"
    CPPLUA_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures"
    CPPLUA_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

  # The public API's tests, which see only include/cpplua (no lib/internal):
  # what a program embedding cpplua can use.
  add_executable(cpplua_api_tests tests/embedding_test.cpp)
  target_link_libraries(cpplua_api_tests PRIVATE cpplua::cpplua cpplua_warnings Catch2::Catch2WithMain)

  include(Catch)
  # ctest lists the tests when it runs. Test names must not contain ';':
  # Catch2's CatchAddTests.cmake reads the listing as a CMake list, and a ';'
  # after an unbalanced '[' in some name (the order changes run to run) cut
  # it, so every test came out NOTFOUND now and then.
  catch_discover_tests(cpplua_tests DISCOVERY_MODE PRE_TEST)
  catch_discover_tests(cpplua_api_tests)

  # The official Lua 5.4.9 test suite (stage 11): all.lua prints
  # "final OK !!!" when every file passes. _port skips the tests that need
  # a POSIX system.
  add_test(NAME lua_5_4_test_suite
    COMMAND cpplua -e "_port=true" all.lua
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/tests/fixtures/lua-5.4.9-tests)
  set_tests_properties(lua_5_4_test_suite PROPERTIES
    PASS_REGULAR_EXPRESSION "final OK !!!"
    TIMEOUT 600)
endif()

# Installing: `cmake --install` puts in everything, `--component lib` only
# what embedding needs (cmake/Install.cmake).
if(CPPLUA_INSTALL)
  include(cmake/Install.cmake)
endif()

if(PROJECT_IS_TOP_LEVEL)
  # The benchmark runner (bench/README.md).
  add_executable(cpplua_bench bench/runner.cpp)
  target_link_libraries(cpplua_bench PRIVATE cpplua_warnings)

  # The real Lua, from original_lua_src/, to measure and compare against
  # (cmake/ReferenceLua.cmake).
  option(CPPLUA_REFERENCE_LUA "Build the reference Lua 5.4.9 (lua_ref, luac_ref)" ON)
  if(CPPLUA_REFERENCE_LUA)
    include(cmake/ReferenceLua.cmake)
  endif()

  # format / format-check / tidy targets. Last, so it can see our targets.
  include(cmake/Tooling.cmake)
endif()
