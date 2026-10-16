#!/usr/bin/env python3
"""Regenerate private native-menu candidates and one-function test fixtures.

Uses only the existing checked Patch Pipeline and N64Recomp executable. No
generated C edits, no changes to the input ELF, ROM or versioned base policy.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import json


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--v77-build",type=Path,required=True)
    parser.add_argument("--v80-build",type=Path,required=True)
    parser.add_argument("--recompiler",type=Path,required=True)
    parser.add_argument("--characters",action="store_true",help="Include the checked character resource adapter")
    parser.add_argument("--character-menu",action="store_true",help="Include the checked additive character selector")
    parser.add_argument("--refresh-presentation",action="store_true",help="Refresh the reviewed presentation layer in an already-composed committed policy")
    parser.add_argument("--v77-policy",type=Path,help="Explicit reviewed baseline policy for v1.0")
    parser.add_argument("--v80-policy",type=Path,help="Explicit reviewed baseline policy for v1.1")
    args=parser.parse_args(argv)
    root=Path(__file__).resolve().parents[1]
    output=args.output.resolve()
    if output==root or output==Path(output.anchor) or output.is_relative_to(root/"extern"):
        parser.error("Private generation must target a dedicated build directory")
    if args.character_menu and not args.characters:
        parser.error("Character selector requires --characters")
    if args.refresh_presentation and not args.character_menu:
        parser.error("Presentation refresh requires --character-menu")
    if output.exists() and not output.is_dir():
        parser.error(f"Output must be a directory: {output}")
    if not args.recompiler.is_file() or not os.access(args.recompiler, os.X_OK):
        parser.error(f"Recompiler must be an existing executable: {args.recompiler}")
    # Validate both revisions before any subprocess or output mutation.
    for revision, source, explicit in ((77,args.v77_build,args.v77_policy), (80,args.v80_build,args.v80_policy)):
        required = [source/f"dkr.us.v{revision}.elf", source/f"dkr.us.v{revision}.z64"]
        policies = [explicit or root/f"runtime-recomp/dkr.us.v{revision}.recomp-policy.json",
                    root/f"runtime-recomp/legacy-mods.v{revision}.recomp-fragment.json",
                    root/f"runtime-recomp/legacy-track-menu.v{revision}.recomp-fragment.json"]
        if args.characters:
            policies.append(root/f"runtime-recomp/legacy-characters.v{revision}.recomp-fragment.json")
        if args.character_menu:
            policies.append(root/f"runtime-recomp/legacy-character-menu.v{revision}.recomp-fragment.json")
        for path in required + policies:
            if not path.is_file():
                parser.error(f"Missing v{revision} input: {path}")
        for path in policies:
            try:
                value = json.loads(path.read_text(encoding="utf-8"))
                if not isinstance(value, dict):
                    raise ValueError("expected a JSON object")
            except (OSError, ValueError) as error:
                parser.error(f"Invalid policy {path}: {error}")
    output.mkdir(parents=True, exist_ok=True)
    return generate(args, root, output)


def run_stage(command, stage):
    try:
        subprocess.run(command, check=True)
    except subprocess.CalledProcessError as error:
        raise RuntimeError(f"{stage} failed (exit {error.returncode})") from None
    except OSError as error:
        raise RuntimeError(f"{stage} could not start: {error}") from None


def generate(args, root, output):
    for revision,source,entry in ((77,args.v77_build,"0x80065D40"),(80,args.v80_build,"0x80065F80")):
        elf=source/f"dkr.us.v{revision}.elf"
        rom=source/f"dkr.us.v{revision}.z64"
        policy=output/f"menu-v{revision}.policy.json"
        source_policy=(args.v77_policy if revision==77 else args.v80_policy) or root/f'runtime-recomp/dkr.us.v{revision}.recomp-policy.json'
        compose_command=[sys.executable,str(root/"scripts/compose_legacy_mod_policy.py"),
            "--policy",str(source_policy),
            "--fragment",str(root/f"runtime-recomp/legacy-mods.v{revision}.recomp-fragment.json"),
            "--elf",str(elf),"--scene-runtime","--track-menu",str(root/f"runtime-recomp/legacy-track-menu.v{revision}.recomp-fragment.json"),
            "--output",str(policy)]
        if args.characters:compose_command += ['--characters',str(root/f'runtime-recomp/legacy-characters.v{revision}.recomp-fragment.json')]
        if args.character_menu:
            compose_command += ['--character-menu',str(root/f'runtime-recomp/legacy-character-menu.v{revision}.recomp-fragment.json')]
        if args.refresh_presentation:
            from compose_legacy_mod_policy import elf_sections,elf_functions
            from legacy_character_presentation_policy import refresh_presentation
            base=json.loads(source_policy.read_text())
            try:
                updated=refresh_presentation(base,elf,f'us.v{revision}',elf_sections(elf),elf_functions(elf,(1,2)))
            except (ValueError, OSError) as error:
                raise RuntimeError(f'v{revision} presentation refresh failed: {error}') from None
            policy.parent.mkdir(parents=True,exist_ok=True)
            policy.write_text(json.dumps(updated,indent=2)+'\n')
        else:run_stage(compose_command, f"v{revision} policy composition")
        for isolated in (True,False):
            functions=output/"menu-pipeline"/f"RecompiledFuncs-v{revision}" if isolated else output/f"generated-v{revision}"
            config=output/f"menu-{'pipeline-' if isolated else ''}v{revision}.toml"
            command=[sys.executable,str(root/"scripts/generate_recomp_config.py"),"--policy",str(policy),
                "--elf",str(elf),"--rom",str(rom),"--output-functions",str(functions),"--entrypoint",entry,"--output",str(config)]
            if isolated:command += ["--functions-per-output-file","1"]
            run_stage(command, f"v{revision} config generation")
            run_stage([str(args.recompiler.resolve()),str(config)], f"v{revision} recompilation ({config.name})")


if __name__=="__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, ValueError, OSError) as error:
        print(f"Menu qualification failed: {error}", file=sys.stderr)
        raise SystemExit(1)
