"""Gate for the from-scratch level header.

A track modelled from nothing has no header to inherit, and `level_header` only
ever had an encoder - it needed a *document* to encode. `level_header_template`
supplies one. The values in it are surveyed from the 65 retail headers rather
than chosen, so the checks here are about that survey still being true and the
result still being encodable.

* **The committed defaults match the retail data.** Regenerate the survey and
  require the same answer. If the decomp assets move, or a field is added to the
  layout, this fails rather than the addon quietly writing a stale header.
* **The document encodes.** 200 bytes, and the two runtime-owned offsets left at
  zero for the runtime to patch.
* **Nothing waits on geometry.** Encoding with no model, no object maps and no
  asset index has to work, because that is exactly the state a panel is in when
  an author fills the form in.
* **Choices describe the layout honestly.** Every descriptor names a real field,
  and its kind, subject and ctype are the field's own.

Run with any Python 3.8+; it does not need Blender.

    python tools/blender/tests/test_header_template.py
"""

from __future__ import annotations

import json
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(_HERE))
sys.path.insert(0, os.path.dirname(os.path.dirname(_HERE)))

from track_lab import (  # noqa: E402
    catalog, level_header, level_header_template,
)

REPO_ROOT = os.path.abspath(os.path.join(_HERE, "..", "..", ".."))


def _generator():
    """The survey script, imported rather than run, so a failure is readable."""
    sys.path.insert(0, os.path.join(REPO_ROOT, "tools", "blender"))
    import generate_header_defaults  # noqa: E402
    return generate_header_defaults


def check_defaults_match_retail():
    """The committed survey must still be what the retail headers say."""
    generator = _generator()
    paths = generator.find_headers()
    if not paths:
        return None  # no extracted assets here; other cases still apply

    documents = [generator.read(path) for path in paths]
    fixed, defaulted, assets, choices = generator.classify(
        generator.survey(documents), len(documents)
    )
    data = level_header_template.load()

    for label, fresh in (("fixed", fixed), ("defaulted", defaulted),
                         ("asset-defaults", assets)):
        stored = data.get(label, {})
        if stored != fresh:
            only_fresh = set(fresh) - set(stored)
            only_stored = set(stored) - set(fresh)
            differing = [p for p in set(fresh) & set(stored)
                         if fresh[p] != stored[p]]
            return (
                "the committed %s block no longer matches the retail headers; "
                "%d new, %d stale, %d differing (first: %s). Re-run "
                "tools/blender/generate_header_defaults.py"
                % (label, len(only_fresh), len(only_stored), len(differing),
                   sorted(only_fresh | only_stored | set(differing))[:3])
            )

    stored_choices = {entry["pointer"] for entry in data.get("choices", [])}
    if stored_choices != {entry["pointer"] for entry in choices}:
        return "the set of fields with no dominant value has changed"
    return None


def check_document_encodes():
    """The template must produce 200 bytes with no assets resolved."""
    document = level_header_template.document()
    enum_values = catalog.load().raw.get("enumValues", {})
    payload = level_header.encode(document, enum_values)

    if document.get("world") != "WORLD_CUSTOM_TRACKS" or payload[0] != 6:
        return "new tracks must default to the Custom Tracks category"
    if "/world" in level_header_template.missing():
        return "the default world must not require an explicit answer"

    if len(payload) != level_header.HEADER_SIZE:
        return "encoded %d bytes, expected %d" % (
            len(payload), level_header.HEADER_SIZE
        )
    for offset in level_header.RUNTIME_OWNED:
        if payload[offset:offset + 2] != b"\x00\x00":
            return ("offset 0x%02X is not zero; the runtime patches it and a "
                    "non-zero value would point the slot at another level"
                    % offset)
    return None


