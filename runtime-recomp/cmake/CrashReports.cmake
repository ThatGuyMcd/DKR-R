include_guard(GLOBAL)
function(dkr_add_crash_reports)
    include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/Storage.cmake")
    dkr_add_storage()
    if(TARGET DKRCrashReports)
        return()
    endif()
    get_filename_component(root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../.." ABSOLUTE)
    add_library(
        DKRCrashReports
        STATIC
        "${root}/runtime-recomp/src/game/crash_reports/native_context.cpp"
        "${root}/runtime-recomp/src/game/crash_reports/queue.cpp"
        "${root}/runtime-recomp/src/game/crash_reports/service.cpp"
        "${root}/runtime-recomp/src/game/crash_reports/transport.cpp"
    )
    target_compile_features(DKRCrashReports PUBLIC cxx_std_20)
    target_include_directories(
        DKRCrashReports
        PUBLIC "${root}/runtime-recomp/src/game"
        PRIVATE "${DKR_DEPENDENCIES_DIR}/rt64/src/contrib" "${DKR_DEPENDENCIES_DIR}/n64-modern-runtime/thirdparty"
    )
    target_compile_definitions(DKRCrashReports PRIVATE NOMINMAX)
    target_link_libraries(DKRCrashReports PUBLIC DKRUpdateInstall DKRStorage PRIVATE DKRUpdateZip)
    if(WIN32)
        target_link_libraries(DKRCrashReports PRIVATE winhttp dbghelp)
    elseif(NOT ANDROID AND NOT APPLE)
        find_package(CURL 7.85 REQUIRED)
        target_link_libraries(DKRCrashReports PRIVATE CURL::libcurl ${CMAKE_DL_LIBS})
    endif()
    if(BUILD_TESTING AND NOT ANDROID AND NOT IOS)
        add_executable(DKRCrashReportTests "${root}/runtime-recomp/tests/crash_report_tests.cpp")
        target_link_libraries(DKRCrashReportTests PRIVATE DKRCrashReports DKRUpdateZip)
        target_include_directories(
            DKRCrashReportTests
            PRIVATE "${DKR_DEPENDENCIES_DIR}/rt64/src/contrib" "${DKR_DEPENDENCIES_DIR}/n64-modern-runtime/thirdparty"
        )
        add_test(NAME DKRCrashReports COMMAND DKRCrashReportTests)
        if(WIN32 OR (UNIX AND NOT APPLE))
            add_executable(DKRCrashContextProbe "${root}/runtime-recomp/tests/crash_context_probe.cpp")
            target_link_libraries(DKRCrashContextProbe PRIVATE DKRCrashReports)
            add_test(
                NAME DKRCrashContext
                COMMAND
                    "${Python3_EXECUTABLE}" "${root}/runtime-recomp/tests/crash_context_tests.py"
                    "$<TARGET_FILE:DKRCrashContextProbe>"
            )
        endif()
        find_program(DKR_NODE node)
        if(DKR_NODE)
            add_test(
                NAME DKRCrashRelay
                COMMAND "${DKR_NODE}" --test "${root}/runtime-recomp/tests/crash_relay_tests.mjs"
            )
        endif()
    endif()
endfunction()
