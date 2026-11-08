#!/usr/bin/env python3
"""Patch Pipeline: wrap NEW CPU output, never rewrite generated inputs.

Every direct and indirect call retains its public name. Only the generated
body gets a private TU-local name. A dispatch table contains only functions
actually emitted in this namespace, with project/system replacements fenced.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re

import tomllib

DEFINITION = re.compile(r"RECOMP_FUNC void (\w+)\(uint8_t\* rdram, recomp_context\* ctx\) \{")
ADDRESS = re.compile(r"// (0x[0-9a-fA-F]{8}):")
SYSTEM = (
    "os",
    "__os",
    "__sc",
    "al",
    "_al",
    "sndp_",
    "sound_",
    "music_",
    "mempool_",
    "dmacopy",
    "mainproc",
    "thread",
    "bgload_",
    "rsp_",
    "fb_",
    "input_",
    "rumble_",
    "contpak_",
    "eeprom_",
    "save_",
)


def protected(name: str, body: str) -> bool:
    return (
        name.startswith(SYSTEM)
        or any(s in name.lower() for s in ("eeprom", "mempak", "mempool", "controller_pak", "flashrom"))
        or bool(re.search(r"\bdkr_\w+\s*\(", body))
    )


def protected_hook(name: str) -> bool:
    return name.startswith(SYSTEM) or any(
        s in name.lower() for s in ("eeprom", "mempak", "mempool", "controller_pak", "flashrom")
    )


EVENTS = {
    "main_game_loop": (1, 2),
    "unload_level_game": (3, 0),
    "level_load": (0, 4),
    "obj_init_racer": (0, 5),
    "postrace_start": (6, 0),
    "menu_init": (0, 7),
}
CALL = re.compile(r"\b([A-Za-z_]\w*)\s*\(")
BLOCKING = (
    "osRecvMesg",
    "osSendMesg",
    "osJamMesg",
    "osCreateThread",
    "osStartThread",
    "osDestroyThread",
    "osStopThread",
    "osSetThreadPri",
    "osPiStartDma",
    "osEPiStartDma",
    "pause_self",
    "yield_self",
    "recomp_syscall_handler",
)
CURATED_AUDIO = {"sound_play", "music_play", "music_stop", "music_fade"}


def policy_names(source: Path, revision: int, config: Path | None = None) -> set[str]:
    """Use the COMPLETE config that produced this CPU input, not name guesses.

    Instruction-only patches have no dkr_* call in their body. They must be
    protected from replacement just like explicit native ownership hooks.
    """
    config = config or source.parent / f"v{revision}.toml"
    doc = tomllib.loads(config.read_text(encoding="utf-8"))
    if doc.get("input", {}).get("entrypoint") != {77: 0x80065D40, 80: 0x80065F80}[revision]:
        raise ValueError("Code protection requires the matching complete CPU Patch Pipeline config")
    patches = doc["patches"]
    if "instruction" not in patches or "hook" not in patches:
        raise ValueError("Incomplete code-mod port-fix protection policy")
    names = set()
    for key in ("stubs", "ignored", "renamed"):
        names.update(patches.get(key, []))
    for key in ("instruction", "hook"):
        for entry in patches[key]:
            names.add(entry["func"])
    return names


# Reviewed against Object_Racer.lap_times[5] and every source use in the
# pinned decomp (racer.c, game_ui.c, objects.c). Offsets are byte displacements,
# not arbitrary memory ranges. No unrelated racer fields are redirected.
LAP_FIELDS = {
    "update_player_racer": {0x128},
    "hud_main_time_trial": {0x124, 0x128},
    "hud_bananas": {0x128},
    "hud_lap_count": {0x128},
    "hud_race_finish_multiplayer": {0x128},
    "hud_race_time": {0x124, 0x128},
    "race_finish_time_trial": {0x128},
    "mode_init_taj_race": {0x128, 0x12C, 0x130, 0x134, 0x138},
}


def lap_memory(name: str, body: str, owned: bool) -> str:
    if name not in LAP_FIELDS:
        return body
    bridge = "dkr_probe_code_lap_address" if owned else "dkr_code_lap_address"
    pattern = re.compile(r"MEM_W\((ctx->r\d+|0X[0-9A-F]+), (ctx->r\d+|0X[0-9A-F]+)\)")

    def replace(m):
        displacement = next((int(x, 16) for x in m.groups() if x.startswith("0X")), None)
        if displacement not in LAP_FIELDS[name]:
            return m[0]
        address = f"(uint32_t)ADD32({m[1]}, {m[2]})"
        return f"MEM_W(0, (gpr)(int32_t)(!dkr_code_callbacks_enabled ? {address} : {bridge}({address},rdram,ctx)))"

    return pattern.sub(replace, body)


def blocking_closure(functions: dict[str, str]) -> set[str]:
    calls = {
        name: set(CALL.findall(re.sub(r"//[^\n]*|/\*.*?\*/", "", body, flags=re.S))) for name, body in functions.items()
    }
    blocked = {
        name
        for name, targets in calls.items()
        if any(t.startswith(BLOCKING) for t in targets) or "LOOKUP_FUNC" in targets
    }
    # A raw native call must not suspend while a code VM/native lease is held.
    # Transitive calls are fenced too, not just functions with an OS-like name.
    while True:
        more = {name for name, targets in calls.items() if targets & blocked} - blocked
        if not more:
            return blocked
        blocked.update(more)


def indirect_calls(body: str, owned: bool) -> str:
    pattern = re.compile(r"LOOKUP_FUNC\((ctx->r[0-9]+)\)\(rdram, ctx\);")
    if body.count("LOOKUP_FUNC(") != len(pattern.findall(body)):
        raise ValueError("Unreviewed indirect guest call shape")
    bridge = "dkr_probe_code_indirect" if owned else "dkr_code_call_indirect"
    return pattern.sub(
        lambda m: (
            "{ const uint32_t code_target=(uint32_t)(" + m[1] + "); "
            "if(!dkr_code_callbacks_enabled || !" + bridge + "(code_target,rdram,ctx)) "
            "LOOKUP_FUNC(code_target)(rdram,ctx); }"
        ),
        body,
    )


def entries(
    functions: dict[str, str], originals: dict[str, str] | None = None, port_fixes: set[str] | None = None
) -> list[dict]:
    result = []
    blocked = blocking_closure(originals or functions)
    for name, body in sorted(functions.items()):
        original = (originals or functions).get(name)
        if not original:
            continue  # Authored private continuations have no retail grant.
        match = ADDRESS.search(original)
        if not match:
            continue
        before, after = EVENTS.get(name, (0, 0))
        result.append(
            dict(
                name=name,
                address=int(match[1], 16),
                protected=protected(name, original) or name in blocked or name in (port_fixes or set()),
                protected_call=protected_hook(name) or name in blocked,
                curated_call=name in CURATED_AUDIO and name not in blocked,
                protected_hook=protected_hook(name),
                before_event=before,
                after_event=after,
            )
        )
    # Alias ambiguity must never resolve to an arbitrary native function.
    by_address = {}
    for e in result:
        if e["address"] in by_address:
            by_address[e["address"]]["protected"] = True
            e["protected"] = True
            by_address[e["address"]]["protected_hook"] = True
            e["protected_hook"] = True
            by_address[e["address"]]["protected_call"] = True
            e["protected_call"] = True
            by_address[e["address"]]["curated_call"] = False
            e["curated_call"] = False
        else:
            by_address[e["address"]] = e
    return sorted(by_address.values(), key=lambda e: e["address"])


TRACK_OPTIONS = {"trackmenu_setup_render": 18, "func_80092188": 20, "menu_track_select_loop": 22, "trackmenu_input": 24}


def track_menu_fields(functions: dict[str, str]) -> str:
    # Reuse ONLY the field table already verified by the legacy Patch Pipeline
    # against this revision's ELF. Never guess globals from decomp addresses.
    source = functions.get("func_80092188", "")
    fields = re.findall(r"const uint32_t dkr_legacy_fields\[\] = \{([^}]+)\}", source)
    if len(fields) != 1 or len(fields[0].split(",")) != 29:
        raise ValueError("Six-column menus require the verified native field table")
    if not re.fullmatch(r"[0-9a-fA-FxXUu,\s]+", fields[0]):
        raise ValueError("Nonconstant native menu fields")
    return fields[0]


def wrap(name: str, body: str, address: int, owned: bool = False, menu_fields: str = "") -> str:
    body = lap_memory(name, body, owned)
    body = indirect_calls(body, owned)
    match = DEFINITION.match(body)
    if not match or match[1] != name:
        raise ValueError(f"Unreviewed guest ABI: {name}")
    internal = "dkr_code_body_" + name
    renamed = (
        body[: match.start()]
        + body[match.start() : match.end()].replace(f"RECOMP_FUNC void {name}(", f"static void {internal}(", 1)
        + body[match.end() :]
    )
    if owned:
        enter = f"dkr_probe_code_hook(0,0x{address:08X}U,rdram,ctx)"
        leave = f"dkr_probe_code_hook(1,0x{address:08X}U,rdram,ctx)"
    else:
        enter = f"dkr_code_hook_enter(0x{address:08X}U,rdram,ctx)"
        leave = f"dkr_code_hook_leave(0x{address:08X}U,rdram,ctx)"
    invocation = f"{internal}(rdram,ctx);"
    if name in TRACK_OPTIONS:
        if not menu_fields:
            raise ValueError("Missing verified six-column menu fields")
        event = TRACK_OPTIONS[name]
        invocation = (
            "{ extern int dkr_legacy_track_menu(uint8_t*,recomp_context*,unsigned,const uint32_t*,unsigned); "
            "static const uint32_t menu_fields[]={" + menu_fields + "}; "
            f"dkr_legacy_track_menu(rdram,ctx,{event}U,menu_fields,0U); "
            + invocation
            + f" dkr_legacy_track_menu(rdram,ctx,{event + 1}U,menu_fields,0U); }}"
        )
    return (
        renamed
        + f"""
