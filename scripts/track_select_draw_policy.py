"""Scoped signed Track Select drawing. Inputs are reviewed retail ELFs only.

The native unlock, placeholder, row/name, arrow and zoom logic is retained.
Five columns use a separate 240-byte stack table, NEVER the nine-entry global.
All peers prepare the same bounded cells; only renderer clipping is local.
"""
import copy
import struct
from legacy_character_presentation_policy import reviewed_revision

def compose_track_draw(policy, elf, revision, sections, symbols):
    if reviewed_revision(elf)!=revision:
        raise ValueError('Unreviewed Track Select draw ELF')
    result=copy.deepcopy(policy)
    words={s+i:struct.unpack_from('>I',d,i)[0] for s,d in sections for i in range(0,len(d),4)}
    sizes={'trackmenu_render_names':1484,'trackmenu_render_2D':1224,
           'texrect_draw_scaled':1604,'texrect_draw':584,'render_text_string':2044,'viewport_main':3424}
    starts={}
    for name,size in sizes.items():
        if len(symbols.get(name,()))!=1:raise ValueError('Ambiguous draw symbol: '+name)
        start,actual=next(iter(symbols[name]))
        if actual!=size:raise ValueError('Changed draw function bounds: '+name)
        starts[name]=start
    def checked(name,offset,expected):
        pc=starts[name]+offset
        if words.get(pc)!=expected:raise ValueError(f'Changed draw instruction: {name}+{offset:#x}')
        return pc
    def hook(name,offset,expected,event,param='0U',extra=''):
        pc=checked(name,offset,expected)
        if any(int(h['beforeVram'],0)==pc for h in result['functionHooks']):
            raise ValueError(f'Draw hook owner conflict: {name}+{offset:#x}')
        call=f'dkr_track_select_draw(rdram, ctx, {event}U, {param})'
        result['functionHooks'].append({'function':name,'beforeVram':hex(pc),
            'text':'extern unsigned dkr_track_select_draw(uint8_t*, recomp_context*, unsigned, unsigned); '+
                (extra.replace('$CALL',call) if extra else call+';'),
            'reason':'Bounded Track Select-only signed drawing; local aspect never enters CPU replay.'})
    def patch(name,offset,expected,value):
        pc=checked(name,offset,expected)
        if any(int(p['vram'],0)==pc for p in result['instructionPatches']):
            raise ValueError(f'Draw patch owner conflict: {name}+{offset:#x}')
        result['instructionPatches'].append({'function':name,'vram':hex(pc),'value':hex(value),
            'reason':'Use the dedicated 15-cell stack table / scoped rectangle guard; preserve native delay slots.'})
    name='trackmenu_render_names'
    patch(name,0,0x27BDFF80,0x27BDFE80) # 0x180-byte frame, table [sp+0x90,sp+0x180)
    patch(name,0x5C8,0x27BD0080,0x27BD0180)
    details=next(iter(symbols['gTrackSelectRenderDetails']))[0]
    patch(name,0xAC,0x26100000|(details&0xFFFF),0x27B00090)
    patch(name,0x508,0x26100000|(details&0xFFFF),0x27B00090)
    patch(name,0x500,0x26730000|((details+144)&0xFFFF),0x27B30180)
    patch(name,0x11C,0x2416FFFF,0x2416FFFE) # j=-2
    patch(name,0x45C,0x24010002,0x24010003) # j<3
    hook(name,4,0xAFBF0054,10)
    dl=next(iter(symbols['sMenuCurrDisplayList']))[0]
    hook(name,0x4B4,0x8E230000,0,hex(dl)+'U')
    hook(name,0x584,0x3C028012,1,hex(dl)+'U')
    # Native horizontal image-half tests only; vertical/opacity gates unchanged.
    name='trackmenu_render_2D'
    for offset,word in [(0x28C,0x10200018),(0x294,0x1A800016),(0x2F4,0x10200013),(0x304,0x1940000F)]:
        patch(name,offset,word,0)
    name='texrect_draw_scaled'
    hook(name,0x330,0x102000A5,2,extra='if ($CALL) ctx->r1=1;')
    hook(name,0x3F8,0x1A400073,2,extra='ctx->r1=$CALL || (int32_t)ctx->r18>0;')
    patch(name,0x3F8,0x1A400073,0x10200073)
    hook(name,0x4E0,0x05610006,3)
    hook(name,0x5C8,0x8E3F0008,4)
    name='texrect_draw'
    hook(name,0xEC,0x19600039,2,extra='ctx->r1=$CALL || (int32_t)ctx->r11>0;')
    patch(name,0xEC,0x19600039,0x10200039)
    hook(name,0xFC,0x04C10004,5)
    hook(name,0x1D4,0x8D480008,6)
    name='render_text_string'
    hook(name,0x670,0x04A10006,7)
    hook(name,0x720,0x3C0E8013,8)
    # Observe the ORIGINAL viewport immediately after its native scissor packet.
    # The renderer repairs clipping; the CPU viewport and fullscreen zoom stay untouched.
    name='viewport_main'
    target=next(iter(symbols['viewport_rsp_set']))[0]
    menu=next(iter(symbols['gCurrentMenuId']))[0]
    hook(name,0x1F4,0x0C000000|((target>>2)&0x3FFFFFF),9,hex(menu)+'U')
    return result
