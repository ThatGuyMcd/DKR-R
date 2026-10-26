# Instrument the complete ROM-free native build, including prepared dependencies.
# Include before any add_subdirectory so libraries and test executables share
# the same sanitizer runtime; do not modify dependency source trees.
include_guard(GLOBAL)
option(DKR_HOST_SANITIZERS "Instrument ROM-free host tests with ASan and UBSan" OFF)
if(NOT DKR_HOST_SANITIZERS)
    return()
endif()
if(NOT BUILD_TESTING OR DKR_RUNTIME_BUILD_GENERATED OR DKR_RUNTIME_BUILD_RT64)
    message(
        FATAL_ERROR
        "DKR_HOST_SANITIZERS requires BUILD_TESTING=ON and ROM-free generated/RT64 options OFF; use the host-sanitizers preset"
    )
endif()
if(
    CMAKE_CROSSCOMPILING
    OR MSVC
    OR NOT CMAKE_SYSTEM_NAME MATCHES "^(Linux|Darwin)$"
    OR NOT CMAKE_C_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$"
    OR NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$"
)
    message(FATAL_ERROR "DKR_HOST_SANITIZERS requires native Linux/macOS GCC or Clang with ASan and UBSan runtimes")
endif()

include(CMakePushCheckState)
include(CheckCSourceCompiles)
include(CheckCXXSourceCompiles)
cmake_push_check_state(RESET)
set(CMAKE_REQUIRED_FLAGS "-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all")
set(CMAKE_REQUIRED_LINK_OPTIONS -fsanitize=address,undefined)
check_c_source_compiles("int main(void) { return 0; }" DKR_C_HAS_HOST_SANITIZERS)
check_cxx_source_compiles("int main() { return 0; }" DKR_CXX_HAS_HOST_SANITIZERS)
cmake_pop_check_state()
if(NOT DKR_C_HAS_HOST_SANITIZERS OR NOT DKR_CXX_HAS_HOST_SANITIZERS)
    message(
        FATAL_ERROR
        "The selected C/C++ toolchains cannot compile and link ASan+UBSan; install the matching sanitizer runtimes"
    )
endif()

add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
add_link_options(-fsanitize=address,undefined -fno-sanitize-recover=all)
