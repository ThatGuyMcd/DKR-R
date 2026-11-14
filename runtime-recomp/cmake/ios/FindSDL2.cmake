# RT64 calls find_package(SDL2). The iOS app builds static SDL2 in-tree, and
# the iPhone SDK does not have a host SDL2 package to find.
if(NOT TARGET SDL2::SDL2)
    message(FATAL_ERROR "iOS configures SDL2 before RT64 looks it up.")
endif()

set(SDL2_FOUND TRUE)
if(CMAKE_BUILD_TYPE)
    string(TOLOWER "${CMAKE_BUILD_TYPE}" _dkr_ios_sdl_config)
else()
    set(_dkr_ios_sdl_config "release")
endif()
set(SDL2_INCLUDE_DIRS
    "${CMAKE_BINARY_DIR}/sdl2/include"
    "${CMAKE_BINARY_DIR}/sdl2/include/SDL2"
    "${CMAKE_BINARY_DIR}/sdl2/include-config-${_dkr_ios_sdl_config}/SDL2"
)
# SDL2main stays on the app target. Linking it into rt64 as well defines main twice.
set(SDL2_LIBRARIES SDL2::SDL2)
