"""Check the Mode Checklist: what challenges, hubs and boss races need.

Synthetic maps pin each rule down - an empty arena misses everything, a
complete one misses nothing, and the edge the decomp is precise about (a
chest's playerID is 1-based and masked) reads the way the game reads it. Then,
when an extraction is available, every retail level of these kinds must pass
its own checklist: a rule that fails Smokey Castle is a wrong rule.

    python tools/blender/tests/test_mode_checklist.py
"""

from __future__ import annotations

import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(_HERE))
sys.path.insert(0, _HERE)

from track_lab import (  # noqa: E402
    assets, catalog as catalog_module, gltf_io, level_links, level_types as lt,
    mode_checklist as mc, validate,
)
from track_lab.gltf_io import MapObject, ObjectMap  # noqa: E402

from test_roundtrip import REPO_ROOT  # noqa: E402

FAILURES = []


def check(condition, message):
    if condition:
        print("  ok   %s" % message)
    else:
        print("  FAIL %s" % message)
        FAILURES.append(message)


def obj(object_id, **fields):
    return MapObject(object_id=object_id, name=object_id, translation=[0.0, 0.0, 0.0],
                     fields=fields)


def ring():
    return [obj(mc.SETUPPOINT, racerIndex=r, entranceID=0) for r in mc.RACERS]


def graph(count=6, classes=None):
    """A closed loop of AI nodes, with destination classes by node id."""
    nodes = []
    for i in range(count):
        nodes.append(obj(mc.AINODE, nodeID=i, unk8=(classes or {}).get(i, 0),
                         adjacent=[(i - 1) % count, (i + 1) % count, 255, 255],
                         elevation=0))
    return nodes


def by_rid(rows):
    return {row.rid: row for row in rows}


def missing(rows):
    return sorted(row.rid for row in rows if row.status == mc.MISSING)


# ---------------------------------------------------------------------------

def test_applies():
    print("which level types have a checklist")
    for key in (lt.BATTLE, lt.BANANAS, lt.EGGS, lt.HUB, lt.BOSS):
        check(mc.applies(key), "%s has one" % key)
    for key in (lt.RACE, lt.TEST_RACE, lt.CUTSCENE, lt.BACKDROP, None):
        check(not mc.applies(key) and mc.rows(ObjectMap(), key) == [],
              "%s has none" % key)


def test_battle():
    print("battle")
    rows = mc.rows(ObjectMap(), lt.BATTLE)
    check(missing(rows) == ["ai_graph", "balloons", "start"],
          "an empty arena misses its ring, graph and balloons (%s)" % missing(rows))
    check(by_rid(rows)["ai_graph"].severity == mc.ERROR,
          "no AI graph is an error: battle bots only move along it")
    check("will not move" in by_rid(rows)["ai_graph"].message,
          "and says the AI racers will not move")

    unlinked = [obj(mc.AINODE, nodeID=i, adjacent=[255] * 4) for i in range(5)]
    rows = mc.rows(ObjectMap(objects=ring() + unlinked), lt.BATTLE)
    check(by_rid(rows)["ai_graph"].status == mc.MISSING,
          "nodes with no links are not a graph")

    complete = ring() + graph(classes={2: 1}) + [obj(mc.BALLOON)]
    rows = mc.rows(ObjectMap(objects=complete), lt.BATTLE)
    check(missing(rows) == [], "a complete arena misses nothing (%s)" % missing(rows))

    rows = mc.rows(ObjectMap(objects=ring() + graph() + [obj(mc.BALLOON)]), lt.BATTLE)
    row = by_rid(rows)["balloon_nodes"]
    check(row.status == mc.MISSING and row.fix == mc.FIX_AI_TAG,
          "a graph without a class-1 node offers Tag Destinations")


