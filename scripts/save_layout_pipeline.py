"""Checked save-layout stage. Inputs are read-only; output MUST be new.

Reuses the complete composed CPU pipeline (characters, tracks, code SDK,
graphics fixes) rather than regenerating from an incomplete base policy.
A supported prepare step (scripts/prepare_payloads.py runs it).
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil

DEF = re.compile(r"RECOMP_FUNC\s+void\s+(\w+)\s*\([^{};]*\)\s*\{")
OPERATIONS = {
    "populate_settings_from_save_data": 0,
    "func_800732E8": 1,
    "func_80073588": 2,
    "func_800738A4": 3,
    "clear_lap_records": 4,
}
PINS = {
    77: {
        "func_800738A4": "ace7fa4ceff245c2152085f37289f31c950f541fc2fe4e8e86169fe2d094a98c",
        "func_80073588": "80cf0a25375965fbfaadaa92ade2097c62832a65fb01c569be6ae3db8689f164",
        "populate_settings_from_save_data": "4a8e586f1791fc699731631b0c84a14d19614237a0103e847af1b80ef9a60d56",
        "func_800732E8": "c6262419220c3fa67ba3ab795b4fba94bce2829c25cdabea2f62d9c7332e6928",
        "clear_lap_records": "82fc9f4782fd420e965566f16ad954b4e83b55c61a417f9e11c979855caaa38f",
    },
    80: {
        "func_800738A4": "9c573baeedc19859e0781ffb862720dce5e8497ade8c7c099c3fbe0a618ec451",
        "func_80073588": "566e81cc90879325ddde9667559a6541c1de50594afdf95eeaab2ce0da1b31a8",
        "populate_settings_from_save_data": "1a2e858683bb6162d1d806a1ea4ab24c9ded1fa0e8d2775c295740c3cadb0b22",
        "func_800732E8": "d169fe3f22787dfa281d69f0aa69b708dfdad03caaa5c6391e2593dcc157bc76",
        "clear_lap_records": "2bd81ee2b02842af88bca9fdd7517623fe1e15922c0484ac35c50965873e04cf",
    },
}
ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "runtime-recomp/guest/save_layout_guest.h"


def sha(text: str) -> str:
    return hashlib.sha256(text.encode()).hexdigest()


def owned_support(source: Path) -> str:
    receipt = source / "save-layout-receipt.json"
    if not receipt.exists():
        return ""
    data = json.loads(receipt.read_text())
    header = (source / "save_layout_guest.h").read_text()
    if data["schema"] != 1 or data["helper_sha256"] != sha(header) or header != HEADER.read_text():
        raise ValueError("Save-layout helper differs from the reviewed authored pipeline input")
    return header


def stage(source: Path, output: Path, revision: int, config: Path, output_config: Path) -> None:
    source, output = source.resolve(), output.resolve()
    config, output_config = config.resolve(), output_config.resolve()
    if revision not in PINS or output.exists() or source == output or source in output.parents:
        raise ValueError("Save layout requires a new separate output directory")
    if output_config.exists() or output_config == config or source in output_config.parents:
        raise ValueError("Save-layout configuration must also be a NEW separate output")
    reserved = ("save_layout_guest.h", "save_layout_functions.c", "save_layout_fault.cpp", "save-layout-receipt.json")
    if any((source / name).exists() for name in reserved):
        raise ValueError("Input already contains save-layout output; do not compose it twice")
    # Minimal ABI declaration avoids importing scheduler implementation headers
    # (and their private concurrentqueue include paths) into a guest CPU target.
    # The exception definition is token-identical across translation units.
    scheduler = (ROOT / "extern/n64-modern-runtime/ultramodern/include/ultramodern/ultramodern.hpp").read_text()
    if "struct thread_terminated : std::exception {};" not in scheduler or "void quit();" not in scheduler:
        raise ValueError("Unreviewed scheduler termination ABI; inspect before regenerating")
    edits: dict[str, str] = {}
    found: dict[str, dict[str, str]] = {}
    hooks: list[str] = []
    for file in sorted(source.glob("*.c")):
        text = file.read_text()
        matches = list(DEF.finditer(text))
        changed = text
        for i, m in reversed(list(enumerate(matches))):
            name = m[1]
            if name not in OPERATIONS:
                continue
            end = matches[i + 1].start() if i + 1 < len(matches) else len(text)
            body = text[m.start() : end]
            if sha(body) != PINS[revision][name]:
                raise ValueError("Unreviewed save function " + name)
            op = OPERATIONS[name]
            entry = f"\n    dkr_save_layout_{op}(rdram, ctx);\n    return;\n"
            opening = body.index("{") + 1
            replacement = body[:opening] + entry + body[opening:]
            changed = changed[: m.start()] + replacement + changed[end:]
            found[name] = {"original_sha256": sha(body), "patched_sha256": sha(replacement)}
            # Complete config makes code-mod hook protection cover the port fix.
            address = re.search(r"//\s*(?:0x)?([0-9A-Fa-f]{8})", body)
            if not address:
                raise ValueError("Missing reviewed function address " + name)
            hooks.append(
                f'\n[[patches.hook]]\nfunc = "{name}"\nbefore_vram = 0x{address[1]}\ntext = "dkr_save_layout_{op}(rdram, ctx); return;"\n'
            )
        if changed != text:
            edits[file.name] = changed
    if set(found) != set(OPERATIONS):
        raise ValueError("Missing save-layout input functions")
    for path in source.rglob("*"):
        if path.is_symlink():
            raise ValueError("CPU pipeline input cannot contain links")
    shutil.copytree(source, output)
    for name, text in edits.items():
        (output / name).write_text(text, newline="\n")
    header = HEADER.read_text()
    (output / "save_layout_guest.h").write_text(header, newline="\n")
    declarations = (
        '\n#ifdef __cplusplus\nextern "C" {\n#endif\nvoid dkr_save_layout_fault(uint8_t*, recomp_context*);\n'
    )
    wrappers = ['#include "funcs.h"', '#include "save_layout_guest.h"']
    for op in range(5):
        name = f"dkr_save_layout_{op}"
        declarations += f"void {name}(uint8_t*, recomp_context*);\n"
        prefix = "uint32_t defaults=0;"
        if op in (2, 4):
            prefix += " recomp_context call=*ctx; call.f_odd=call.mips3_float_mode?&call.f1.u32l:&call.f0.u32h; call.r4=23; get_misc_asset(rdram,&call); defaults=(uint32_t)call.r2;"
        wrappers.append(
            f"RECOMP_FUNC void {name}(uint8_t* rdram, recomp_context* ctx) {{\n    {prefix}\n    if(!dkr_sl_apply(rdram,ctx,{op},{revision},defaults)) dkr_save_layout_fault(rdram,ctx);\n}}\n"
        )
    declarations += "\n#ifdef __cplusplus\n}\n#endif\n"
    with (output / "funcs.h").open("a") as f:
        f.write(declarations)
    (output / "save_layout_functions.c").write_text("\n".join(wrappers), newline="\n")
    (output / "save_layout_fault.cpp").write_text(
        """extern "C" {
#include "funcs.h"
}
#include <exception>
#include <cstdio>
namespace ultramodern {
void quit();
struct thread_terminated : std::exception {};
}
void dkr_save_layout_fault(uint8_t*,recomp_context*) {
    std::fputs("[save-layout][fatal] Invalid guest arrays or buffer; saving stopped before an out-of-bounds write.\\n",stderr);
    ultramodern::quit();
    throw ultramodern::thread_terminated{};
}
""",
        newline="\n",
    )
    (output / "save-layout-receipt.json").write_text(
        json.dumps(dict(schema=1, revision=revision, helper_sha256=sha(header), functions=found), indent=2) + "\n"
    )
    content = config.read_text().replace(source.as_posix(), output.as_posix())
    output_config.write_text(content + "\n# Authored fixed retail save projection\n" + "".join(hooks), newline="\n")
    print(f"Save-layout v{revision}: five pinned functions, new output {output}")


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--revision", type=int, required=True)
    p.add_argument("--config", type=Path, required=True)
    p.add_argument("--output-config", type=Path, required=True)
    a = p.parse_args()
    stage(a.source, a.output, a.revision, a.config, a.output_config)
