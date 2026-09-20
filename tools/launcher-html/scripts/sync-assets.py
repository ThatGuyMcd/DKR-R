"""Copy the launcher's own art and recover its embedded, unmodified TTF files."""
from pathlib import Path
import re
import shutil

HERE = Path(__file__).resolve().parents[1]
ROOT = HERE.parents[1]
ASSETS = HERE / "assets"
ASSETS.mkdir(exist_ok=True)
for source, target in {
    "Backgrounds/DKR-R-Launcher-Background.png": "background.png",
    "Icons/DKR-R-Spinning-Icon.png": "logo-front.png",
    "Icons/DKR-R-Short-Logo.png": "logo-back.png",
    "Icons/DKR-R-Icon.png": "favicon.png",
}.items():
    shutil.copyfile(ROOT / "assets/ui" / source, ASSETS / target)
for name in ("racing_banana", "jumpman"):
    source = ROOT / f"runtime-recomp/src/game/generated/{name}_font.c"
    array = re.search(r"=\s*\{([^}]+)\}", source.read_text()).group(1)
    data = bytes(int(value) & 255 for value in re.findall(r"-?\d+", array))
    assert data[:4] == b"\x00\x01\x00\x00", "Expected an uncompressed TTF"
    (ASSETS / f"{name}.ttf").write_bytes(data)
print("Launcher artwork and fonts synchronized.")