def test_weapon_nodes():
    print("weapon nodes: on a balloon, and on the graph")

    def at(item, x):
        item.translation = [float(x), 0.0, 0.0]
        return item

    nodes = graph(6, classes={1: 1, 4: 1})
    at(nodes[1], 0)
    at(nodes[4], 900)
    arena = ring() + nodes + [at(obj(mc.BALLOON), 40)]
    rows = by_rid(mc.rows(ObjectMap(objects=arena), lt.BATTLE))
    row = rows["weapon_reach"]
    check(row.status == mc.ADVISE and row.label == "Weapon nodes on a balloon 1/2",
          "a weapon node with no balloon near is advice, not missing (%s, %s)"
          % (row.status, row.label))
    check("node 4" in row.message and "node 4" in row.detail.lower(),
          "and names the node (%s)" % row.message)
    check(row.objects == [len(ring()) + 4], "and selects it")
    check("weapon_links" not in rows, "a linked weapon node is not reported loose")
    check(mc.summary(list(rows.values())) == "5 of 5 ready",
          "advice counts as ready (%s)" % mc.summary(list(rows.values())))
    warned = [i for i in mc.issues(ObjectMap(objects=arena), lt.BATTLE)
              if "node 4" in i[1]]
    check(warned and warned[0][0] == mc.WARNING,
          "validation reports it as a warning, never an error")

    at(nodes[4], 60)
    rows = by_rid(mc.rows(ObjectMap(objects=arena), lt.BATTLE))
    check(rows["weapon_reach"].ok,
          "within %d units counts as on it: retail's Darkwater Beach puts "
          "balloons up to 78 away" % mc.ai_graph.WEAPON_REACH_CHECK)

    nodes[4].fields["adjacent"] = [255] * 4
    rows = by_rid(mc.rows(ObjectMap(objects=arena), lt.BATTLE))
    row = rows.get("weapon_links")
    check(row is not None and row.status == mc.MISSING and row.fix == mc.FIX_AI_LINK
          and row.severity == mc.WARNING,
          "an unlinked weapon node is a warning with Link Weapon Nodes")
    check(row is not None and "node 4" in row.message and row.label == "Weapon nodes linked 1/2",
          "naming it (%s)" % (row.label if row else None))

    rows = by_rid(mc.rows(ObjectMap(objects=ring() + graph(6, classes={1: 1})), lt.BATTLE))
    check("weapon_reach" not in rows,
          "with no balloon at all only the balloon row speaks")


def test_bananas():
    print("bananas")
    chests = [obj(mc.CHEST, playerID=r + 1) for r in mc.RACERS]
    flags = [obj(mc.FLAG, playerIndex=r) for r in mc.RACERS]
    nodes = graph(8, classes={0: 1, 1: 4, 3: 5, 5: 6, 7: 7})
    complete = ring() + chests + flags + nodes + [obj(mc.COIN_CREATOR), obj(mc.BALLOON)]
    rows = mc.rows(ObjectMap(objects=complete), lt.BANANAS)
    check(missing(rows) == [], "a complete banana arena misses nothing (%s)"
          % missing(rows))

    # playerID is racer + 1, masked: the game reads (playerID - 1) & 3, so a
    # chest numbered 0 belongs to racer 3 and two chests numbered 2 leave
    # someone without.
    check(mc.owner(mc.CHEST, 0) == 3 and mc.owner(mc.CHEST, 5) == 0,
          "a chest's owner is (playerID - 1) & 3")
    shifted = [obj(mc.CHEST, playerID=v) for v in (0, 1, 2, 2)]
    rows = mc.rows(ObjectMap(objects=ring() + shifted), lt.BANANAS)
    row = by_rid(rows)["chests"]
    check(row.status == mc.MISSING and "racer 2" in row.message,
          "chests numbered 0, 1, 2, 2 leave racer 2 without one, as the game "
          "reads them (%s)" % row.message)
    check(mc.missing_racers(ObjectMap(objects=shifted), mc.CHEST, "playerID") == [2],
          "the Add button adds only racer 2's chest")
    check(row.severity == mc.ERROR, "a missing chest is an error: that racer cannot win")

    rows = mc.rows(ObjectMap(objects=ring() + chests + [obj(mc.COIN)]), lt.BANANAS)
    check(by_rid(rows)["bananas"].severity == mc.WARNING,
          "loose bananas without a creator are a warning, not an error")
    rows = mc.rows(ObjectMap(objects=ring() + chests), lt.BANANAS)
    check(by_rid(rows)["bananas"].severity == mc.ERROR,
          "no banana at all is an error")

    rows = mc.rows(ObjectMap(objects=ring() + chests + graph(classes={0: 1, 1: 4})),
                   lt.BANANAS)
    row = by_rid(rows)["bank_nodes"]
    check(row.status == mc.MISSING and row.label == "Bank nodes 1/4",
          "bank nodes are counted per racer (%s)" % row.label)


