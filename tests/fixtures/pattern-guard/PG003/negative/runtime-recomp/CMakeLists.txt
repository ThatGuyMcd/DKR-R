add_executable(DKRPortGame src/game/main.cpp)
set_target_properties(DKRPortGame PROPERTIES OUTPUT_NAME "DKR-R-Runtime")
# Third-party SOURCES swaps are how staged dependency patches compile.
get_target_property(rt64_sources rt64 SOURCES)
list(FILTER rt64_sources EXCLUDE REGEX "(^|/)rt64_application_window\\.cpp$")
set_property(TARGET rt64 PROPERTY SOURCES "${rt64_sources}")
set_source_files_properties("${CMAKE_BINARY_DIR}/generated/stage/src/render/rt64_texture_cache.cpp"
    TARGET_DIRECTORY rt64 PROPERTIES INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}/generated/stage/src/render")
target_sources(DKRPortGame PRIVATE src/game/feature.cpp)
