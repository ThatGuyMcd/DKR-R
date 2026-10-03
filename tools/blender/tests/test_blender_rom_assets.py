"""Exercise the Game Assets setup inside Blender.

``test_rom_extract.py`` proves the extracted tree is the decomp's. This proves
the flow around it: the sidebar asks for assets before anything else, "continue
without" lets the author through, the extract operator runs the worker and the
timer adopts its tree, a bad ROM is refused with a message the panel can show,
cancelling leaves nothing behind, and a folder can be used instead.

The extraction folder and the preferences are redirected to scratch, so the
author's own Blender configuration is never touched.

    blender --background --factory-startup --python tools/blender/tests/test_blender_rom_assets.py
"""

from __future__ import annotations

import glob
import os
import shutil
import sys
import tempfile
import traceback
import types

import bpy

_HERE = os.path.dirname(os.path.abspath(__file__))
for argument in sys.argv:
    if argument.endswith("test_blender_rom_assets.py"):
        _HERE = os.path.dirname(os.path.abspath(argument))
        break

sys.path.insert(0, os.path.abspath(os.path.join(_HERE, "..")))
sys.path.insert(0, _HERE)

import dkr_track_editor  # noqa: E402
from dkr_track_editor import prefs, rom_extract  # noqa: E402
from dkr_track_editor.operators import rom_assets  # noqa: E402
from dkr_track_editor.ui import panels  # noqa: E402

from test_roundtrip import REPO_ROOT  # noqa: E402

FAILURES = []


def check(condition, message):
    if condition:
        print("  ok   %s" % message)
    else:
        print("  FAIL %s" % message)
        FAILURES.append(message)


def find_rom():
    for path in sorted(glob.glob(os.path.join(REPO_ROOT, "extern", "dkr-decomp",
                                              "baseroms", "*"))):
        try:
            if rom_extract.identify(path).supported:
                return path
        except rom_extract.RomError:
            continue
    return None


def finish(job):
    """Wait for the worker, then run the timer the way Blender's loop would."""
    job.thread.join(timeout=300)
    for _ in range(10):
        if rom_assets._poll_job() is None:
            break


def setup_scratch():
    scratch = tempfile.mkdtemp(prefix="dkr-assets-setup-")
    fake = types.SimpleNamespace(asset_root="", assets_declined=False)
    prefs.preferences = lambda: fake
    prefs.preference_root = lambda: fake.asset_root
    prefs.extraction_root = lambda create=True: os.path.join(scratch, "assets")
    prefs.search_hints = lambda: [os.path.join(scratch, "assets")]
    prefs.invalidate()
    return scratch, fake


def test_registration():
    print("registration")
    for name in ("extract_rom_assets", "cancel_rom_extraction", "use_asset_folder",
                 "decline_assets", "setup_assets"):
        check(hasattr(bpy.ops.dkr, name), "operator dkr.%s exists" % name)
    check(hasattr(bpy.types, "DKR_PT_assets"), "the Game Assets panel is registered")


def test_gate(fake):
    print("gate")
    bpy.ops.wm.read_factory_settings(use_empty=True)
    context = bpy.context
    check(prefs.resolve(context) is None, "no tree is found in the scratch setup")
    check(prefs.setup_pending(context), "setup is pending without a tree")
    check(panels.DKR_PT_assets.poll(context), "Game Assets shows")
    check(not panels.DKR_PT_level_type.poll(context), "Level Type waits for the assets")
    context.scene.dkr.level_type = "RACE"
    check(not panels.DKR_PT_track.poll(context),
          "level panels wait too, even with a level type chosen")

    bpy.ops.dkr.decline_assets()
    check(fake.assets_declined, "continue without is remembered")
    check(not prefs.setup_pending(context), "nothing is pending after declining")
    check(panels.DKR_PT_level_type.poll(context), "Level Type shows after declining")
    check(panels.DKR_PT_track.poll(context), "level panels show after declining")
    check(panels.DKR_PT_assets.poll(context), "Game Assets stays, compact, to set up later")

    bpy.ops.dkr.setup_assets()
    check(not fake.assets_declined and prefs.setup_pending(context),
          "Set Up Assets brings the setup back")


