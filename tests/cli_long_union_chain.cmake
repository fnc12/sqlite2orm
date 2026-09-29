# A compound chain has no nesting, so neither the expression nor the query depth limit bounds it:
# the time spent on it has to grow linearly with the number of arms. It once grew quadratically
# (200000 arms took 46 s in Release) while each arm was generated as an argument of another
# union_ call; the test's TIMEOUT is what fails that regression, a linear run takes a few seconds.
#
# Usage: cmake -DCLI=<sqlite2orm> -DWORK_DIR=<dir> -DARMS=<count> -P cli_long_union_chain.cmake
math(EXPR tail_arms "${ARMS} - 1")
string(REPEAT " UNION SELECT 1" ${tail_arms} sql_tail)
file(MAKE_DIRECTORY "${WORK_DIR}")
file(WRITE "${WORK_DIR}/long_union_chain.sql" "SELECT 1${sql_tail};\n")

execute_process(
    COMMAND "${CLI}" "${WORK_DIR}/long_union_chain.sql"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "sqlite2orm exited with ${status}: ${errors}")
endif()
if(NOT errors STREQUAL "")
    message(FATAL_ERROR "unexpected diagnostics: ${errors}")
endif()

string(REPEAT ", select(1)" ${tail_arms} code_tail)
if(NOT output STREQUAL "auto rows = storage.select(union_(select(1)${code_tail}));\n")
    string(SUBSTRING "${output}" 0 200 output_head)
    message(FATAL_ERROR "unexpected output, starting with: ${output_head}")
endif()
file(REMOVE "${WORK_DIR}/long_union_chain.sql")
