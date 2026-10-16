cmake_minimum_required(VERSION 3.28)
project(lua2 VERSION 0.1.0 LANGUAGES CXX)

# C++26 (clang spells it -std=c++2c), no compiler extensions.
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Warnings applied to our own targets only (not to Catch2).
add_library(lua2_warnings INTERFACE)
target_compile_options(lua2_warnings INTERFACE
  -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)

# The interpreter itself lives in a library so tests can link it directly.
add_library(lua2_core
  src/core/version.cpp
)
target_include_directories(lua2_core PUBLIC src)
target_link_libraries(lua2_core PRIVATE lua2_warnings)

# The command-line front end: the future `lua` executable.
add_executable(lua2 src/main.cpp)
target_link_libraries(lua2 PRIVATE lua2_core lua2_warnings)

# Tests
include(CTest)
if(BUILD_TESTING)
  include(FetchContent)
  FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.9.1
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(Catch2)

  add_executable(lua2_tests
    tests/version_test.cpp
  )
  target_link_libraries(lua2_tests PRIVATE lua2_core lua2_warnings Catch2::Catch2WithMain)

  include(Catch)
  catch_discover_tests(lua2_tests)
endif()
