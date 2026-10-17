# A project that uses cpplua the way someone else's would, to check that an
# installed cpplua works (CI builds it against each kind of install):
#
#   cmake -S tests/install -B build/consumer -DCMAKE_PREFIX_PATH=<install prefix>
#       find_package(cpplua), from `cmake --install` or a package
#   cmake -S tests/install -B build/consumer -DCPPLUA_SOURCE=<cpplua checkout>
#       add_subdirectory, as FetchContent does it
#
# Then build it and run `consumer`: it prints "consumer OK" and exits with 0.
cmake_minimum_required(VERSION 3.28)
project(cpplua_consumer LANGUAGES CXX)

set(CPPLUA_SOURCE "" CACHE PATH "A cpplua source tree to add_subdirectory instead of find_package")
if(CPPLUA_SOURCE)
  add_subdirectory(${CPPLUA_SOURCE} cpplua)
  if(TARGET cpplua_tests)
    message(FATAL_ERROR "cpplua built its tests as a subproject")
  endif()
else()
  find_package(cpplua 5.4 CONFIG REQUIRED)
endif()

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE cpplua::cpplua)
