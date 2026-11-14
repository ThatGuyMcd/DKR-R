include_guard(GLOBAL)

function(dkr_add_online_stability)
    # Set here, not at file scope: the first include can come from inside
    # another function, whose scope a file-level variable would not outlive.
    get_filename_component(_dkr_online_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../.." ABSOLUTE)
    if(NOT TARGET DKRQualificationDeliveryTests)
        add_executable(
            DKRQualificationDeliveryTests
            "${_dkr_online_root}/runtime-recomp/tests/qualification_delivery_tests.cpp"
        )
        target_include_directories(DKRQualificationDeliveryTests PRIVATE "${_dkr_online_root}/runtime-recomp/src/game")
        target_compile_features(DKRQualificationDeliveryTests PRIVATE cxx_std_20)
        if(MSVC)
            target_compile_options(DKRQualificationDeliveryTests PRIVATE /UNDEBUG)
        else()
            target_compile_options(DKRQualificationDeliveryTests PRIVATE -UNDEBUG)
        endif()
        if(BUILD_TESTING)
            add_test(NAME DKRQualificationDelivery COMMAND DKRQualificationDeliveryTests)
        endif()
    endif()
    if(NOT TARGET DKRSnapshotHistoryTests)
        add_executable(
            DKRSnapshotHistoryTests
            "${_dkr_online_root}/runtime-recomp/tests/snapshot_history_tests.cpp"
            "${_dkr_online_root}/runtime-recomp/src/game/netplay/rollback_state_store.cpp"
        )
        target_include_directories(DKRSnapshotHistoryTests PRIVATE "${_dkr_online_root}/runtime-recomp/src/game")
        target_compile_features(DKRSnapshotHistoryTests PRIVATE cxx_std_20)
        if(MSVC)
            target_compile_options(DKRSnapshotHistoryTests PRIVATE /UNDEBUG)
        else()
            target_compile_options(DKRSnapshotHistoryTests PRIVATE -UNDEBUG)
        endif()
        if(BUILD_TESTING)
            add_test(NAME DKRSnapshotHistory COMMAND DKRSnapshotHistoryTests)
        endif()
    endif()
    # This classifier belongs to the selected RT64/present-gate integration.
    # Qualify it against that production library, not the pinned host capsule.
    if(TARGET DKRPortGame AND NOT TARGET DKROnlinePictureTests)
        add_executable(DKROnlinePictureTests "${_dkr_online_root}/runtime-recomp/tests/online_picture_tests.cpp")
        target_include_directories(DKROnlinePictureTests PRIVATE "${_dkr_online_root}/runtime-recomp/src/game")
        target_link_libraries(DKROnlinePictureTests PRIVATE DKRNetplayCore Threads::Threads)
        target_compile_features(DKROnlinePictureTests PRIVATE cxx_std_20)
        if(MSVC)
            target_compile_options(DKROnlinePictureTests PRIVATE /UNDEBUG)
        else()
            target_compile_options(DKROnlinePictureTests PRIVATE -UNDEBUG)
        endif()
        if(BUILD_TESTING)
            add_test(NAME DKROnlinePicture COMMAND DKROnlinePictureTests)
        endif()
    endif()
    if(NOT TARGET DKROnlinePresentationTests)
        add_executable(
            DKROnlinePresentationTests
            "${_dkr_online_root}/runtime-recomp/tests/online_presentation_tests.cpp"
        )
        target_include_directories(DKROnlinePresentationTests PRIVATE "${_dkr_online_root}/runtime-recomp/src/game")
        target_compile_features(DKROnlinePresentationTests PRIVATE cxx_std_20)
        target_link_libraries(DKROnlinePresentationTests PRIVATE Threads::Threads)
        if(MSVC)
            target_compile_options(DKROnlinePresentationTests PRIVATE /UNDEBUG)
        else()
            target_compile_options(DKROnlinePresentationTests PRIVATE -UNDEBUG)
        endif()
        if(BUILD_TESTING)
            add_test(NAME DKROnlinePresentation COMMAND DKROnlinePresentationTests)
        endif()
    endif()
    foreach(
        target
        DKRNetplayCore
        DKRPortGame
        DKRLegacyModCore
        DKRLegacyImportLibrary
        DKRLegacyModWorker
    )
        if(TARGET ${target})
            target_include_directories(${target} PRIVATE "${_dkr_online_root}/runtime-recomp/src/game")
        endif()
    endforeach()
    include("${_dkr_online_root}/runtime-recomp/cmake/SessionChat.cmake")
    dkr_add_session_chat()
    target_link_libraries(DKRNetplayCore PUBLIC DKRSessionChat)
    include("${_dkr_online_root}/runtime-recomp/cmake/PeerRelay.cmake")
    dkr_add_peer_relay()
    target_link_libraries(DKRNetplayCore PUBLIC DKRPeerRelay)
    if(NOT TARGET DKROnlineStabilityCore)
        add_library(
            DKROnlineStabilityCore
            STATIC
            "${_dkr_online_root}/runtime-recomp/src/game/online_stability/control.cpp"
            "${_dkr_online_root}/runtime-recomp/src/game/online_stability/live_delay.cpp"
            "${_dkr_online_root}/runtime-recomp/src/game/online_stability/preparation_policy.cpp"
            "${_dkr_online_root}/runtime-recomp/src/game/online_stability/process_services.cpp"
        )
        target_compile_features(DKROnlineStabilityCore PUBLIC cxx_std_20)
        target_include_directories(DKROnlineStabilityCore PUBLIC "${_dkr_online_root}/runtime-recomp/src/game")
        if(MSVC)
            target_compile_definitions(DKROnlineStabilityCore PRIVATE NOMINMAX)
        endif()
    endif()
    if(NOT TARGET DKROnlineStabilityTests)
        add_executable(DKROnlineStabilityTests "${_dkr_online_root}/runtime-recomp/tests/online_stability_tests.cpp")
        target_link_libraries(DKROnlineStabilityTests PRIVATE DKROnlineStabilityCore)
        if(MSVC)
            target_compile_options(DKROnlineStabilityTests PRIVATE /UNDEBUG)
        else()
            target_compile_options(DKROnlineStabilityTests PRIVATE -UNDEBUG)
        endif()
        if(BUILD_TESTING)
            add_test(NAME DKROnlineStability COMMAND DKROnlineStabilityTests)
        endif()
    endif()
    foreach(test online_flow online_repair)
        if(NOT TARGET DKR_${test}_tests)
            add_executable(DKR_${test}_tests "${_dkr_online_root}/runtime-recomp/tests/${test}_tests.cpp")
            target_link_libraries(DKR_${test}_tests PRIVATE DKRNetplayCore DKROnlineStabilityCore)
            if(test STREQUAL "online_flow")
                # Its network object calls the SDK tools broker, which lives in the game.
                target_sources(
                    DKR_${test}_tests
                    PRIVATE "${_dkr_online_root}/runtime-recomp/tests/sdk2_network_idle.cpp"
                )
            endif()
            if(TARGET DKRLegacyModCore)
                target_link_libraries(DKR_${test}_tests PRIVATE DKRLegacyModCore)
            endif()
            if(TARGET DKRLegacyModCore)
                target_compile_definitions(DKR_${test}_tests PRIVATE DKR_PIPELINE_TRANSFER=1)
            endif()
            if(TARGET DKRPortGame)
                get_target_property(test_links DKRPortGame LINK_DIRECTORIES)
                if(test_links)
                    target_link_directories(DKR_${test}_tests PRIVATE ${test_links})
                endif()
            endif()
            if(MSVC)
                target_compile_options(DKR_${test}_tests PRIVATE /UNDEBUG)
            else()
                target_compile_options(DKR_${test}_tests PRIVATE -UNDEBUG)
            endif()
            if(BUILD_TESTING)
                add_test(NAME DKR_${test} COMMAND DKR_${test}_tests)
            endif()
        endif()
    endforeach()
    if(TARGET DKRLegacyImportLibrary AND NOT TARGET DKROnlineRetentionTests)
        add_executable(
            DKROnlineRetentionTests
            "${_dkr_online_root}/runtime-recomp/tests/online_retention_tests.cpp"
            "${_dkr_online_root}/runtime-recomp/src/game/rom_revision.cpp"
            "${_dkr_online_root}/runtime-recomp/src/game/startup_performance.cpp"
        )
        target_link_libraries(DKROnlineRetentionTests PRIVATE DKRLegacyImportLibrary DKROnlineStabilityCore)
        if(MSVC)
            target_compile_options(DKROnlineRetentionTests PRIVATE /UNDEBUG)
        else()
            target_compile_options(DKROnlineRetentionTests PRIVATE -UNDEBUG)
        endif()
        if(TARGET DKRPortGame)
            get_target_property(retention_links DKRPortGame LINK_DIRECTORIES)
            if(retention_links)
                target_link_directories(DKROnlineRetentionTests PRIVATE ${retention_links})
            endif()
        endif()
    endif()
    if(TARGET DKRLegacyModCore AND TARGET DKRUpdateClient AND NOT TARGET DKRUpdateLifetimeTests)
        add_executable(
            DKRUpdateLifetimeTests
            "${_dkr_online_root}/runtime-recomp/tests/update_process_lifetime_tests.cpp"
        )
        target_link_libraries(DKRUpdateLifetimeTests PRIVATE DKRUpdateClient DKROnlineStabilityCore)
        if(MSVC)
            target_compile_options(DKRUpdateLifetimeTests PRIVATE /UNDEBUG)
        else()
            target_compile_options(DKRUpdateLifetimeTests PRIVATE -UNDEBUG)
        endif()
    endif()
    if(TARGET DKROnlineModPreparation AND NOT TARGET DKROnlinePreparationChecks)
        # Link the existing profile corpus checks to the final production
        # importer/preparation targets, after the retained pipeline has composed
        # them. A standalone historical importer is not sufficient evidence.
        add_executable(
            DKROnlinePreparationChecks
            EXCLUDE_FROM_ALL
            "${_dkr_online_root}/runtime-recomp/tests/online_mod_profile_tests.cpp"
            "${_dkr_online_root}/runtime-recomp/src/game/rom_revision.cpp"
            "${_dkr_online_root}/runtime-recomp/src/game/startup_performance.cpp"
        )
        target_link_libraries(
            DKROnlinePreparationChecks
            PRIVATE DKRLegacyImportLibrary DKROnlineModPreparation DKRLegacyModLaunch DKROnlineStabilityCore
        )
        target_include_directories(
            DKROnlinePreparationChecks
            PRIVATE "${DKR_DEPENDENCIES_DIR}/n64-modern-runtime/thirdparty"
        )
        if(TARGET DKRPortGame)
            get_target_property(preparation_links DKRPortGame LINK_DIRECTORIES)
            if(preparation_links)
                target_link_directories(DKROnlinePreparationChecks PRIVATE ${preparation_links})
            endif()
        endif()
    endif()
endfunction()
