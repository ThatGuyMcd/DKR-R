"""Source text for tests that assert what first-party code contains.

Source(text) is the text with all whitespace removed, and every needle it is
searched with (``in``, index, find, count, startswith, endswith, split) has
its whitespace removed the same way. A test that checks a call, a guard or an
order in a source file then holds however the formatter lays the file out
(scripts/format_sources.py); only a change to the code itself fails it.
``.raw`` keeps the file as written, for tests that read literal contents.
"""

from __future__ import annotations

import re
from typing import SupportsIndex

_SPACE = re.compile(r"\s+")


def squash(text: str) -> str:
    return _SPACE.sub("", text)


class Source(str):
    raw: str

    def __new__(cls, text: str) -> Source:
        self = super().__new__(cls, squash(text))
        self.raw = text
        return self

    def __contains__(self, item: object) -> bool:
        return isinstance(item, str) and str.__contains__(self, squash(item))

    def __getitem__(self, key: SupportsIndex | slice) -> Source:
        return Source(str.__getitem__(self, key))

    def index(self, sub: str, *args: SupportsIndex | None) -> int:  # type: ignore[override]
        return str.index(self, squash(sub), *args)

    def find(self, sub: str, *args: SupportsIndex | None) -> int:  # type: ignore[override]
        return str.find(self, squash(sub), *args)

    def count(self, sub: str, *args: SupportsIndex | None) -> int:  # type: ignore[override]
        return str.count(self, squash(sub), *args)

    def startswith(self, prefix: str | tuple[str, ...], *args: SupportsIndex | None) -> bool:  # type: ignore[override]
        prefixes = tuple(squash(p) for p in prefix) if isinstance(prefix, tuple) else squash(prefix)
        return str.startswith(self, prefixes, *args)

    def endswith(self, suffix: str | tuple[str, ...], *args: SupportsIndex | None) -> bool:  # type: ignore[override]
        suffixes = tuple(squash(s) for s in suffix) if isinstance(suffix, tuple) else squash(suffix)
        return str.endswith(self, suffixes, *args)

    def split(self, sep: str | None = None, maxsplit: SupportsIndex = -1) -> list[str]:  # type: ignore[override]
        if sep is None or not squash(sep):
            return [Source(self)]
        return [Source(part) for part in str.split(self, squash(sep), maxsplit)]
