# Optional renderer adapter, composed AFTER existing stability/mobile patches.
# The pinned dependency checkout is strictly read-only. Every RT64 translation
# unit is staged so quoted headers cannot silently compile two class layouts.
set(water_default ON)
if(APPLE)
    # This adapter supplies DXIL/SPIR-V, not Metal shader binaries. Preserve
    # the existing Mac renderer until that separate port has been qualified.
    set(water_default OFF)
endif()
option(DKR_ENHANCED_WATER "Build the optional enhanced-water renderer adapter" ${water_default})
if(NOT DKR_ENHANCED_WATER)
    return()
endif()
if(APPLE)
    message(FATAL_ERROR "Enhanced water currently supports DX12/Vulkan. Configure DKR_ENHANCED_WATER=OFF for Metal.")
endif()

set(water_stage "${CMAKE_BINARY_DIR}/generated/enhanced-water")
set(water_scratch "${CMAKE_BINARY_DIR}/patch-work/enhanced-water")
file(REMOVE_RECURSE "${water_scratch}")
set(water_patch "${DKRPORT_ROOT}/patches/enhanced-water/renderer.patch")
set(water_adapter "${DKRPORT_ROOT}/patches/enhanced-water/dkr-adapter.patch")
set(water_budget "${DKRPORT_ROOT}/patches/enhanced-water/dkr-budget.patch")
set(water_safety "${DKRPORT_ROOT}/patches/enhanced-water/dkr-safety.patch")
set(water_views "${DKRPORT_ROOT}/patches/enhanced-water/dkr-views.patch")
set(water_field_portability "${DKRPORT_ROOT}/patches/enhanced-water/dkr-field-portability.patch")
set(water_rays "${DKRPORT_ROOT}/patches/enhanced-water/dkr-rays.patch")
set(water_ray_caps "${DKRPORT_ROOT}/patches/enhanced-water/dkr-ray-capabilities.patch")
set(water_projection "${DKRPORT_ROOT}/patches/enhanced-water/dkr-projection.patch")
set(water_recovery "${DKRPORT_ROOT}/patches/enhanced-water/dkr-recovery.patch")
set(water_performance "${DKRPORT_ROOT}/patches/enhanced-water/dkr-performance.patch")
set(water_reflection_search "${DKRPORT_ROOT}/patches/enhanced-water/dkr-reflection-search.patch")
set(water_stability "${DKRPORT_ROOT}/patches/enhanced-water/dkr-stability.patch")
set(water_waterfall "${DKRPORT_ROOT}/patches/enhanced-water/dkr-waterfall.patch")
set(water_lava "${DKRPORT_ROOT}/patches/enhanced-water/dkr-lava.patch")
set(water_lava_lighting "${DKRPORT_ROOT}/patches/enhanced-water/dkr-lava-lighting.patch")
set(water_lava_glow "${DKRPORT_ROOT}/patches/enhanced-water/dkr-lava-glow.patch")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${water_lava_glow}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${water_lava_lighting}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${water_lava}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${water_waterfall}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${water_stability}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${water_performance}" "${water_reflection_search}")
set_property(
    DIRECTORY
    APPEND
    PROPERTY
        CMAKE_CONFIGURE_DEPENDS
            "${water_patch}"
            "${water_adapter}"
            "${water_budget}"
            "${water_safety}"
            "${water_views}"
            "${water_field_portability}"
            "${water_rays}"
            "${water_ray_caps}"
            "${water_projection}"
            "${water_recovery}"
)

# Compose the capability adapter on the ACTIVE backend sources, including
# Android's surface/cache/rotation corrections. Stage all interface headers
# and backend consumers together so no translation unit sees an old ABI.
get_target_property(water_plume_sources plume SOURCES)
get_target_property(water_plume_root plume SOURCE_DIR)
file(GLOB water_plume_headers "${water_plume_root}/*.h")
set(water_plume_stage "${water_stage}/src/contrib/plume")
set(water_plume_scratch "${water_scratch}/src/contrib/plume")
file(MAKE_DIRECTORY "${water_plume_scratch}")
foreach(header IN LISTS water_plume_headers)
    get_filename_component(name "${header}" NAME)
    configure_file("${header}" "${water_plume_scratch}/${name}" COPYONLY)
