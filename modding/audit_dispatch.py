#!/usr/bin/env python3
"""Read-only CPU/SDK source audit. Does not run a game or a mod callback."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import stage_magic_code_payload as pipeline


def audit(source, normal, owned, revision):
    originals = {}
    for file in sorted(source.glob("*.c")):
        text = file.read_text(encoding="utf-8")
        matches = list(pipeline.DEFINITION.finditer(text))
        for i, m in enumerate(matches):
            originals[m[1]] = text[m.start() : matches[i + 1].start() if i + 1 < len(matches) else len(text)]
    receipts = [
        json.loads((normal / "magic-code-dispatch.json").read_text()),
        json.loads((owned / "manifest.json").read_text()),
    ]
    fixes = pipeline.policy_names(source, revision) | set(pipeline.TRACK_OPTIONS)
    result = {"revision": revision, "executed": False, "protected_functions": len(fixes), "namespaces": []}
    pattern = re.compile(r"MEM_W\((ctx->r\d+|0X[0-9A-F]+), (ctx->r\d+|0X[0-9A-F]+)\)")
    for root, receipt in zip((normal, owned), receipts):
        functions = receipt.get("functions", receipt.get("code_dispatch_functions", []))
        by_name = {f["name"]: f for f in functions}
        assert functions and receipt["revision"] == revision
        for name in fixes & by_name.keys():
            assert by_name[name]["protected"], f"Unprotected port fix {name}"
        for name in pipeline.CURATED_AUDIO:
            assert by_name[name]["curated_call"], f"No reviewed audio handler {name}"
        for name in (
            "get_misc_asset",
            "get_settings",
            "set_level_default_vehicle",
            "set_time_trial_enabled",
            "draw_text",
            "set_text_font",
            "set_text_colour",
            "set_text_background_colour",
        ):
            assert name in by_name and not by_name[name]["protected_call"], f"Trophy SDK call unavailable: {name}"
        text = "\n".join(p.read_text(encoding="utf-8") for p in sorted(root.glob("*.c")))
        for name, event in pipeline.TRACK_OPTIONS.items():
            assert name in by_name, f"Missing six-column menu owner {name}"
            for number in (event, event + 1):
                assert f"dkr_legacy_track_menu(rdram,ctx,{number}U,menu_fields,0U)" in text, (
                    f"Missing cursor scope {name}/{number}"
                )
        laps = {}
        for name, fields in pipeline.LAP_FIELDS.items():
            if name not in by_name:
                continue
            expected = sum(
                any(x.startswith("0X") and int(x, 16) in fields for x in m.groups())
                for m in pattern.finditer(originals[name])
            )
            match = re.search(
                r"static void dkr_code_body_"
                + re.escape(name)
                + r"\(.*?(?=\nRECOMP_FUNC void "
                + re.escape(name)
                + r"\()",
                text,
                re.S,
            )
            assert match, f"Missing body wrapper {name}"
            body = match[0]
            actual = len(re.findall(r"\b(?:dkr_code_lap_address|dkr_probe_code_lap_address)\(", body))
            assert actual == expected and expected > 0, f"Lap coverage {name}: {actual}/{expected}"
            assert not any(
                any(x.startswith("0X") and int(x, 16) in fields for x in m.groups()) for m in pattern.finditer(body)
            ), f"Unmapped lap memory {name}"
            laps[name] = actual
        result["namespaces"].append(
            {
                "private": root == owned,
                "functions": len(functions),
                "lap_accesses": laps,
                "dispatch_sha256": hashlib.sha256((root / "magic_code_dispatch.c").read_bytes()).hexdigest(),
            }
        )
    return result


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source", type=Path, required=True)
    p.add_argument("--normal", type=Path, required=True)
    p.add_argument("--owned", type=Path, required=True)
    p.add_argument("--revision", type=int, choices=(77, 80), required=True)
    a = p.parse_args()
    print(json.dumps(audit(a.source, a.normal, a.owned, a.revision), indent=2))
