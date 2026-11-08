"""Report first-party source sizes; writes nothing."""
from pathlib import Path

text = Path("runtime-recomp/src/game/runtime_ui.cpp").read_text()
print(len(text.replace("\r\n", "\n")))
