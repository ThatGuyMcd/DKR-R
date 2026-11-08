# Stable release composition; the owner-accepted water body is unchanged.
if(DKR_RELEASE_CAPSULE)
    include("${DKR_RELEASE_CAPSULE}/overlays/liquid-streaming/NativeOverlay.cmake")
else()
    include("${CMAKE_CURRENT_LIST_DIR}/../../liquid-streaming/NativeOverlay.cmake")
endif()
if(PROJECT_NAME STREQUAL "DKRRuntimePreparation")
    function(dkr_release_evidence)
        file(MAKE_DIRECTORY "${DKR_RELEASE_EVIDENCE}")
        if(ANDROID)
            execute_process(COMMAND "${Python3_EXECUTABLE}" -B
                "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/android_store.py"
                --source "${DKR_MOD_STORE_STAGE}/native/src/game/mod_store.cpp"
                --output "${CMAKE_BINARY_DIR}/generated/release-store"
                RESULT_VARIABLE store_status)
            if(NOT store_status EQUAL 0)
                message(FATAL_ERROR "Android Store platform composition failed")
            endif()
            get_target_property(store_sources DKRPortGame SOURCES)
            foreach(name mod_store mod_store_install mod_store_names)
                list(TRANSFORM store_sources REPLACE ".*/${name}\\.cpp$" "${CMAKE_BINARY_DIR}/generated/release-store/${name}.cpp")
                set_property(SOURCE "${CMAKE_BINARY_DIR}/generated/release-store/${name}.cpp" APPEND PROPERTY INCLUDE_DIRECTORIES
                    "${DKR_MOD_STORE_STAGE}/native/src/game" "${DKR_SDK2_REUSE_STAGE}/native/src/game")
            endforeach()
            set_property(TARGET DKRPortGame PROPERTY SOURCES "${store_sources}")
            set_property(SOURCE "${CMAKE_BINARY_DIR}/generated/release-store/mod_store.cpp" APPEND PROPERTY INCLUDE_DIRECTORIES
                "${DKR_MOD_STORE_STAGE}/native/src/game" "${DKR_SDK2_REUSE_STAGE}/native/src/game")
        endif()
        foreach(target DKRPortGame DKRCustomMagicCodes DKRCameraPresentation DKRExperimentalOwnedRegistry rt64)
            if(TARGET ${target})
                foreach(property SOURCES INCLUDE_DIRECTORIES COMPILE_DEFINITIONS LINK_LIBRARIES)
                    get_target_property(value ${target} ${property})
                    file(WRITE "${DKR_RELEASE_EVIDENCE}/${target}-${property}.txt" "${value}\n")
                endforeach()
            endif()
        endforeach()
    endfunction()
    cmake_language(DEFER CALL dkr_release_evidence)
endif()
