"""Validate UTF-8 catalogues and embed immutable UI resources, never sources."""

import argparse
import hashlib
import json
from pathlib import Path
import re


def placeholders(text):
    return sorted(re.findall(r"\{([a-z][a-z0-9_]*)\}", text))


PRINTF = r"%(?:\d+\$)?[-+ #0]*(?:\*|\d+)?(?:\.(?:\*|\d+))?(?:hh|ll|[hlzjtL])?[diuoxXfFeEgGaAcCsSpn%]"


def valid_placeholders(value):
    return not re.search(r"[{}]", re.sub(r"\{[a-z][a-z0-9_]*\}", "", value))


def printf_contract(value):
    tokens = re.findall(PRINTF, value)
    if any(token.endswith("n") for token in tokens) or sum(token.count("%") for token in tokens) != value.count("%"):
        raise ValueError("Invalid printf contract")
    return tokens


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("Duplicate catalogue property: " + key)
        result[key] = value
    return result


def valid_text(value):
    return (
        isinstance(value, str)
        and bool(value)
        and len(value.encode("utf-8")) <= 8192
        and not any(ord(char) < 32 and char not in "\n\t" or 0x7F <= ord(char) <= 0x9F for char in value)
    )


def load(root):
    packs = []
    for path in sorted(root.glob("*.json")):
        pack = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique_object)
        if pack["code"] != path.stem or not re.fullmatch(r"[a-z]{2}(?:-[A-Z]{2})?", pack["code"]):
            raise ValueError("Catalogue code must match its canonical filename: " + str(path))
        if not valid_text(pack["name"]):
            raise ValueError("Invalid native language name: " + str(path))
        packs.append(pack)
    packs.sort(key=lambda pack: (pack["code"] != "en", pack["code"]))
    if not packs or packs[0]["code"] != "en":
        raise ValueError("English catalogue is required")
    english = packs[0]["messages"]
    if not english:
        raise ValueError("Empty English catalogue")
    aliases = {}
    for key, message in english.items():
        if not re.fullmatch(r"[a-z][a-z0-9_.]*", key) or not message["context"]:
            raise ValueError("Every message needs a stable key and translator context: " + key)
        source = message["text"]
        if not valid_text(source) or "###" in source:
            raise ValueError("Message contains an ID or NUL: " + key)
        if not valid_placeholders(source):
            raise ValueError("Malformed named placeholder: " + key)
        if message.get("format") == "printf":
            printf_contract(source)
        if message.get("alias", True):
            if source in aliases:
                raise ValueError("Ambiguous English alias: " + source)
            aliases[source] = key
            for alternative in message.get("aliases", []):
                if not valid_text(alternative) or alternative in aliases:
                    raise ValueError("Invalid or ambiguous source alias: " + alternative)
                if message.get("format") == "printf" and printf_contract(alternative) != printf_contract(source):
                    raise ValueError("Changed alias printf contract: " + key)
                aliases[alternative] = key
    # Existing sign captions use uppercase while prose uses sentence case.
    # Store aliases in resources; never case-fold user text at runtime.
    for source, key in list(aliases.items()):
        if english[key].get("format") == "printf":
            continue
        for variant in (source.upper(), source.lower(), source.title(), source.capitalize()):
            aliases.setdefault(variant, key)
    for pack in packs[1:]:
        missing = set(english) - set(pack["messages"])
        extra = set(pack["messages"]) - set(english)
        if missing or extra:
            raise ValueError(f"{pack['code']}: missing={sorted(missing)}, extra={sorted(extra)}")
        for key, value in pack["messages"].items():
            if not valid_text(value) or "##" in value:
                raise ValueError(f"{pack['code']}: invalid message {key}")
            if not valid_placeholders(value):
                raise ValueError(f"{pack['code']}: malformed named placeholder {key}")
            if pack["code"] not in ("ru", "uk") and re.search(r"[\u0400-\u052f]", value):
                raise ValueError(f"{pack['code']}: unexpected Cyrillic in Latin translation {key}")
            source = english[key]["text"]
            if placeholders(source) != placeholders(value):
                raise ValueError(f"{pack['code']}: changed placeholders {key}")
            if english[key].get("format") == "printf":
                # Include %n and wide arguments so additions cannot evade the
                # check. No positional reordering across the legacy ABI.
                if printf_contract(source) != printf_contract(value):
                    raise ValueError(f"{pack['code']}: changed printf contract {key}")
    return packs, aliases