def test_eggs():
    print("eggs")
    heads = []
    for r in mc.RACERS:
        heads.append(obj(mc.HEAD_FOR, unk8=mc.HEAD_DROP, unk9=r))
        heads.append(obj(mc.HEAD_FOR, unk8=mc.HEAD_HOME, unk9=r))
    flags = [obj(mc.FLAG, playerIndex=r) for r in mc.RACERS]
    complete = ring() + heads + flags + [obj(mc.EGG_CREATOR), obj(mc.BALLOON)]
    rows = mc.rows(ObjectMap(objects=complete), lt.EGGS,
                   surfaces=set(mc.NEST_SURFACES) | {"SURFACE_DEFAULT"})
    check(missing(rows) == [], "a complete egg arena misses nothing (%s)" % missing(rows))
    check("ai_graph" not in by_rid(rows) and by_rid(rows)["no_graph"].status == mc.NOTE,
          "eggs asks for no AI graph and says why")

    rows = mc.rows(ObjectMap(objects=complete), lt.EGGS)
    check(by_rid(rows)["nests"].status == mc.UNKNOWN,
          "without geometry the nests are unknown, not missing")
    check(not any("SURFACE_EGG" in message for _s, message, _o, _w in
                  mc.issues(ObjectMap(objects=complete), lt.EGGS)),
          "and validation stays quiet about them")

    rows = mc.rows(ObjectMap(objects=complete), lt.EGGS,
                   surfaces={"SURFACE_EGG_01", "SURFACE_EGG_03"})
    row = by_rid(rows)["nests"]
    check(row.status == mc.MISSING and "SURFACE_EGG_02" in row.message
          and "SURFACE_EGG_04" in row.message and row.severity == mc.ERROR,
          "a racer whose nest surface is missing is named (%s)" % row.message)

    rows = mc.rows(ObjectMap(objects=ring() + [obj(mc.EGG_CREATOR)]), lt.EGGS)
    check(by_rid(rows)["head_for"].severity == mc.ERROR
          and "freezes" in by_rid(rows)["head_for"].message,
          "no kind-0 Head-For Point is an error: an egg carrier searches forever")
    check(by_rid(rows)["balloons"].severity == mc.ERROR,
          "no weapon balloon is an error in eggs: an idle bot searches forever")

    partial = ring() + heads[:6] + [obj(mc.EGG_CREATOR), obj(mc.BALLOON)]
    rows = mc.rows(ObjectMap(objects=partial), lt.EGGS)
    row = by_rid(rows)["head_for"]
    check(row.status == mc.MISSING and row.severity == mc.WARNING
          and "Racer 3" in row.message,
          "one racer without its pair is a warning naming it (%s)" % row.message)
    check(mc.missing_racers(ObjectMap(objects=partial), mc.HEAD_FOR, "unk9",
                            {"unk8": mc.HEAD_HOME}) == [3],
          "the Add button fills in only racer 3's points")


def test_hub():
    print("hub")
    entrances = [obj(mc.SETUPPOINT, racerIndex=0, entranceID=e) for e in (0, 1)]
    rows = mc.rows(ObjectMap(), lt.HUB)
    check(missing(rows) == ["entrance0", "exits"], "an empty hub misses entrance 0 "
          "and any way out (%s)" % missing(rows))
    check(not by_rid(rows)["entrance0"].reported,
          "entrance 0 is left to the start grid's own validation")

    exits = [
        obj(mc.EXIT, destinationMapId="ASSET_LEVEL_FOSSILCANYON", returnSpawnIndex=1),
        obj(mc.EXIT, returnSpawnIndex=255),
        obj(mc.EXIT, destinationMapId="ASSET_LEVEL_CENTRALAREAHUB",
            returnSpawnIndex=255, **{level_links.TRACK_FIELD: "frosty-ridge"}),
        obj(mc.EXIT, destinationMapId="ASSET_LEVEL_ANCIENTLAKE", returnSpawnIndex=4),
    ]
    rows = mc.rows(ObjectMap(objects=entrances + exits), lt.HUB)
    row = by_rid(rows)["destinations"]
    check(row.status == mc.MISSING and row.objects == [3] and row.severity == mc.ERROR,
          "the one exit with no destination is an error, and is selectable "
          "(%r)" % row.objects)
    check(by_rid(rows)["returns"].objects == [5]
          and "entrance 4" in by_rid(rows)["returns"].message,
          "an exit returning to an entrance the hub lacks is named")

    rows = mc.rows(ObjectMap(objects=entrances + [obj(mc.TAJ)]), lt.HUB)
    check({"npc_graph", "taj_course"} <= set(missing(rows)),
          "Taj asks for a graph to walk and a course to race (%s)" % missing(rows))


