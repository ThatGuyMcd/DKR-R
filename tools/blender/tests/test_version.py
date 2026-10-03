"""Check that the addon states one version, and that the changelog has it.

Blender reads the version from ``blender_manifest.toml`` when the zip installs
as an extension and from ``bl_info`` when it installs as a legacy addon, so the
two have to agree. The Track Lab repository releases whatever version the
manifest names, with that version's ``CHANGELOG.md`` section as the notes, so
a version without a section would go out with empty notes.

    python tools/blender/tests/test_version.py
"""

from __future__ import annotations

import os
import re
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
ADDON_ROOT = os.path.dirname(_HERE)
sys.path.insert(0, ADDON_ROOT)

import track_lab  # noqa: E402

MANIFEST = os.path.join(ADDON_ROOT, "track_lab", "blender_manifest.toml")
CHANGELOG = os.path.join(ADDON_ROOT, "CHANGELOG.md")

FAILURES = []


def check(condition, message):
    if condition:
        print("  ok   %s" % message)
    else:
        print("  FAIL %s" % message)
        FAILURES.append(message)


def manifest_version():
    with open(MANIFEST, encoding="utf-8") as handle:
        match = re.search(r'^version\s*=\s*"([^"]+)"', handle.read(), re.MULTILINE)
    return match.group(1) if match else None


def changelog_versions():
    with open(CHANGELOG, encoding="utf-8") as handle:
        return re.findall(r"^## \[?(\d+\.\d+\.\d+)\]?", handle.read(), re.MULTILINE)


def main():
    version = manifest_version()
    check(version is not None, "the manifest names a version")
    check(bool(version) and re.fullmatch(r"\d+\.\d+\.\d+", version) is not None,
          "the manifest version is MAJOR.MINOR.PATCH (%s)" % version)

    from_bl_info = ".".join(str(part) for part in track_lab.bl_info["version"])
    check(from_bl_info == version,
          "bl_info says %s, the manifest says %s" % (from_bl_info, version))

    versions = changelog_versions()
    check(version in versions, "CHANGELOG.md has a section for %s" % version)
    check(bool(versions) and versions[0] == version,
          "the newest CHANGELOG.md section is the current version")

    print()
    if FAILURES:
        print("%d FAILURE(S)" % len(FAILURES))
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
