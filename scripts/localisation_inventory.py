"""Audit presentation call sites without rewriting or compiling their sources."""

import argparse
import ast
import json
from pathlib import Path
import re

ARGUMENTS = {
    "PaddockText": 2,
    "PaddockHeading": 0,
    "PaddockTextAt": 4,
    "PaddockAlert": 0,
    "PaddockFeedback": 0,
    "PaddockSeparatorText": 0,
    "PaddockTextLink": 0,
    "PaddockButton": 0,
    "PaddockCheckbox": 1,
    "PaddockSectionTab": 0,
    "PaddockSearch": 3,
    "PaddockSelect": 1,
    "DrawPageHeading": 0,
    "PaddockImportButton": 0,
    "PaddockRaceButton": 0,
    "SettingsDropdown": 1,
    "SettingsCheckbox": 1,
    "SettingsNote": 0,
    "SettingsHeading": 0,
    "DetailText": 0,
    "DetailHeading": 0,
    "SettingsLabel": 0,
    "SettingsHelp": 0,
    "SettingsCheck": 1,
    "SettingsRange": 1,
    "SettingsCardTitle": 0,
    "SettingsDisclosure": 1,
    "SettingsRaceButton": 0,
    "SettingsButton": 0,
    "ControlsDisclosure": 1,
    "ControlsNote": 0,
    "ControlsTitle": 0,
    "DrawSoundVolumeRow": 1,
    "DrawSoundBandRow": 1,
    "DrawSoundChip": 0,
    "DrawSoundLinkButton": 0,
    "DetailRaceButton": 0,
    "PaddockModalText": 0,
    "StoreField": 1,
    "StoreNote": 0,
    "StoreHeading": 0,
    "StoreButton": 0,
    "StoreCombo": 0,
    "TextUnformatted": 0,
    "Text": 0,
    "TextWrapped": 0,
    "TextColored": 1,
    "SetTooltip": 0,
    "Button": 0,
    "SmallButton": 0,
    "Checkbox": 0,
    "Selectable": 0,
    "BeginPopupModal": 0,
    "RadioButton": 0,
    "OlText": 0,
    "OlHeading": 0,
    "OlParagraph": 0,
    "OlParagraphHeight": 0,
    "OlParagraphAt": 2,
    "OlButton": 0,
    "OlCta": 0,
    "OlLink": 0,
    "OlFieldLabel": 0,
    "OlSelect": 1,
    "OlRange": 1,
    "OlSwitchRow": (1, 2),
    "DrawOlFeature": (1, 2),
    "SettingsCardHead": (0, 1),
    "DrawSoundCardHead": (0, 1),
    "OlNotify": 0,
}
ENUM_ARGUMENTS = {"SettingsDropdown": 3, "PaddockSelect": 3, "OlSelect": 3, "StoreCombo": 2}
TOKEN = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[A-Za-z_]\w*|[^\s]')


def cpp_literal(token):
    def utf8_escape(match):
        raw = bytes(int(part, 16) for part in match.group().split("\\x")[1:])
        try:
            decoded = raw.decode("utf-8")
            return decoded if any(ord(char) > 127 for char in decoded) else match.group()
        except UnicodeDecodeError:
            return match.group()

    return ast.literal_eval(re.sub(r"(?:\\x[0-9a-fA-F]{2})+", utf8_escape, token))


def calls(text):
    fixed_options = []
    for match in re.finditer(r"\bconst\s+std::vector<std::string>\s+(\w+)\s*\{([^{};]*)\}", text):
        values = [token.group() for token in TOKEN.finditer(match[2])]
        if all(token == "," or token.startswith('"') for token in values):
            fixed_options.append((match[1], match.start(), values))
    tokens = [
        (match.group(), match.start()) for match in TOKEN.finditer(text) if not match.group().startswith(("//", "/*"))
    ]
    for pos, (name, offset) in enumerate(tokens[:-1]):
        if name not in ARGUMENTS or tokens[pos + 1][0] != "(":
            continue
        depth, argument, parts = 0, [], []
        for token, _ in tokens[pos + 2 :]:
            if token == ")" and depth == 0:
                parts.append(argument)
                break
            if token == "," and depth == 0:
                parts.append(argument)
                argument = []
                continue
            if token in ("(", "[", "{"):
                depth += 1
            elif token in (")", "]", "}"):
                depth -= 1
            argument.append(token)
        indices = ARGUMENTS[name]
        indices = list(indices if isinstance(indices, tuple) else (indices,))
        if name in ENUM_ARGUMENTS:
            indices.append(ENUM_ARGUMENTS[name])
        for index in indices:
            if index >= len(parts):
                continue
            argument = parts[index]
            if index == ENUM_ARGUMENTS.get(name) and len(argument) == 1:
                candidates = [values for key, start, values in fixed_options if key == argument[0] and start < offset]
                if candidates:
                    argument = ["{", *candidates[-1], "}"]
            quoted = [token for token in argument if token.startswith('"')]
            if not quoted:
                continue
            if index == ENUM_ARGUMENTS.get(name):
                # Inline choices or const vectors containing only literals.
                # Dynamic ROM, pad and mod names remain unclassified.
                if not argument or argument[0] != "{":
                    continue
                for token in quoted:
                    value = cpp_literal(token)
                    if re.search("[A-Za-z]", value) and "##" not in value:
                        yield {
                            "function": name,
                            "argument": index,
                            "line": text.count("\n", 0, offset) + 1,
                            "source": value,
                            "literal": True,
                            "enum": True,
                        }
                continue
            try:
                source = "".join(cpp_literal(token) for token in quoted)
            except (SyntaxError, ValueError):
                continue
            if "##" in source:
                source = source.split("##", 1)[0]
            if len(source.strip()) < 2 or not re.search("[A-Za-z]", source):
                continue
            yield {
                "function": name,
                "argument": index,
                "line": text.count("\n", 0, offset) + 1,
                "source": source,
                "literal": len(quoted) == len(argument),
            }


def inventory(entry, includes=()):
    pending, seen, rows = [entry.resolve()], set(), []
    while pending:
        path = pending.pop()
        if path in seen:
            continue
        seen.add(path)
        text = path.read_text(encoding="utf-8-sig")
        for row in calls(text):
            rows.append({"path": path.as_posix(), **row})
        for include in re.findall(r'^\s*#include\s+"([^"\n]+\.inl)"', text, re.M):
            child = Path(include)
            if not child.is_absolute():
                child = path.parent / child
            if not child.is_file():
                child = next((base / include for base in includes if (base / include).is_file()), child)
            if child.is_file():
                pending.append(child.resolve())
    return rows


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ui", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--include", type=Path, action="append", default=[])
    args = parser.parse_args()
    rows = inventory(args.ui, args.include)
    args.output.write_text(json.dumps(rows, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"{len(rows)} call sites; {len({row['source'] for row in rows})} distinct copy candidates")
