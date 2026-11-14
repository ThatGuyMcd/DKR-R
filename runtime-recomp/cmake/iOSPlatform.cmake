# iOS sideload target. Host shader tools stay on macOS. The phone build links
# static SDL2 and the project-owned document picker. No extra entitlements.
if(NOT IOS)
    return()
endif()

# Every target in this build, not just the app. Static libs like the legacy
# mod worker are compiled before dkr_ios_finish runs.
add_compile_definitions(DKR_IOS=1)

set(DKR_RUNTIME_BUILD_SDL3_INPUT_HOST OFF CACHE BOOL "iOS uses in-process SDL2 input" FORCE)

set(DKR_HOST_FILE_TO_C "" CACHE FILEPATH "Native host RT64 file_to_c executable")
set(DKR_HOST_SPIRV_CROSS_MSL "" CACHE FILEPATH "Native host RT64 spirv_cross_msl executable")
if(DKR_RUNTIME_BUILD_RT64 AND (NOT EXISTS "${DKR_HOST_FILE_TO_C}" OR NOT EXISTS "${DKR_HOST_SPIRV_CROSS_MSL}"))
    message(FATAL_ERROR "iOS requires DKR_HOST_FILE_TO_C and DKR_HOST_SPIRV_CROSS_MSL.")
endif()

set(SDL_SHARED OFF CACHE BOOL "Static SDL2 in the IPA" FORCE)
set(SDL_STATIC ON CACHE BOOL "Static SDL2 in the IPA" FORCE)
set(SDL_TEST OFF CACHE BOOL "No SDL tests in the IPA" FORCE)
set(SDL_TESTS OFF CACHE BOOL "No SDL tests in the IPA" FORCE)
if(NOT EXISTS "${DKR_DEPENDENCIES_DIR}/sdl2/CMakeLists.txt")
    message(FATAL_ERROR "iOS requires the pinned SDL2 checkout. Run scripts/bootstrap_dependencies.py --only sdl2.")
endif()
add_subdirectory("${DKR_DEPENDENCIES_DIR}/sdl2" "${CMAKE_BINARY_DIR}/sdl2" EXCLUDE_FROM_ALL)
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/ios")

set(DKR_IOS_NFD_SOURCE
    "${CMAKE_CURRENT_LIST_DIR}/../src/ios/ios_file_dialog.mm"
    CACHE FILEPATH
    "UIDocumentPicker NFD backend"
    FORCE
)

function(dkr_ios_finish target)
    if(
        NOT DKR_RELEASE_VERSION
            MATCHES
            "^([1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(-rc[1-9][0-9]*)?(\\+[0-9A-Za-z.-]+)?$"
    )
        message(FATAL_ERROR "iOS packaging needs major.minor.patch or major.minor.patch-rcN.")
    endif()
    if(CMAKE_MATCH_1 GREATER 9999 OR CMAKE_MATCH_2 GREATER 99 OR CMAKE_MATCH_3 GREATER 99)
        message(FATAL_ERROR "iOS bundle versions require at most 4/2/2 digits.")
    endif()
    set(DKR_IOS_BUNDLE_VERSION "${DKR_RELEASE_VERSION_NUMERIC}")
    set(DKR_IOS_SHORT_VERSION "${DKR_RELEASE_VERSION_NUMERIC}")
    set(plist_dir "${CMAKE_BINARY_DIR}/ios")
    file(MAKE_DIRECTORY "${plist_dir}")
    configure_file("${DKRPORT_ROOT}/packaging/ios/Info.plist.in" "${plist_dir}/Info.plist" @ONLY)

    target_compile_definitions(${target} PRIVATE DKR_IOS=1)
    target_sources(
        ${target}
        PRIVATE
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/ios/ios_platform.mm"
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/ios/ios_metal_layer.mm"
    )
    set_source_files_properties(
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/ios/ios_platform.mm"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/ios/ios_metal_layer.mm"
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc"
    )
    target_link_libraries(${target} PRIVATE SDL2::SDL2 SDL2::SDL2main)
    target_link_libraries(
        ${target}
        PRIVATE
            "-framework UIKit"
            "-framework Metal"
            "-framework QuartzCore"
            "-framework CoreGraphics"
            "-framework Foundation"
            "-framework AVFoundation"
            "-framework AudioToolbox"
            "-framework GameController"
            "-framework CoreHaptics"
    )
    set_target_properties(
        ${target}
        PROPERTIES
            OUTPUT_NAME "DKR-R"
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_INFO_PLIST "${plist_dir}/Info.plist"
            XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "io.github.thatguymcd.dkrr.ios"
            XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
    )
endfunction()
