#include <dkr/game.h>

static DkrU32 starting_bananas;
static DkrU32 affected;

DKR_CALLBACK(on_boot) void boot(void) {
    double value = dkr_setting_number("bananas");
    starting_bananas = (DkrU32)(value + 0.5);
    affected = (DkrU32)dkr_setting_number("affect");
    char label[96];
    DkrU32 bytes = dkr_setting_string("label", 0, 0);
    if (bytes > 0 && bytes <= sizeof(label)) {
        dkr_setting_string("label", label, sizeof(label));
        dkr_log(label);
    }
}

/* Teaching example: any native roster setup, including demo/menu setups.
 * Add reviewed mode/race-type guards for a narrower published rule. */
DKR_CALLBACK(after_track_setup_racers) void apply_start(void) {
    DkrU32 count = dkr_racer_count();
    for (DkrU32 i = 0; i < count; ++i) {
        DkrRacerState racer;
        if (!dkr_racer_get(i, &racer))
            continue;
        if (racer.finished)
            continue;
        if (affected == 1 && racer.player < 0)
            continue;
        if (affected == 2 && racer.player >= 0)
            continue;
        dkr_racer_set(i, DKR_RACER_BANANAS, starting_bananas);
    }
}
