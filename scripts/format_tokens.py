"""C/C++ source tokens with layout removed, to prove a pass changed layout only.

tokens(source) lexes without preprocessing: line splices are joined, comments
and whitespace dropped, string, character and raw string literals kept whole
with their prefixes, identifiers and numbers kept whole, and every other
character is its own token (so `> >` and `>>` compare equal, as clang-format
may rewrite one as the other in templates). Two sources with equal tokens
differ only in whitespace, comments, or such punctuation spacing.
"""

from __future__ import annotations

import re

RAW = re.compile(r'(?:u8|u|U|L)?R"([^ ()\\\t\v\f\n]{0,16})\(')
STRING = re.compile(r'(?:u8|u|U|L)?"(?:\\.|[^"\\\n])*"')
CHAR = re.compile(r"(?:u8|u|U|L)?'(?:\\.|[^'\\\n])+'")
WORD = re.compile(r"[A-Za-z_][A-Za-z0-9_]*|\.?[0-9](?:[0-9A-Za-z_.']|[eEpP][+-])*")
SPACE = re.compile(r"\s+")


def tokens(source: bytes) -> list[str]:
    text = source.decode("utf-8", errors="surrogateescape").replace("\r\n", "\n").replace("\\\n", "")
    out: list[str] = []
    i, n = 0, len(text)
    while i < n:
        ch = text[i]
        if ch.isspace():
            i = SPACE.match(text, i).end()  # type: ignore[union-attr]
            continue
        if text.startswith("//", i):
            end = text.find("\n", i)
            i = n if end < 0 else end
            continue
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            i = n if end < 0 else end + 2
            continue
        raw = RAW.match(text, i)
        if raw:
            close = text.find(")" + raw.group(1) + '"', raw.end())
            end = n if close < 0 else close + len(raw.group(1)) + 2
            out.append(text[i:end])
            i = end
            continue
        for pattern in (STRING, CHAR, WORD):
            match = pattern.match(text, i)
            if match:
                out.append(match.group(0))
                i = match.end()
                break
        else:
            out.append(ch)
            i += 1
    return out