RECOMP_FUNC void {name}(uint8_t* rdram, recomp_context* ctx) {{
    if(!dkr_code_hook_active(0x{address:08X}U)) {{ {invocation} return; }}
    gpr code_in[4]={{ctx->r4,ctx->r5,ctx->r6,ctx->r7}};
    for(unsigned code_runs=0;code_runs<4;++code_runs) {{
        ctx->r4=code_in[0];ctx->r5=code_in[1];ctx->r6=code_in[2];ctx->r7=code_in[3];
        if(!{enter}) {{ {invocation} }}
        gpr code_out[4]={{ctx->r4,ctx->r5,ctx->r6,ctx->r7}};
        ctx->r4=code_in[0];ctx->r5=code_in[1];ctx->r6=code_in[2];ctx->r7=code_in[3];
        int code_repeat={leave};
        ctx->r4=code_out[0];ctx->r5=code_out[1];ctx->r6=code_out[2];ctx->r7=code_out[3];
        if(!code_repeat)break;
    }}
}}
"""
    )


def table_source(items: list[dict], revision: int, owned: bool = False) -> str:
    prefix = f"dkr_experimental_v{revision}" if owned else f"dkr_v{revision}"
    rows = "\n".join(
        f'    {{0x{e["address"]:08X}U,{e["name"]},{int(e["protected"])},{int(e["protected_hook"])},{e["before_event"]},{e["after_event"]},"{e["name"]}",{int(e["protected_call"])},{int(e["curated_call"])}}},'
        for e in items
    )
    return f"""#include "funcs.h"
