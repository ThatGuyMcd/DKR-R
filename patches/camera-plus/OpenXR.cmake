# Optional OpenXR loader for PC builds. Loading the game on a machine with no
# runtime/headset must not require a separately installed openxr_loader DLL.
# The static loader discovers a runtime only after the player requests VR.
function(dkr_camera_openxr target)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${DKRPORT_ROOT}/patches/camera-plus/vr_device_patch.py"
        verify --output "${DKR_CAMERA_RT64_STAGE}" RESULT_VARIABLE vr_result)
    if(NOT vr_result EQUAL 0)
        message(FATAL_ERROR "Camera++ VR needs the verified staged device patch, never an edited submodule")
    endif()
    if(NOT WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "Camera++ immersive output currently targets Windows and Linux PC runtimes")
    endif()
    if(NOT DKR_OPENXR_SOURCE OR NOT EXISTS "${DKR_OPENXR_SOURCE}/include/openxr/openxr.h")
        message(FATAL_ERROR "Select the pinned Khronos OpenXR-SDK source; do not use an unversioned system loader")
    endif()
    find_package(Git REQUIRED)
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${DKR_OPENXR_SOURCE}" rev-parse HEAD
        OUTPUT_VARIABLE xr_commit OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE xr_result)
    if(NOT xr_result EQUAL 0 OR NOT xr_commit STREQUAL "75c53b6e853dc12c7b3c771edc9c9c841b15faaa")
        message(FATAL_ERROR "Unreviewed OpenXR SDK revision")
    endif()
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${DKR_OPENXR_SOURCE}" status --porcelain --untracked-files=all
        OUTPUT_VARIABLE xr_dirty OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE xr_result)
    if(NOT xr_result EQUAL 0 OR NOT xr_dirty STREQUAL "")
        message(FATAL_ERROR "OpenXR source must remain unchanged")
    endif()
    # Explicitly avoid all executable samples, qualification programs/layers
    # and tests. Configuring/building this target does not probe a runtime.
    set(BUILD_LOADER ON CACHE BOOL "" FORCE)
    set(DYNAMIC_LOADER OFF CACHE BOOL "" FORCE)
    set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_CONFORMANCE_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_CONFORMANCE_CLI OFF CACHE BOOL "" FORCE)
    if(NOT TARGET openxr_loader)
        add_subdirectory("${DKR_OPENXR_SOURCE}" "${CMAKE_BINARY_DIR}/camera-openxr" EXCLUDE_FROM_ALL)
    endif()
    target_sources(${target} PRIVATE "${DKR_CAMERA_STAGE}/src/game/camera_xr.cpp")
    target_include_directories(${target} PRIVATE
        "${DKR_CAMERA_RT64_STAGE}/src/contrib/plume"
        "${DKR_CAMERA_RT64_STAGE}/src/contrib/plume/contrib/volk"
        "${DKR_CAMERA_RT64_STAGE}/src/contrib/plume/contrib/Vulkan-Headers/include"
        "${DKR_CAMERA_RT64_STAGE}/src/contrib/plume/contrib/VulkanMemoryAllocator/include")
    target_link_libraries(${target} PRIVATE openxr_loader)
    target_compile_definitions(${target} PRIVATE NOMINMAX)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_definitions(${target} PRIVATE PLUME_SDL_VULKAN_ENABLED=1)
    endif()
    # Release packaging must include this notice alongside the renderer's
    # existing notices. The SDK source and upstream submodules are untouched.
    install(FILES "${DKR_OPENXR_SOURCE}/LICENSE"
        DESTINATION "licenses" RENAME "OpenXR-SDK-LICENSE.txt")
endfunction()
