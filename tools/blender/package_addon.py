"""Build the installable ``track_lab.zip``.

Blender takes the addon either way: as an extension, which is the path Blender
4.2 and later prefer and which reads ``blender_manifest.toml``, or as a legacy
addon installed from disk, which reads ``bl_info`` in ``__init__.py``. Both
descriptors ship, so one zip covers both.

    python tools/blender/package_addon.py
    python tools/blender/package_addon.py --output dist/
"""

from __future__ import annotations

import argparse
import os
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
PACKAGE_DIR = os.path.join(HERE, "track_lab")
PACKAGE_NAME = "track_lab"

#: Never ship these.
EXCLUDE_DIRS = {"__pycache__", ".git"}
EXCLUDE_SUFFIXES = (".pyc", ".pyo", ".orig", ".rej")


def iter_files():
    for root, dirs, files in os.walk(PACKAGE_DIR):
        dirs[:] = sorted(d for d in dirs if d not in EXCLUDE_DIRS)
        for name in sorted(files):
            if name.endswith(EXCLUDE_SUFFIXES) or name.startswith("."):
                continue
            path = os.path.join(root, name)
            relative = os.path.relpath(path, PACKAGE_DIR)
            yield path, os.path.join(PACKAGE_NAME, relative).replace("\\", "/")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", default=HERE, help="directory to write the zip into")
    args = parser.parse_args(argv)

    catalog = os.path.join(PACKAGE_DIR, "data", "catalog.json")
    if not os.path.isfile(catalog):
        print("FAIL: %s is missing; run generate_catalog.py first" % catalog)
        return 1

    os.makedirs(args.output, exist_ok=True)
    target = os.path.join(args.output, PACKAGE_NAME + ".zip")

    count = 0
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=args.output, prefix=".track_lab-", suffix=".zip", delete=False) as file:
            temporary = file.name
        with zipfile.ZipFile(temporary, "w", zipfile.ZIP_DEFLATED) as archive:
            for path, arcname in iter_files():
                info = zipfile.ZipInfo(arcname, date_time=(1980, 1, 1, 0, 0, 0))
                info.create_system = 3
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o100644 << 16
                with open(path, "rb") as source:
                    archive.writestr(info, source.read())
                count += 1
        if not count:
            raise ValueError("No addon files were selected")
        with zipfile.ZipFile(temporary) as archive:
            bad_member = archive.testzip()
            if bad_member:
                raise ValueError("Corrupt addon archive member: " + bad_member)
        os.chmod(temporary, 0o644)
        os.replace(temporary, target)
    except (OSError, ValueError, zipfile.BadZipFile) as error:
        print("FAIL: could not package addon: %s" % error, file=sys.stderr)
        return 1
    finally:
        if temporary and os.path.exists(temporary):
            os.unlink(temporary)

    size = os.path.getsize(target)
    print("files  : %d" % count)
    print("size   : %.1f KiB" % (size / 1024.0))
    print("wrote  : %s" % _display_path(target))
    return 0


def _display_path(path):
    """Show a repo-relative path when it is one; Windows drives may differ."""
    try:
        return os.path.relpath(path, REPO_ROOT)
    except ValueError:
        return path


if __name__ == "__main__":
    sys.exit(main())
