"""Read-only regression contracts for player-facing launcher copy."""

import ast
import json
from pathlib import Path
import re
import unittest

from source_text import Source

ROOT = Path(__file__).resolve().parents[2]
GAME = ROOT / "runtime-recomp/src/game"
# Reviewed copy that v1.0.11 no longer ships: the named release layer replaced
# that part of the launcher, and the fold wrote the shipped bytes back. Each
# string must stay absent; docs/fold-in/launcher-copy-review.md lists them for
# the Phase C copy review, which re-baselines them against the shipped strings.
SUPERSEDED = {
    "COPY-053": "camera-plus, v1.0.11",
    "COPY-054": "camera-plus, v1.0.11",
    "COPY-055": "camera-plus, v1.0.11",
    "COPY-056": "camera-plus, v1.0.11",
    "COPY-063": "all-player-detail, v1.0.11",
    "COPY-157": "shared-saves, v1.0.11",
    "COPY-158": "shared-saves, v1.0.11",
    "COPY-159": "shared-saves, v1.0.11",
    "COPY-160": "shared-saves, v1.0.11",
    "COPY-161": "shared-saves, v1.0.11",
    "COPY-162": "shared-saves, v1.0.11",
    "COPY-163": "shared-saves, v1.0.11",
    "COPY-164": "shared-saves, v1.0.11",
    "COPY-165": "shared-saves, v1.0.11",
    "COPY-166": "shared-saves, v1.0.11",
    "COPY-167": "shared-saves, v1.0.11",
    "COPY-168": "shared-saves, v1.0.11",
    "COPY-169": "shared-saves, v1.0.11",
    "COPY-170": "shared-saves, v1.0.11",
    "COPY-171": "shared-saves, v1.0.11",
    "COPY-172": "shared-saves, v1.0.11",
    "COPY-173": "shared-saves, v1.0.11",
    "COPY-175": "shared-saves, v1.0.11",
    "COPY-176": "shared-saves, v1.0.11",
    "COPY-177": "shared-saves, v1.0.11",
    "COPY-178": "shared-saves, v1.0.11",
    "COPY-179": "shared-saves, v1.0.11",
    "COPY-180": "shared-saves, v1.0.11",
    "COPY-181": "shared-saves, v1.0.11",
    "COPY-182": "shared-saves, v1.0.11",
    "COPY-183": "shared-saves, v1.0.11",
    "COPY-184": "shared-saves, v1.0.11",
    "COPY-185": "shared-saves, v1.0.11",
    "COPY-186": "shared-saves, v1.0.11",
    "COPY-187": "shared-saves, v1.0.11",
    "COPY-188": "shared-saves, v1.0.11",
    "COPY-195": "rollback-connection, v1.0.11",
    "COPY-196": "rollback-only, v1.0.11",
    "COPY-197": "rollback-only, v1.0.11",
    "COPY-198": "rollback-only, v1.0.11",
    "COPY-199": "rollback-only, v1.0.11",
    "COPY-200": "rollback-only, v1.0.11",
    "COPY-202": "rollback-only, v1.0.11",
    "COPY-203": "rollback-only, v1.0.11",
    "COPY-205": "rollback-only, v1.0.11",
    "COPY-206": "rollback-only, v1.0.11",
    "COPY-229": "rollback-connection, v1.0.11",
    "COPY-230": "rollback-connection, v1.0.11",
    "COPY-232": "rollback-connection, v1.0.11",
    "COPY-242": "rollback-connection, v1.0.11",
    "COPY-246": "rollback-connection, v1.0.11",
    "COPY-247": "rollback-connection, v1.0.11",
    "COPY-248": "online-directory, v1.0.11",
    "COPY-249": "online-directory, v1.0.11",
    "COPY-250": "rollback-only, v1.0.11",
    "COPY-253": "online-directory, v1.0.11",
    "COPY-268": "lobby-stability, v1.0.11",
    "COPY-314": "camera-plus, v1.0.11",
    "COPY-315": "camera-plus, v1.0.11",
    "COPY-316": "camera-plus, v1.0.11",
    "COPY-317": "camera-plus, v1.0.11",
    "COPY-318": "camera-plus, v1.0.11",
    "COPY-319": "camera-plus, v1.0.11",
    "COPY-321": "camera-plus, v1.0.11",
    "COPY-322": "camera-plus, v1.0.11",
    "COPY-323": "camera-plus, v1.0.11",
    "COPY-324": "camera-plus, v1.0.11",
    "COPY-397": "shared-saves, v1.0.11",
    "COPY-399": "launcher-polish, v1.0.11",
    "COPY-413": "shared-saves, v1.0.11",
    "COPY-414": "shared-saves, v1.0.11",
}


