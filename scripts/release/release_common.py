"""What every release step agrees on: the version, the tag, the packages.

The version is the repository's VERSION file; the tag is Version<version>
(open question 5 in the fold plan settles the scheme before B9b). Release
notes and the updater notes for a version live in docs/releases/<version>/.
"""

from __future__ import annotations

import hashlib
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
# Ed25519 public key every installed launcher trusts; never rotated implicitly.
TRUSTED_KEY = "c6673fd87da3c3c7fb4991b81476a88a316e2a4b2960bde664d7c1cffdd0bbc6"


def version() -> str:
    return (ROOT / "VERSION").read_text(encoding="utf-8").strip()


def tag(value: str | None = None) -> str:
    return "Version" + (value or version())


def release_docs(value: str | None = None) -> Path:
    """docs/releases/<version>/: RELEASE-NOTES.txt, UPDATER.txt, pages/."""
    return ROOT / "docs/releases" / (value or version())


def packages(value: str | None = None) -> dict[str, tuple[str, str, str]]:
    """Update platform -> (system, arch, package name) for a version."""
    v = value or version()
    return {
        "windows-x64": ("windows", "x86_64", f"DKR-R-{v}-Windows-x64.zip"),
        "linux-x86_64": ("linux", "x86_64", f"DKR-R-{v}-Linux-x86_64.AppImage"),
        "linux-aarch64": ("linux", "aarch64", f"DKR-R-{v}-Linux-aarch64.AppImage"),
    }


def steam_deck(value: str | None = None) -> str:
    """The Steam Deck package: an exact copy of the Linux x86_64 one."""
    return f"DKR-R-{value or version()}-SteamDeck-x86_64.AppImage"


def sha(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def contracts() -> dict[str, str]:
    """The frozen rollback contracts (netplay/legacy_identity.hpp), which the
    runtime binary embeds: revision -> contract."""
    header = ROOT / "runtime-recomp/src/game/netplay/legacy_identity.hpp"
    text = header.read_text(encoding="utf-8")
    return dict(re.findall(r'^#define DKR_LEGACY_OWNED_BUILD_ID_V(77|80) "([0-9a-f]{64})"$', text, re.M))


# The Wave Race water licence the Linux package carries beside the shaders.
WATER_LICENSE = ROOT / "runtime-recomp/src/water/LICENSE-WAVE-RACE.txt"


def identity() -> dict[str, str]:
    """Every frozen online identity constant (netplay/legacy_identity.hpp):
    the rollback contracts and the engine identity salts."""
    header = ROOT / "runtime-recomp/src/game/netplay/legacy_identity.hpp"
    text = header.read_text(encoding="utf-8")
    return dict(re.findall(r'^#define (DKR_LEGACY_\w+) "([0-9a-f]+)"$', text, re.M))
