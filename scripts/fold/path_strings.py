"""Normalize reference file paths without changing C/C++ literal boundaries."""

from collections.abc import Callable
from pathlib import Path
import re

LITERAL = re.compile(
    r'(?:u8|u|U|L)?R"(?P<delimiter>[^ ()\\\t\v\f\n]{0,16})\(.*?\)(?P=delimiter)"'
    r'|(?:u8|u|U|L)?"(?:\\.|[^"\\\n])*"'
    r"|(?:u8|u|U|L)?'(?:\\.|[^'\\\n])*'",
    re.S,
)


def normalize(text: str, resolve: Callable[[Path], Path | None]) -> str:
    """Resolve complete, unescaped absolute-path literals only.

    Consuming every literal first prevents a closing quote followed by a
    filesystem division operator from being mistaken for a new path string.
    Raw strings and escaped strings keep their exact content.
    """

    def literal(match: re.Match[str]) -> str:
        value = match.group(0)
        if not value.startswith('"/') or "\\" in value:
            return value
        path = Path(value[1:-1])
        resolved = resolve(path)
        return f'"{resolved}"' if resolved is not None else value

    return LITERAL.sub(literal, text)
