"""Check the Level Type's rules against the retail tracks.

Everything the Level Type drives - which objects the Place list shows, how many
start positions a grid gets, what validation demands and what the header's race
type says - is checked here on data that is right by definition. A rule that
hides an object a retail level uses, or rejects a retail level in its own
level type, is a wrong rule.

    python tools/blender/tests/test_level_types.py
"""

from __future__ import annotations

import copy
import json
import math
import os
import sys
import types

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(_HERE))
sys.path.insert(0, _HERE)

from dkr_track_editor import (  # noqa: E402
    assets, catalog as catalog_module, gltf_io, level_header,
    level_header_template as template, level_types as lt, validate,
)
from dkr_track_editor.gltf_io import ObjectMap  # noqa: E402

from test_roundtrip import REPO_ROOT  # noqa: E402

FAILURES = []


def check(condition, message):
    if condition:
        print("  ok   %s" % message)
    else:
        print("  FAIL %s" % message)
        FAILURES.append(message)


def _level(tree, label):
    """By label, since an extraction may suffix the name with its revision."""
    for level in tree.levels():
        if level.label == label:
            return level
    return None


def _header(level):
    with open(level.header_path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def _settings(**values):
    settings = types.SimpleNamespace(
        level_type=lt.NONE, challenge_type=lt.BATTLE, special_type=lt.CUTSCENE,
        boss="BOSS_RACE_TRICKY1", vehicles={"VEHICLE_CAR"},
        default_vehicle="VEHICLE_CAR", laps=3, vehicle_override="NONE",
    )
    for name, value in values.items():
        setattr(settings, name, value)
    return settings


# ---------------------------------------------------------------------------

def test_keys():
    print("families and keys")
    for key in lt.KEYS:
        family, sub = lt.family_of(key)
        check(lt.key_of(family, sub or lt.BATTLE, sub or lt.CUTSCENE) == key,
              "%s is its family %s plus %s" % (key, family, sub))
        check(lt.key_for_race_type(lt.race_type(key)) == key,
              "%s round-trips through %s" % (key, lt.race_type(key)))
    check(lt.key_of(lt.NONE) is None, "no family is no key")
    check(lt.key_for_race_type("RACETYPE_UNK1") is None,
          "RACETYPE_UNK1, which no level uses, is offered nowhere")
    check(lt.key_for_race_type("RACETYPE_CHALLENGE") is None,
          "the RACETYPE_CHALLENGE mask is not offered as a type")
    check(lt.current_key(_settings(level_type=lt.CHALLENGE, challenge_type=lt.EGGS))
          == lt.EGGS, "a scene's key reads its sub-selector")


def test_retail_race_types(tree):
    print("retail race types")
    levels = tree.levels()
    check(len(levels) >= 65, "%d retail headers" % len(levels))
    wrong = [level.label for level in levels
             if lt.key_for_race_type(level.race_type) is None
             or lt.race_type(lt.key_for_race_type(level.race_type)) != level.race_type]
    check(not wrong, "every retail race_type is exactly one key (%s)" % ", ".join(wrong))
    for label, key in (("Horseshoe Gulch", lt.TEST_RACE), ("Fire Mountain", lt.EGGS),
                       ("Bluey1", lt.BOSS)):
        level = _level(tree, label)
        check(level is not None and lt.key_for_race_type(level.race_type) == key,
              "%s is %s" % (label, lt.label(key)))


def test_nothing_retail_uses_is_hidden(tree, catalog):
    print("nothing retail uses is hidden")
    hidden = set()
    for level in tree.levels():
        key = lt.key_for_race_type(level.race_type)
        for path in level.object_maps:
            for obj in gltf_io.load(path).objects:
                object_type = catalog.get(obj.object_id)
                if object_type is not None and not lt.visible(object_type, key):
                    hidden.add("%s in %s" % (object_type.label, level.label))
    check(not hidden, "every object of every retail map is visible in its own "
          "level type (%s)" % ", ".join(sorted(hidden)[:6]))


def test_visibility_rules(catalog):
    print("visibility rules")

    def where(object_id):
        object_type = catalog.get(object_id)
        return [key for key in lt.PLAYABLE if lt.visible(object_type, key)]

    check(where("ASSET_OBJECT_EGGCREATOR") == [lt.EGGS],
          "the Egg Creator only shows in Challenge > Eggs")
    for door in ("ASSET_OBJECT_LEVELDOOR", "ASSET_OBJECT_WORLDGATE",
                 "ASSET_OBJECT_BOSSDOOR"):
        check(where(door) == [lt.HUB], "%s only shows in a Hub" % door)
    check(not where("ASSET_OBJECT_BOOST"),
          "Boost, which only a cutscene uses, is hidden where racers play")
    check(lt.HUB not in where(lt.WEAPON_BALLOON) and lt.RACE in where(lt.WEAPON_BALLOON),
          "weapon balloons show everywhere but a Hub")
    check(where("ASSET_OBJECT_BEACHTREE") == list(lt.PLAYABLE),
          "scenery carries no rule")
    check(all(lt.visible(t, lt.CUTSCENE) for t in catalog.types.values()),
          "Special applies no filter")
    check(all(lt.visible(t, None) for t in catalog.types.values()),
          "no level type applies no filter")

    collectables = lt.visible_types(catalog, lt.RACE, "collectables")
    labels = [entry.label for entry in collectables]
    check("Green Balloon" in labels and "Colored Balloon" in labels,
          "the Collectables tab offers the green and the coloured balloon")
    check("WeaponBalloon" not in labels,
          "the balloon is offered by colour, not as one generic row")
    structure = lt.visible_types(catalog, lt.RACE, "structure")
    check(all(e.object_type.category != "pickups" for e in structure),
          "the Structure tab holds no pickups")
    everything = lt.visible_types(catalog, lt.RACE, "structure", show_all=True)
    check(len(everything) > len(structure) and any(not e.ok for e in everything),
          "Show incompatible types adds the hidden ones, marked")
    counts = lt.category_counts(catalog, lt.RACE, "structure")
    check("pickups" not in counts and counts.get("racing"),
          "categories are counted per tab")

    green = lt.preset("GREEN")
    check(green.fields == {"balloonType": "BALLOON_TYPE_TRAP"}, "green is a trap")
    check(lt.preset("RAINBOW").fields == {"balloonType": "BALLOON_TYPE_MAGNET"},
          "the coloured balloon is the rainbow magnet")
    check(lt.preset_for(lt.WEAPON_BALLOON, {"balloonType": "BALLOON_TYPE_TRAP"}) is green,
          "a placed balloon is named by its colour")

    labels = [entry.label for entry in structure]
    check("Loop Entry" in labels and "Loop Exit" in labels,
          "the Structure tab offers the loop's entry and exit")
    check("Modechange" not in labels and "modechange" not in labels,
          "the mode changer is offered as the loop's two ends, not one generic row")
    check(lt.preset("LOOP_ENTRY").fields == {"vehicleID": "VEHICLE_LOOPDELOOP"},
          "the loop entry switches racers to the loop physics")
    check(lt.preset_for(lt.MODECHANGE, {"vehicleID": "VEHICLE_CAR", "radius": 200})
          is lt.preset("LOOP_EXIT"), "a placed mode changer back to the car is a loop exit")
    entry_type = catalog.get(lt.MODECHANGE)
    allowed = {value for field in entry_type.fields if field.name == "vehicleID"
               for value in field.values}
    check(all(value in allowed for p in lt.PRESETS if p.object_id == lt.MODECHANGE
              for value in p.fields.values()),
          "both loop presets write a vehicleID the catalogue knows")
    text = lt.place_description(entry_type, lt.RACE, lt.preset("LOOP_ENTRY"))
    check("vehicleID = VEHICLE_LOOPDELOOP" in text,
          "a preset's tooltip names the fields it sets")

    text = lt.place_description(catalog.get("ASSET_OBJECT_EGGCREATOR"), lt.RACE)
    check("Egg Creator" in text or "eggs" in text, "a tooltip says what the type is")
    check("Not used in a Race level" in text, "and whether this level type uses it")
    check("Retail uses it" in lt.auto_help(catalog.get("ASSET_OBJECT_FIRTREE")),
          "types with no written help get one from the survey")


def test_vehicles():
    print("vehicles")
    check(lt.allowed_vehicles(lt.BOSS, {"VEHICLE_CAR", "VEHICLE_PLANE"},
                              "VEHICLE_CAR", "BOSS_RACE_BLUEY1") == ["VEHICLE_HOVERCRAFT"],
          "a boss race is raced in the boss's vehicle")
    check(lt.allowed_vehicles(lt.EGGS, {"VEHICLE_CAR", "VEHICLE_PLANE"},
                              "VEHICLE_PLANE") == ["VEHICLE_PLANE"],
          "a challenge allows exactly one vehicle")
    check(lt.allowed_vehicles(lt.RACE, set(), "VEHICLE_CAR") == ["VEHICLE_CAR"],
          "a track always allows at least one")
    overrides = lt.header_overrides(lt.CUTSCENE, vehicles={"VEHICLE_PLANE"})
    check("/avaliable-vehicles" not in overrides and "/lap-count" not in overrides,
          "a cutscene's header keeps its own vehicles and laps")


def test_templates():
    print("start grid templates")
    for key in lt.KEYS:
        check(len(lt.grid_template(key)) == lt.spawn_count(key),
              "%s grid has %d start positions" % (key, lt.spawn_count(key)))
    for x, y, yaw in lt.grid_template(lt.BATTLE, radius=1000.0):
        angle = math.radians(yaw)
        forward = (-math.sin(angle), math.cos(angle))
        towards = (-x / 1000.0, -y / 1000.0)
        check(forward[0] * towards[0] + forward[1] * towards[1] > 0.999,
              "a challenge racer at (%g, %g) faces the centre" % (x, y))
    race = lt.grid_template(lt.RACE)
    check(all(y > 0 for _x, y, _yaw in race[:4]) and all(y < 0 for _x, y, _yaw in race[4:]),
          "racers 0-3 are the front row, 4-7 the back")
    check(race[0][0] < race[3][0] and race[4][0] > race[7][0],
          "the indices snake: 0 left to 3 right, then 4 right back to 7")
    check(lt.snap_angle(3.0) == 5.625 and lt.snap_angle(2.0) == 0.0,
          "angles snap to the 5.625 degree step an angleY stores")


def test_header_bytes(catalog):
    print("header bytes")
    enum_values = catalog.raw["enumValues"]
    expected = {lt.RACE: 0, lt.BOSS: 8, lt.BATTLE: 64, lt.BANANAS: 65,
                lt.EGGS: 66, lt.HUB: 5, lt.CUTSCENE: 6, lt.BACKDROP: 7,
                lt.TEST_RACE: 3}
    for key, value in expected.items():
        overrides = lt.header_overrides(key, boss="BOSS_RACE_BLUEY1",
                                        vehicles={"VEHICLE_CAR"}, laps=2)
        overrides["/world"] = "WORLD_CENTRAL_AREA"
        data = level_header.encode(template.document(overrides), enum_values)
        check(data[0x4C] == value, "%s writes race type %d at 0x4C" % (key, value))
        if key == lt.BOSS:
            check(data[0xB8] == enum_values["BossSetupTypes"]["BOSS_RACE_BLUEY1"],
                  "the chosen boss lands at 0xB8")
        if lt.needs_checkpoints(key):
            check(data[0x4B] == 2, "%s writes its laps at 0x4B" % key)


def test_untouched_remix_keeps_its_header(tree, catalog):
    print("an untouched remix keeps its header")
    enum_values = catalog.raw["enumValues"]
    changed = []
    for level in tree.levels():
        header = _header(level)
        values = lt.from_header(header)
        if values is None:
            changed.append("%s (no level type)" % level.label)
            continue
        settings = _settings(**values)
        before = level_header.encode(header, enum_values, tree.asset_index)
        after = level_header.encode(
            template.apply_overrides(copy.deepcopy(header),
                                     lt.settings_overrides(settings)),
            enum_values, tree.asset_index,
        )
        if before != after:
            changed.append(level.label)
    check(not changed, "all %d retail headers re-encode byte-identical after "
          "import fills the Level Type and export lays it back (%s)"
          % (len(tree.levels()), ", ".join(changed[:6])))


def test_retail_levels_validate(tree, catalog):
    print("retail levels in their own level type")
    added = []
    for level in tree.levels():
        key = lt.key_for_race_type(level.race_type)
        objects = []
        for path in level.object_maps:
            objects += gltf_io.load(path).objects
        merged = ObjectMap(objects=objects)
        before = {i.message for i in validate.validate(
            merged, catalog, require_racing_track=False).errors}
        for issue in validate.validate(merged, catalog, level_key=key).errors:
            if issue.message not in before:
                added.append("%s: %s" % (level.label, issue.message))
    check(not added, "no retail level gains an error from its level type's rules "
          "(%s)" % "; ".join(added[:4]))

    report = validate.validate(ObjectMap(objects=[]), catalog, level_key=lt.NONE)
    check(len(report.errors) == 1 and "level type" in report.errors[0].message,
          "with no level type, choosing one is the only thing reported")


def main():
    catalog = catalog_module.load()
    test_keys()
    test_visibility_rules(catalog)
    test_vehicles()
    test_templates()
    test_header_bytes(catalog)

    tree = assets.AssetTree.discover(REPO_ROOT)
    if tree is None:
        print("SKIP: no extracted asset tree, so the retail checks did not run")
    else:
        test_retail_race_types(tree)
        test_nothing_retail_uses_is_hidden(tree, catalog)
        test_untouched_remix_keeps_its_header(tree, catalog)
        test_retail_levels_validate(tree, catalog)

    print()
    if FAILURES:
        print("%d FAILURE(S)" % len(FAILURES))
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
