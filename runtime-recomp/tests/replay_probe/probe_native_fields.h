#pragma once
// Shared production dispatch body. The native owner supplies its checkpointed
// flags/state and notification policy; the fixture supplies bounded inputs.
int dkr_probe_native_fields(const char* name, uint8_t* ram, struct recomp_context* context, unsigned event,
                            const uint32_t* fields) {
    (void)ram;
    (void)context;
    (void)event;
    (void)fields;
    dkr_probe_checkpoint();
    uint64_t mod_result = 0;
    if (dkr_probe_mod_dispatch(name, ram, context, 0, 0, fields, event, &mod_result))
        return (int)mod_result;
    /* runtime_legacy_mods.cpp:122-125 returns 0 before inspecting fields/event
       when the character state/selector is absent. Fresh private profiles have
       no custom content. A horn reaches event 8 through sound_play even during
       a race; it does not imply a character-menu transition. */
    if (offline && strcmp(name, "dkr_legacy_character_menu") == 0)
        return 0;
    // runtime_legacy_mods.cpp's no-session/no-selector return precedes field
    // inspection. The full-scenes payload has the reviewed stock and vehicle-notification
    // menu surface (including menu_init/return-to-stock notifications).
    // It has no legacy or Track Lab catalogue, so that exact no-state branch
    // never dereferences a host field table or overwrites a guest return.
    if (offline && canonical_presentation && strcmp(name, "dkr_legacy_track_menu") == 0 && DKR_PROBE_HAS_FULL_SCENES &&
        dkr_menu_notification(event))
        return 0;
    if (offline && canonical_presentation && strcmp(name, "dkr_legacy_track_menu") == 0 &&
        ((state.scene_unload_phase == 3 && event == 14) || (state.scene_unload_phase == 2 && event == 10)))
        return 0;
    if (strcmp(name, "dkr_legacy_track_menu") == 0)
        fprintf(stderr, "[rollback][menu-refused] event=%u full-scenes=%u offline=%u canonical=%u\n", event,
                (unsigned)DKR_PROBE_HAS_FULL_SCENES, (unsigned)offline, (unsigned)canonical_presentation);
    dkr_probe_block(name);
}
