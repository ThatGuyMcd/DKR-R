# All dependency modifications use checked isolated copies, never the checkout.
find_package(Git REQUIRED)
function(dkr_stage_patch target source stage_name patch)
    set(stage "${CMAKE_BINARY_DIR}/generated/${stage_name}")
    set(files ${ARGN})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${patch}")
    foreach(file IN LISTS files)
        get_filename_component(parent "${file}" DIRECTORY)
        file(MAKE_DIRECTORY "${stage}/${parent}")
        configure_file("${source}/${file}" "${stage}/${file}" COPYONLY)
    endforeach()
    execute_process(COMMAND "${GIT_EXECUTABLE}" init --quiet "${stage}"
        COMMAND_ERROR_IS_FATAL ANY)
    foreach(mode IN ITEMS check apply)
        set(flags --recount --whitespace=error)
        if(mode STREQUAL "check")
            list(APPEND flags --check)
        endif()
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${stage}" apply ${flags} "${patch}"
            RESULT_VARIABLE result ERROR_VARIABLE error)
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "${stage_name} ${mode} failed: ${error}")
        endif()
    endforeach()
    get_target_property(sources ${target} SOURCES)
    foreach(file IN LISTS files)
        get_filename_component(name "${file}" NAME)
        set(matches ${sources})
        list(FILTER matches INCLUDE REGEX "(^|/)${name}$")
        list(LENGTH matches count)
        if(NOT count EQUAL 1)
            message(FATAL_ERROR "${stage_name}: expected one ${name}, found ${count}")
        endif()
        list(REMOVE_ITEM sources ${matches})
        list(APPEND sources "${stage}/${file}")
        get_filename_component(parent "${file}" DIRECTORY)
        set_source_files_properties("${stage}/${file}" TARGET_DIRECTORY ${target}
            PROPERTIES INCLUDE_DIRECTORIES "${source}/${parent};${DKRPORT_ROOT}/runtime-recomp/src/game")
    endforeach()
    set_property(TARGET ${target} PROPERTY SOURCES "${sources}")
endfunction()

dkr_stage_patch(ultramodern "${DKR_MODERN_RUNTIME_SOURCE}" performance-runtime
    "${DKRPORT_ROOT}/patches/performance/runtime-snapshot-pool.patch" ultramodern/src/events.cpp)
dkr_stage_patch(ultramodern "${CMAKE_BINARY_DIR}/generated/performance-runtime" host-task-lifetime
    "${DKRPORT_ROOT}/patches/performance/runtime-host-task-lifetime.patch" ultramodern/src/events.cpp)
dkr_stage_patch(ultramodern "${CMAKE_BINARY_DIR}/generated/host-task-lifetime" vi-bootstrap
    "${DKRPORT_ROOT}/patches/performance/runtime-vi-bootstrap.patch" ultramodern/src/events.cpp)
dkr_stage_patch(librecomp "${DKR_MODERN_RUNTIME_SOURCE}" session-bootstrap
    "${DKRPORT_ROOT}/patches/performance/runtime-session-bootstrap.patch" librecomp/src/recomp.cpp)
if(BUILD_TESTING AND NOT ANDROID)
    add_executable(DKRViBootstrapTests "${DKRPORT_ROOT}/runtime-recomp/tests/vi_bootstrap_tests.cpp")
    target_compile_definitions(DKRViBootstrapTests PRIVATE
        DKR_VI_SOURCE="${CMAKE_BINARY_DIR}/generated/vi-bootstrap/ultramodern/src/events.cpp")
    target_include_directories(DKRViBootstrapTests PRIVATE
        "${DKR_MODERN_RUNTIME_SOURCE}/ultramodern/include"
        "${DKR_MODERN_RUNTIME_SOURCE}/thirdparty"
        "${DKR_MODERN_RUNTIME_SOURCE}/thirdparty/concurrentqueue"
        "${DKRPORT_ROOT}/runtime-recomp/src/game")
    if(MSVC)
        target_compile_options(DKRViBootstrapTests PRIVATE /Gy /Gw)
        target_link_options(DKRViBootstrapTests PRIVATE /OPT:REF)
    else()
        target_compile_options(DKRViBootstrapTests PRIVATE -ffunction-sections -fdata-sections)
        target_link_options(DKRViBootstrapTests PRIVATE -Wl,--gc-sections)
        target_link_libraries(DKRViBootstrapTests PRIVATE pthread)
    endif()
    add_test(NAME DKRViBootstrap COMMAND DKRViBootstrapTests)
    set_tests_properties(DKRViBootstrap PROPERTIES TIMEOUT 30)
endif()
# Fault injection is deliberately absent from distributed binaries.
option(DKR_TASK_QUALIFICATION "Private host-task delay qualification" OFF)
option(DKR_WATER_QUALIFICATION "Private offline water preview qualification; never package" OFF)
if(DKR_WATER_QUALIFICATION)
    add_compile_definitions(DKR_WATER_QUALIFICATION=1)
endif()
if(DKR_TASK_QUALIFICATION)
    add_compile_definitions(DKR_TASK_QUALIFICATION=1)
    # ultramodern's directory was configured before this file was included.
    target_compile_definitions(ultramodern PRIVATE DKR_TASK_QUALIFICATION=1)
endif()
dkr_stage_patch(ultramodern "${DKR_MODERN_RUNTIME_SOURCE}" performance-idle
    "${DKRPORT_ROOT}/patches/performance/runtime-idle-diagnostics.patch" ultramodern/src/mesgqueue.cpp)
dkr_stage_patch(ultramodern "${CMAKE_BINARY_DIR}/generated/performance-idle" performance-events
    "${DKRPORT_ROOT}/patches/performance/runtime-deferred-events.patch" ultramodern/src/mesgqueue.cpp)
