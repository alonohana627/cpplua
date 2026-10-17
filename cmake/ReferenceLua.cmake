# The reference Lua (docs/plan/vm.md, part 0): the real Lua 5.4.9, built from
# its source, to measure lua2 against and to compare results and bytecode.
#
#   lua_ref    the `lua` interpreter
#   luac_ref   the `luac` compiler (`luac_ref -l -l -p file.lua` lists bytecode)
#
# The source is the original distribution, unchanged, in original_lua_src/:
# lua-5.4.9.tar.gz from https://www.lua.org/ftp/. The folder is only a
# reference, so Git ignores it; when it is missing, configuring downloads the
# archive, checks its SHA-256 and extracts it there. If that cannot be done
# (no network, a wrong checksum), the reference is skipped with a warning and
# lua2 itself builds as usual.

set(LUA_REF_VERSION 5.4.9)
set(LUA_REF_URL https://www.lua.org/ftp/lua-${LUA_REF_VERSION}.tar.gz)
set(LUA_REF_SHA256 2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6)
set(LUA_REF_DIR ${PROJECT_SOURCE_DIR}/original_lua_src)
set(LUA_SOURCE_DIR ${LUA_REF_DIR}/src)

# Gets original_lua_src/ if it is not there. Sets LUA_REF_AVAILABLE.
function(lua2_fetch_reference_lua)
  set(LUA_REF_AVAILABLE TRUE PARENT_SCOPE)
  if(EXISTS ${LUA_SOURCE_DIR}/lua.c)
    return()
  endif()
  set(LUA_REF_AVAILABLE FALSE PARENT_SCOPE)
  if(EXISTS ${LUA_REF_DIR})
    message(WARNING "original_lua_src/ exists but has no src/lua.c; delete it to download "
                    "the reference Lua again. Skipping lua_ref and luac_ref.")
    return()
  endif()

  message(STATUS "Reference Lua: downloading ${LUA_REF_URL}")
  set(archive ${PROJECT_BINARY_DIR}/lua-${LUA_REF_VERSION}.tar.gz)
  file(DOWNLOAD ${LUA_REF_URL} ${archive} STATUS status TLS_VERIFY ON)
  list(GET status 0 code)
  if(NOT code EQUAL 0)
    list(GET status 1 reason)
    message(WARNING "Could not download the reference Lua (${reason}). "
                    "Skipping lua_ref and luac_ref.")
    file(REMOVE ${archive})
    return()
  endif()
  file(SHA256 ${archive} actual)
  if(NOT actual STREQUAL LUA_REF_SHA256)
    message(WARNING "The downloaded ${archive} has SHA-256 ${actual}, not the expected "
                    "${LUA_REF_SHA256}. Skipping lua_ref and luac_ref.")
    file(REMOVE ${archive})
    return()
  endif()

  # Extract next to the build, then move the distribution's folder into
  # place in one step, so an interrupted extraction leaves no half folder.
  set(staging ${PROJECT_BINARY_DIR}/lua-ref-extract)
  file(REMOVE_RECURSE ${staging})
  file(ARCHIVE_EXTRACT INPUT ${archive} DESTINATION ${staging})
  file(RENAME ${staging}/lua-${LUA_REF_VERSION} ${LUA_REF_DIR})
  file(REMOVE_RECURSE ${staging})
  file(REMOVE ${archive})
  message(STATUS "Reference Lua: extracted into original_lua_src/")
  set(LUA_REF_AVAILABLE TRUE PARENT_SCOPE)
endfunction()

lua2_fetch_reference_lua()
if(NOT LUA_REF_AVAILABLE)
  return()
endif()

enable_language(C)

set(LUA_CORE
  lapi lcode lctype ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes lparser lstate
  lstring ltable ltm lundump lvm lzio)
set(LUA_LIBRARIES
  lauxlib lbaselib lcorolib ldblib liolib lmathlib loadlib loslib lstrlib ltablib lutf8lib linit)
list(TRANSFORM LUA_CORE PREPEND ${LUA_SOURCE_DIR}/ OUTPUT_VARIABLE LUA_CORE_FILES)
list(TRANSFORM LUA_CORE_FILES APPEND .c)
list(TRANSFORM LUA_LIBRARIES PREPEND ${LUA_SOURCE_DIR}/ OUTPUT_VARIABLE LUA_LIBRARY_FILES)
list(TRANSFORM LUA_LIBRARY_FILES APPEND .c)

# Built the way Lua's own makefile builds it: C99, -O2, LUA_COMPAT_5_3, with
# no warnings of ours (it is not our code).
add_library(lua_ref_core STATIC ${LUA_CORE_FILES} ${LUA_LIBRARY_FILES})
target_compile_definitions(lua_ref_core PUBLIC LUA_COMPAT_5_3)
target_compile_options(lua_ref_core PRIVATE -O2 -w)
set_target_properties(lua_ref_core PROPERTIES C_STANDARD 99)

add_executable(lua_ref ${LUA_SOURCE_DIR}/lua.c)
target_link_libraries(lua_ref PRIVATE lua_ref_core)
target_compile_options(lua_ref PRIVATE -O2 -w)

# luac uses Lua's internal headers, and links the core only.
add_executable(luac_ref ${LUA_SOURCE_DIR}/luac.c)
target_link_libraries(luac_ref PRIVATE lua_ref_core)
target_compile_options(luac_ref PRIVATE -O2 -w)
