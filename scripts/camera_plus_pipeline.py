"""Compose Camera++ into NEW Patch Pipeline output; never run the game/mod.

The accepted portrait stage and every generated CPU directory remain read-only.
This initial native stage builds the isolated presentation domain. Renderer/XR
capabilities must not be advertised before their real consumers are connected.
"""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import sys

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
AUTH = ROOT / "patches/camera-plus"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def authored_inputs():
    return {
        p.relative_to(ROOT).as_posix(): digest(p)
        for p in sorted(AUTH.rglob("*"))
        if p.is_file() and "__pycache__" not in p.parts
    }


def new_output(path, *inputs):
    path = path.resolve()
    if path.exists() or any(
        path == p.resolve() or p.resolve() in path.parents or path in p.resolve().parents for p in inputs
    ):
        raise ValueError("Choose a NEW Camera++ output, outside and not above all inputs")
    if any(part.lower() in ("extern", "recompiledfuncs", "recompiledpatches") for part in path.parts):
        raise ValueError("Camera++ output cannot enter a protected tree")
    if (ROOT / "build").resolve() not in path.parents:
        raise ValueError("Camera++ generated output must stay inside the workspace build directory")
    return path


def replace(text, old, new, count=1):
    if text.count(old) != count:
        raise ValueError("Unreviewed Camera++ transformation site: " + old[:100])
    return text.replace(old, new)


def transform(name, text):
    if name == "magic_code_library.hpp":
        text = replace(
            text, "approved=false,deterministic=false;", "approved=false,deterministic=false,presentation=false;"
        )
        return replace(
            text,
            "const std::map<std::string,unsigned>& modules,std::stop_token stop={})const;",
            "const std::map<std::string,unsigned>& modules,std::stop_token stop={},bool presentation=false)const;",
        )
    if name == "magic_code_library.cpp":
        text = replace(
            text,
            '!item.is_object()||item.size()!=7||!item.contains("id")',
            """!item.is_object()||(item.size()!=7&&item.size()!=8)||
           (item.size()==8&&(!item.contains("presentation")||!item["presentation"].is_boolean()))||
           (item.size()==7&&item.contains("presentation"))||!item.contains("id")""",
        )
        text = replace(
            text,
            "entry.compatibility=compatibility_issue(hash);",
            'entry.presentation=item.value("presentation",false);\n        entry.compatibility=compatibility_issue(hash);',
        )
        text = replace(
            text,
            '{"deterministic",package->deterministic},{"kept_online",kept_online||was_kept}',
            '{"deterministic",package->deterministic},{"presentation",package->modules.contains("presentation")},{"kept_online",kept_online||was_kept}',
        )
        text = replace(
            text,
            "const std::map<std::string,unsigned>& modules,std::stop_token stop)const {",
            "const std::map<std::string,unsigned>& modules,std::stop_token stop,bool presentation)const {",
        )
        text = replace(
            text,
            '        if(p->id!=id)bad("package ID differs from its offline profile");',
            """        if(p->id!=id)bad("package ID differs from its offline profile");
        // Derive the domain from authenticated package metadata, never the
        // mutable catalogue hint. Local cameras cannot enter race/save hashes.
        if(p->modules.contains("presentation")!=presentation)continue;""",
        )
        return replace(
            text,
            "resolve(packages,enabled,revision,online,modules)",
            "resolve(packages,enabled,revision,presentation?false:online,modules)",
        )
    raise ValueError("Unknown Camera++ transformation: " + name)


