# The owned rollback contracts, read from src/game/netplay/legacy_identity.hpp
# (the only place they change) into DKR_OWNED_BUILD_ID_V77/V80.
set(dkr_identity_header "${CMAKE_CURRENT_LIST_DIR}/../src/game/netplay/legacy_identity.hpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${dkr_identity_header}")
foreach(revision IN ITEMS 77 80)
    file(STRINGS "${dkr_identity_header}" line
        REGEX "^#define DKR_LEGACY_OWNED_BUILD_ID_V${revision} \"[0-9a-f]+\"$")
    list(LENGTH line count)
    if(NOT count EQUAL 1 OR NOT line MATCHES "\"([0-9a-f]+)\"$")
        message(FATAL_ERROR "legacy_identity.hpp must define DKR_LEGACY_OWNED_BUILD_ID_V${revision} once")
    endif()
    string(LENGTH "${CMAKE_MATCH_1}" length)
    if(NOT length EQUAL 64)
        message(FATAL_ERROR "DKR_LEGACY_OWNED_BUILD_ID_V${revision} is not a sha256")
    endif()
    set(DKR_OWNED_BUILD_ID_V${revision} "${CMAKE_MATCH_1}")
endforeach()