#include "mods/magic_code_dispatch.h"
static const dkr_code_dispatch_entry entries[]={{
{rows}
}};
const dkr_code_dispatch_entry* {prefix}_code_dispatch_table(size_t* count) {{
    *count=sizeof(entries)/sizeof(entries[0]);return entries;
}}
"""


def owned_header() -> str:
    return """#include "mods/magic_code_dispatch.h"
static inline int dkr_probe_code_hook(unsigned leaving,uint32_t address,uint8_t* ram,recomp_context* context) {
    const uint64_t args[]={address};uint64_t result=0;
    const int handled=dkr_probe_mod_dispatch(leaving?"dkr_code_leave":"dkr_code_enter",
        ram,context,args,1,0,0,&result);
    if(handled!=1)dkr_probe_block("code-world-dispatch-unavailable");
    return (int)result;
}
static inline int dkr_probe_code_indirect(uint32_t address,uint8_t* ram,recomp_context* context) {
    if(address<0x81000000U)return 0;
    const uint64_t args[]={address};uint64_t result=0;
    if(dkr_probe_mod_dispatch("dkr_code_indirect",ram,context,args,1,0,0,&result)!=1)
        dkr_probe_block("code-world-indirect-unavailable");
    return (int)result;
}
static inline uint32_t dkr_probe_code_lap_address(uint32_t address,uint8_t* ram,recomp_context* context) {
    if(!dkr_code_callbacks_enabled)return address;
    const uint64_t args[]={address};uint64_t result=0;
    if(dkr_probe_mod_dispatch("dkr_code_lap_address",ram,context,args,1,0,0,&result)!=1)
        dkr_probe_block("code-world-lap-address-unavailable");
    return (uint32_t)result;
}
"""


def stage(source: Path, output: Path, revision: int, config: Path | None = None) -> None:
    source = source.resolve()
    output = output.resolve()
    if output.exists() or source == output or source in output.parents:
        raise ValueError("Code dispatch staging requires a NEW directory outside its protected input")
    functions = {}
    files = {}
    for path in sorted(source.glob("*.c")):
        text = path.read_text(encoding="utf-8")
        matches = list(DEFINITION.finditer(text))
        parts = []
        for i, m in enumerate(matches):
            name = m[1]
            if name in functions:
                raise ValueError(f"Duplicate guest definition {name}")
            body = text[m.start() : matches[i + 1].start() if i + 1 < len(matches) else len(text)]
            functions[name] = body
            parts.append(name)
        files[path.name] = (text[: matches[0].start()] if matches else text, parts)
    fixes = policy_names(source, revision, config) | set(TRACK_OPTIONS)
    menu_fields = track_menu_fields(functions)
    items = entries(functions, port_fixes=fixes)
    by_address = {e["address"]: e for e in items}
    grants = {
        name: by_address[int(m[1], 16)]
        for name, body in functions.items()
        if (m := ADDRESS.search(body)) and int(m[1], 16) in by_address
    }
    output.mkdir(parents=True)
    for path in sorted(source.iterdir()):
        if path.is_symlink() or not path.is_file():
            raise ValueError("Unexpected generated payload entry")
        if path.suffix != ".c":
            (output / path.name).write_bytes(path.read_bytes())
    for filename, (header, names) in files.items():
        pieces = [header, '#include "mods/magic_code_dispatch.h"\n']
        for name in names:
            pieces.append(
                wrap(name, functions[name], grants[name]["address"], menu_fields=menu_fields)
                if name in grants
                else indirect_calls(functions[name], False)
            )
        (output / filename).write_text("".join(pieces), encoding="utf-8", newline="\n")
    (output / "magic_code_dispatch.c").write_text(table_source(items, revision), encoding="utf-8", newline="\n")
    manifest = dict(
        schema=2,
        revision=revision,
        protected_policy=sorted(fixes),
        functions=items,
        inputs={p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(source.iterdir()) if p.is_file()},
    )
    (output / "magic-code-dispatch.json").write_text(
        json.dumps(manifest, sort_keys=True, indent=2) + "\n", encoding="utf-8", newline="\n"
    )


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("source", type=Path)
    p.add_argument("output", type=Path)
    p.add_argument("--revision", type=int, choices=(77, 80), required=True)
    p.add_argument(
        "--policy-config", type=Path, help="Complete config that generated this input (default: sibling v77/v80.toml)"
    )
    a = p.parse_args()
    stage(a.source, a.output, a.revision, a.policy_config)
