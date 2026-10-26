cmake_minimum_required(VERSION 3.24)
project(DKRBuildIteration LANGUAGES C)
include("${DKR_CMAKE_DIR}/SourceRoots.cmake")
file(WRITE "${CMAKE_BINARY_DIR}/roots.txt" "${DKR_SOURCE_ROOT}\n${DKR_DEPENDENCIES_DIR}\n")
include("${DKR_CMAKE_DIR}/StagedPatch.cmake")
add_library(fixture STATIC "${CMAKE_CURRENT_SOURCE_DIR}/fixture.c")
dkr_stage_patch(fixture "${CMAKE_CURRENT_SOURCE_DIR}" first "${CMAKE_CURRENT_SOURCE_DIR}/first.patch" fixture.c)
dkr_stage_patch(
    fixture
    "${CMAKE_BINARY_DIR}/generated/first"
    second
    "${CMAKE_CURRENT_SOURCE_DIR}/second.patch"
    fixture.c
)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
include("${DKR_CMAKE_DIR}/RevisionSymbolMap.cmake")
dkr_generate_revision_symbol_map("${CMAKE_CURRENT_SOURCE_DIR}/payload" "alternate_" "${CMAKE_BINARY_DIR}/map.hpp")
