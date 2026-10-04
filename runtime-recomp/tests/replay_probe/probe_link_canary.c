#include <stdint.h>
#include <stddef.h>

/* Intentionally DO NOT include the private funcs/recomp headers: these are
   original stable symbol names in a separate translation unit. Linking the
   isolated guest beside them must succeed without interposition. This is a
   link-coexistence test, NOT native application/bootstrap qualification. */
struct recomp_context;
typedef void recomp_function(uint8_t*, struct recomp_context*);
static unsigned stable_calls;

void main_game_loop(uint8_t* ram, struct recomp_context* ctx) { (void)ram;(void)ctx;++stable_calls; }
void mode_game(uint8_t* ram, struct recomp_context* ctx) { (void)ram;(void)ctx;++stable_calls; }
void input_swap_id(uint8_t* ram, struct recomp_context* ctx) { (void)ram;(void)ctx;++stable_calls; }
void osRecvMesg_recomp(uint8_t* ram, struct recomp_context* ctx) { (void)ram;(void)ctx;++stable_calls; }
void dkr_netplay_resolve_authored_input_frame(uint8_t* ram, struct recomp_context* ctx) { (void)ram;(void)ctx;++stable_calls; }
int64_t cop0_status_read(struct recomp_context* ctx) { (void)ctx;++stable_calls;return 0x1234; }
void cop0_status_write(struct recomp_context* ctx, int64_t value) { (void)ctx;(void)value;++stable_calls; }
recomp_function* get_function(int32_t address) { (void)address;++stable_calls;return main_game_loop; }

unsigned dkr_probe_stable_link_calls(void) { return stable_calls; }
int dkr_probe_check_stable_link_canaries(void) {
    stable_calls=0;
    main_game_loop(NULL,NULL);mode_game(NULL,NULL);input_swap_id(NULL,NULL);
    osRecvMesg_recomp(NULL,NULL);dkr_netplay_resolve_authored_input_frame(NULL,NULL);
    if(cop0_status_read(NULL)!=0x1234)return 0;
    cop0_status_write(NULL,0);
    if(get_function(0)!=main_game_loop)return 0;
    return stable_calls==8;
}
