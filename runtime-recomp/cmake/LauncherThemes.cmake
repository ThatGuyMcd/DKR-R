include_guard(GLOBAL)
function(dkr_add_launcher_theme_checks)
    if(TARGET DKRLauncherThemeTests)
        return()
    endif()
    get_filename_component(root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../.." ABSOLUTE)
    add_executable(DKRLauncherThemeTests "${root}/runtime-recomp/tests/launcher_theme_tests.cpp")
    target_include_directories(DKRLauncherThemeTests PRIVATE "${root}/runtime-recomp/src/game")
    target_compile_features(DKRLauncherThemeTests PRIVATE cxx_std_20)
    if(WIN32)
        target_link_libraries(DKRLauncherThemeTests PRIVATE user32)
    endif()
    set(imgui "${DKR_DEPENDENCIES_DIR}/rt64/src/contrib/imgui")
    add_executable(
        DKRLauncherThemeDrawTests
        "${root}/runtime-recomp/tests/launcher_theme_draw_tests.cpp"
        "${imgui}/imgui.cpp"
        "${imgui}/imgui_draw.cpp"
        "${imgui}/imgui_tables.cpp"
        "${imgui}/imgui_widgets.cpp"
    )
    target_include_directories(DKRLauncherThemeDrawTests PRIVATE "${root}/runtime-recomp/src/game" "${imgui}")
    target_compile_features(DKRLauncherThemeDrawTests PRIVATE cxx_std_20)
    foreach(target DKRLauncherThemeTests DKRLauncherThemeDrawTests)
        if(MSVC)
            target_compile_options(${target} PRIVATE /UNDEBUG)
        else()
            target_compile_options(${target} PRIVATE -UNDEBUG)
        endif()
    endforeach()
    if(BUILD_TESTING)
        add_test(NAME DKRLauncherThemes COMMAND DKRLauncherThemeTests)
        add_test(NAME DKRLauncherThemeDraw COMMAND DKRLauncherThemeDrawTests)
    endif()
endfunction()

function(dkr_add_launcher_theme_render_check)
    if(TARGET DKRPortGame AND NOT TARGET DKRLauncherThemeRenderChecks)
        get_filename_component(root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../.." ABSOLUTE)
        set(imgui "${DKR_DEPENDENCIES_DIR}/rt64/src/contrib/imgui")
        add_executable(
            DKRLauncherThemeRenderChecks
            EXCLUDE_FROM_ALL
            "${root}/runtime-recomp/tests/launcher_theme_render_tests.cpp"
            "${imgui}/imgui.cpp"
            "${imgui}/imgui_draw.cpp"
            "${imgui}/imgui_tables.cpp"
            "${imgui}/imgui_widgets.cpp"
            "${imgui}/backends/imgui_impl_sdlrenderer2.cpp"
        )
        target_compile_features(DKRLauncherThemeRenderChecks PRIVATE cxx_std_20)
        get_target_property(game_includes DKRPortGame INCLUDE_DIRECTORIES)
        get_target_property(game_links DKRPortGame LINK_DIRECTORIES)
        target_include_directories(
            DKRLauncherThemeRenderChecks
            PRIVATE
                ${game_includes}
                "${root}/runtime-recomp/src/game"
                "${imgui}"
                "${DKR_DEPENDENCIES_DIR}/rt64/src/contrib/stb"
        )
        if(game_links)
            target_link_directories(DKRLauncherThemeRenderChecks PRIVATE ${game_links})
        endif()
        if(TARGET SDL2::SDL2)
            target_link_libraries(DKRLauncherThemeRenderChecks PRIVATE SDL2::SDL2)
        else()
            target_link_libraries(DKRLauncherThemeRenderChecks PRIVATE SDL2)
        endif()
        if(MSVC)
            target_compile_options(DKRLauncherThemeRenderChecks PRIVATE /UNDEBUG)
        else()
            target_compile_options(DKRLauncherThemeRenderChecks PRIVATE -UNDEBUG)
        endif()
    endif()
endfunction()
