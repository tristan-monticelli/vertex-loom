foreach(REQUIRED IN ITEMS RENDER_BENCH RUNTIME_BENCH TEST_ROOT)
    if(NOT DEFINED ${REQUIRED})
        message(FATAL_ERROR "${REQUIRED} is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")

function(run_benchmark EXECUTABLE REPORT MIN_FPS)
    execute_process(
        COMMAND "${EXECUTABLE}" ${ARGN} --min-fps "${MIN_FPS}" --report "${REPORT}"
        RESULT_VARIABLE RESULT
        OUTPUT_VARIABLE OUTPUT
        ERROR_VARIABLE ERROR)
    if(NOT RESULT EQUAL 0)
        message(FATAL_ERROR "benchmark failed: ${EXECUTABLE}\n${OUTPUT}\n${ERROR}")
    endif()
    if(NOT EXISTS "${REPORT}")
        message(FATAL_ERROR "benchmark did not produce report: ${REPORT}")
    endif()
endfunction()

run_benchmark("${RENDER_BENCH}" "${TEST_ROOT}/render-small.json" 30
              --packets 100 --frames 60)
run_benchmark("${RENDER_BENCH}" "${TEST_ROOT}/render-representative.json" 20
              --packets 10000 --frames 120)
run_benchmark("${RUNTIME_BENCH}" "${TEST_ROOT}/runtime-small.json" 30
              --instances 100 --frames 60)
run_benchmark("${RUNTIME_BENCH}" "${TEST_ROOT}/runtime-representative.json" 20
              --instances 10000 --frames 120)

message("Release performance smoke passed for small and representative profiles")
