"""Generate the level-header defaults a from-scratch track starts from.

A remix inherits its header from the track it reworks. A track with no ancestor
has nothing to inherit, and something has to decide what its 200 bytes hold.
This derives that from the 65 retail headers rather than inventing it:

* a field with the same value in all 65 is **fixed** — nobody chooses it
* a field that varies but has a dominant value is **defaulted** to that value
* a field with no dominant value is an **author choice**, and is left out of the
  defaults entirely so `level_header_template` has to be given one

Run it when the decomp assets move; the output is committed so the addon does
not need an extracted asset tree to write a header.

    python tools/blender/generate_header_defaults.py

The threshold for "dominant" is deliberately high. A value that only 20 of 65
tracks share is not a default, it is the most common of several real choices,
and picking it silently would give every from-scratch track the same world and
race type without ever saying so.
"""

from __future__ import annotations

import argparse
import collections
import json
import glob
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
REPO_ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))

from track_lab import level_header  # noqa: E402

VANILLA = os.path.join(REPO_ROOT, "extern", "dkr-decomp", "assets", ".vanilla")
OUTPUT = os.path.join(HERE, "track_lab", "data",
                      "level_header_defaults.json")

#: A value has to appear in at least this many of the retail headers to be
#: treated as a default rather than as one option among several.
DOMINANT = 33


def find_headers(revision="us.v80"):
    pattern = os.path.join(VANILLA, revision, "levels", "headers", "*.json")
    return sorted(glob.glob(pattern))


def read(path):
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def lookup(document, pointer):
    """Read a JSON-pointer-ish path, following list indices as well as keys."""
    current = document
    for part in str(pointer).strip("/").split("/"):
        if isinstance(current, list):
            try:
                current = current[int(part)]
            except (ValueError, IndexError):
                return None
            continue
        if not isinstance(current, dict) or part not in current:
            return None
        current = current[part]
    return current


def survey(documents):
    """What each field holds across every retail header."""
    seen = collections.defaultdict(collections.Counter)
    for document in documents:
        for field in level_header.LAYOUT:
            value = lookup(document, field.pointer)
            seen[field.pointer][json.dumps(value, sort_keys=True)] += 1
    return seen


def _names_an_asset(pointer, value):
    """Whether writing this default would need an extracted asset tree.

    An ``asset`` field stores an index, and the extracted headers carry the
    build id instead - a string only an asset tree can resolve. Defaults like
    that are held back from the template so a track with no ancestor can write
    its header without the decomp configured; a number is already an index and
    is fine.
    """
    if not isinstance(value, str):
        return False
    for field in level_header.LAYOUT:
        if field.pointer == pointer:
            return field.kind == "asset"
    return False


def classify(seen, total):
    fixed, defaulted, assets, choices = {}, {}, {}, []
    for pointer, counts in seen.items():
        encoded, count = counts.most_common(1)[0]
        value = json.loads(encoded)
        if len(counts) == 1:
            # Absent everywhere means the field is simply not written; leaving
            # it out keeps the encoder's own zero rather than asserting one.
            if value is None:
                continue
            (assets if _names_an_asset(pointer, value) else fixed)[pointer] = value
        elif count >= DOMINANT:
            if value is None:
                continue
            (assets if _names_an_asset(pointer, value) else defaulted)[pointer] = value
        else:
            choices.append({
                "pointer": pointer,
                "distinct": len(counts),
                "most-common": value,
                "most-common-count": count,
            })
    choices.sort(key=lambda entry: entry["pointer"])
    return fixed, defaulted, assets, choices


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--revision", default="us.v80")
    parser.add_argument("--output", default=OUTPUT)
    args = parser.parse_args(argv)

    paths = find_headers(args.revision)
    if not paths:
        print("no extracted level headers under %s" % VANILLA)
        return 1

    documents = [read(path) for path in paths]
    fixed, defaulted, assets, choices = classify(survey(documents),
                                                 len(documents))

    payload = {
        "schemaVersion": 1,
        "source": {"revision": args.revision, "headers": len(documents),
                   "dominant-threshold": DOMINANT},
        "fixed": fixed,
        "defaulted": defaulted,
        "asset-defaults": assets,
        "choices": choices,
    }

    os.makedirs(os.path.dirname(args.output), exist_ok=True)
    with open(args.output, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(json.dumps(payload, indent=2, sort_keys=True) + "\n")

    print("headers surveyed : %d" % len(documents))
    print("fixed            : %d" % len(fixed))
    print("defaulted        : %d" % len(defaulted))
    print("asset defaults   : %d (held back; need an asset tree)" % len(assets))
    print("author choices   : %d" % len(choices))
    for entry in choices:
        print("    %-38s %d distinct, most common %r in %d"
              % (entry["pointer"], entry["distinct"], entry["most-common"],
                 entry["most-common-count"]))
    print("wrote            : %s" % args.output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
