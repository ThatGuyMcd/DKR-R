# Project test executables use DKR*Tests names. Configure their assertion
# contract after all declarations, including optional targets from modules.
# Restrict discovery to this directory: dependency test targets in their own
# subdirectories must retain their upstream build settings.
include_guard(DIRECTORY)

function(dkr_configure_test_assertions)
    get_property(project_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(test_target IN LISTS project_targets)
        get_target_property(target_type "${test_target}" TYPE)
        if(test_target MATCHES "^DKR.+Tests$" AND target_type STREQUAL "EXECUTABLE")
            if(MSVC)
                target_compile_options("${test_target}" PRIVATE /UNDEBUG)
            else()
                target_compile_options("${test_target}" PRIVATE -UNDEBUG)
            endif()
        endif()
    endforeach()
endfunction()

# Private qualification executables can be built even with BUILD_TESTING=OFF.
cmake_language(DEFER CALL dkr_configure_test_assertions)

# Keep a bounded default for every first-party test while retaining explicit
# timing contracts on concurrency tests and longer tooling fixtures.
function(dkr_configure_test_metadata)
    get_property(project_tests DIRECTORY PROPERTY TESTS)
    # Optional SDK-backed tests are labeled only when configured.
    set(flake_tests
        DKRDirectSession
        DKRFriendService
        DKRFriendDelivery
        DKRFriendLocalIntegration
        DKRQuickJoinTransport
        DKRQuickJoinLifecycle
        DKRExperimentalQuickJoin
        DKRExperimentalSession
        DKRExperimentalNetwork
        DKRExperimentalLaunch
        DKRNetplayAdverseNetwork
    )
    foreach(test_name IN LISTS project_tests)
        if(NOT test_name MATCHES "^DKR")
            continue()
        endif()
        get_property(timeout TEST "${test_name}" PROPERTY TIMEOUT)
        if(NOT timeout)
            set_tests_properties("${test_name}" PROPERTIES TIMEOUT 120)
        endif()
        get_property(labels TEST "${test_name}" PROPERTY LABELS)
        if(NOT labels)
            if(test_name MATCHES "Gpu|Live|Metal|SoftwarePixels")
                set_tests_properties("${test_name}" PROPERTIES LABELS "graphics")
            else()
                set_tests_properties("${test_name}" PROPERTIES LABELS "host;runtime")
            endif()
        endif()
        if(test_name IN_LIST flake_tests)
            set_property(TEST "${test_name}" APPEND PROPERTY LABELS flake)
        endif()
    endforeach()
endfunction()
cmake_language(DEFER CALL dkr_configure_test_metadata)
