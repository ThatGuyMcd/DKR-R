add_executable(DKRPortGame src/game/main.cpp)
set_target_properties(DKRPortGame PROPERTIES SOURCES "${CMAKE_BINARY_DIR}/generated/main.cpp")
get_target_property(game_sources DKRPortGame SOURCES)
list(TRANSFORM game_sources REPLACE "src/game" "${CMAKE_BINARY_DIR}/generated")
set_property(TARGET DKRPortGame PROPERTY SOURCES "${game_sources}")
set_source_files_properties(src/game/main.cpp PROPERTIES
    INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}/generated/feature")
dkr_repair_replace_source(DKRPortGame src/game/main.cpp "${CMAKE_BINARY_DIR}/generated/main.cpp")
