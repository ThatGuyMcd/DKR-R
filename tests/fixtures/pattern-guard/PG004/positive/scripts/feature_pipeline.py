"""Stage a rewritten copy of a first-party source."""
from pathlib import Path
import sys


def once(text, old, new):
    if text.count(old) != 1:
        raise SystemExit(f"expected one {old!r}")
    return text.replace(old, new)


source = Path("runtime-recomp/src/game/runtime_ui.cpp").read_text()
(Path(sys.argv[1]) / "runtime_ui.cpp").write_text(once(source, "int sidebar = 1;", "int sidebar = 2;"))
