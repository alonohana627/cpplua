# os.exit without closing the interpreter still writes what files hold in
# their buffers, as the C library's exit() does (issue #7).
# Run as: cmake -DCPPLUA=<binary> -DWORK=<folder> -P os_exit_flush.cmake
file(MAKE_DIRECTORY "${WORK}")
set(out "${WORK}/os_exit_flush.txt")
file(REMOVE "${out}")
execute_process(
  COMMAND "${CPPLUA}" -e "local f = io.open([[${out}]], 'w') f:write('data') os.exit(3)"
  RESULT_VARIABLE status)
if(NOT status EQUAL 3)
  message(FATAL_ERROR "exit status ${status}, expected 3")
endif()
file(READ "${out}" content)
if(NOT content STREQUAL "data")
  message(FATAL_ERROR "the file holds '${content}', expected 'data'")
endif()
