#!/usr/bin/env python3
"""Patch Pipeline: derive raw-code hook grants from the COMPLETE port policy.

Inputs are read-only. This is an inventory, not executable code or a claim that
the private owned closure can dispatch every retail function.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from compose_legacy_mod_policy import elf_functions


def inventory(elf: Path, receipt: dict) -> dict:
    if (
        receipt.get("schema") != 1
        or receipt.get("complete") is not True
        or receipt.get("revision") not in ("us.v77", "us.v80")
        or receipt.get("elf_sha256") != hashlib.sha256(elf.read_bytes()).hexdigest()
    ):
        raise ValueError("Hook inventory requires the complete matching composed policy")
    policy = receipt["policy"]
    symbols = elf_functions(elf)
    for item in policy.get("manualFunctions", []):
        symbols.setdefault(item["name"], set()).add((int(item["vram"], 0), int(item["size"], 0)))
    protected = {}
    for key, name_key in (
        ("functionHooks", "function"),
        ("instructionPatches", "function"),
        ("stubs", "name"),
        ("ignored", "name"),
        ("renamed", "name"),
    ):
        for item in policy.get(key, []):
            protected.setdefault(item[name_key], set()).add(key)
    # Scheduler, saves, native IO and render ownership are not optional fixes.
    # Broad raw code cannot remove the runtime's lifetime/security invariants.
    prefixes = (
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
    functions = []
    for name, definitions in sorted(symbols.items()):
        if len(definitions) != 1:
            raise ValueError(f"Ambiguous retail function: {name}")
        address, size = next(iter(definitions))
        reasons = set(protected.get(name, ()))
        if name.startswith(prefixes):
            reasons.add("native-system-ownership")
        if any(part in name.lower() for part in ("eeprom", "mempak", "mempool", "controller_pak", "flashrom")):
            reasons.add("persistent-storage-ownership")
        functions.append(dict(name=name, address=address, bytes=size, protected=bool(reasons), reasons=sorted(reasons)))
    # Alias symbols at an identical entry must share the strictest grant.
    protected_addresses = {f["address"] for f in functions if f["protected"]}
    for f in functions:
        if f["address"] in protected_addresses and not f["protected"]:
            f.update(protected=True, reasons=["protected-symbol-alias"])
    return dict(
        schema=1,
        revision=receipt["revision"],
        elf_sha256=receipt["elf_sha256"],
        policy_sha256=hashlib.sha256(json.dumps(policy, sort_keys=True, separators=(",", ":")).encode()).hexdigest(),
        functions=functions,
    )


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--elf", type=Path, required=True)
    p.add_argument("--composed-policy", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    if a.output.exists() or a.output.resolve() in (a.elf.resolve(), a.composed_policy.resolve()):
        raise ValueError("Hook inventory output must be a NEW file")
    result = inventory(a.elf, json.loads(a.composed_policy.read_text(encoding="utf-8")))
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
