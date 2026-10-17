# Install rules and packages.
#
# Two components:
#   cli   the `cpplua` interpreter (bin/)
#   lib   what embedding needs: the static library, include/cpplua/, the
#         CMake package (find_package(cpplua)) and pkg-config's cpplua.pc
#
#   cmake --install build/release --prefix <dir>                  everything
#   cmake --install build/release --prefix <dir> --component lib  the library only
#
# Packages (CPack), from the build folder:
#   cpack                               cpplua-<version>-<system>: everything
#   cpack --config CPackLibConfig.cmake libcpplua-<version>-<system>: the library only
# as .zip on Windows, .tar.gz and .deb on Linux.

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CPPLUA_CMAKE_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/cpplua)

install(TARGETS cpplua
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT cli)

install(TARGETS cpplua_lib EXPORT cppluaTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT lib
  FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT lib)

install(EXPORT cppluaTargets
  NAMESPACE cpplua::
  DESTINATION ${CPPLUA_CMAKE_DIR}
  COMPONENT lib)

configure_package_config_file(cmake/cppluaConfig.cmake.in
  ${PROJECT_BINARY_DIR}/cppluaConfig.cmake
  INSTALL_DESTINATION ${CPPLUA_CMAKE_DIR})
# 5.4.x releases keep the embedding API; a new Lua minor version may not.
write_basic_package_version_file(${PROJECT_BINARY_DIR}/cppluaConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMinorVersion)
install(FILES
    ${PROJECT_BINARY_DIR}/cppluaConfig.cmake
    ${PROJECT_BINARY_DIR}/cppluaConfigVersion.cmake
  DESTINATION ${CPPLUA_CMAKE_DIR}
  COMPONENT lib)

# pkg-config, for builds that are not CMake. Relative to ${prefix}, so the
# installed tree can move.
file(RELATIVE_PATH CPPLUA_PC_TO_PREFIX
  /prefix/${CMAKE_INSTALL_LIBDIR}/pkgconfig /prefix)
string(REGEX REPLACE "/$" "" CPPLUA_PC_TO_PREFIX "${CPPLUA_PC_TO_PREFIX}")
configure_file(cmake/cpplua.pc.in ${PROJECT_BINARY_DIR}/cpplua.pc @ONLY)
install(FILES ${PROJECT_BINARY_DIR}/cpplua.pc
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
  COMPONENT lib)

install(FILES LICENSE README.md
  DESTINATION ${CMAKE_INSTALL_DOCDIR}
  COMPONENT lib)

# Packages
set(CPACK_PACKAGE_NAME cpplua)
set(CPACK_PACKAGE_VENDOR "Alon Ohana")
set(CPACK_PACKAGE_VERSION ${CPPLUA_VERSION_STRING})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY ${PROJECT_DESCRIPTION})
set(CPACK_PACKAGE_HOMEPAGE_URL ${PROJECT_HOMEPAGE_URL})
set(CPACK_RESOURCE_FILE_LICENSE ${PROJECT_SOURCE_DIR}/LICENSE)
set(CPACK_PACKAGE_CHECKSUM SHA256)
if(WIN32)
  set(CPPLUA_PACKAGE_SYSTEM windows-x86_64)
  set(CPACK_GENERATOR ZIP)
else()
  set(CPPLUA_PACKAGE_SYSTEM linux-x86_64)
  set(CPACK_GENERATOR TGZ DEB)
endif()
set(CPACK_PACKAGE_FILE_NAME cpplua-${CPPLUA_VERSION_STRING}-${CPPLUA_PACKAGE_SYSTEM})
set(CPACK_SOURCE_GENERATOR "")

# Debian: "5.4.9~beta" sorts before the 5.4.9 release.
set(CPACK_DEBIAN_PACKAGE_VERSION ${PROJECT_VERSION})
if(CPPLUA_VERSION_SUFFIX)
  string(APPEND CPACK_DEBIAN_PACKAGE_VERSION "~${CPPLUA_VERSION_SUFFIX}")
endif()
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Alon Ohana")
set(CPACK_DEBIAN_PACKAGE_SECTION interpreters)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
# The full package carries the library's files too.
set(CPACK_DEBIAN_PACKAGE_PROVIDES libcpplua-dev)
set(CPACK_DEBIAN_PACKAGE_CONFLICTS libcpplua-dev)
set(CPACK_DEBIAN_PACKAGE_REPLACES libcpplua-dev)

include(CPack)

# The library-only package: the same settings, with only the lib component.
configure_file(cmake/CPackLibConfig.cmake.in ${PROJECT_BINARY_DIR}/CPackLibConfig.cmake @ONLY)