def embed(root, output):
    packs, aliases = load(root)

    def quote(text):
        return json.dumps(text, ensure_ascii=False)

    def view(text):
        # A sized literal: building a string_view from a bare literal makes the
        # compiler count every character at compile time, and the whole table
        # then runs past clang's constexpr step limit with libstdc++.
        return quote(text) + "sv"

    lines = [
        "// Generated resource bytes; edit the UTF-8 catalogues, not this file.",
        "#pragma once",
        "#include <array>",
        "namespace dkr::localisation::data {",
        "using namespace std::string_view_literals;",
        f"inline constexpr std::array<Language,{len(packs)}> languages{{{{",
    ]
    lines += [f"{{{quote(pack['code'])},{quote(pack['name'])}}}," for pack in packs]
    lines += [
        "}};",
        "struct Message { std::string_view id; bool printf_format; "
        f"std::array<std::string_view,{len(packs)}> values; }};",
        f"inline constexpr std::array<Message,{len(packs[0]['messages'])}> messages{{{{",
    ]
    for key, message in sorted(packs[0]["messages"].items()):
        values = [message["text"]] + [pack["messages"][key] for pack in packs[1:]]
        flag = "true" if message.get("format") == "printf" else "false"
        lines.append("{" + view(key) + "," + flag + ",{{" + ",".join(map(view, values)) + "}}},")
    lines += [
        "}};",
        "struct Alias { std::string_view english,id; };",
        f"inline constexpr std::array<Alias,{len(aliases)}> aliases{{{{",
    ]
    lines += ["{" + view(source) + "," + view(key) + "}," for source, key in sorted(aliases.items())]
    lines += ["}};"]
    characters = set(range(0x20, 0x7F)) | set(range(0xA0, 0x100))
    for pack in packs:
        values = [pack["name"]] + [
            item["text"] if isinstance(item, dict) else item for item in pack["messages"].values()
        ]
        for value in values:
            characters.update(ord(char) for char in value + value.upper() + value.lower() if ord(char) >= 0x20)
    if any(char > 0xFFFF for char in characters):
        raise ValueError("Initial font group must fit the pinned ImGui BMP glyph type")
    ranges = []
    for char in sorted(characters):
        if ranges and ranges[-1] == char - 1:
            ranges[-1] = char
        else:
            ranges.extend([char, char])
    ranges.append(0)
    lines.append(
        f"inline constexpr std::array<std::uint16_t,{len(ranges)}> glyph_ranges{{{{"
        + ",".join(map(str, ranges))
        + "}};"
    )
    font = (root / "NotoSans-Regular.ttf").read_bytes()
    if font[:4] != b"\0\1\0\0":
        raise ValueError("Expected a TrueType fallback font")
    lines.append(f"inline constexpr std::array<unsigned char,{len(font)}> font{{{{")
    lines += [",".join(map(str, font[pos : pos + 64])) + "," for pos in range(0, len(font), 64)]
    lines += ["}};", "} // namespace dkr::localisation::data", ""]
    data = "\n".join(lines).encode("utf-8")
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.is_file() or output.read_bytes() != data:
        output.write_bytes(data)
    print(
        f"Embedded {len(packs)} languages, {len(packs[0]['messages'])} messages, "
        f"{len(aliases)} aliases; font {hashlib.sha256(font).hexdigest()}"
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--resources", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.output:
        embed(args.resources, args.output)
    else:
        packs, aliases = load(args.resources)
        print(f"{len(packs)} catalogues with matching keys; {len(aliases)} source aliases")
