#!/usr/bin/env python3
"""Patch Pipeline: build an isolated, closed-import guest replay probe.

Inputs (including protected generated CPU code and recomp.h) are read-only.
Output must be a NEW directory outside those inputs. This is not a runtime
adapter: every native import traps, rather than silently pretending success.
The standalone target links no launcher, audio, renderer or online runtime.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re

HEADER_HASH = "58977ed0a489f7df8b17c377a87cc6670857a5fba15af78fc7d38132fc59b33f"
DEF = re.compile(r"RECOMP_FUNC\s+void\s+(\w+)\s*\([^{};]*\)\s*\{")
PROTO = re.compile(r"(?:extern\s+)?\b(void|int|unsigned|float|double|gpr|u?int(?:8|16|32|64)_t|recomp_func_t\s*\*)\s+(\w+)\s*\(([^();{}]*)\)\s*;")
CALL = re.compile(r"\b([A-Za-z_]\w*)\s*\(")
# Project-owned import absent from N64Recomp's emitted funcs.h. ABI checked
# against runtime-recomp/src/game/virtual_pak.cpp, not inferred from its name.
PROJECT_IMPORTS = {
    "osPfsInit_recomp": ("void", ["uint8_t*", "recomp_context*"]),
    # runtime-recomp/src/game/runtime_stubs.cpp
    "rmonPrintf_recomp": ("void", ["uint8_t*", "recomp_context*"]),
    "__osSiGetAccess_recomp": ("void", ["uint8_t*", "recomp_context*"]),
    "__osSiRelAccess_recomp": ("void", ["uint8_t*", "recomp_context*"]),
    # Verified void guest-register ABI in librecomp/src/math_routines.cpp.
    "__f_to_ll_recomp": ("void", ["uint8_t*", "recomp_context*"]),
    "__ll_to_f_recomp": ("void", ["uint8_t*", "recomp_context*"]),
    "__ull_rshift_recomp": ("void", ["uint8_t*", "recomp_context*"]),
    "__ll_lshift_recomp": ("void", ["uint8_t*", "recomp_context*"]),
    # Project overrides in virtual_pak.cpp, including the low-level fallback.
    "osPfsNumFiles_recomp": ("void", ["uint8_t*", "recomp_context*"]),
    "osPfsChecker_recomp": ("void", ["uint8_t*", "recomp_context*"]),
    "osPfsIsPlug_recomp": ("void", ["uint8_t*", "recomp_context*"]),
    "__osContRamWrite_recomp": ("void", ["uint8_t*", "recomp_context*"]),
    "__osGetId_recomp": ("void", ["uint8_t*", "recomp_context*"]),
    "__osPfsGetStatus_recomp": ("void", ["uint8_t*", "recomp_context*"]),
    "__osPfsSelectBank_recomp": ("void", ["uint8_t*", "recomp_context*"]),
    # Mixed native/guest signatures verified in runtime_legacy_mods.cpp.
    "dkr_legacy_character_cinematic_id": ("unsigned", ["uint8_t*", "recomp_context*", "unsigned", "unsigned"]),
    "dkr_legacy_character_race_sound": ("unsigned", ["uint8_t*", "recomp_context*", "uint32_t", "unsigned"]),
}

# EXACT reviewed emitted mode_game bodies. Two unload call sites have no
# live host local across the split: hi/lo/result/c1cs are declaration-only;
# the jump-table addend is recomputed from checkpointed guest registers after
# the first site and is not used on the second continuation. Guest locals,
# saved RA/s0 and updateRate are in the 0x50-byte guest stack frame. A changed
# pipeline payload requires a new audit, never approximate string matching.
MODE_SCENE_HASH = {
    77: "b93a6c3fa7409349d5db0689dd9fa21c25590b16e3cf76ff3204419c0d3e113e",
    80: "323ee412a71e5b56e7b70ec25b104f85137f290b48c89ce15e14a13d75107ded",
}

# Authored CPU phase only: original main_game_loop remains emitted and fenced.
# These pins cover the complete original body, including every native hook.
# The scheduler/VI and interpolation-registry phases are NOT made into no-ops.
MAIN_CPU_HASH = {
    77: "6983546d4dbd1c734c0256516a866cdc83a28c2a1b15d4351b1fb377e0b3d0ab",
    80: "29b360351b098f56d112f51b629bb88b929440153465a251339402fdc0fa15f5",
}
VIDEO_CPU_HASH = {
    77: {"fb_update": "0985bdc3788dcd9dcb3f9a087762510426f573e4e69c9feec1872bca99197d99",
         "fb_swap": "263cb9dae5645bd95a93148533e04dc65b40a16309c2b0b6e58098064e9a5850"},
    80: {"fb_update": "dab5246c92faea42ecc1e9d5225a39a11ce5aac3d0f0a42b71f52546a7b5b69f",
         "fb_swap": "68891ec728466b61473c722cff4263c48c9ff773653659ad204999fb878f889d"},
}
UNLOAD_CPU_HASH = {
    77: "1e32bb1436b0a5b59c56e5936b02001d246b24bac4b3c48434fddd62ef3b3abe",
    80: "44e44141c85723579d36ae1a92d0c8d393885b5a5556b2526d082ab843276b25",
}
LOAD_CPU_HASH = {
    77: {"load_level_game": "cbd6dc82a16f60d2758e338893383ac73a89eec02fe487d46b0fd9ced335bd97",
         "level_load": "57dc04a00e938e23f1e48115d7e111f4763a53f92fbf4fd1f9a789d49f71b93c"},
    80: {"load_level_game": "1810c31061fd27f8dcd4f8981ab445a9b770ad0b8747ff69dd11a9ba69c525f8",
         "level_load": "93236d6aa9579ad4b8c856aca32b70b5be7ec23c12c4c8462495eeeb236890de"},
}

# Full menu ownership is a separate opt-in payload. Pins cover the complete
# reviewed retail bodies (both revisions), never a best-effort call rewrite.
# The four mode_menu cuts have declaration-only host temporaries; all live
# values/return addresses are in registers and its 0x30-byte guest stack.
MENU_CPU_HASH = {
    77: {
        "mode_intro": "612b26825655d249c84911577e6be70457db4b51c5899f973f8eebe4087e730b",
        "mode_menu": "8e1853a0070393b0ccac5eedead3d667603aed1ce4b1a6430a8211c5f1e6715d",
        "load_level_menu": "93ddefe3987bd331e94f7f5e72fac3167b1e05c266978c3dcf75459d4b953934",
        "unload_level_menu": "5de25288e852d320339f42f0c4657aad533985c242fd631c98f8e34a50b4666d",
        "load_menu_with_level_background": "4baeff8b295c4d60ff93c7ce7d59398ebb5f4c4798b07e11e40fb0f62b2fb951",
        "load_next_ingame_level": "9089cd9c290d5bc226081d66c32fd4da67876941eebb417c0d4cde5c7626ef39",
        "bgload_tick": "4e2f5486871d6bd5e4b30f5df7017cb29ac0a5de95e173e95c465b1856c260af",
        "load_level_for_menu": "b045459873096567c26b11c441b3d5c5175ba5de49f567d8e20e0b02d9a0a3ee",
    },
    80: {
        "mode_intro": "0cace885025102286a9fb712803468803142f5686903d8b351e1662992e5584c",
        "mode_menu": "fe5303da26bacbbfcc677aab3ea478897318700c5013748d40adbba66011bbd6",
        "load_level_menu": "4a927321a86f549570c81f208df2eba3670683509850562c64a8b78c11e603a0",
        "unload_level_menu": "d5fb26e3ef7501daf6a0b99ed25f19e4715091e561000d97251ae05d443ebab1",
        "load_menu_with_level_background": "30bc0a2370422c23046dc6cebaff1b348ffa13b37b486c23fd7cc10c0e53f1ea",
        "load_next_ingame_level": "910752a94b97b748d4fa5149e1605c27b07f40a19748b7a06e687cfc2e66b3f5",
        "bgload_tick": "766014fd850e5be273f7f45d0d7ff32ccada5225d10402d095360c6ce4d7214b",
        "load_level_for_menu": "f1d68b08f4c2fc1e84bc45dc6ccfdf4976fe28ff4b1d5e134a3401a05d588b8a",
    },
}


def menu_scene_cuts(body, revision):
    if hashlib.sha256(body.encode()).hexdigest() != MENU_CPU_HASH.get(revision, {}).get("mode_menu"):
        raise ValueError("Unreviewed mode_menu continuation")
    for name in ("hi", "lo", "result", "c1cs"):
        if len(re.findall(r"\b"+name+r"\b", strip(body))) != 1:
            raise ValueError("Live menu host temporary cannot cross a scene cut")
    calls=list(re.finditer(r"    unload_level_(?:menu|game)\(rdram, ctx\);",body))
    if len(calls)!=4 or [x[0] for x in calls] != [
            "    unload_level_menu(rdram, ctx);", "    unload_level_game(rdram, ctx);",
            "    unload_level_menu(rdram, ctx);", "    unload_level_menu(rdram, ctx);"]:
        raise ValueError("Menu unload sites changed")
    # Before the prologue, resume at the exact call site: never poll input or
    # execute menu_loop twice for the same logical frame.
    entry="\n    switch (dkr_probe_scene_entry()) {\n        case 0: break;\n"
    entry+="".join(f"        case {site}: goto dkr_probe_menu_unload_{site};\n" for site in range(3,7))
    entry+='        default: dkr_probe_block("invalid-menu-scene-continuation");\n    }\n'
    for site,match in reversed(list(enumerate(calls,3))):
        body=body[:match.start()]+f"dkr_probe_menu_unload_{site}:\n    if (dkr_probe_scene_cut({site})) return;\n"+body[match.start():]
    return body.replace("    int c1cs = 0;", "    int c1cs = 0;"+entry,1)


def owned_menu_cpu(functions, revision):
    """Confirmed menu constructors, not live bgload/scheduler permission."""
    for name,expected in MENU_CPU_HASH.get(revision,{}).items():
        if name not in functions or hashlib.sha256(functions[name].encode()).hexdigest()!=expected:
            raise ValueError(f"Unreviewed owned menu dependency: {name}")
    if revision not in MENU_CPU_HASH:
        raise ValueError("Unsupported owned menu revision")
    replacements={}
    # INTRO is also a confirmed resource-changing tick. Its first constructor
    # must consume the existing cold-world capability through the private
    # menu loader, not call the live scene-reset/interpolation hooks. Preserve
    # every retail input, timer, argument, delay slot and guest stack write.
    intro=functions["mode_intro"]
    if intro.count("    load_menu_with_level_background(rdram, ctx);")!=1:
        raise ValueError("INTRO menu constructor site changed")
    replacements["mode_intro"]=intro.replace(
        "    load_menu_with_level_background(rdram, ctx);",
        "    dkr_probe_menu_background_cpu(rdram, ctx);",1)
    unload=functions["unload_level_menu"].replace("void unload_level_menu(","void dkr_probe_menu_unload_cpu(",1)
    unload=unload.replace("    int c1cs = 0;", "    int c1cs = 0;\n    dkr_probe_scene_unload_begin();\n    dkr_probe_scene_unload_render_drained();",1)
    if unload.count("    return;")!=1: raise ValueError("Menu teardown epilogue changed")
    replacements["dkr_probe_menu_unload_cpu"]=unload.replace("    return;","    dkr_probe_scene_unload_end();\n    return;",1)
    load=functions["load_level_menu"].replace("void load_level_menu(","void dkr_probe_menu_load_cpu(",1)
    if load.count("    level_load(rdram, ctx);")!=1 or load.count("    return;")!=1:
        raise ValueError("Menu constructor sites changed")
    load=load.replace("    int c1cs = 0;","    int c1cs = 0;\n    dkr_probe_menu_load_begin(rdram, ctx);",1)
    load=load.replace("    level_load(rdram, ctx);","    dkr_probe_level_load_cpu(rdram, ctx);",1)
    replacements["dkr_probe_menu_load_cpu"]=load.replace("    return;","    dkr_probe_scene_load_ready(rdram, ctx);\n    return;",1)
    wrapper=functions["load_menu_with_level_background"].replace("void load_menu_with_level_background(","void dkr_probe_menu_background_cpu(",1)
    if wrapper.count("    load_level_menu(rdram, ctx);")!=1 or wrapper.count("    return;")!=1:
        raise ValueError("Menu background constructor sites changed")
    wrapper=wrapper.replace("    int c1cs = 0;","    int c1cs = 0;\n    dkr_probe_menu_construct_begin();",1)
    wrapper=wrapper.replace("    load_level_menu(rdram, ctx);","    dkr_probe_menu_load_cpu(rdram, ctx);",1)
    replacements["dkr_probe_menu_background_cpu"]=wrapper.replace("    return;","    dkr_probe_menu_construct_ready(rdram, ctx);\n    return;",1)
    next_level=functions["load_next_ingame_level"].replace("void load_next_ingame_level(","void dkr_probe_next_ingame_cpu(",1)
    if next_level.count("    load_level_game(rdram, ctx);")!=1:
        raise ValueError("Next-level constructor changed")
    replacements["dkr_probe_next_ingame_cpu"]=next_level.replace("    load_level_game(rdram, ctx);","    dkr_probe_scene_load_cpu(rdram, ctx);",1)
    menu_level=functions["load_level_for_menu"]
    if menu_level.count("    unload_level_menu(rdram, ctx);")!=1 or menu_level.count("    load_level_menu(rdram, ctx);")!=1 or menu_level.count("    return;")!=1:
        raise ValueError("Menu level-change ownership sites changed")
    menu_level=menu_level.replace("    int c1cs = 0;","    int c1cs = 0;\n    dkr_probe_menu_level_change_begin(rdram, ctx);",1)
    menu_level=menu_level.replace("    unload_level_menu(rdram, ctx);","    dkr_probe_menu_unload_cpu(rdram, ctx);",1)
    menu_level=menu_level.replace("    load_level_menu(rdram, ctx);","    dkr_probe_menu_load_cpu(rdram, ctx);",1)
    replacements["load_level_for_menu"]=menu_level.replace("    return;","    dkr_probe_menu_level_change_ready(rdram, ctx);\n    return;",1)
    preview=functions["bgload_tick"]
    if preview.count("    osSendMesg_recomp(rdram, ctx);")!=1:
        raise ValueError("Background preview request site changed")
    # Preserve the retail four-tick delay. This is an owned request, NOT a
    # fabricated OS queue completion. The loader runs at the agreed boundary.
    replacements["bgload_tick"]=preview.replace("    osSendMesg_recomp(rdram, ctx);","    dkr_probe_menu_background_request(rdram, ctx);",1)
    return replacements


def owned_load_cpu(body, level, revision):
    """Private confirmed constructor, preserving the entire retail CPU load.

    The original entry points remain fenced. Neither the stable lobby's
    load hooks nor a live interpolation registry is entered. The replacement
    owner must implement the reviewed no-mod/Accurate state explicitly.
    """
    pins=LOAD_CPU_HASH.get(revision,{})
    if any(hashlib.sha256(source.encode()).hexdigest()!=pins.get(name)
           for name,source in (("load_level_game",body),("level_load",level))):
        raise ValueError("Unreviewed confirmed scene constructor")
    begin='extern void dkr_netplay_gameplay_level_begin(uint8_t*, recomp_context*); dkr_netplay_gameplay_level_begin(rdram, ctx);'
    ready='extern void dkr_netplay_gameplay_level_ready(uint8_t*, recomp_context*); dkr_netplay_gameplay_level_ready(rdram, ctx);'
    reset='extern void dkr_runtime_scene_reset(uint8_t*, recomp_context*); extern void dkr_presentation_scene_begin(uint8_t*, recomp_context*); dkr_runtime_scene_reset(rdram, ctx); dkr_presentation_scene_begin(rdram, ctx); extern void dkr_legacy_scene_begin(uint8_t*, recomp_context*); dkr_legacy_scene_begin(rdram, ctx);'
    if body.count(begin)!=1 or body.count(ready)!=1 or body.count('    level_load(rdram, ctx);')!=1 or level.count(reset)!=1:
        raise ValueError("Confirmed scene constructor ownership sites changed")
    game=body.replace('void load_level_game(','void dkr_probe_scene_load_cpu(',1).replace(
        begin,'dkr_probe_scene_load_begin(rdram, ctx);',1).replace(
        ready,'dkr_probe_scene_load_ready(rdram, ctx);',1).replace(
        '    level_load(rdram, ctx);','    dkr_probe_level_load_cpu(rdram, ctx);',1)
    child=level.replace('void level_load(','void dkr_probe_level_load_cpu(',1).replace(
        reset,'dkr_probe_scene_load_reset(rdram, ctx);',1)
    return game,child


def owned_unload_cpu(body, revision):
    """Exact retail free sequence in a private, confirmed, drained CPU world.

    The original unload/scheduler remain untouched and fenced. A separate
    ownership gate is checked BEFORE the first guest stack or allocator write.
    This does not synthesize a graphics message or skip a live worker wait.
    """
    if hashlib.sha256(body.encode()).hexdigest()!=UNLOAD_CPU_HASH.get(revision):
        raise ValueError("Unreviewed confirmed scene teardown")
    entry='extern void dkr_netplay_gameplay_level_end(uint8_t*, recomp_context*); dkr_netplay_gameplay_level_end(rdram, ctx);'
    wait='    gfxtask_wait(rdram, ctx);'
    if body.count(entry)!=1 or body.count(wait)!=1 or body.count('    return;')!=1:
        raise ValueError("Confirmed teardown ownership sites changed")
    return body.replace('void unload_level_game(','void dkr_probe_scene_unload_cpu(',1).replace(
        entry,'dkr_probe_scene_unload_begin();',1).replace(
        wait,'    dkr_probe_scene_unload_render_drained();',1).replace(
        '    return;','    dkr_probe_scene_unload_end();\n    return;',1)


def authored_video_cpu(body, swap, revision):
    """Keep retail blackout/role writes, not VI queue waits or fake events."""
    pins=VIDEO_CPU_HASH.get(revision,{})
    if any(hashlib.sha256(source.encode()).hexdigest()!=pins.get(name)
           for name,source in (("fb_update",body),("fb_swap",swap))):
        raise ValueError("Unreviewed framebuffer CPU state phase")
    offset=0 if revision==77 else 0x450
    split=f"L_{0x8007A9E0+offset:08X}:"
    epilogue=f"    // 0x{0x8007AB08+offset:08X}: lw"
    if body.count(split)!=1 or body.count(epilogue)!=1:
        raise ValueError("Framebuffer CPU split/epilogue changed")
    prefix=body[:body.index(split)].replace("void fb_update(","void dkr_probe_authored_video_cpu(",1)
    # Own the fixed-cadence return slot; s0/s1/s2, RA and SP retain the exact
    # retail epilogue. No VI receive is invoked or claimed to have completed.
    return prefix+split+"\n    MEM_W(0X28, ctx->r29) = 2;\n"+body[body.index(epilogue):]


def authored_declarations(full_scenes=False):
    # funcs.h closes its original extern-C block. New guest entries need their
    # own linkage block because both C and C++ private owners include it.
    menu = ''.join(f'void {name}(uint8_t*, recomp_context*);\n' for name in (
        "dkr_probe_scene_resume_menu_cpu", "dkr_probe_menu_unload_cpu", "dkr_probe_menu_load_cpu",
        "dkr_probe_menu_background_cpu", "dkr_probe_next_ingame_cpu")) if full_scenes else ''
    return '\n#ifdef __cplusplus\nextern "C" {\n#endif\n' + \
        'void dkr_probe_authored_main_cpu(uint8_t*, recomp_context*);\n' + \
        'void dkr_probe_authored_video_cpu(uint8_t*, recomp_context*);\n' + \
        'void dkr_probe_scene_unload_cpu(uint8_t*, recomp_context*);\n' + \
        'void dkr_probe_scene_load_cpu(uint8_t*, recomp_context*);\n' + \
        'void dkr_probe_level_load_cpu(uint8_t*, recomp_context*);\n' + \
        'void dkr_probe_scene_resume_main_cpu(uint8_t*, recomp_context*);\n' + \
        'void dkr_probe_scene_resume_mode_cpu(uint8_t*, recomp_context*);\n' + \
        menu + '#ifdef __cplusplus\n}\n#endif\n'


def authored_main_cpu(body, revision, full_scenes=False):
    if hashlib.sha256(body.encode()).hexdigest() != MAIN_CPU_HASH.get(revision):
        raise ValueError("Unreviewed authored main-loop phase")
    for name in ("hi", "lo", "result", "c1cs"):
        if len(re.findall(r"\b"+name+r"\b", strip(body))) != 1:
            raise ValueError("Live main-loop host temporary crosses the CPU boundary")
    offset = 0 if revision == 77 else 0x240
    start = f"    // 0x{0x8006C724+offset:08X}: lw"
    cleanup = f"L_{0x8006CA3C+offset:08X}:"
    copy_tail = f"L_{0x8006CA64+offset:08X}:"
    timer_reset = f"L_{0x8006CA34+offset:08X}:"
    video_wait = f"L_{0x8006CAA8+offset:08X}:"
    epilogue = f"L_{0x8006CAD4+offset:08X}:"
    for marker in (start, "    after_30:", cleanup, copy_tail, timer_reset, video_wait, epilogue):
        if body.count(marker) != 1:
            raise ValueError("Authored main-loop instruction boundary changed")
    cpu = body[body.index(start):body.index("    after_30:")] + "    after_30:\n"
    # The separate driver confirms after replay. Never invoke the stable
    # authority commitment from inside a speculative CPU tick.
    stable = "extern void dkr_netplay_authoritative_frame_commit(uint8_t*, recomp_context*); "
    call = "dkr_netplay_authoritative_frame_commit(rdram, ctx); "
    if cpu.count(stable) != 1 or cpu.count(call) != 1 or cpu.count("    mode_game(rdram, ctx);") != 1:
        raise ValueError("Authored main-loop owner/continuation sites changed")
    cpu = cpu.replace(stable, "", 1).replace(call, "", 1)
    cpu = cpu.replace("    mode_game(rdram, ctx);",
        "    mode_game(rdram, ctx);\n    if (dkr_probe_scene_pending()) return;", 1)
    if full_scenes:
        if cpu.count("    mode_menu(rdram, ctx);")!=1 or cpu.count("    after_18:")!=1:
            raise ValueError("Authored main menu continuation site changed")
        cpu=cpu.replace("    mode_menu(rdram, ctx);",
            "    mode_menu(rdram, ctx);\n    if (dkr_probe_scene_pending()) return;",1)
    # Retain the exact timer reset, allocator/camera tail and framebuffer CPU
    # copy. Only the scheduler wait and VI-derived cadence remain outside this
    # fixed-30-Hz phase. A confirmed loader sets timer=2; dropping its copy or
    # leaving timer=1 forever would corrupt the very next authored frame.
    timer_address=0x800DD3F0 if revision==77 else 0x800DD960
    tail = f"    if (MEM_B(0, (gpr)(int32_t)0x{timer_address:08X}U) != 1) goto {cleanup[:-1]};\n"
    tail += body[body.index(timer_reset):body.index(video_wait)] + video_wait + "\n"
    end = body[body.index(epilogue):]
    pending_menu='''        if (dkr_probe_scene_pending() >= 3) {
            mode_menu(rdram, ctx);
            if (dkr_probe_scene_pending()) return;
            goto after_18;
        }
''' if full_scenes else ''
    return '''RECOMP_FUNC void dkr_probe_authored_main_cpu(uint8_t* rdram, recomp_context* ctx) {
    uint64_t hi = 0, lo = 0, result = 0;
    int c1cs = 0;
    if (dkr_probe_scene_pending()) {
''' + pending_menu + '''        mode_game(rdram, ctx);
        if (dkr_probe_scene_pending()) return;
        goto after_19;
    }
    // Private fixed-cadence owner has selected its unleased output buffer.
    // Preserve the exact guest frame and return-address layout for scene cuts.
    ctx->r29 = ADD32(ctx->r29, -0X18);
    MEM_W(0X14, ctx->r29) = ctx->r31;
''' + f"    ctx->r7 = (gpr)(int32_t)0x{0x801234E8+(0 if revision==77 else 0x580):08X}U;\n" + cpu + tail + end

# The retail audio filter/player constructors assign exactly these callbacks.
# This is an immutable private dispatch table, never a live get_function or a
# permissive native fallback. Unknown/custom code pointers remain fenced.
AUDIO_CALLBACKS = (
    "sndp_voice_handler", "__CSPVoiceHandler", "__amDMA",
    "alSavePull", "alSaveParam", "alMainBusPull", "alMainBusParam",
    "alAuxBusPull", "alAuxBusParam", "alFxPull", "alFxParam", "alFxParamHdl",
    "alEnvmixerPull", "alEnvmixerParam", "alResamplePull", "alResampleParam",
    "alAdpcmPull", "alRaw16Pull", "alLoadParam",
)
AUDIO_SYMBOL_HASH = {
    77: "fb8b4f0e790c35b1c93e8e3221670bd253432ac6a60ff33f575a259366341082",
    80: "1b8f55e25798b86951359aa4faa88968da27df65f6ae5c09b36702883b57c9ba",
}
# bgdraw_set_func has one non-null retail registration: Track Select's
# background. Pin both the registration/dispatcher and its target. This is a
# separate confirmed-menu permission, never a blanket indirect-call escape.
MENU_CALLBACKS = ("func_8008F618",)
MENU_INDIRECT_HASH = {
    77: {
        "func_8008F618": "4ad1e08242029f24a826f6d1cf3b1db7c95cc38b6bb51067add491312079cd38",
        "bgdraw_render": "f77a294f38a46d586f9d4f9e78a20be559aba6f3f684c8850449323c620f6cd3",
        "bgdraw_set_func": "967c3547f79808adbf71fc38ff41460df768f818d36c020128fe1a6a599f4bce",
        "menu_track_select_init": "c7105c42d8ba3d4eeb657037a44ff54adb2326ec9e11f3b2ca1940483a70c13e",
    },
    80: {
        "func_8008F618": "cb8fdf63bf07af167599b38b9b98ac18c50050568ad1ee6fba0b13c87e8c4cdb",
        "bgdraw_render": "e7c1ee0efd3f00e8e8fce620c1aabc7e98d406ee78ac96d3c9d4caf577febe6a",
        "bgdraw_set_func": "c72c4565c29086412f719384ac7a6ceb3c37c582ca4d13f763c1be831b35660c",
        "menu_track_select_init": "e2715d42b12fc45c0af0c2e2619fab51f16c8925300163f886a839ea01893a58",
    },
}
RSP_INPUT_HASH = {
    "runtime-recomp/RecompiledRSP/aspMain.cpp": "ba5cef9b93a5d0dfae1ec06b8a0ab291460e221ecb02c01bbaf1e8d47efcaab6",
    "extern/n64-modern-runtime/librecomp/include/librecomp/rsp.hpp": "2d3a939023401557ccaf1e9c8a2f810a7519591df154609123089980b9f03633",
    "extern/n64-modern-runtime/librecomp/include/librecomp/rsp_vu.hpp": "18534c9c9fc66f0d2185d87de7cf1dd271ac1f5d4cb8791654c1eab7d8fc4e98",
    "extern/n64-modern-runtime/librecomp/include/librecomp/rsp_vu_impl.hpp": "0e1afe4b3782dd177eb41c10cd34947cc060497a80750418b4b889d0b867ad0c",
}
INPUT_NATIVE_HASH = {
    "extern/n64-modern-runtime/ultramodern/src/input.cpp": "57e14a451ddc7d571baf8d886c650a5efcbfab94749720bad36f1b6393693524",
    "extern/n64-modern-runtime/librecomp/src/cont.cpp": "84441a9fb63dbc992a771d3cf15ec452aee4db68cceff8c29235cc2a256e3352",
    "runtime-recomp/src/game/virtual_pak.cpp": "42287fda7a76eb3864417a4c2863e761c1464cc3126e272bf6a83edaaf0194ee",
    # Oct 1 reviewed opt-in capture hook; stable drive/input branches unchanged.
    "runtime-recomp/src/game/runtime_netplay.cpp": "aaeee3894a0087e9512cf426a65f044cfd76f7aee82dac713b2969f12126160c",
    "runtime-recomp/src/game/runtime_magic_codes.cpp": "e7d661472bb041991b3e8cfc6373c1d647b0fa300281d21448ed74efdb01021f",
    # Oct 4 PR46 merge: added minimap table pairs and music load observer.
    # Owned vanilla bootstrap still bypasses mod assets and clears an old
    # offline music binding through the observer's no-level native branch.
    "runtime-recomp/src/game/custom_tracks_hooks.cpp": "a1c21cf25e28f8a2d1eedabadc85c77b3f9eb4ef0528fa7687b6cdc4a2dcb6f0",
    "runtime-recomp/src/game/runtime_legacy_mods.cpp": "eff1460b853514ac2b200735c6085b1089585019b8ac1f0d11130c50b999e0ed",
}

# Additional native participants reached by full retail menus. The tail latch
# is checkpointed; water diagnostics retain their exact disabled branch.
FULL_SCENE_NATIVE_HASH = {
    "runtime-recomp/src/game/finish_presentation_policy.hpp": "0368b5447ae477d1debd7d32829a989a690a9e809fd95223a391a8574a9aed6a",
    "runtime-recomp/src/game/widescreen_policy.hpp": "b926703163fa542f8fbd7af1aee297595c1a58a74c61a82d457c8fbae4ccbb62",
    "runtime-recomp/src/game/runtime_stubs.cpp": "2552788db2decd11c30ae8d6609c42d51d3072578118e4a1d1f3bbd7cec34f31",
    "runtime-recomp/src/game/intro_tail_policy.hpp": "5f5362ff5de40f5f9aa3a653c6e3849b8f27997b9db4592abd833022b9d47a36",
    "runtime-recomp/src/game/water_profile.cpp": "d04da9797b34f8001e9304ccbd76171ead1eef8acc4fe2a1c215406b71064efb",
    # Both revisions keep the water UV masks and append PR46's music globals.
    "runtime-recomp/src/game/revision_addresses.hpp": "d8dd9d0f964082f69c86b8a9486ca12ecd47272702d141b03ab9de9cb898d43e",
    # sequence_loaded/started are identities only under the existing no-mods
    # owner contract: no bound sequence or saved carrier row. Not admission
    # of custom music into the checkpointed simulation.
    "runtime-recomp/src/game/custom_music.cpp": "3954cb3188645663ff15c51eec02ead2805b25bfc39837c71693303a41aaa137",
    "runtime-recomp/src/game/online_roster_policy.hpp": "3c914fdcad650d62839eed84f76ea22d5743a2fbf1d30512b88d2317e38892f4",
    "runtime-recomp/src/game/runtime_enhancements.cpp": "7a868375b1a8e4e0f3e5e1c60036795fa2630e7d4b3cd6e954308495743918de",
    "runtime-recomp/src/game/character_select_animation_policy.hpp": "ab8a0f4220c00ce1b5726ff75953f0c4944b5b4e24eba942b442e07268ad04da",
    "runtime-recomp/src/game/character_select_music_policy.hpp": "e2e308c4821e2b11cba32ecafd7195cdf7ed9cc4c4166c8b4078ec513441da6c",
}


def full_scene_native_audit():
    root = Path(__file__).resolve().parents[1]
    for name, expected in FULL_SCENE_NATIVE_HASH.items():
        if hashlib.sha256((root/name).read_bytes()).hexdigest() != expected:
            raise ValueError(f"Unreviewed full-scene native participant: {name}")


def input_native_audit():
    """Guard the reviewed private ownership boundary against native drift."""
    root = Path(__file__).resolve().parents[1]
    for name, expected in INPUT_NATIVE_HASH.items():
        if hashlib.sha256((root/name).read_bytes()).hexdigest() != expected:
            raise ValueError(f"Unreviewed native input/Pak/rumble boundary: {name}")


def audio_rsp_payload(rsp_source=None):
    """Read only; return checked private copies, never modify the RSP/runtime."""
    root = Path(__file__).resolve().parents[1]
    sources = {}
    for name, expected in RSP_INPUT_HASH.items():
        source_path = (rsp_source / Path(name).name) if rsp_source is not None and name.startswith("runtime-recomp/RecompiledRSP/") else root/name
        raw = source_path.read_bytes()
        if hashlib.sha256(raw).hexdigest() != expected:
            raise ValueError(f"Unreviewed private audio RSP input: {name}")
        sources[Path(name).name] = raw.decode()
    header = sources["rsp.hpp"]
    header = header.replace('#include "ultramodern/ultra64.h"',
        '#include <cassert>\nstruct OSTask; // Opaque: private probe never calls the native scheduler.', 1)
    # Replace ONLY the hash-reviewed DMA implementations in our private copy.
    # The retail 24-bit/8-byte hardware address normalization is unchanged.
    # Validate full RAM/DMEM spans once, before writing, rather than calling
    # the scalar guest-memory trap/budget checker for every transferred byte.
    for direction, length, write in (("rdram_to_dmem", "rd_len", 0),
                                      ("dmem_to_rdram", "wr_len", 1)):
        signature = f"static inline void dma_{direction}(uint8_t* rdram, uint32_t dmem_addr, uint32_t dram_addr, uint32_t {length})"
        pattern = re.escape(signature) + r" \{\n.*?^\}"
        replacement = signature + " {\n" + (
            f"    dkr_probe_audio_dma(rdram, dmem, dmem_addr, dram_addr & 0xFFFFF8U, {length}, {write}, __FILE__, __LINE__);\n}}")
        header, count = re.subn(pattern, lambda _: replacement, header, flags=re.S | re.M)
        if count != 1:
            raise ValueError("Unreviewed private RSP DMA implementation shape")
    sources["rsp.hpp"] = header
    source = sources.pop("aspMain.cpp")
    source = '#include <type_traits>\n'+source
    source = source.replace("    RSP rsp{};", "    static_assert(std::is_trivially_destructible_v<RSP>);\n    RSP rsp{};", 1)
    # The RSP function has only primitive/trivially destructible locals; the
    # private C trap may cross it. Every loop/dispatcher label is budgeted.
    source, count = re.subn(r"^(L_[A-Fa-f0-9]+|do_indirect_jump):\s*$", r"\1: dkr_probe_checkpoint();", source, flags=re.M)
    if count < 30:
        raise ValueError("Unreviewed RSP control-flow budget shape")
    return sources, source


def audio_dispatch(functions, revision):
    path = Path(__file__).resolve().parents[1] / f"extern/dkr-decomp/ver/symbols/symbol_addrs.us.v{revision}.txt"
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != AUDIO_SYMBOL_HASH.get(revision):
        raise ValueError("Unreviewed retail audio address map")
    symbols = dict(re.findall(r"^(\w+)\s*=\s*(0x[0-9A-Fa-f]+);", data.decode(), re.M))
    dispatch = {}
    for name in AUDIO_CALLBACKS:
        if name not in functions or name not in symbols:
            raise ValueError(f"Missing reviewed retail audio callback {name}")
        entry = re.search(r"// (0x[0-9A-Fa-f]+):", functions[name])
        address = int(symbols[name], 16)
        if not entry or int(entry[1], 16) != address or address in dispatch:
            raise ValueError(f"Audio callback address mismatch: {name}")
        dispatch[address] = name
    return dispatch


def menu_dispatch(functions, revision):
    pins = MENU_INDIRECT_HASH.get(revision)
    if not pins or any(name not in functions or hashlib.sha256(functions[name].encode()).hexdigest()!=expected
                       for name,expected in pins.items()):
        raise ValueError("Unreviewed retail menu indirect-call boundary")
    path = Path(__file__).resolve().parents[1] / f"extern/dkr-decomp/ver/symbols/symbol_addrs.us.v{revision}.txt"
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest()!=AUDIO_SYMBOL_HASH[revision]:
        raise ValueError("Unreviewed retail menu address map")
    symbols=dict(re.findall(r"^(\w+)\s*=\s*(0x[0-9A-Fa-f]+);",data.decode(),re.M))
    result={}
    for name in MENU_CALLBACKS:
        entry=re.search(r"// (0x[0-9A-Fa-f]+):",functions[name])
        address=int(symbols[name],16)
        if not entry or int(entry[1],16)!=address or address in result:
            raise ValueError("Menu callback address mismatch")
        result[address]=name
    return result


def indirect_stub(audio, menu):
    if set(audio)&set(menu):raise ValueError("Overlapping indirect owner permissions")
    cases=[]
    for dispatch,permission,label in ((audio,"dkr_probe_audio_enabled","audio"),
                                      (menu,"dkr_probe_menu_callback_enabled","menu")):
        for address,target in sorted(dispatch.items()):
            cases.append(f'case 0x{address:08X}U: if (!{permission}()) dkr_probe_block("unowned-{label}-indirect-dispatch"); return {target};')
    return 'recomp_func_t* get_function(int32_t vram) {\n' + \
        'dkr_probe_checkpoint();\nswitch ((uint32_t)vram) {\n' + '\n'.join(cases) + \
        '\ndefault: dkr_probe_block("unreviewed-indirect-callback");\n}\n}'


def mode_scene_cuts(body, revision):
    if revision not in MODE_SCENE_HASH or hashlib.sha256(body.encode()).hexdigest() != MODE_SCENE_HASH[revision]:
        raise ValueError("Unreviewed mode_game continuation: audit guest/host liveness before splitting")
    for name in ("hi", "lo", "result", "c1cs"):
        if len(re.findall(r"\b"+name+r"\b", strip(body))) != 1:
            raise ValueError("Live host temporary cannot be checkpointed across a scene cut")
    marker="    int c1cs = 0;"
    if body.count(marker)!=1 or body.count("    unload_level_game(rdram, ctx);")!=2:
        raise ValueError("Scene-cut prologue/call sites changed")
    entry='''
    switch (dkr_probe_scene_entry()) {
        case 0: break;
        case 1: goto dkr_probe_unload_1;
        case 2: goto dkr_probe_unload_2;
        default: dkr_probe_block("invalid-mode-scene-continuation");
    }
'''
    body=body.replace(marker,marker+entry,1)
    needle="    unload_level_game(rdram, ctx);"
    parts=body.split(needle)
    return parts[0]+'''dkr_probe_unload_1:
    if (dkr_probe_scene_cut(1)) return;
'''+needle+parts[1]+'''dkr_probe_unload_2:
    if (dkr_probe_scene_cut(2)) return;
'''+needle+parts[2]


def owned_scene_resume(main,mode,revision,menu=None):
    # Reuse the EXACT previously audited guest/host continuation boundaries.
    # No guessed stack unwind or second old-scene object/render update.
    cpu=authored_main_cpu(main,revision,menu is not None).replace('void dkr_probe_authored_main_cpu(',
        'void dkr_probe_scene_resume_main_cpu(',1).replace('mode_game(rdram, ctx);',
        'dkr_probe_scene_resume_mode_cpu(rdram, ctx);')
    cpu=cpu.replace('    int c1cs = 0;','    int c1cs = 0;\n    if (!dkr_probe_scene_pending()) dkr_probe_block("no-confirmed-scene-continuation");',1)
    child=mode_scene_cuts(mode,revision).replace('void mode_game(','void dkr_probe_scene_resume_mode_cpu(',1)
    child=child.replace('dkr_probe_scene_cut(', 'dkr_probe_scene_transaction_cut(')
    child=child.replace('unload_level_game(rdram, ctx);','dkr_probe_scene_unload_cpu(rdram, ctx);')
    child=child.replace('load_level_game(rdram, ctx);','dkr_probe_scene_load_cpu(rdram, ctx);')
    if menu is not None:
        cpu=cpu.replace('mode_menu(rdram, ctx);','dkr_probe_scene_resume_menu_cpu(rdram, ctx);')
        child=child.replace('load_menu_with_level_background(rdram, ctx);','dkr_probe_menu_background_cpu(rdram, ctx);')
    if child.count('    return;')!=1:
        raise ValueError("Confirmed mode continuation epilogue changed")
    child=child.replace('    return;','    dkr_probe_scene_transaction_end();\n    return;',1)
    if menu is None: return cpu,child
    menu_child=menu_scene_cuts(menu,revision).replace('void mode_menu(', 'void dkr_probe_scene_resume_menu_cpu(',1)
    for original,owned in (
        ('dkr_probe_scene_cut','dkr_probe_scene_transaction_cut'),
        ('unload_level_game','dkr_probe_scene_unload_cpu'),
        ('unload_level_menu','dkr_probe_menu_unload_cpu'),
        ('load_level_game','dkr_probe_scene_load_cpu'),
        ('load_menu_with_level_background','dkr_probe_menu_background_cpu'),
        ('load_next_ingame_level','dkr_probe_next_ingame_cpu')):
        menu_child=menu_child.replace(original+'(',owned+'(')
    if menu_child.count('    return;')!=1:
        raise ValueError("Confirmed menu continuation epilogue changed")
    menu_child=menu_child.replace('    return;','    dkr_probe_scene_transaction_end();\n    return;',1)
    return cpu,child,menu_child


def strip(source):
    return re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"', "", source, flags=re.S)


def declarations(source):
    result = {}
    for ret, name, params in PROTO.findall(strip(source)):
        if name in ("if", "return", "switch", "sizeof"):
            continue
        types = []
        for param in params.split(",") if params.strip() not in ("", "void") else []:
            param = re.sub(r"(\*)\s*([A-Za-z_]\w*)$", r"\1 \2", param.strip())
            # All supported imports use simple C scalars/pointers. Refuse a
            # signature we cannot preserve instead of guessing an ABI.
            if not re.fullmatch(r"(?:const\s+)?(?:unsigned\s+)?[A-Za-z_]\w*(?:\s*\*)*(?:\s+\w+)?", param):
                raise ValueError(f"Unsupported native signature {name}: {param}")
            known = {"void", "int", "float", "double", "gpr", "uint8_t", "int8_t",
                     "uint16_t", "int16_t", "uint32_t", "int32_t", "uint64_t", "int64_t",
                     "uintptr_t", "recomp_context", "char"}
            words = param.split()
            if len(words) > 1 and words[-1] not in known and "*" not in words[-1]:
                param = param[:param.rfind(words[-1])].strip()
            # Handle uint8_t* rdram and uint8_t *rdram as well.
            param = re.sub(r"(\*)\s*[A-Za-z_]\w*$", r"\1", param)
            param = re.sub(r"\s*\*\s*", "*", param)
            types.append(param)
        signature = (ret.strip(), types)
        if name in result and result[name] != signature:
            raise ValueError(f"Conflicting prototype for {name}: {result[name]} / {signature}")
        result[name] = signature
    return result


BOSS_FINISH_SOURCE_HASH = {
    77: "4dbdfbcd16a4109164aa5464217c5e15bf73a86bcb48dac0e34006bd891db64c",
    80: "47105c45c8b525b75f31e901dbac27e9b43f35940cbc5ca28fab786343926662",
}

LOCAL_WORLD_SOURCE_HASH = {
    77: "f477c163ca9e835a5259b17195737c53c1958d890484fcd833dd92e74a81f9a0",
    80: "d0f72ecf1722b9afddf7bd5cd8f6bf7d3df693372833ffc71087a08f5d98fed7",
}


def local_world_observation_body(body, revision):
    """Observe both world boundaries without changing guest instructions.

    Capture at the exact call site, not inside recursive sorting. This pins
    the caller's 0x170-byte frame and objectsVisible array at sp+0x58 in both
    revisions. Registers, delay slots and sort behavior
    remain byte-for-byte identical after removing the two observation calls.
    """
    if hashlib.sha256(body.encode()).hexdigest() != LOCAL_WORLD_SOURCE_HASH[revision]:
        raise ValueError("Unreviewed world-render scenery observation source")
    calls = ("    sort_objects_by_dist(rdram, ctx);\n",
             "    func_80012C3C(rdram, ctx);\n")
    observations = tuple(f"    dkr_probe_local_world_draw(rdram, ctx, {phase});\n" for phase in range(2))
    patched = body
    for call, observation in zip(calls, observations):
        if body.count(call) != 1:
            raise ValueError("Unexpected scenery world-boundary call")
        patched = patched.replace(call, observation + call, 1)
    original = patched
    for observation in observations:
        original = original.replace(observation, "", 1)
    if original != body:
        raise ValueError("Scenery observation changed original guest instructions")
    return patched


def boss_finish_diagnostic_body(body, revision):
    """Observation-only adapter in NEW owned output, never a retail rewrite.

    Fail closed on source drift. Removing the added calls must recover every
    original byte, including delay slots, labels, registers and return logic.
    No diagnostic return value may influence the simulation.
    """
    if hashlib.sha256(body.encode()).hexdigest() != BOSS_FINISH_SOURCE_HASH[revision]:
        raise ValueError("Unreviewed racer_boss_finish diagnostic source")
    original = body
    opening = "RECOMP_FUNC void racer_boss_finish(uint8_t* rdram, recomp_context* ctx) {\n"
    if body.count(opening) != 1 or body.count("    return;\n") != 1:
        raise ValueError("Unexpected boss-finish entry/return shape")
    body = body.replace(opening, opening + '    dkr_probe_boss_trace("begin", rdram, ctx);\n', 1)
    expected_calls = {
        "get_settings": 1, "get_misc_asset": 2, "music_play": 2,
        "level_properties_push": 22, "set_eeprom_settings_value": 1,
        "level_transition_begin": 6, "instShowBearBar": 2,
        "is_in_two_player_adventure": 1, "swap_lead_player": 1,
        "get_save_file_index": 1, "safe_mark_write_save_file": 1,
    }
    for callee, expected in expected_calls.items():
        call = f"    {callee}(rdram, ctx);\n"
        if body.count(call) != expected:
            raise ValueError(f"Unexpected boss-finish call count: {callee}")
        body = body.replace(call,
            f'    dkr_probe_boss_trace("before-{callee}", rdram, ctx);\n' + call +
            f'    dkr_probe_boss_trace("after-{callee}", rdram, ctx);\n')
    body = body.replace("    return;\n", '    dkr_probe_boss_trace("end", rdram, ctx);\n    return;\n', 1)
    stripped = re.sub(r'^    dkr_probe_boss_trace\("[\w-]+", rdram, ctx\);\n', '', body, flags=re.M)
    if stripped != original:
        raise ValueError("Boss diagnostics changed original guest instructions")
    return body


def instrument_function(name, body):
    body = body.replace("{", '{\n    dkr_probe_enter("' + name + '");', 1)
    # Keep an unbraced `if (hook()) return;` a single statement. Injecting two
    # unbraced statements would accidentally make the return unconditional.
    body = re.sub(r"\breturn\s*;", "{ dkr_probe_leave(); return; }", body)
    # Some emitted functions have an implicit void return. Cover fallthrough
    # as well; after an explicit return this statement is simply unreachable.
    closing = body.rfind("}")
    if closing < 0: raise ValueError(f"Guest function has no closing brace: {name}")
    body = body[:closing] + "\n    dkr_probe_leave();\n" + body[closing:]
    body = re.sub(r"^(L_[A-Fa-f0-9]+:)\s*$", r"\1 dkr_probe_checkpoint();", body, flags=re.M)
    return body


def native_stub(name, signature, full_scenes=False):
    ret, types = signature
    params = ", ".join(f"{t} p{i}" for i, t in enumerate(types)) or "void"
    audited_assets = {
        "dkr_legacy_asset_api": ("int", ["uint8_t*", "recomp_context*", "unsigned"]),
        **{n: ("void", ["uint8_t*", "recomp_context*"]) for n in (
            "dkr_legacy_pi_start_dma", "osPiStartDma_recomp", "osRecvMesg_recomp",
            "osInvalDCache_recomp", "dkr_v11_asset_mutex_acquire", "dkr_v11_asset_mutex_release",
            "dkr_custom_tracks_asset_load_begin", "dkr_custom_tracks_asset_load_end",
            "dkr_custom_tracks_table_load_begin", "dkr_custom_tracks_table_load_end")},
        **{n: ("void", ["uint8_t*", "recomp_context*"]) for n in (
            "osEepromProbe_recomp", "osEepromRead_recomp", "osEepromWrite_recomp",
            "osEepromLongRead_recomp", "osEepromLongWrite_recomp")},
        **{n: ("void", ["uint8_t*", "recomp_context*"]) for n in (
            "__f_to_ll_recomp", "__ll_to_f_recomp", "__ull_rshift_recomp",
            "__ll_lshift_recomp", "__ull_div_recomp", "__ull_rem_recomp")},
        **{n: ("void", ["uint8_t*", "recomp_context*"]) for n in (
            "osGetCount_recomp", "osGetTime_recomp", "osSetTime_recomp")},
        "dkr_audio_event_queue_next": ("uint32_t", ["uint8_t*", "recomp_context*"]),
        **{n: ("int", ["uint8_t*", "recomp_context*"]) for n in (
            "dkr_audio_voice_guard", "dkr_audio_bus_guard")},
        **{n: ("void", ["uint8_t*", "recomp_context*"]) for n in (
            "osContGetReadData_recomp", "osContStartReadData_recomp",
            "osPfsIsPlug_recomp", "osMotorInit_recomp", "osMotorStart_recomp",
            "osMotorStop_recomp", "__osMotorAccess_recomp",
            "dkr_netplay_resolve_authored_input_frame")},
        **{n: ("void", ["uint8_t*", "recomp_context*"]) for n in (
            "osPfsInitPak_recomp", "osPfsInit_recomp", "osPfsFreeBlocks_recomp", "osPfsNumFiles_recomp",
            "osPfsFindFile_recomp", "osPfsAllocateFile_recomp", "osPfsDeleteFile_recomp", "osPfsFileState_recomp",
            "osPfsReadWriteFile_recomp", "osPfsChecker_recomp", "osPfsRepairId_recomp",
            "__osPfsSelectBank_recomp", "__osContRamWrite_recomp", "__osPfsGetStatus_recomp",
            "__osGetId_recomp", "__osContRamRead_recomp")},
        **{n: ("int", ["uint8_t*", "recomp_context*"]) for n in (
            "dkr_virtual_pak_preferred_status", "dkr_virtual_pak_reformat")},
        **{n: ("void", ["uint8_t*", "recomp_context*"]) for n in (
            "dkr_apply_launch_magic_codes", "dkr_magic_code_credits_started",
            "dkr_magic_code_balloon_awarded", "dkr_magic_codes_frame_complete",
            "dkr_fix_fullscreen_clear_scissor", "osViBlack_recomp")},
        **{n: ("void", ["uint8_t*", "recomp_context*"]) for n in (
            "dkr_title_intro_audio_tail", "dkr_netplay_character_select_enter",
            "dkr_netplay_character_select_lock", "dkr_netplay_character_select_ai_seed",
            "dkr_character_select_music_unblock", "dkr_character_select_music_mask",
            "dkr_character_menu_music_mask", "dkr_character_select_animation_tick",
            "dkr_character_select_animation_fraction", "dkr_track_select_fullscreen_preview",
            "dkr_track_select_background_cover", "dkr_track_select_lens_flare_tint_begin",
            "dkr_track_select_lens_flare_tint_end", "dkr_custom_tracks_track_id_override")},
    }
    if name in audited_assets and signature != audited_assets[name]:
        raise ValueError(f"Unreviewed owned-asset ABI: {name}")
    # water_profile.cpp: these diagnostic-only hooks return before reading RAM
    # or changing any state when DKR_WATER_PROFILE is disabled. The private
    # services explicitly keep it disabled; no generic scalar-import bypass.
    if name in ("dkr_water_profile_begin", "dkr_water_profile_end"):
        expected = ("void", ["uint32_t"]) if name.endswith("begin") else ("void", ["uint8_t*", "uint32_t"])
        if signature != expected: raise ValueError("Unreviewed water-profile ABI")
        return f'void {name}({params}) {{ dkr_probe_profile_disabled("{name}"); }}'
    # librecomp/src/recomp.cpp: these operate solely on checkpointed guest
    # registers. Preserve its FR handling and refusal of other changed bits.
    if name in ("cop0_status_read", "cop0_status_write"):
        expected = ("gpr", ["recomp_context*"]) if name.endswith("read") else ("void", ["recomp_context*", "gpr"])
        if signature != expected: raise ValueError("Unreviewed COP0 status ABI")
        invocation = "dkr_probe_cop0_read(p0)" if name.endswith("read") else "dkr_probe_cop0_write(p0,p1)"
        return f'{ret} {name}({params}) {{ {"return " if ret != "void" else ""}{invocation}; }}'
    # This pointer is a native constant field table, not a guest address. The
    # private no-mods service never dereferences it. Only this audited ABI may
    # pass it through; unknown pointer signatures remain fenced.
    if name == "dkr_legacy_character_menu":
        if signature != ("int", ["uint8_t*", "recomp_context*", "unsigned", "const uint32_t*"]):
            raise ValueError("Unreviewed character-menu hook ABI")
        return f'int {name}({params}) {{ return dkr_probe_native_fields("{name}", p0, p1, p2, p3); }}'
    if name == "dkr_legacy_track_menu":
        if signature != ("int", ["uint8_t*", "recomp_context*", "unsigned", "const uint32_t*", "unsigned"]):
            raise ValueError("Unreviewed track-menu hook ABI")
        # The private owner permits ONLY notification 14 with no observed
        # return, never the interactive track-menu or a native field pointer.
        observed_guard='' if full_scenes else 'if (p4) dkr_probe_block("unowned-track-menu-observation"); '
        return f'int {name}({params}) {{ {observed_guard}return dkr_probe_native_fields("{name}", p0, p1, p2, p3); }}'
    if types[:2] == ["uint8_t*", "recomp_context*"] and ret in ("void", "int", "unsigned", "uint32_t") and all(t in ("int", "unsigned", "uint32_t", "int32_t") for t in types[2:]):
        args = ", ".join(f"(uint64_t)p{i}" for i in range(2, len(types)))
        if args:
            return f'{ret} {name}({params}) {{ uint64_t args[] = {{{args}}}; {"return " if ret != "void" else ""}dkr_probe_native_args("{name}", p0, p1, args, {len(types)-2}); }}'
        invocation = f'dkr_probe_native("{name}", p0, p1)'
        return f'{ret} {name}({params}) {{ {"return " if ret != "void" else ""}{invocation}; }}'
    return f'{ret} {name}({params}) {{ dkr_probe_block("{name}"); }}'


def isolation_header(guest_names, import_names, revision):
    """Link isolation, not a grant of native replay/runtime admission.

    The exact same reviewed guest bodies and import fences are retained. ALL
    emitted definitions (including header-driven imports/indirect dispatch)
    receive a revision-specific C symbol. Strings remain diagnostic originals.
    This prevents a future owned adapter from interposing stable CPU/services.
    """
    if revision not in (77, 80):
        raise ValueError("Unsupported isolated guest revision")
    names = sorted(set(guest_names) | set(import_names))
    if any(not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", name) for name in names):
        raise ValueError("Invalid isolated guest/import identifier")
    prefix = f"dkr_experimental_v{revision}_"
    return "#pragma once\n// Patch Pipeline-owned C link boundary; NOT live admission.\n" + \
        "\n".join(f"#define {name} {prefix}{name}" for name in names) + "\n"


def audio_isolation_header(revision):
    # Types matter too: inline RSP member functions otherwise retain the
    # stable runtime's C++ link names even when DMEM itself is renamed.
    return isolation_header(("dkrAspMain", "dmem", "rspReciprocals",
        "rspInverseSquareRoots", "RSP", "RspContext", "RspExitReason",
        "RspUcodeFunc"), (), revision)


def generate(source_dir: Path, header_path: Path, output: Path, roots, revision, scene_cuts=False, audio_services=False, input_services=False, authored_cpu=False, isolate_symbols=False, full_scenes=False, boss_finish_diagnostics=False, rsp_source=None):
    source_dir, header_path, output = source_dir.resolve(), header_path.resolve(), output.resolve()
    if output.exists() or output == source_dir or source_dir in output.parents or header_path.parent in output.parents:
        raise ValueError("Probe output must be a new, separate directory; protected inputs are never rewritten")
    if input_services:
        input_native_audit()
    if full_scenes and not (authored_cpu and isolate_symbols):
        raise ValueError("Full scenes require an explicit isolated authored CPU owner")
    if boss_finish_diagnostics and not (full_scenes and authored_cpu and isolate_symbols):
        raise ValueError("Boss diagnostics require the isolated full-scene owned adapter")
    if full_scenes:
        full_scene_native_audit()
    header_bytes = header_path.read_bytes()
    if hashlib.sha256(header_bytes).hexdigest() != HEADER_HASH:
        raise ValueError("Unreviewed recomp.h; audit memory macros before updating the probe")
    header = header_bytes.decode("utf-8")
    functions, prototypes, origins = {}, declarations(header), {}
    def merge(items):
        for name, signature in items.items():
            if name in prototypes and prototypes[name] != signature:
                raise ValueError(f"Conflicting native declaration for {name}")
            prototypes[name] = signature
    merge(PROJECT_IMPORTS)
    merge(declarations((source_dir / "funcs.h").read_text(encoding="utf-8")))
    for file in sorted(source_dir.glob("*.c")):
        source = file.read_text(encoding="utf-8")
        matches = list(DEF.finditer(source))
        for i, match in enumerate(matches):
            body = source[match.start():matches[i + 1].start() if i + 1 < len(matches) else len(source)]
            name = match[1]
            if name in functions: raise ValueError(f"Duplicate guest definition {name}")
            functions[name] = body
            merge(declarations(body))
            origins[name] = {"file": str(file), "sha256": hashlib.sha256(body.encode()).hexdigest()}
    if boss_finish_diagnostics:
        functions["racer_boss_finish"] = boss_finish_diagnostic_body(functions["racer_boss_finish"], revision)
    if full_scenes:
        functions["render_level_geometry_and_objects"] = local_world_observation_body(functions["render_level_geometry_and_objects"], revision)
    if authored_cpu:
        if not (scene_cuts and audio_services and input_services):
            raise ValueError("Authored CPU requires explicit scene/audio/input ownership")
        functions["dkr_probe_authored_main_cpu"] = authored_main_cpu(functions["main_game_loop"], revision, full_scenes)
        origins["dkr_probe_authored_main_cpu"] = {**origins["main_game_loop"], "phase": "private-fixed-cadence-cpu-only"}
        functions["dkr_probe_authored_video_cpu"]=authored_video_cpu(functions["fb_update"],functions["fb_swap"],revision)
        origins["dkr_probe_authored_video_cpu"]={**origins["fb_update"],"phase":"private-blackout-and-buffer-role-cpu-only"}
        functions["dkr_probe_scene_unload_cpu"]=owned_unload_cpu(functions["unload_level_game"],revision)
        origins["dkr_probe_scene_unload_cpu"]={**origins["unload_level_game"],"phase":"private-confirmed-drained-teardown"}
        functions["dkr_probe_scene_load_cpu"],functions["dkr_probe_level_load_cpu"]=owned_load_cpu(
            functions["load_level_game"],functions["level_load"],revision)
        for new,original in (("dkr_probe_scene_load_cpu","load_level_game"),("dkr_probe_level_load_cpu","level_load")):
            origins[new]={**origins[original],"phase":"private-confirmed-constructor"}
        resumed=owned_scene_resume(functions["main_game_loop"],functions["mode_game"],revision,
            functions["mode_menu"] if full_scenes else None)
        functions["dkr_probe_scene_resume_main_cpu"],functions["dkr_probe_scene_resume_mode_cpu"]=resumed[:2]
        if full_scenes:
            functions["dkr_probe_scene_resume_menu_cpu"]=resumed[2]
            origins["dkr_probe_scene_resume_menu_cpu"]={**origins["mode_menu"],"phase":"confirmed-menu-continuation"}
            for name,body in owned_menu_cpu(functions,revision).items():
                functions[name]=body
                origins[name]={"phase":"confirmed-menu-constructor","source_pins":MENU_CPU_HASH[revision]}
        for new,original in (("dkr_probe_scene_resume_main_cpu","main_game_loop"),("dkr_probe_scene_resume_mode_cpu","mode_game")):
            origins[new]={**origins[original],"phase":"private-confirmed-continuation"}
    helpers = set(re.findall(r"^#define\s+(\w+)\s*\(", header, re.M))
    helpers.update(re.findall(r"static\s+inline\s+\w+\s+(\w+)\s*\(", header))
    helpers.update(("if", "while", "switch", "for", "return", "sizeof", "void"))
    helpers.add("dkr_probe_scene_pending")
    helpers.add("dkr_probe_boss_trace")
    helpers.add("dkr_probe_local_world_draw")
    helpers.update(("dkr_probe_scene_entry","dkr_probe_block"))
    helpers.update(("dkr_probe_scene_unload_begin","dkr_probe_scene_unload_render_drained","dkr_probe_scene_unload_end"))
    helpers.update(("dkr_probe_scene_load_begin","dkr_probe_scene_load_reset","dkr_probe_scene_load_ready"))
    helpers.update(("dkr_probe_scene_transaction_cut","dkr_probe_scene_transaction_end"))
    helpers.update(("dkr_probe_menu_load_begin", "dkr_probe_menu_construct_begin", "dkr_probe_menu_construct_ready",
                    "dkr_probe_menu_level_change_begin", "dkr_probe_menu_level_change_ready", "dkr_probe_menu_background_request"))
    helpers.update(("sqrtf", "sqrt", "truncf", "trunc", "roundf", "round", "fabsf", "fabs",
                    "floorf", "floor", "ceilf", "ceil", "fmodf", "fmod", "abs", "assert"))
    dispatch = audio_dispatch(functions, revision) if audio_services else {}
    menu_callbacks = menu_dispatch(functions, revision) if full_scenes else {}
    rsp_headers, rsp_source = audio_rsp_payload(rsp_source) if audio_services else ({}, None)
    roots = list(roots)
    if authored_cpu:
        roots.append("dkr_probe_authored_main_cpu")
        roots.append("dkr_probe_authored_video_cpu")
        roots.append("dkr_probe_scene_unload_cpu")
        roots.append("dkr_probe_scene_load_cpu")
        roots.append("dkr_probe_scene_resume_main_cpu")
    if audio_services:
        roots = list(dict.fromkeys(roots + ["alAudioFrame", "__clearAudioDMA", "sound_play"] + list(AUDIO_CALLBACKS)))
    if full_scenes:
        roots = list(dict.fromkeys(roots + list(MENU_CALLBACKS)))
    pending, used, blocked, unknown = list(roots), set(), set(), set()
    while pending:
        name = pending.pop()
        if name in used: continue
        if name not in functions: raise ValueError(f"Missing guest root/callee {name}")
        used.add(name)
        for callee in CALL.findall(strip(functions[name])):
            if callee in functions: pending.append(callee)
            elif callee in prototypes: blocked.add(callee)
            elif callee not in helpers: unknown.add((name, callee))
    if unknown: raise ValueError(f"Unclassified calls: {sorted(unknown)}")
    # These can be invoked by header macros/inline helpers, outside the body
    # call scan. They must not resolve to a live runtime via indirect dispatch.
    blocked.update(("get_function", "switch_error", "do_break", "cop0_status_write",
                    "cop0_status_read", "recomp_syscall_handler", "pause_self"))
    for name in blocked:
        if name not in prototypes: raise ValueError(f"No checked prototype for native import {name}")

    # Substitute ALL direct header guest memory accesses, including inline
    # doubleword/unaligned helpers. Macro shape is protected by the header hash.
    insert = ('#include "isolated_symbols.h"\n' if isolate_symbols else '') + '#include "probe_bridge.h"\n'
    header = header.replace("// Compiler definition", insert + "// Compiler definition", 1)
    for name, typ, width in (("W", "int32_t", 4), ("H", "int16_t", 2),
                            ("B", "int8_t", 1), ("HU", "uint16_t", 2), ("BU", "uint8_t", 1)):
        pattern = r"#define MEM_" + name + r"\(offset, reg\) \\\n[^\n]*"
        replacement = f"#define MEM_{name}(offset, reg) (*({typ}*)dkr_probe_memory_{width}_at(rdram, (uint64_t)(reg) + (uint64_t)(offset), __FILE__, __LINE__))"
        header, count = re.subn(pattern, replacement, header)
        if count != 1: raise ValueError(f"Memory macro mismatch MEM_{name}")
    header, count = re.subn(r"#define SD\(val, offset, reg\) \{ \\\n.*?\n\}",
        "#define SD(val, offset, reg) { MEM_W((offset) + 4, reg) = (uint32_t)(val); MEM_W(offset, reg) = (uint32_t)((gpr)(val) >> 32); }", header, flags=re.S)
    if count != 1 or re.search(r"rdram\s*\+", header): raise ValueError("Uninstrumented guest memory access remains")
    if any(re.search(r"\brdram\s*\[|\brdram\s*\+", strip(functions[n])) for n in used):
        raise ValueError("Guest body has direct pointer arithmetic outside the checked memory macros")

    if scene_cuts:
        if "mode_game" not in used: raise ValueError("Scene cuts require the reviewed mode_game closure")
        functions["mode_game"]=mode_scene_cuts(functions["mode_game"],revision)
        if full_scenes:
            functions["mode_menu"]=menu_scene_cuts(functions["mode_menu"],revision)

    isolated_header = isolation_header(used, blocked, revision) if isolate_symbols else ""
    audio_isolated_header = audio_isolation_header(revision) if isolate_symbols and audio_services else ""
    if audio_isolated_header:
        # Applied only to new private copies. Intrinsic/system headers stay
        # outside any private namespace and the protected inputs stay intact.
        rsp_headers["rsp.hpp"] = '#include "isolated_audio_symbols.h"\n' + rsp_headers["rsp.hpp"]
    output.mkdir(parents=True)
    if isolate_symbols:
        (output / "isolated_symbols.h").write_text(isolated_header, encoding="utf-8")
    if audio_services:
        if audio_isolated_header:
            (output / "isolated_audio_symbols.h").write_text(audio_isolated_header, encoding="utf-8")
        (output / "librecomp").mkdir()
        for name, source in rsp_headers.items():
            (output / "librecomp" / name).write_text(source, encoding="utf-8")
        (output / "private_audio_rsp.cpp").write_text(rsp_source, encoding="utf-8")
    (output / "recomp.h").write_text(header, encoding="utf-8")
    extra = authored_declarations(full_scenes) if authored_cpu else ""
    (output / "funcs.h").write_text((source_dir / "funcs.h").read_text(encoding="utf-8") + extra, encoding="utf-8")
    ordered = sorted(used)
    checked_declarations = ['#include "recomp.h"']
    for name in sorted(blocked):
        ret, types = prototypes[name]
        checked_declarations.append(f'{ret} {name}({", ".join(types) or "void"});')
    (output / "imports.h").write_text("\n".join(checked_declarations) + "\n", encoding="utf-8")
    for index in range(0, len(ordered), 50):
        (output / f"guest_{index // 50}.c").write_text('#include "funcs.h"\n#include "imports.h"\n' +
            "\n".join(instrument_function(n, functions[n]) for n in ordered[index:index + 50]), encoding="utf-8")
    stubs = ['#include "recomp.h"', '#include "probe_bridge.h"']
    if audio_services:
        stubs.append('#include "funcs.h"')
    for name in sorted(blocked):
        if name == "get_function" and audio_services:
            if prototypes[name] != ("recomp_func_t*", ["int32_t"]):
                raise ValueError("Unreviewed indirect guest callback ABI")
            stubs.append(indirect_stub(dispatch, menu_callbacks))
        else:
            stubs.append(native_stub(name, prototypes[name], full_scenes))
    (output / "blocked_imports.c").write_text("\n".join(stubs) + "\n", encoding="utf-8")
    (output / "manifest.json").write_text(json.dumps({"revision": revision, "roots": roots, "scene_cuts": scene_cuts, "full_scenes": full_scenes,
        "boss_finish_diagnostics": boss_finish_diagnostics,
        "local_world_observations": full_scenes,
        "local_world_observation_source": origins["render_level_geometry_and_objects"] if full_scenes else {},
        "boss_finish_diagnostic_source": origins["racer_boss_finish"] if boss_finish_diagnostics else {},
        "symbol_prefix": f"dkr_experimental_v{revision}_" if isolate_symbols else "",
        "audio_symbol_isolation": bool(audio_isolated_header),
        "audio_services": audio_services, "input_services": input_services, "authored_cpu": authored_cpu,
        "authored_cpu_source": origins["main_game_loop"] if authored_cpu else {},
        "authored_video_sources": {name: origins[name] for name in ("fb_update","fb_swap")} if authored_cpu else {},
        "confirmed_scene_sources": {name: origins[name] for name in ("unload_level_game","load_level_game","level_load")} if authored_cpu else {},
        "input_native_inputs": INPUT_NATIVE_HASH if input_services else {},
        "full_scene_native_inputs": FULL_SCENE_NATIVE_HASH if full_scenes else {},
        "audio_dispatch": {f"0x{address:08X}": name for address, name in sorted(dispatch.items())},
        "menu_dispatch": {f"0x{address:08X}": name for address,name in sorted(menu_callbacks.items())},
        "menu_indirect_sources": MENU_INDIRECT_HASH[revision] if full_scenes else {},
        "audio_rsp_inputs": RSP_INPUT_HASH if audio_services else {},
        "guest_functions": {n: origins[n] for n in ordered}, "blocked_native_imports": sorted(blocked),
        "recomp_header_sha256": HEADER_HASH,
        "safety": "Isolated guest CPU closure. Only audited owned imports are admitted; normal boot and live admission are separately enforced by the owned adapter."}, indent=2) + "\n", encoding="utf-8")
    print(f"Probe v{revision}: {len(used)} real guest functions; {len(blocked)} fenced native imports")


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("generated", type=Path)
    p.add_argument("header", type=Path)
    p.add_argument("output", type=Path)
    p.add_argument("--revision", type=int, choices=(77, 80), required=True)
    p.add_argument("--roots", nargs="+", default=["main_game_loop", "obj_update", "input_swap_id"])
    p.add_argument("--scene-cuts", action="store_true", help="Private audited resumable mode_game unload intents; not live scene admission")
    p.add_argument("--audio-services", action="store_true", help="Private retail audio CPU closure and exact callback map; not live audio admission")
    p.add_argument("--input-services", action="store_true", help="Private audited logical controller/save/rumble owner; not live SI/Pak admission")
    p.add_argument("--authored-cpu", action="store_true", help="Exact private retail main-loop CPU phase; excludes scheduler/VI/renderer ownership")
    p.add_argument("--isolate-symbols", action="store_true", help="Revision-specific C link isolation; preserves all native fences, NOT live admission")
    p.add_argument("--full-scenes", action="store_true", help="Confirmed menu continuations and constructors; release qualification remains separate")
    p.add_argument("--boss-finish-diagnostics", action="store_true", help="Bounded observation-only boss-finish tracing; no gameplay changes")
    p.add_argument("--rsp-source", type=Path, help="Read-only RSP input directory for an isolated worktree; original SHA-256 pins remain mandatory")
    a = p.parse_args()
    generate(a.generated, a.header, a.output, a.roots, a.revision, a.scene_cuts, a.audio_services, a.input_services, a.authored_cpu, a.isolate_symbols, a.full_scenes, a.boss_finish_diagnostics, a.rsp_source)
