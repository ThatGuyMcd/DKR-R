# Diagnose missing checkouts before dependency CMake code emits partial errors.
set(_dkr_required_inputs
    n64-modern-runtime/CMakeLists.txt
    n64-modern-runtime/N64Recomp/include/recomp.h
    n64-modern-runtime/N64Recomp/lib/fmt/CMakeLists.txt
    gekkonet/GekkoLib/CMakeLists.txt
    monocypher/src/monocypher.c
)
if(DKR_RUNTIME_BUILD_RT64 OR (BUILD_TESTING AND NOT ANDROID))
    list(APPEND _dkr_required_inputs rt64/src/contrib/hlslpp/include/hlsl++.h)
endif()
if(DKR_NETPLAY_WEBRTC)
    list(
        APPEND _dkr_required_inputs
        libdatachannel/CMakeLists.txt
        libdatachannel/deps/libjuice/CMakeLists.txt
        libdatachannel/deps/json/CMakeLists.txt
        libdatachannel/deps/plog/CMakeLists.txt
        mbedtls/CMakeLists.txt
    )
endif()
if(DKR_RUNTIME_BUILD_RT64)
    list(APPEND _dkr_required_inputs rt64/CMakeLists.txt)
    if(DKR_RUNTIME_BUILD_SDL3_INPUT_HOST)
        list(APPEND _dkr_required_inputs sdl3/CMakeLists.txt)
    endif()
endif()
set(_dkr_missing_inputs)
foreach(input IN LISTS _dkr_required_inputs)
    if(NOT EXISTS "${DKR_DEPENDENCIES_DIR}/${input}")
        list(APPEND _dkr_missing_inputs "  ${input}")
    endif()
endforeach()
if(_dkr_missing_inputs)
    list(JOIN _dkr_missing_inputs "\n" _dkr_missing_text)
    message(
        FATAL_ERROR
        "Prepared dependencies are incomplete at ${DKR_DEPENDENCIES_DIR}:\n${_dkr_missing_text}\n"
        "Run scripts/bootstrap_dependencies.py from the repository root (see --help), "
        "or set DKR_DEPENDENCIES_DIR to a complete prepared extern directory. "
        "CMake never downloads or edits dependency checkouts."
    )
endif()
if(BUILD_TESTING)
    # Python tooling tests run on the host even in an Android configuration.
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    find_package(Git REQUIRED)
endif()