endforeach()
set(water_plume_staged_sources)
foreach(source IN LISTS water_plume_sources)
    get_filename_component(name "${source}" NAME)
    if(name MATCHES "^plume_.*\\.(cpp|h)$")
        if(NOT IS_ABSOLUTE "${source}")
            set(source "${water_plume_root}/${source}")
        endif()
        configure_file("${source}" "${water_plume_scratch}/${name}" COPYONLY)
        get_filename_component(parent "${source}" DIRECTORY)
        get_filename_component(stem "${source}" NAME_WE)
        if(EXISTS "${parent}/${stem}.h")
            configure_file("${parent}/${stem}.h" "${water_plume_scratch}/${stem}.h" COPYONLY)
        endif()
        foreach(property IN ITEMS INCLUDE_DIRECTORIES COMPILE_OPTIONS COMPILE_DEFINITIONS)
            get_source_file_property(value "${source}" TARGET_DIRECTORY plume ${property})
            if(value AND NOT value STREQUAL "NOTFOUND")
                set_source_files_properties(
                    "${water_plume_stage}/${name}"
                    TARGET_DIRECTORY plume
                    PROPERTIES ${property} "${value}"
                )
            endif()
        endforeach()
        list(APPEND water_plume_staged_sources "${water_plume_stage}/${name}")
    else()
        list(APPEND water_plume_staged_sources "${source}")
    endif()
endforeach()
# The D3D backend is not compiled on Linux/Android, but remains a checked
# patch input so the same capability patch is verified on every platform.
if(NOT WIN32)
    configure_file("${water_plume_root}/plume_d3d12.cpp" "${water_plume_scratch}/plume_d3d12.cpp" COPYONLY)
endif()
set_property(TARGET plume PROPERTY SOURCES "${water_plume_staged_sources}")
target_include_directories(plume BEFORE PUBLIC "${water_plume_stage}")
target_include_directories(plume PRIVATE "${water_plume_root}")
target_include_directories(rt64 BEFORE PUBLIC "${water_plume_stage}" "${CMAKE_CURRENT_LIST_DIR}/../src/game")
file(
    GLOB_RECURSE water_headers
    CONFIGURE_DEPENDS
    "${DKR_RT64_SOURCE}/include/*.h"
    "${DKR_RT64_SOURCE}/src/common/*.h"
    "${DKR_RT64_SOURCE}/src/gbi/*.h"
    "${DKR_RT64_SOURCE}/src/hle/*.h"
    "${DKR_RT64_SOURCE}/src/render/*.h"
    "${DKR_RT64_SOURCE}/src/shared/*.h"
    "${DKR_RT64_SOURCE}/src/include/*.h"
)
foreach(header IN LISTS water_headers)
    file(RELATIVE_PATH relative "${DKR_RT64_SOURCE}" "${header}")
    get_filename_component(parent "${relative}" DIRECTORY)
    file(MAKE_DIRECTORY "${water_scratch}/${parent}")
    configure_file("${header}" "${water_scratch}/${relative}" COPYONLY)
endforeach()

get_target_property(water_original_sources rt64 SOURCES)
set(water_sources)
foreach(source IN LISTS water_original_sources)
    # Generated shaders remain in their original build directory. The one
    # changed compute shader is replaced below with a distinct output path.
    if(source MATCHES "RSPProcessCS\\.hlsl\\.(spirv|dxil|metal)\\.c$")
        continue()
    endif()
    if(source MATCHES "/src/(common|gbi|hle|render)/([^/]+\\.cpp)$")
        set(relative "src/${CMAKE_MATCH_1}/${CMAKE_MATCH_2}")
        get_filename_component(parent "${relative}" DIRECTORY)
        file(MAKE_DIRECTORY "${water_scratch}/${parent}")
        configure_file("${source}" "${water_scratch}/${relative}" COPYONLY)
        # Preserve source-specific forced includes/options from earlier stages.
        foreach(property IN ITEMS INCLUDE_DIRECTORIES COMPILE_OPTIONS COMPILE_DEFINITIONS)
            get_source_file_property(value "${source}" TARGET_DIRECTORY rt64 ${property})
            if(value AND NOT value STREQUAL "NOTFOUND")
                set_source_files_properties(
                    "${water_stage}/${relative}"
                    TARGET_DIRECTORY rt64
                    PROPERTIES ${property} "${value}"
                )
            endif()
        endforeach()
        list(APPEND water_sources "${water_stage}/${relative}")
    else()
        list(APPEND water_sources "${source}")
    endif()
