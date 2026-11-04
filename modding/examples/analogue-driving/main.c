#include <dkr/magic_codes.h>
#include <dkr/driving.h>
_Static_assert(sizeof(DkrInput)==20,"The API 1 input ABI must remain unchanged.");
_Static_assert(sizeof(DkrDrivingInput)==20,"Analogue input v1 ABI.");

DKR_CALLBACK(on_frame) void analogue_driving(void) {
    for (DkrU32 player=0;player<4;++player) {
        DkrDrivingInput input;
        if (dkr2_driving_input(player,&input)==0 && (input.flags&1))
            dkr2_driving_apply(player);
    }
}
