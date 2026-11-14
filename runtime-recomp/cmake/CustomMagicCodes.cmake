# Project-owned portable code-mod boundary. Upstream loaders and generated
# payloads are read-only; dispatch adapters are composed by the Patch Pipeline.
add_library(DKRCustomMagicCodes STATIC EXCLUDE_FROM_ALL
    "${DKRPORT_ROOT}/runtime-recomp/src/game/mods/magic_code_package.cpp"
    "${DKRPORT_ROOT}/runtime-recomp/src/game/mods/magic_code_vm.cpp"
    "${DKRPORT_ROOT}/runtime-recomp/src/game/mods/magic_code_link.cpp"
    "${DKRPORT_ROOT}/runtime-recomp/src/game/mods/magic_code_world.cpp"
    "${DKRPORT_ROOT}/runtime-recomp/src/game/mods/magic_code_library.cpp"
    "${DKRPORT_ROOT}/runtime-recomp/src/game/mods/magic_code_library_jobs.cpp"
    "${DKRPORT_ROOT}/runtime-recomp/src/game/mods/magic_code_game_world.cpp"
    "${DKRPORT_ROOT}/runtime-recomp/src/game/mods/magic_code_game_services.cpp"
    "${DKRPORT_ROOT}/runtime-recomp/src/game/mods/magic_code_effects.cpp"
    "${DKRPORT_ROOT}/runtime-recomp/src/game/mods/magic_code_trophy_definition.cpp"
    "${DKRPORT_ROOT}/runtime-recomp/src/game/mods/sdk2_bridge.cpp")
target_sources(DKRCustomMagicCodes PRIVATE "${DKRPORT_ROOT}/runtime-recomp/src/game/mods/magic_code_trophy_catalog.cpp")
target_compile_features(DKRCustomMagicCodes PUBLIC cxx_std_20)
target_include_directories(DKRCustomMagicCodes PUBLIC
    "${DKRPORT_ROOT}/runtime-recomp/src/game/mods" "${DKR_RECOMP_SOURCE}/include"
    "${DKRPORT_ROOT}/runtime-recomp/include")
target_include_directories(DKRCustomMagicCodes BEFORE PRIVATE
    "${DKR_DEPENDENCIES_DIR}/rt64/src/contrib")
target_compile_definitions(DKRCustomMagicCodes PRIVATE NOMINMAX)
target_link_libraries(DKRCustomMagicCodes PUBLIC DKRLegacyModCore PRIVATE N64Recomp)
target_link_libraries(DKRLegacyModLaunch PUBLIC DKRCustomMagicCodes)
target_link_libraries(DKRLegacyImportLibrary PUBLIC DKRCustomMagicCodes)
target_link_libraries(DKRCustomMagicCodes PUBLIC DKRLegacyImportLibrary)
target_compile_definitions(DKRLegacyModLaunch PRIVATE DKR_RELEASE_VERSION="${DKR_RELEASE_VERSION}")
if(MSVC)
    target_compile_options(DKRCustomMagicCodes PRIVATE /fp:strict /FS)
else()
    target_compile_options(DKRCustomMagicCodes PRIVATE -ffp-contract=off -fno-fast-math -frounding-math)
endif()
add_executable(DKRMagicCodeInspect EXCLUDE_FROM_ALL
    "${DKRPORT_ROOT}/runtime-recomp/src/tools/magic_code_inspect.cpp")
target_link_libraries(DKRMagicCodeInspect PRIVATE DKRCustomMagicCodes)
target_compile_definitions(DKRMagicCodeInspect PRIVATE DKR_CODE_HOST_VERSION="${DKR_RELEASE_VERSION}")
set_target_properties(DKRMagicCodeInspect PROPERTIES OUTPUT_NAME DKR-MagicCode-Inspect)
if(BUILD_TESTING AND NOT ANDROID)
    add_executable(DKRCodeModHandoffTests "${DKRPORT_ROOT}/runtime-recomp/tests/code_mod_handoff_tests.cpp")
    target_link_libraries(DKRCodeModHandoffTests PRIVATE DKRCustomMagicCodes)
    add_test(NAME DKRCodeModHandoff COMMAND DKRCodeModHandoffTests)
    add_executable(DKRCodeModNativeArgumentsTests "${DKRPORT_ROOT}/runtime-recomp/tests/code_mod_native_arguments_tests.cpp")
    target_link_libraries(DKRCodeModNativeArgumentsTests PRIVATE DKRCustomMagicCodes)
    add_test(NAME DKRCodeModNativeArguments COMMAND DKRCodeModNativeArgumentsTests)
endif()
if(WIN32)
    # RT64's static archive names its pinned SDL dependency without exporting
    # its Windows search directory. The inspector never initializes SDL.
    set(code_sdl "${DKR_DEPENDENCIES_DIR}/rt64/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/lib/x64")
    target_link_directories(DKRMagicCodeInspect PRIVATE "${code_sdl}")
    add_custom_command(TARGET DKRMagicCodeInspect POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${code_sdl}/SDL2.dll" "$<TARGET_FILE_DIR:DKRMagicCodeInspect>/SDL2.dll")
endif()

function(dkr_stage_magic_code_payload output source revision)
    get_filename_component(parent "${source}" DIRECTORY)
    set(DKR_CODE_POLICY_V${revision} "${parent}/v${revision}.toml" CACHE FILEPATH "Complete Patch Pipeline config for code-mod protection")
    set(policy "${DKR_CODE_POLICY_V${revision}}")
    if(NOT EXISTS "${policy}")
        message(FATAL_ERROR "Set DKR_CODE_POLICY_V${revision} to the complete config that generated its CPU input")
    endif()
    file(SHA256 "${policy}" policy_digest)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${policy}")
    file(GLOB inputs CONFIGURE_DEPENDS "${source}/*.c" "${source}/*.h")
    set(parts "")
    foreach(input IN LISTS inputs)
        file(SHA256 "${input}" digest)
        get_filename_component(name "${input}" NAME)
        string(APPEND parts "${name}:${digest}|")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${input}")
    endforeach()
    set(script "${DKRPORT_ROOT}/scripts/stage_magic_code_payload.py")
    file(SHA256 "${script}" script_digest)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${script}")
    string(SHA256 identity "${revision}|${script_digest}|${policy_digest}|${parts}")
    set(directory "${CMAKE_BINARY_DIR}/generated/code-dispatch-v${revision}-${identity}")
    if(NOT EXISTS "${directory}/magic-code-dispatch.json")
        execute_process(COMMAND "${Python3_EXECUTABLE}" "${script}" "${source}" "${directory}"
            --revision "${revision}" --policy-config "${policy}" RESULT_VARIABLE status)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "Custom Magic Codes CPU staging failed; protected inputs were not changed")
        endif()
    endif()
    set(${output} "${directory}" PARENT_SCOPE)
endfunction()