endforeach()
file(MAKE_DIRECTORY "${water_scratch}/src/shaders")
# Keep quoted shader includes in one tree: DXC's pragma-once identity must
# not alternate between staged and dependency-relative header spellings.
file(GLOB water_shader_includes CONFIGURE_DEPENDS "${DKR_RT64_SOURCE}/src/shaders/*.hlsli")
foreach(file IN LISTS water_shader_includes)
    get_filename_component(name "${file}" NAME)
    configure_file("${file}" "${water_scratch}/src/shaders/${name}" COPYONLY)
endforeach()
configure_file(
    "${DKR_RT64_SOURCE}/src/shaders/RSPProcessCS.hlsl"
    "${water_scratch}/src/shaders/RSPProcessCS.hlsl"
    COPYONLY
)
foreach(file IN ITEMS RasterPS.hlsl RasterVS.hlsl)
    configure_file("${DKR_RT64_SOURCE}/src/shaders/${file}" "${water_scratch}/src/shaders/${file}" COPYONLY)
endforeach()
foreach(
    file
    IN
    ITEMS
        WaterfallVS.hlsl
        WaterfallPS.hlsl
        WaterfallFlow.hlsli
        LavaFlow.hlsli
        LavaLightingVS.hlsl
        LavaLightingPS.hlsl
        LavaLightingCS.hlsl
        LavaLighting.hlsli
)
    configure_file("${DKRPORT_ROOT}/runtime-recomp/src/water/${file}" "${water_scratch}/src/shaders/${file}" COPYONLY)
endforeach()
foreach(
    file
    IN
    ITEMS
        WaterRayScene.h
        WaterRayScene.cpp
        WaterProjection.h
        WaterResourcePolicy.h
        WaterWorkPolicy.h
        WaterBounds.h
        WaterPipelineSafety.h
        LavaLightingBounds.h
)
    configure_file("${DKRPORT_ROOT}/runtime-recomp/src/water/${file}" "${water_scratch}/src/render/${file}" COPYONLY)
endforeach()
configure_file(
    "${DKRPORT_ROOT}/runtime-recomp/src/water/WaterRays.hlsli"
    "${water_scratch}/src/shaders/WaterRays.hlsli"
    COPYONLY
)
configure_file(
    "${DKRPORT_ROOT}/runtime-recomp/src/water/WaterRayProbeCS.hlsl"
    "${water_scratch}/src/shaders/WaterRayProbeCS.hlsl"
    COPYONLY
)
foreach(file IN ITEMS WaterProjection.hlsli WaterProjectionProbeCS.hlsl)
    configure_file("${DKRPORT_ROOT}/runtime-recomp/src/water/${file}" "${water_scratch}/src/shaders/${file}" COPYONLY)
endforeach()
execute_process(COMMAND "${GIT_EXECUTABLE}" init --quiet "${water_scratch}" COMMAND_ERROR_IS_FATAL ANY)
foreach(
    patch
    IN
    ITEMS
        "${water_patch}"
        "${water_adapter}"
        "${water_budget}"
        "${water_safety}"
        "${water_views}"
        "${water_field_portability}"
        "${water_rays}"
        "${water_ray_caps}"
        "${water_projection}"
        "${water_recovery}"
        "${water_performance}"
        "${water_reflection_search}"
        "${water_stability}"
        "${water_waterfall}"
        "${water_lava}"
        "${water_lava_lighting}"
        "${water_lava_glow}"
)
    foreach(mode IN ITEMS check apply)
        set(flags --recount --ignore-space-change --whitespace=error)
        if(mode STREQUAL "check")
            list(APPEND flags --check)
        endif()
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${water_scratch}" apply ${flags} "${patch}"
            RESULT_VARIABLE result
            ERROR_VARIABLE error
        )
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "Enhanced water ${mode}: ${error}")
        endif()
    endforeach()
endforeach()
# Publish after the complete patch chain succeeds. Stable mtimes keep Ninja idle.
file(GLOB_RECURSE water_composed RELATIVE "${water_scratch}" "${water_scratch}/*")
foreach(relative IN LISTS water_composed)
    if(relative MATCHES "^\\.git/")
        continue()
    endif()
    get_filename_component(parent "${relative}" DIRECTORY)
    file(MAKE_DIRECTORY "${water_stage}/${parent}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${water_scratch}/${relative}" "${water_stage}/${relative}"
        COMMAND_ERROR_IS_FATAL ANY
    )