def test_bad_rom(scratch):
    print("bad ROM")
    bogus = os.path.join(scratch, "not-a-rom.z64")
    with open(bogus, "wb") as handle:
        handle.write(b"PK\x03\x04" + bytes(0x2000))
    try:
        result = bpy.ops.dkr.extract_rom_assets(filepath=bogus)
    except RuntimeError:
        result = {"CANCELLED"}
    check(result == {"CANCELLED"}, "a zip is refused")
    job = rom_assets.current_job()
    check(job is not None and "unzip" in job.error, "the refusal is kept for the panel")
    check(prefs.setup_pending(bpy.context), "still pending after a refusal")


def test_cancel(rom):
    print("cancel")
    job = rom_assets.start(rom, "test")
    job.cancel.set()
    finish(job)
    check(not job.result and not job.error, "a cancelled run has no result and no error")
    root = prefs.extraction_root()
    leftovers = [n for n in os.listdir(root)] if os.path.isdir(root) else []
    check(not leftovers, "a cancelled run leaves nothing in the extraction folder")


def test_extract(rom, fake):
    print("extract")
    result = bpy.ops.dkr.extract_rom_assets(filepath=rom)
    check(result == {"FINISHED"}, "the extract operator starts")
    job = rom_assets.current_job()
    check(job is not None and job.running, "a worker is running")
    check(prefs.setup_pending(bpy.context), "setup stays pending while extracting")
    check(panels.DKR_PT_assets.poll(bpy.context), "Game Assets shows the progress")
    check(not bpy.ops.dkr.extract_rom_assets.poll(), "a second extraction cannot start")
    finish(job)
    check(job.result and not job.error, "the extraction finished (%s)" % (job.error or "ok"))
    check(fake.asset_root == job.result, "the preference points at the new tree")
    tree = prefs.resolve(bpy.context)
    check(tree is not None and tree.root == job.result, "the addon resolves the new tree")
    check(not prefs.setup_pending(bpy.context), "nothing is pending any more")
    check(not panels.DKR_PT_assets.poll(bpy.context), "Game Assets steps aside")
    check(panels.DKR_PT_level_type.poll(bpy.context), "Level Type appears")
    return job.result


def test_use_folder(scratch, fake, tree_root):
    print("use folder")
    fake.asset_root = ""
    prefs.invalidate()
    empty = os.path.join(scratch, "empty")
    os.makedirs(empty, exist_ok=True)
    try:
        result = bpy.ops.dkr.use_asset_folder(directory=empty)
    except RuntimeError:
        result = {"CANCELLED"}
    check(result == {"CANCELLED"}, "a folder without assets is refused")

    parent = os.path.dirname(tree_root)
    result = bpy.ops.dkr.use_asset_folder(directory=parent)
    check(result == {"FINISHED"}, "a folder above a tree is accepted")
    check(os.path.normcase(fake.asset_root) == os.path.normcase(tree_root),
          "the preference points at the tree inside it")


def test_known_roms():
    print("known ROMs")
    found = rom_assets.known_roms()
    check(all(identity.supported for _path, identity in found),
          "only supported ROMs are offered (%d found)" % len(found))
    check(rom_assets.known_roms() is found, "the answer is cached between redraws")


def main():
    dkr_track_editor.register()
    scratch, fake = setup_scratch()
    try:
        test_registration()
        test_gate(fake)
        test_bad_rom(scratch)
        test_known_roms()
        rom = find_rom()
        if rom is None:
            print("skip: no supported ROM in extern/dkr-decomp/baseroms")
        else:
            test_cancel(rom)
            tree_root = test_extract(rom, fake)
            if tree_root:
                test_use_folder(scratch, fake, tree_root)
    finally:
        rom_assets.teardown()
        dkr_track_editor.unregister()
        shutil.rmtree(scratch, ignore_errors=True)

    if FAILURES:
        print("\n%d failure(s)" % len(FAILURES))
        return 1
    print("\nall ok")
    return 0


if __name__ == "__main__":
    try:
        _code = main()
    except BaseException:  # noqa: BLE001 - an uncaught error must not read as a pass
        traceback.print_exc()
        _code = 1
    sys.exit(_code)
