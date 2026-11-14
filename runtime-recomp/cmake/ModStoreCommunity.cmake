include_guard(GLOBAL)
get_filename_component(_dkr_store_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
function(dkr_add_mod_store_community_tests)
    if(NOT TARGET DKRModStoreCommunityTests)
        add_executable(
            DKRModStoreCommunityTests
            "${_dkr_store_root}/runtime-recomp/tests/mod_store_community_tests.cpp"
        )
        target_compile_features(DKRModStoreCommunityTests PRIVATE cxx_std_20)
        target_include_directories(
            DKRModStoreCommunityTests
            PRIVATE "${_dkr_store_root}/runtime-recomp/src/game" "${DKR_DEPENDENCIES_DIR}/rt64/src/contrib"
        )
        if(MSVC)
            target_compile_options(DKRModStoreCommunityTests PRIVATE /UNDEBUG)
        else()
            target_compile_options(DKRModStoreCommunityTests PRIVATE -UNDEBUG)
        endif()
        if(BUILD_TESTING)
            add_test(NAME DKRModStoreCommunity COMMAND DKRModStoreCommunityTests)
        endif()
    endif()
endfunction()
