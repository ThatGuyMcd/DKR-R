# Authored portrait decoding is shared by the game and synthetic host fixtures.
include_guard(GLOBAL)
add_library(
    DKRCharacterPortraits
    STATIC
    "${CMAKE_CURRENT_LIST_DIR}/../src/game/character_portraits/character_portraits.cpp"
)
target_compile_features(DKRCharacterPortraits PUBLIC cxx_std_20)
target_compile_definitions(DKRCharacterPortraits PRIVATE NOMINMAX)
target_include_directories(
    DKRCharacterPortraits
    PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../src/game/character_portraits"
    PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../src/game"
)
target_link_libraries(DKRCharacterPortraits PUBLIC DKRLegacyModCore PRIVATE Threads::Threads)
