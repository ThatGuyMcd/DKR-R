# Compiler warnings for first-party targets. Nothing here applies itself:
# call dkr_enable_warnings(<target>) per first-party target and
# dkr_disable_warnings(<target>) for recompiled or generated code. Third-party
# targets under extern/ keep their own flags.
#
# DKR_WERROR promotes only a curated subset to errors (each one is a real bug
# on at least one platform we ship). Blanket -Werror is per target through
# DKR_STRICT_MODULES, which starts empty and grows as a module is cleaned up.
include_guard(GLOBAL)
option(DKR_WERROR "Promote the curated warning subset to errors on first-party targets" OFF)
set(DKR_STRICT_MODULES "" CACHE STRING "First-party targets built with blanket -Werror or /WX")

function(dkr_enable_warnings target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "dkr_enable_warnings: ${target} is not a target")
    endif()
    get_target_property(type ${target} TYPE)
    if(type STREQUAL "INTERFACE_LIBRARY")
        message(FATAL_ERROR "dkr_enable_warnings: ${target} is an INTERFACE library; use the targets that compile it")
    endif()

    if(MSVC)
        # Also covers clang-cl, which maps /W4 to -Wall -Wextra.
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
        if(DKR_WERROR)
            target_compile_options(
                ${target}
                PRIVATE
                    /we4013 # undefined function assumed extern returning int
                    /we4700 # uninitialized local variable used
                    /we4716 # function must return a value
                    /we4477 # format string does not match argument type
                    $<$<COMPILE_LANGUAGE:C>:/we4047> # differs in levels of indirection (int/pointer)
            )
        endif()
        set(strict /WX)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(
            ${target}
            PRIVATE -Wall -Wextra -Wshadow -Wformat=2 -Wundef -Wvla -Wimplicit-fallthrough -Wmissing-declarations
        )
        if(DKR_WERROR)
            target_compile_options(
                ${target}
                PRIVATE
                    -Werror=return-type
                    -Werror=uninitialized
                    -Werror=format-security
                    $<$<COMPILE_LANGUAGE:C>:-Werror=implicit-function-declaration>
                    $<$<COMPILE_LANGUAGE:C>:-Werror=int-conversion>
            )
        endif()
        set(strict -Werror)
    else()
        message(WARNING "dkr_enable_warnings: no warning set for ${CMAKE_CXX_COMPILER_ID}; ${target} uses defaults")
        return()
    endif()

    if(target IN_LIST DKR_STRICT_MODULES)
        target_compile_options(${target} PRIVATE ${strict})
    endif()
endfunction()

# Recompiled N64 code and generated tables are not edited by hand, so warnings
# there are noise that hides first-party ones.
function(dkr_disable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /w)
    else()
        target_compile_options(${target} PRIVATE -w)
    endif()
endfunction()