def test_boss():
    print("boss")
    pair = [obj(mc.SETUPPOINT, racerIndex=r, entranceID=0) for r in (0, 1)]
    checkpoints = [obj(mc.CHECKPOINT, index=i) for i in range(4)]
    rows = mc.rows(ObjectMap(objects=pair + checkpoints), lt.BOSS,
                   boss="BOSS_RACE_BUBBLER1")
    check(missing(rows) == [], "a boss race with a pair and checkpoints is ready")
    check("hovercraft" in by_rid(rows)["boss"].detail,
          "the boss row names the vehicle the race is run in")
    rows = mc.rows(ObjectMap(objects=pair), lt.BOSS, boss="NOT_A_BOSS")
    check(by_rid(rows)["boss"].status == mc.MISSING, "an unknown boss is caught")
    check(by_rid(rows)["checkpoints"].severity == mc.ERROR,
          "no checkpoints is an error in a boss race")


def test_validation_mirrors_the_checklist(catalog):
    print("validation reports what the checklist misses")
    arena = ObjectMap(objects=ring() + [obj(mc.COIN_CREATOR)])
    report = validate.validate(arena, catalog, level_key=lt.BANANAS)
    messages = [i.message for i in report.errors]
    check(any("will not move" in m for m in messages),
          "bananas without a graph: AI racers will not move")
    check(any("treasure chest" in m for m in messages),
          "bananas without chests is an error")

    report = validate.validate(ObjectMap(objects=ring()), catalog, level_key=lt.EGGS)
    check(any("Egg Creator" in i.message for i in report.errors),
          "eggs without an egg creator is an error")

    pair = [obj(mc.SETUPPOINT, racerIndex=r, entranceID=0) for r in (0, 1)]
    report = validate.validate(ObjectMap(objects=pair), catalog,
                               level_key=lt.BOSS, boss="BOSS_RACE_TRICKY1")
    errors = [i.message for i in report.errors]
    check(any("No checkpoints" in m for m in errors),
          "a boss race without checkpoints is an error")
    check(not any("no checkpoints; laps" in i.message for i in report.warnings),
          "and is not also reported as the race warning")

    hub = ObjectMap(objects=[obj(mc.SETUPPOINT, racerIndex=0, entranceID=0),
                             obj(mc.EXIT, returnSpawnIndex=255)])
    report = validate.validate(hub, catalog, level_key=lt.HUB)
    check(any("lead nowhere" in i.message for i in report.errors),
          "a hub exit with no destination is an error")
    check(not any("no destinationMapId" in i.message for i in report),
          "and is not also the generic exit warning")

    linked = ObjectMap(objects=[
        obj(mc.SETUPPOINT, racerIndex=0, entranceID=0),
        obj(mc.EXIT, destinationMapId=level_links.PLACEHOLDER_LEVEL,
            returnSpawnIndex=255, **{level_links.TRACK_FIELD: "Frosty Ridge"})])
    report = validate.validate(linked, catalog, level_key=lt.HUB)
    check(any("not a track id" in i.message for i in report.errors),
          "a custom destination that is not a track id is an error")


def test_retail_levels_pass(tree):
    print("retail levels pass their own checklist")
    import json
    failed = []
    advice = []
    seen = 0
    for level in tree.levels():
        key = lt.key_for_race_type(level.race_type)
        if not mc.applies(key):
            continue
        objects = []
        for path in level.object_maps:
            objects += gltf_io.load(path).objects
        merged = ObjectMap(objects=objects)
        if key == lt.HUB and not merged.count_of(mc.EXIT):
            continue  # the cutscene levels that borrow the hub race type
        with open(level.header_path, "r", encoding="utf-8") as handle:
            boss = json.load(handle).get("boss-race-id")
        seen += 1
        for row in mc.rows(merged, key, boss=boss):
            if row.status == mc.MISSING:
                failed.append("%s: %s" % (level.label, row.label))
            if row.status == mc.ADVISE:
                advice.append("%s: %s" % (level.label, row.label))
    check(seen >= 20, "checked %d retail challenges, hubs and boss races" % seen)
    check(advice == ["Smokey Castle: Weapon nodes on a balloon 1/3"],
          "only Smokey Castle draws weapon-node advice, for its two class-1 "
          "nodes far from any balloon (%s)" % advice)
    check(not failed, "none misses anything (%s)" % "; ".join(failed[:6]))


def main():
    catalog = catalog_module.load()
    test_applies()
    test_battle()
    test_weapon_nodes()
    test_bananas()
    test_eggs()
    test_hub()
    test_boss()
    test_validation_mirrors_the_checklist(catalog)
    tree = assets.AssetTree.discover(REPO_ROOT)
    if tree is None:
        print("SKIP: no extracted asset tree, so the retail checks did not run")
    else:
        test_retail_levels_pass(tree)
    print()
    if FAILURES:
        print("%d FAILURE(S)" % len(FAILURES))
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