endforeach()
list(
    APPEND water_sources
    "${water_stage}/src/render/rt64_water_renderer.cpp"
    "${water_stage}/src/render/WaterRayScene.cpp"
)
set_property(TARGET rt64 PROPERTY SOURCES "${water_sources}")
target_include_directories(rt64 BEFORE PUBLIC "${water_stage}/src" "${water_stage}/include")
target_compile_definitions(rt64 PUBLIC DKR_ENHANCED_WATER=1)

# DXC/file_to_c are the existing dependency's build tools, not new binaries.
get_target_property(water_rt64_binary rt64 BINARY_DIR)
set(water_shader_out "${CMAKE_BINARY_DIR}/generated/enhanced-water-shaders")
if(CMAKE_HOST_WIN32)
    set(water_dxc_arch x64)
    set(water_dxc "${DKR_RT64_SOURCE}/src/contrib/dxc/bin/x64/dxc.exe")
elseif(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
    set(water_dxc_arch arm64)
    set(water_dxc "${DKR_RT64_SOURCE}/src/contrib/dxc/bin/arm64/dxc-linux")
else()
    set(water_dxc_arch x64)
    set(water_dxc "${DKR_RT64_SOURCE}/src/contrib/dxc/bin/x64/dxc-linux")
endif()

function(dkr_water_shader name original type entry)
    file(MAKE_DIRECTORY "${water_shader_out}/shaders")
    set(input "${water_stage}/src/shaders/${original}.hlsl")
    set(output "${water_shader_out}/shaders/${name}.hlsl")
    set(deps
        "${input}"
        "${water_stage}/src/shared/rt64_water_params.h"
        "${water_stage}/src/shaders/WaterRays.hlsli"
        "${water_stage}/src/shaders/WaterProjection.hlsli"
        ${water_headers}
    )
    list(
        APPEND deps
        "${water_stage}/src/shaders/WaterfallFlow.hlsli"
        "${water_stage}/src/shaders/RasterPS.hlsl"
        "${water_stage}/src/shaders/RasterVS.hlsl"
    )
    list(APPEND deps "${water_stage}/src/shaders/LavaFlow.hlsli" "${water_stage}/src/shaders/LavaLighting.hlsli")
    foreach(format IN ITEMS spirv dxil)
        if(format STREQUAL "dxil" AND NOT WIN32)
            continue()
        endif()
        set(model 6_3)
        set(vulkan_environment vulkan1.0)
        if(name STREQUAL "WaterHardwarePS" OR name STREQUAL "WaterRayProbeCS" OR name STREQUAL "LavaLightingCS")
            set(model 6_5)
            set(vulkan_environment vulkan1.2)
        endif()
        set(options
            -E
            ${entry}
            -T
            ${type}_${model}
            -I
            "${water_stage}/src"
            -I
            "${DKR_RT64_SOURCE}/src"
            -I
            "${DKR_RT64_SOURCE}/src/shaders"
            ${ARGN}
        )
        if(format STREQUAL "spirv")
            list(APPEND options -spirv -fspv-target-env=${vulkan_environment} -fvk-use-dx-layout)
            if(type STREQUAL "vs")
                list(APPEND options -fvk-invert-y)
            endif()
            set(suffix SPIRV)
        else()
            list(APPEND options -Wno-ignored-attributes)
            set(suffix DXIL)
        endif()
        add_custom_command(
            OUTPUT "${output}.${format}.c" "${output}.${format}.h"
            COMMAND
                "${CMAKE_COMMAND}" -E env
                "LD_LIBRARY_PATH=${DKR_RT64_SOURCE}/src/contrib/dxc/lib/${water_dxc_arch}:$ENV{LD_LIBRARY_PATH}"
                "${water_dxc}" ${options} "${input}" -Fo "${output}.${format}"
            COMMAND
                file_to_c "${output}.${format}" "${name}Blob${suffix}" "${output}.${format}.c" "${output}.${format}.h"
            DEPENDS ${deps} file_to_c
            VERBATIM
        )
        target_sources(rt64 PRIVATE "${output}.${format}.c")
        list(APPEND water_shader_outputs "${output}.${format}.c" "${output}.${format}.h")
    endforeach()
    set(water_shader_outputs "${water_shader_outputs}" PARENT_SCOPE)
endfunction()
target_include_directories(rt64 BEFORE PRIVATE "${water_shader_out}")
dkr_water_shader(RSPProcessCS RSPProcessCS cs CSMain)
dkr_water_shader(WaterVS WaterVS vs VSMain)
dkr_water_shader(WaterPS WaterPS ps PSMain)
dkr_water_shader(WaterHardwarePS WaterPS ps PSMain -D HARDWARE_WATER)
dkr_water_shader(LavaLightingVS LavaLightingVS vs VSMain)
dkr_water_shader(LavaLightingPS LavaLightingPS ps PSMain)
dkr_water_shader(LavaLightingCS LavaLightingCS cs CSMain)
dkr_water_shader(WaterfallVS WaterfallVS vs VSMain)
dkr_water_shader(WaterfallPS WaterfallPS ps PSMain)
dkr_water_shader(WaterfallMSPS WaterfallPS ps PSMain -D MULTISAMPLING)
if(BUILD_TESTING AND NOT ANDROID)
    foreach(file IN ITEMS WaterFieldReferenceCS.hlsl WaterFieldCompareCS.hlsl WaterCaptureProbeCS.hlsl)
        configure_file("${DKRPORT_ROOT}/runtime-recomp/src/water/${file}" "${water_stage}/src/shaders/${file}" COPYONLY)
    endforeach()
    dkr_water_shader(WaterFieldReferenceCS WaterFieldReferenceCS cs CSMain)
    dkr_water_shader(WaterFieldCompareCS WaterFieldCompareCS cs CSMain)
    dkr_water_shader(WaterRayProbeCS WaterRayProbeCS cs CSMain)
    dkr_water_shader(WaterProjectionProbeCS WaterProjectionProbeCS cs CSMain)
    dkr_water_shader(WaterCaptureProbeCS WaterCaptureProbeCS cs CSMain)
    dkr_water_shader(WaterCaptureMSProbeCS WaterCaptureProbeCS cs CSMain -D CAPTURE_MSAA)
    add_executable(
        DKREnhancedWaterRayGpuTests
        EXCLUDE_FROM_ALL
        "${CMAKE_CURRENT_LIST_DIR}/../tests/enhanced_water_ray_gpu_tests.cpp"
    )
    target_include_directories(
        DKREnhancedWaterRayGpuTests
        PRIVATE "${water_shader_out}" "${DKR_RT64_SOURCE}/src/contrib/hlslpp/include"
    )
    target_link_libraries(DKREnhancedWaterRayGpuTests PRIVATE rt64)
    if(WIN32)
        target_link_directories(
            DKREnhancedWaterRayGpuTests
            PRIVATE "${DKR_RT64_SOURCE}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/lib/x64"
        )
        target_link_libraries(DKREnhancedWaterRayGpuTests PRIVATE SDL2)
    else()
        target_link_libraries(DKREnhancedWaterRayGpuTests PRIVATE ${SDL2_LIBRARIES})
    endif()
endif()
dkr_water_shader(WaterClassicPS WaterPS ps PSMain -D CLASSIC_STYLE)
dkr_water_shader(WaterClassicMSPS WaterPS ps PSMain -D CLASSIC_STYLE -D CLASSIC_MSAA)
dkr_water_shader(WaterNormalsCS WaterNormalsCS cs CSMain)
dkr_water_shader(WaterCaptureCS WaterCaptureCS cs CSMain)
dkr_water_shader(WaterCaptureMSCS WaterCaptureCS cs CSMain -D CAPTURE_MSAA)
dkr_water_shader(WaterOriginalCS WaterOriginalCS cs CSMain)
dkr_water_shader(WaterOriginalMSCS WaterOriginalCS cs CSMain -D CAPTURE_MSAA)
dkr_water_shader(WaterDepthReduceCS WaterDepthReduceCS cs CSMain)
dkr_water_shader(WaterFieldCS WaterFieldCS cs CSMain)
dkr_water_shader(WaterStatsCS WaterStatsCS cs CSMain)
dkr_water_shader(WaterSurfacePS WaterSurfacePS ps PSMain)
dkr_water_shader(WaterSprayVS WaterSprayVS vs VSMain)
dkr_water_shader(WaterSprayPS WaterSprayPS ps PSMain)
dkr_water_shader(WaterSprayMSPS WaterSprayPS ps PSMain -D SPRAY_MSAA)
add_custom_target(DKREnhancedWaterShaders DEPENDS ${water_shader_outputs})
add_dependencies(rt64 DKREnhancedWaterShaders)
