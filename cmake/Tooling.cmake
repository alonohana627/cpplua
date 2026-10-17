# Formatter, linter and static analyzer targets.
#
#   cmake --build --preset debug --target format        rewrite files with clang-format
#   cmake --build --preset debug --target format-check  fail if any file is unformatted
#   cmake --build --preset debug --target tidy          clang-tidy + Clang Static Analyzer, in parallel
#
# Or set LUA2_CLANG_TIDY=ON to run clang-tidy on every compile (slower builds).

file(GLOB_RECURSE LUA2_SOURCES CONFIGURE_DEPENDS
  ${PROJECT_SOURCE_DIR}/include/*.hpp
  ${PROJECT_SOURCE_DIR}/lib/*.hpp
  ${PROJECT_SOURCE_DIR}/lib/*.cpp
  ${PROJECT_SOURCE_DIR}/src/*.hpp
  ${PROJECT_SOURCE_DIR}/src/*.cpp
  ${PROJECT_SOURCE_DIR}/tests/*.hpp
  ${PROJECT_SOURCE_DIR}/tests/*.cpp
  ${PROJECT_SOURCE_DIR}/bench/*.cpp)

find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
  add_custom_target(format
    COMMAND ${CLANG_FORMAT} -i ${LUA2_SOURCES}
    COMMENT "clang-format: rewriting sources"
    VERBATIM)
  add_custom_target(format-check
    COMMAND ${CLANG_FORMAT} --dry-run --Werror ${LUA2_SOURCES}
    COMMENT "clang-format: checking sources"
    VERBATIM)
endif()

find_program(CLANG_TIDY clang-tidy)
find_program(RUN_CLANG_TIDY run-clang-tidy)
find_package(Python3 COMPONENTS Interpreter)
if(CLANG_TIDY AND RUN_CLANG_TIDY AND Python3_FOUND)
  # run-clang-tidy reads compile_commands.json and checks one file per core.
  # The regex skips fetched dependencies (Catch2 under _deps/) and the original
  # Lua source (original_lua_src/), which are not our code.
  add_custom_target(tidy
    COMMAND ${Python3_EXECUTABLE} ${RUN_CLANG_TIDY}
      -p ${PROJECT_BINARY_DIR}
      -clang-tidy-binary ${CLANG_TIDY}
      -quiet
      "^(?!.*(_deps|original_lua_src))"
    COMMENT "clang-tidy: linting and analyzing"
    VERBATIM)
endif()

option(LUA2_CLANG_TIDY "Run clang-tidy as part of every compile" OFF)
if(LUA2_CLANG_TIDY AND CLANG_TIDY)
  foreach(target IN ITEMS lua2_lib lua2 lua2_tests)
    if(TARGET ${target})
      set_target_properties(${target} PROPERTIES CXX_CLANG_TIDY ${CLANG_TIDY})
    endif()
  endforeach()
endif()
