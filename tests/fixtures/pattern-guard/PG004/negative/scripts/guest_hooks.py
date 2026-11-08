"""Apply declared hooks (allow-listed)."""
from pathlib import Path

text = Path("runtime-recomp/src/game/runtime_ui.cpp").read_text()
Path("out.c").write_text(text.replace("a", "b"))
