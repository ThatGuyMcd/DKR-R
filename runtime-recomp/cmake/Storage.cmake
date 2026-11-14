include_guard(GLOBAL)
function(dkr_add_storage)
    if(TARGET DKRStorage)
        return()
    endif()
    get_filename_component(root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../.." ABSOLUTE)
    add_library(
        DKRStorage
        STATIC
        "${root}/runtime-recomp/src/game/storage/maintenance.cpp"
        "${root}/runtime-recomp/src/game/storage/service.cpp"
    )
    target_compile_features(DKRStorage PUBLIC cxx_std_20)
    target_compile_definitions(DKRStorage PRIVATE NOMINMAX)
    target_include_directories(
        DKRStorage
        PUBLIC "${root}/runtime-recomp/src/game"
        PRIVATE "${DKR_DEPENDENCIES_DIR}/n64-modern-runtime/thirdparty" "${DKR_MBEDTLS_SOURCE}/include"
    )
    target_link_libraries(DKRStorage PUBLIC DKRUpdateInstall PRIVATE ${_dkr_update_hash})
    if(TARGET DKRPortGame)
        target_link_libraries(DKRPortGame PRIVATE DKRStorage)
    endif()
    if(BUILD_TESTING AND NOT ANDROID AND NOT IOS)
        add_executable(DKRStorageTests "${root}/runtime-recomp/tests/storage_tests.cpp")
        target_compile_features(DKRStorageTests PRIVATE cxx_std_20)
        target_include_directories(DKRStorageTests PRIVATE "${DKR_DEPENDENCIES_DIR}/n64-modern-runtime/thirdparty")
        target_link_libraries(DKRStorageTests PRIVATE DKRStorage)
        if(MSVC)
            target_compile_options(DKRStorageTests PRIVATE /UNDEBUG)
        else()
            target_compile_options(DKRStorageTests PRIVATE -UNDEBUG)
        endif()
        add_test(NAME DKRStorage COMMAND DKRStorageTests)
    endif()
endfunction()
