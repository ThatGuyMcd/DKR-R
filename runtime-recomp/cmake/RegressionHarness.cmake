# ROM-free benchmarks use production implementations and synthetic state only.
if(BUILD_TESTING)
    add_executable(
        DKRRegressionBenchmark
        "${CMAKE_CURRENT_LIST_DIR}/../tests/regression_benchmark.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/../src/game/netplay/experimental_checkpoint_hash.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/../src/game/netplay/rollback_state_store.cpp"
    )
    target_compile_features(DKRRegressionBenchmark PRIVATE cxx_std_20)
    target_include_directories(
        DKRRegressionBenchmark
        PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../src/game" "${DKR_DEPENDENCIES_DIR}/n64-modern-runtime/thirdparty"
    )
    add_test(NAME DKRRegressionBenchmark COMMAND DKRRegressionBenchmark)
    set_tests_properties(DKRRegressionBenchmark PROPERTIES LABELS "host;benchmark" TIMEOUT 60)
endif()
