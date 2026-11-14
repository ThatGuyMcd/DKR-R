cmake_minimum_required(VERSION 3.24)

function(run_checked)
    execute_process(COMMAND ${ARGV} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Assertion contract fixture failed (${result}):\n${output}\n${error}")
    endif()
endfunction()

foreach(configuration IN ITEMS Release RelWithDebInfo MinSizeRel)
    set(fixture_build "${TEST_BINARY_DIR}/${configuration}")
    run_checked("${CMAKE_COMMAND}"
        -S "${CMAKE_CURRENT_LIST_DIR}/test_assertions" -B "${fixture_build}"
        -G "${TEST_GENERATOR}" "-DCMAKE_BUILD_TYPE=${configuration}"
        "-DCMAKE_C_COMPILER=${TEST_C_COMPILER}"
        "-DCMAKE_CXX_COMPILER=${TEST_CXX_COMPILER}"
    )
    run_checked("${CMAKE_COMMAND}" --build "${fixture_build}" --config "${configuration}")
    run_checked("${TEST_CTEST_COMMAND}" --test-dir "${fixture_build}"
        -C "${configuration}" --output-on-failure
    )
endforeach()

# BUILD_TESTING controls CTest registration, not qualification executable safety.
run_checked("${CMAKE_COMMAND}"
    -S "${CMAKE_CURRENT_LIST_DIR}/test_assertions" -B "${TEST_BINARY_DIR}/no-ctest"
    -G "${TEST_GENERATOR}" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
    "-DCMAKE_C_COMPILER=${TEST_C_COMPILER}"
    "-DCMAKE_CXX_COMPILER=${TEST_CXX_COMPILER}"
)
run_checked("${CMAKE_COMMAND}" --build "${TEST_BINARY_DIR}/no-ctest" --config Release)
