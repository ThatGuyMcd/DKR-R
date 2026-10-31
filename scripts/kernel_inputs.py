"""Typed views of the guest-code parsers the private kernel generators share.

scripts/camera_scene_kernel.py and scripts/generate_visual_model_kernel.py
read N64Recomp output with generate_replay_probe.py's function parser and the
decomp ELF with compose_legacy_mod_policy.py's symbol reader. Those modules
aren't typed; this gives the kernels' strict typing one place to rely on.
"""

from __future__ import annotations

from collections.abc import Callable
from pathlib import Path
import re
from typing import cast

import compose_legacy_mod_policy
import generate_replay_probe

# One generated function definition (group 1 is its name).
DEF: re.Pattern[str] = generate_replay_probe.DEF
# A call site (group 1 is the callee).
CALL: re.Pattern[str] = generate_replay_probe.CALL
# Source without comments and string literals.
strip = cast(Callable[[str], str], generate_replay_probe.strip)  # pyright: ignore[reportUnknownMemberType]
# The reviewed N64Recomp recomp.h.
HEADER_HASH = cast(str, generate_replay_probe.HEADER_HASH)


def elf_symbols(path: Path, symbol_types: tuple[int, ...]) -> dict[str, set[tuple[int, int]]]:
    """Symbol name to its (address, size) entries, for the given ELF symbol types."""
    return cast(
        "dict[str, set[tuple[int, int]]]",
        compose_legacy_mod_policy.elf_functions(path, symbol_types),  # pyright: ignore[reportUnknownMemberType]
    )