def native(baseline, output):
    # The previous stage checks every copied byte against its authored patches
    # and current first-party inputs, not merely a self-issued receipt.
    from character_portrait_pipeline import verify_native

    baseline = baseline.resolve(strict=True)
    verify_native(baseline)
    output = new_output(output, baseline, ROOT / "runtime-recomp/src")
    for path in baseline.rglob("*"):
        if path.is_symlink():
            raise ValueError("Native baseline must not contain links")
    shutil.copytree(baseline / "src", output / "src")
    shutil.copytree(ROOT / "runtime-recomp/tests/replay_probe", output / "tests/replay_probe")
    shutil.copytree(baseline.parent / "modding", output.parent / "modding")
    for name in ("magic_code_library.hpp", "magic_code_library.cpp"):
        source = baseline / "src/game/mods" / name
        (output / "src/game/mods" / name).write_text(transform(name, source.read_text()), newline="\n")
    sys.path.insert(0, str(AUTH))
    from integration_pipeline import TRANSFORMS
    from integration_pipeline import transform as integrate

    for name in sorted(TRANSFORMS):
        source = (ROOT / "runtime-recomp" / name) if name.startswith("tests/") else baseline / name
        (output / name).write_text(integrate(name, source.read_text(), replace, AUTH), newline="\n")
    for path in (AUTH / "native").iterdir():
        if path.is_file():
            shutil.copyfile(path, output / "src/game" / path.name)
    shutil.copytree(AUTH / "sdk", output / "include")
    files = {p.relative_to(output).as_posix(): digest(p) for p in sorted(output.rglob("*")) if p.is_file()}
    receipt = {
        "schema": 2,
        "baseline": baseline.as_posix(),
        "baseline_receipt": digest(baseline / "native-receipt.json"),
        "files": files,
        "symbols": {
            p.relative_to(output.parent / "modding").as_posix(): digest(p)
            for p in sorted((output.parent / "modding").rglob("*"))
            if p.is_file()
        },
        "authored": authored_inputs(),
    }
    (output / "camera-receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print("Fresh Camera++ native stage:", output)


def verify(output):
    receipt = json.loads((output / "camera-receipt.json").read_text())
    baseline_name = receipt["baseline"]
    # Receipts remain portable between this Windows workspace and its WSL
    # mount; the input itself is still checked byte-for-byte below.
    if baseline_name.lower().startswith("c:/dkrport/"):
        baseline = ROOT / Path(baseline_name[len("c:/dkrport/") :])
    else:
        baseline = Path(baseline_name)
    from character_portrait_pipeline import verify_native

    verify_native(baseline)
    if receipt["schema"] != 2 or digest(baseline / "native-receipt.json") != receipt["baseline_receipt"]:
        raise ValueError("Camera++ baseline receipt changed")
    authored = authored_inputs()
    if authored != receipt["authored"]:
        raise ValueError("Regenerate Camera++ after authored inputs change")
    actual = {
        p.relative_to(output).as_posix(): digest(p)
        for p in output.rglob("*")
        if p.is_file() and p.name != "camera-receipt.json"
    }
    if actual != receipt["files"]:
        raise ValueError("Camera++ output contains unreviewed edits or unexpected files")
    symbols = {
        p.relative_to(output.parent / "modding").as_posix(): digest(p)
        for p in sorted((output.parent / "modding").rglob("*"))
        if p.is_file()
    }
    baseline_symbols = {
        p.relative_to(baseline.parent / "modding").as_posix(): digest(p)
        for p in sorted((baseline.parent / "modding").rglob("*"))
        if p.is_file()
    }
    if symbols != receipt["symbols"] or symbols != baseline_symbols:
        raise ValueError("Camera++ reference symbol inputs changed")
    for name in ("magic_code_library.hpp", "magic_code_library.cpp"):
        rel = Path("src/game/mods") / name
        if (output / rel).read_text() != transform(name, (baseline / rel).read_text()):
            raise ValueError("Camera++ library composition changed")
    sys.path.insert(0, str(AUTH))
    from integration_pipeline import TRANSFORMS
    from integration_pipeline import transform as integrate

    for name in sorted(TRANSFORMS):
        source = (ROOT / "runtime-recomp" / name) if name.startswith("tests/") else baseline / name
        if (output / name).read_text() != integrate(name, source.read_text(), replace, AUTH):
            raise ValueError("Camera++ integration changed: " + name)
    # Verify every other baseline file remains identical, not only its hash.
    for path in (baseline / "src").rglob("*"):
        if path.is_file():
            rel = path.relative_to(baseline)
            if rel.as_posix() in TRANSFORMS:
                continue
            if rel.as_posix() in ("src/game/mods/magic_code_library.hpp", "src/game/mods/magic_code_library.cpp"):
                continue
            if path.read_bytes() != (output / rel).read_bytes():
                raise ValueError("Unrelated native source changed: " + str(rel))
    for source in (ROOT / "runtime-recomp/tests/replay_probe").rglob("*"):
        if source.is_file():
            name = source.relative_to(ROOT / "runtime-recomp").as_posix()
            if name not in TRANSFORMS and source.read_bytes() != (output / name).read_bytes():
                raise ValueError("Unrelated owned bridge source changed: " + name)
    print("Camera++ native composition and protected inputs inspected")


def sdk(output):
    output = new_output(output, ROOT / "modding", AUTH)
    base = ROOT / "modding"
    for name in ("build.py", "runtime.c", "mod.ld", "reference-sdk-only.toml"):
        dest = output / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(base / name, dest)
    shutil.copytree(base / "include", output / "include")
    shutil.copyfile(AUTH / "sdk/dkr/presentation.h", output / "include/dkr/presentation.h")
    project = output / "examples/camera-plus"
    project.mkdir(parents=True)
    for name in ("main.c", "dkr.json", "settings.json"):
        shutil.copyfile(AUTH / "mod" / name, project / name)
    schema = json.loads((project / "settings.json").read_text())
    config = [
        "[manifest]",
        'id = "dkr_camera_plus"',
        'version = "0.1.0"',
        'display_name = "ModernCamera++"',
        'description = "Choose and tune your own driving camera."',
        'authors = ["DKR-R contributors"]',
        'game_id = "dkr"',
        'minimum_recomp_version = "1.0.7"',
        "dependencies = []",
    ]
    known = set()
    for group in schema["groups"]:
        for option in group["options"]:
            option = dict(option)
            if option["id"] in known:
                raise ValueError("Duplicate Camera++ setting")
            known.add(option["id"])
            option.setdefault("type", "Number")
            config += ["\n[[manifest.config_options]]"]
            config += [key + " = " + json.dumps(value) for key, value in option.items()]
    if len(known) > 128:
        raise ValueError("Camera++ settings exceed API 1 schema capacity")
    config += ["\n[build]", 'sources = ["main.c"]']
    (project / "mod.toml").write_text("\n".join(config) + "\n")
    print("Camera++ SDK/example staged without executing code:", output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=["native", "verify", "sdk"])
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--baseline", type=Path)
    args = parser.parse_args()
    if args.operation == "native":
        if not args.baseline:
            parser.error("native requires --baseline")
        native(args.baseline, args.output)
    elif args.operation == "sdk":
        sdk(args.output)
    else:
        verify(args.output.resolve(strict=True))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError) as error:
        print("Camera++ pipeline:", error, file=sys.stderr)
        sys.exit(1)
