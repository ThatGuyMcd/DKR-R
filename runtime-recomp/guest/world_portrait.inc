    // CharacterFlag is an in-world participant sign, not the minigame HUD.
    // Resolve this particular racer's roster slot, never replace a donor
    // globally: two custom racers may legitimately share the same donor.
    {
        const unsigned portrait_player=(uint16_t)MEM_H(0,ctx->r4);
        const unsigned portrait_index=MEM_BU(2,ctx->r4);
        const unsigned portrait_slot=portrait_player<4?portrait_player:
            (portrait_player==0xffff && portrait_index<8?portrait_index:8);
        if(portrait_slot<8) {
            recomp_context portrait_ctx=*ctx;
            portrait_ctx.f_odd=portrait_ctx.mips3_float_mode?&portrait_ctx.f1.u32l:&portrait_ctx.f0.u32h;
            get_settings(rdram,&portrait_ctx);
            const uint32_t portrait_settings=(uint32_t)portrait_ctx.r2;
            if(portrait_settings) {
                const uint32_t portrait_cell=dkr_legacy_character_portrait_lookup(
                    rdram,&portrait_ctx,portrait_settings+portrait_slot*0x18);
                if((portrait_cell&0xe0000003U)==0x80000000U) {
                    // MEM_* requires the sign-extended MIPS register form.
                    // Passing uint32_t directly rebases KSEG0 outside RAM.
                    const uint32_t portrait_draw=MEM_W(0,(gpr)(int32_t)portrait_cell);
                    if((portrait_draw&0xe0000003U)==0x80000000U) {
                        const uint32_t portrait_texture=MEM_W(0,(gpr)(int32_t)portrait_draw);
                        if((portrait_texture&0xe0000003U)==0x80000000U)
                            ctx->r11=(gpr)(int32_t)portrait_texture;
                    }
                }
            }
        }
    }