def source(name):
    return Source((GAME / name).read_text(encoding="utf-8"))


def literals(text):
    chunks = re.findall(r'"(?:\\.|[^"\\])*"(?:\s*"(?:\\.|[^"\\])*")*', text)
    result = []
    for chunk in chunks:
        try:
            result.append("".join(ast.literal_eval(token) for token in re.findall(r'"(?:\\.|[^"\\])*"', chunk)))
        except (SyntaxError, ValueError):
            pass
    return result


class LauncherCopyTests(unittest.TestCase):
    def test_superseded_entries_exist(self):
        inventory = json.loads((Path(__file__).with_name("launcher_copy_inventory.json")).read_text(encoding="utf-8"))
        self.assertLessEqual(set(SUPERSEDED), {entry["id"] for entry in inventory["entries"]})

    def test_reviewed_inventory(self):
        inventory = json.loads((Path(__file__).with_name("launcher_copy_inventory.json")).read_text(encoding="utf-8"))
        self.assertGreaterEqual(len(inventory["entries"]), 400)
        sources = {}
        for entry in inventory["entries"]:
            with self.subTest(entry=entry["id"]):
                values = sources.setdefault(entry["file"], literals(source(entry["file"]).raw))
                if entry["id"] in SUPERSEDED:
                    self.assertEqual(entry["verification"], "literal")
                    self.assertNotIn(entry["approved"], values, f"superseded ({SUPERSEDED[entry['id']]}) but present")
                elif entry["verification"] == "literal":
                    self.assertIn(entry["approved"], values)
                elif entry["verification"] == "removed":
                    self.assertNotIn(entry["previous"], values)

    def test_play_has_no_storage_paths(self):
        play = source("runtime_play_ui.inl")
        self.assertNotIn("PathUtf8(save.adventure.path)", play)
        self.assertNotIn("Current data folder:", play)
        self.assertIn("Existing saves aren't moved automatically.", play)
        self.assertIn("Your offline save needs attention.", play)

    def test_play_button_only_checks(self):
        updates = source("runtime_updates_ui.inl")
        button = updates.split("void DrawPlayUpdateButton", 1)[1].split("std::string UpdateDisplayMessage", 1)[0]
        self.assertIn("updates->check()", button)
        self.assertNotIn("updates->download()", button)
        self.assertNotIn("updates->install(", button)
        self.assertIn("!busy && !locked", button)
        self.assertIn("g_update_notice_requested = true", button)
        self.assertIn("###play-update-check", button)

    def test_background_policy_is_preserved(self):
        updates = source("runtime_updates_ui.inl")
        self.assertIn("snapshot.version == snapshot.preferences.skipped", updates)
        self.assertIn("snapshot.version == g_update_notice_seen", updates)
        self.assertIn("open_update_result", updates)
        self.assertIn("Include preview updates", updates)

    def test_release_language(self):
        files = [p for p in GAME.glob("runtime_*ui.inl")] + [GAME / "runtime_ui.cpp"]
        displayed = "\n".join(value for p in files for value in literals(p.read_text(encoding="utf-8")))
        for forbidden in (
            "playtest qualification",
            "Online is in beta",
            "in development",
            "not finished cooking",
            "WHATâ\x80\x99S NEW SINCE 1.0.0",
            "developer build",
        ):
            self.assertNotIn(forbidden, displayed)

    def test_raw_network_classification_stays_before_display(self):
        ui = source("runtime_ui.cpp")
        self.assertIn("classify_online_failure(view.status)", ui)
        self.assertIn("g_online_failure_modal_message = view.status", ui)
        self.assertIn("DrawTechnicalDetails(OnlineTextForDisplay(g_online_failure_modal_message))", ui)
        self.assertNotIn("This two-racer lobby is full", ui)

    def test_details_are_bounded_and_readable(self):
        details = source("runtime_messages_ui.inl")
        self.assertIn('BeginChild("##technical-text"', details)
        self.assertIn("180.0F", details)
        self.assertIn("PaddockReading(14.0F", details)
        self.assertIn("Copy details", details)

    def test_unwanted_information_disclosures_are_removed(self):
        online = source("runtime_online_ui.inl")
        graphics = source("runtime_graphics_ui.inl")
        self.assertNotIn('CollapsingHeader("Connection details")', online)
        self.assertNotIn('"Device identity: "', online)
        self.assertNotIn('CollapsingHeader((player.display_name + " - details")', online)
        self.assertNotIn('CollapsingHeader("Reflection details")', graphics)
        self.assertIn("Hardware ray tracing isn't supported. Using screen-space reflections.", graphics)

    def test_import_success_and_failure_are_distinct(self):
        ui = source("runtime_ui.cpp")
        self.assertIn('state.succeeded ? "TEXTURE PACK IMPORTED"', ui)
        self.assertIn("TEXTURE PACK COULDN'T BE IMPORTED", ui)

    def test_lava_glow_never_schedules_old_ray_lighting(self):
        graphics = source("runtime_graphics_ui.inl")
        adapter = source("enhanced_water_rt64.hpp")
        registry = adapter.split("inline const Registry& task_registry", 1)[1].split("struct CraftFrame", 1)[0]
        self.assertNotIn("lava_lighting_mode", registry)
        self.assertIn("const bool glow=lighting;", registry)
        self.assertIn("material.sunDirection.w=0;", adapter)
        self.assertNotIn("if(ray_lighting", adapter)
        self.assertNotIn('"RTX lighting quality"', graphics)
        self.assertNotIn('"lava-lighting-mode"', graphics)
        self.assertIn("Nearby glow is lightweight. It doesn't use ray-traced lighting.", graphics)

    def test_streamer_privacy_is_shared_and_persistent(self):
        ui = source("runtime_ui.cpp")
        online = source("runtime_online_ui.inl")
        self.assertIn('"online_hide_lobby_code="', ui)
        self.assertIn('key == "online_hide_lobby_code"', ui)
        self.assertIn('g_online_hide_lobby_code ? "*"', online)
        self.assertIn('g_online_hide_lobby_code ? "Show" : "Hide"', online)
        self.assertIn('OlNotify("Lobby code copied.")', online)
        self.assertIn('g_online_hide_lobby_code ? "*****"', online)
        self.assertIn("OnlineTextForDisplay(text)", online)

    def test_save_and_mod_safety_stays_visible(self):
        # The saves page here was replaced by shared-saves in v1.0.11; these
        # are COPY-166 and COPY-175 and are checked as superseded above.
        self.assertIn("COPY-166", SUPERSEDED)
        self.assertIn("COPY-175", SUPERSEDED)
        mods = source("runtime_mod_library_ui.inl")
        self.assertIn("Other mods, original source files and saves are kept.", mods)
        self.assertIn("mod_support(item.details", mods)
        self.assertIn('DrawTechnicalDetails(technical, "Compatibility details")', mods)


if __name__ == "__main__":
    unittest.main()