def check_overrides_apply():
    """An author's answers must reach the bytes, and a typo must not pass."""
    enum_values = catalog.load().raw.get("enumValues", {})
    world = enum_values.get("World", {})
    if not world:
        return "the catalogue carries no World enum to test against"
    name = sorted(world, key=lambda k: world[k])[-1]

    document = level_header_template.document({"/world": name, "/lap-count": 7})
    if document.get("lap-count") != 7:
        return "an override did not reach the document"

    payload = level_header.encode(document, enum_values)
    if payload[0x4B] != 7:
        return "lap-count came out as %d, not 7" % payload[0x4B]
    expected = world[name] & 0xFF
    if payload[0x00] != expected:
        return "world came out as 0x%02X, expected 0x%02X" % (payload[0x00], expected)

    try:
        level_header_template.document({"/not-a-field": 1})
    except level_header_template.TemplateError:
        pass
    else:
        return "a pointer the layout does not know was accepted"
    return None


def check_nothing_waits_on_geometry():
    """A header must encode before any geometry or object map exists."""
    document = level_header_template.document()
    for pointer in level_header_template.RUNTIME_PATCHED:
        parts = pointer.strip("/").split("/")
        current = document
        for part in parts:
            if not isinstance(current, dict) or part not in current:
                current = None
                break
            current = current[part]
        if current is not None:
            return ("%s is in the template, but the runtime patches it from the "
                    "shipped payload; setting it invites an author to think it "
                    "matters" % pointer)

    # No asset_index at all is the state a panel is in.
    enum_values = catalog.load().raw.get("enumValues", {})
    try:
        level_header.encode(document, enum_values, None)
    except Exception as error:  # noqa: BLE001
        return "encoding without an asset index raised %s: %s" % (
            type(error).__name__, error
        )
    return None


def check_choices_describe_the_layout():
    """Every descriptor must name a real field and mirror its type."""
    by_pointer = {field.pointer: field for field in level_header.LAYOUT}
    for choice in level_header_template.CHOICES:
        field = by_pointer.get(choice.pointer)
        if field is None:
            return "%s is not a field of the layout" % choice.pointer
        if (choice.kind, choice.subject, choice.ctype) != (
                field.kind, field.subject, field.ctype):
            return ("%s describes itself as %s/%s/%s but the layout says %s/%s/%s"
                    % (choice.pointer, choice.kind, choice.subject, choice.ctype,
                       field.kind, field.subject, field.ctype))
        if not choice.label:
            return "%s has no label for a panel to show" % choice.pointer

    enum_values = catalog.load().raw.get("enumValues", {})
    for choice in level_header_template.CHOICES:
        if choice.kind in ("enum", "bitfield") and choice.subject:
            if choice.subject not in enum_values:
                return ("%s wants the %s enum, which the catalogue does not "
                        "carry, so a dropdown could not be populated"
                        % (choice.pointer, choice.subject))
    return None


def check_survey_is_not_taste():
    """The eight fields with no dominant value must not be silently defaulted."""
    data = level_header_template.load()
    undecided = {entry["pointer"] for entry in data.get("choices", [])}
    described = {choice.pointer for choice in level_header_template.CHOICES}
    stray = undecided - described - set(level_header_template.RUNTIME_PATCHED)
    if stray:
        return ("%s vary across retail with no dominant value and are neither "
                "an author choice nor runtime-patched, so a from-scratch track "
                "gets one silently" % sorted(stray))
    return None


CASES = (
    ("committed defaults match retail", check_defaults_match_retail),
    ("the template encodes to 200 bytes", check_document_encodes),
    ("author answers reach the bytes", check_overrides_apply),
    ("nothing waits on geometry", check_nothing_waits_on_geometry),
    ("choices describe the layout", check_choices_describe_the_layout),
    ("no undecided field is defaulted", check_survey_is_not_taste),
)


def main():
    failures = []
    for label, case in CASES:
        try:
            problem = case()
        except Exception as error:  # noqa: BLE001
            problem = "%s: %s" % (type(error).__name__, error)
        print("%-36s %s" % (label, "PASS" if not problem else "FAIL"))
        if problem:
            print("    %s" % problem)
            failures.append(label)

    data = level_header_template.load()
    print("")
    print("fixed %d | defaulted %d | author choices %d | descriptors %d"
          % (len(data.get("fixed", {})), len(data.get("defaulted", {})),
             len(data.get("choices", [])), len(level_header_template.CHOICES)))
    if failures:
        print("%d case failure(s)" % len(failures))
        return 1
    print("a track with no ancestor can write its own header")
    return 0


if __name__ == "__main__":
    sys.exit(main())
