# io.stdin:seek works when stdin is a file (issue #7): the standard streams
# seek through the C library, as Lua's do.
# Run as: cmake -DCPPLUA=<binary> -DWORK=<folder> -P stdin_seek.cmake
file(MAKE_DIRECTORY "${WORK}")
set(input "${WORK}/stdin_seek.txt")
file(WRITE "${input}" "hello world\n")
execute_process(
  COMMAND "${CPPLUA}" -e "io.write(io.read(5), ',', io.stdin:seek('set', 6), ',', io.read('l'))"
  INPUT_FILE "${input}"
  OUTPUT_VARIABLE output
  RESULT_VARIABLE status)
if(NOT status EQUAL 0 OR NOT output STREQUAL "hello,6,world")
  message(FATAL_ERROR "status ${status}, output '${output}', expected 'hello,6,world'")
endif()
