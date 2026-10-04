"""Sanity-check the validation rules against retail tracks.

A rule that rejects a shipped Diddy Kong Racing track is a wrong rule, not a
finding. This runs the validator over every extracted retail object map and
reports what it flags, so the rules can be judged against data that is known
good by definition.

Retail maps cover hubs, cutscenes and challenge levels as well as races, so the
race-specific checks are only applied to maps that actually look like races.

    python tools/blender/tests/test_validate.py
"""

from __future__ import annotations

import collections
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(_HERE))

from track_lab import catalog as catalog_module, gltf_io, validate  # noqa: E402

from test_roundtrip import REPO_ROOT, find_object_maps  # noqa: E402


def looks_like_a_race(object_map):
    """A map with a start grid and checkpoints is one people race on."""
    return bool(
        object_map.count_of(validate.SETUPPOINT)
        and object_map.count_of(validate.CHECKPOINT)
    )


def main():
    paths = find_object_maps()
    if not paths:
        print("SKIP: no extracted object maps found")
        return 0

    catalog = catalog_module.load()
    counts = collections.Counter()
    by_message = collections.Counter()
    errors = []

    for path in paths:
        object_map = gltf_io.load(path)
        report = validate.validate(
            object_map, catalog, require_racing_track=looks_like_a_race(object_map)
        )
        for issue in report:
            counts[issue.severity] += 1
            by_message[(issue.severity, issue.message.split(";")[0][:70])] += 1
            if issue.severity == validate.ERROR:
                errors.append("%s: %s" % (os.path.basename(path), issue.message))

    print("maps validated : %d" % len(paths))
    print("errors         : %d" % counts[validate.ERROR])
    print("warnings       : %d" % counts[validate.WARNING])
    print("info           : %d" % counts[validate.INFO])
    print()
    print("most common findings:")
    for (severity, message), count in by_message.most_common(12):
        if severity == validate.INFO:
            continue
        print("  %-8s x%-5d %s" % (severity, count, message))

    if errors:
        print()
        print("ERRORS on retail data (these mean a rule is wrong):")
        for line in errors[:20]:
            print("  " + line)
        return 1
    print()
    print("PASS: no rule rejects a retail track")
    return 0


if __name__ == "__main__":
    sys.exit(main())
