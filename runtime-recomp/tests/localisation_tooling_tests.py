"""Translation resources must fail closed on malformed or unsafe copy."""

import importlib.util
import json
from pathlib import Path
import re
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("embed_localisation", ROOT / "scripts/embed_localisation.py")
EMBED = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EMBED)
INV_SPEC = importlib.util.spec_from_file_location("localisation_inventory", ROOT / "scripts/localisation_inventory.py")
INVENTORY = importlib.util.module_from_spec(INV_SPEC)
INV_SPEC.loader.exec_module(INVENTORY)


class Catalogues(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.english = {
            "code": "en",
            "name": "English",
            "messages": {
                "sample": {"text": "Hello {name}", "context": "Greeting; name is user content."},
                "formatted": {"text": "Count: %.*s / %d", "context": "Legacy printf ABI", "format": "printf"},
            },
        }
        self.french = {
            "code": "fr",
            "name": "Français",
            "messages": {"sample": "Bonjour {name}", "formatted": "Nombre : %.*s / %d"},
        }
        self.write()

    def tearDown(self):
        self.temporary.cleanup()

    def write(self):
        for pack in (self.english, self.french):
            (self.root / (pack["code"] + ".json")).write_text(json.dumps(pack, ensure_ascii=False), encoding="utf-8")

    def test_valid_utf8_and_format_metadata(self):
        packs, aliases = EMBED.load(self.root)
        self.assertEqual(packs[1]["name"], "Français")
        self.assertEqual(aliases["Count: %.*s / %d"], "formatted")
        self.assertNotIn("COUNT: %.*S / %D", aliases)

    def test_missing_extra_or_duplicate_keys(self):
        self.french["messages"].pop("sample")
        self.write()
        with self.assertRaises(ValueError):
            EMBED.load(self.root)
        self.french["messages"]["sample"] = "Bonjour {name}"
        self.french["messages"]["extra"] = "Supplément"
        self.write()
        with self.assertRaises(ValueError):
            EMBED.load(self.root)
        (self.root / "fr.json").write_text('{"code":"fr","code":"en"}', encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            EMBED.load(self.root)

    def test_argument_contract_and_format_injection(self):
        for replacement in ("Bonjour {other}", "Bonjour {name} {name}", "Bonjour\x00{name}", "Bonjour\x81{name}"):
            self.french["messages"]["sample"] = replacement
            self.write()
            with self.assertRaises(ValueError):
                EMBED.load(self.root)
        self.french["messages"]["sample"] = "Bonjour {name}"
        for replacement in ("Nombre : %.*s / %s", "Nombre : %.*s / %d %n", "Nombre : %.*s / %d %Q"):
            self.french["messages"]["formatted"] = replacement
            self.write()
            with self.assertRaisesRegex(ValueError, "printf"):
                EMBED.load(self.root)

    def test_prose_percent_sign_is_data(self):
        self.french["messages"]["sample"] = "Bonjour {name}, à 100% !"
        self.write()
        EMBED.load(self.root)

    def test_malformed_placeholders_or_mixed_scripts(self):
        for replacement in ("Bonjour {name} {bad!}", "Bonjour {name} }", "Bоnjour {name}"):
            self.french["messages"]["sample"] = replacement
            self.write()
            with self.assertRaises(ValueError):
                EMBED.load(self.root)

    def test_source_and_alias_printf_are_validated(self):
        self.english["messages"]["formatted"]["text"] = "Bad %n"
        self.write()
        with self.assertRaisesRegex(ValueError, "printf"):
            EMBED.load(self.root)
        self.english["messages"]["formatted"]["text"] = "Count: %.*s / %d"
        self.english["messages"]["formatted"]["aliases"] = ["Number: %s / %d"]
        self.write()
        with self.assertRaisesRegex(ValueError, "printf"):
            EMBED.load(self.root)

    def test_inventory_is_read_only_and_keeps_provenance(self):
        text = r"""// PaddockText(type, colour, "Not a call", width);
PaddockText(type, colour, "Ready", width);
ImGui::TextUnformatted("Restore players\xE2\x80\x99 bindings");
StoreNote(user_name, width);"""
        rows = list(INVENTORY.calls(text))
        self.assertEqual([row["source"] for row in rows], ["Ready", "Restore players’ bindings"])
        self.assertTrue(all(row["literal"] for row in rows))

    def test_authored_message_references_exist(self):
        english = json.loads((ROOT / "runtime-recomp/assets/localisation/en.json").read_text(encoding="utf-8"))[
            "messages"
        ]
        folders = [
            ROOT / "runtime-recomp/src/game/localisation",
            ROOT / "runtime-recomp/src/game/library_ui",
            ROOT / "runtime-recomp/src/game/mod_store",
            ROOT / "runtime-recomp/src/game/netplay/session_chat",
        ]
        for path in [
            file for folder in folders for file in folder.glob("*") if file.suffix in (".hpp", ".cpp", ".inl")
        ]:
            pattern = r'(?:localisation::|\b)(text|format|plural)\("([a-z][a-z0-9_.]*)"'
            for kind, key in re.findall(pattern, path.read_text(encoding="utf-8")):
                self.assertIn(key + ".other" if kind == "plural" else key, english, str(path))

    def test_inventory_finds_inline_enum_captions(self):
        rows = list(INVENTORY.calls('StoreCombo("Sort", current, {"Name", "Top rated"}, width);'))
        self.assertEqual([row["source"] for row in rows], ["Sort", "Name", "Top rated"])
        self.assertTrue(rows[-1]["enum"])

    def test_inventory_follows_only_fixed_literal_options(self):
        rows = list(
            INVENTORY.calls("""static const std::vector<std::string> kStates{"Visible","Hidden"};
PaddockSelect("visible","Visibility",&selected,kStates,width);
std::vector<std::string> user_names{"Play"};
PaddockSelect("name","Name",&selected,user_names,width);""")
        )
        self.assertEqual([row["source"] for row in rows if row.get("enum")], ["Visible", "Hidden"])

    def test_friendly_error_summaries_have_translations(self):
        english = json.loads((ROOT / "runtime-recomp/assets/localisation/en.json").read_text(encoding="utf-8"))[
            "messages"
        ]
        known = {source for value in english.values() for source in [value["text"], *value.get("aliases", [])]}
        policy = (ROOT / "runtime-recomp/src/game/launcher_copy_policy.hpp").read_text(encoding="utf-8")
        summaries = policy[
            policy.index("inline std::string error_summary") : policy.index("inline std::string progress")
        ]
        blocks = re.findall(r'"(?:[^"\\]|\\.)*"(?:\s*"(?:[^"\\]|\\.)*")*', summaries)
        checked = 0
        for block in blocks:
            caption = "".join(INVENTORY.cpp_literal(token) for token in re.findall(r'"(?:[^"\\]|\\.)*"', block))
            if len(caption) > 20 and caption.endswith("."):
                self.assertIn(caption, known)
                checked += 1
        self.assertGreater(checked, 25)


if __name__ == "__main__":
    unittest.main()
