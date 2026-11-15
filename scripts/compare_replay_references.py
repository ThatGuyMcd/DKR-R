#!/usr/bin/env python3
"""Compare private DKRCOMP2 replay evidence without normalizing any bytes.

Exit codes: 0 = identical, 1 = different (including identity/layout), 2 = invalid
input or output error. These artifacts contain guest assets; JSON contains only
metadata, SHA256 identifiers, and the first differing byte values/offsets.
"""

import argparse
from contextlib import ExitStack
import hashlib
import json
import os
from pathlib import Path
import stat
import struct
import sys

MAGIC = b"DKRCOMP2"
HEADER = struct.Struct("<8s8Q")
STATE_HEADER = struct.Struct("<8s4I")
FIELDS = ("revision", "players", "state_bytes", "effect_frames", "schema", "native_bytes", "context_bytes", "cpu_bytes")
IDENTITY_FIELDS = ("revision", "players", "schema", "native_bytes", "context_bytes", "cpu_bytes")
RAM_BYTES = 16 * 1024 * 1024
CONTEXT_SLOTS = 32
MAX_STATE_BYTES = 256 * 1024 * 1024
MAX_EFFECT_BYTES = 16 * 1024 * 1024
MAX_EFFECT_FRAMES = 100_000
MAX_ARTIFACT_BYTES = 1024 * 1024 * 1024
CHUNK_BYTES = 64 * 1024


class InvalidReference(ValueError):
    """A reference is not a bounded, complete current-format artifact."""


def read_exact(stream, count, label):
    data = stream.read(count)
    if len(data) != count:
        raise InvalidReference(f"truncated {label}")
    return data


def inspect_reference(stream):
    """Validate layout with bounded reads; retain offsets, never guest payloads."""
    info = os.fstat(stream.fileno())
    if not stat.S_ISREG(info.st_mode):
        raise InvalidReference("reference must be a regular file")
    if info.st_size > MAX_ARTIFACT_BYTES:
        raise InvalidReference("artifact exceeds 1 GiB limit")
    magic, *values = HEADER.unpack(read_exact(stream, HEADER.size, "DKRCOMP2 header"))
    if magic != MAGIC:
        raise InvalidReference("expected DKRCOMP2 magic; regenerate older references")
    metadata = dict(zip(FIELDS, values))
    if metadata["revision"] not in (77, 80):
        raise InvalidReference("revision must be 77 or 80")
    if not 2 <= metadata["players"] <= 4:
        raise InvalidReference("players must be between 2 and 4")
    if not 0 < metadata["state_bytes"] <= MAX_STATE_BYTES:
        raise InvalidReference("state_bytes must be between 1 and 256 MiB")
    if metadata["effect_frames"] > MAX_EFFECT_FRAMES:
        raise InvalidReference("effect_frames exceeds 100000 limit")
    if not metadata["schema"]:
        raise InvalidReference("schema identity must be nonzero")
    # Allow declared ABI sizes to differ across inputs: report that as an
    # identity difference, while requiring each input to be self-consistent.
    if not 1 <= metadata["context_bytes"] <= 4096:
        raise InvalidReference("context_bytes must be between 1 and 4096")
    cpu_bytes = STATE_HEADER.size + RAM_BYTES + CONTEXT_SLOTS * metadata["context_bytes"]
    if metadata["cpu_bytes"] != cpu_bytes:
        raise InvalidReference("cpu_bytes does not match the declared context layout")
    if not 0 < metadata["native_bytes"] <= metadata["state_bytes"] - cpu_bytes:
        raise InvalidReference("state is missing declared CPU/native participants")
    state_end = HEADER.size + metadata["state_bytes"]
    if state_end > info.st_size:
        raise InvalidReference("truncated final checkpoint")
    state_magic, version, frame, memory_bytes, contexts = STATE_HEADER.unpack(
        read_exact(stream, STATE_HEADER.size, "DKRSTATE header")
    )
    if state_magic != b"DKRSTATE" or version != 1:
        raise InvalidReference("expected DKRSTATE format version 1")
    if memory_bytes != RAM_BYTES or contexts > CONTEXT_SLOTS:
        raise InvalidReference("invalid DKRSTATE memory/context layout")
    metadata["snapshot_version"] = version
    metadata["snapshot_frame"] = frame
    metadata["memory_bytes"] = memory_bytes
    metadata["active_contexts"] = contexts
    stream.seek(state_end)
    # Check the minimum journal framing before looping, even for zero bytes.
    if metadata["effect_frames"] > (info.st_size - state_end) // 8:
        raise InvalidReference("truncated effect length table")
    effects = []
    for index in range(metadata["effect_frames"]):
        (length,) = struct.unpack("<Q", read_exact(stream, 8, f"effect {index} length"))
        if length > MAX_EFFECT_BYTES:
            raise InvalidReference(f"effect {index} exceeds 16 MiB limit")
        offset = stream.tell()
        if length > info.st_size - offset:
            raise InvalidReference(f"truncated effect {index}")
        effects.append((offset, length))
        stream.seek(length, os.SEEK_CUR)
    if stream.tell() != info.st_size:
        raise InvalidReference("trailing data after effect journals")
    stream.seek(0)
    digest = hashlib.sha256()
    remaining = info.st_size
    while remaining:
        chunk = read_exact(stream, min(CHUNK_BYTES, remaining), "artifact while hashing")
        digest.update(chunk)
        remaining -= len(chunk)
    if stream.read(1):
        raise InvalidReference("reference grew while being read")
    after = os.fstat(stream.fileno())
    if (info.st_size, info.st_mtime_ns, info.st_ctime_ns) != (after.st_size, after.st_mtime_ns, after.st_ctime_ns):
        raise InvalidReference("reference changed while being read")
    return {"metadata": metadata, "file_bytes": info.st_size, "sha256": digest.hexdigest(), "effects": effects}


def first_byte_difference(left, right, left_offset, left_size, right_offset, right_size):
    """Compare payload bytes directly, with offsets relative to the payload."""
    left.seek(left_offset)
    right.seek(right_offset)
    common = min(left_size, right_size)
    cursor = 0
    while cursor < common:
        count = min(CHUNK_BYTES, common - cursor)
        a = read_exact(left, count, "comparison payload")
        b = read_exact(right, count, "comparison payload")
        if a != b:
            index = next(index for index, (x, y) in enumerate(zip(a, b)) if x != y)
            return {"byte_offset": cursor + index, "left_byte": a[index], "right_byte": b[index]}
        cursor += count
    if left_size != right_size:
        return {
            "byte_offset": common,
            "left_byte": read_exact(left, 1, "left payload")[0] if left_size > common else None,
            "right_byte": read_exact(right, 1, "right payload")[0] if right_size > common else None,
        }
    return None


def compare_references(left_path, right_path):
    result = {
        "format": "DKRCOMP2",
        "status": "invalid",
        "inputs": [],
        "identity_differences": [],
        "first_state_difference": None,
        "first_effect_difference": None,
        "errors": [],
    }
    with ExitStack() as stack:
        streams, references = [], []
        for side, path in (("left", left_path), ("right", right_path)):
            entry = {"side": side, "path": str(path)}
            result["inputs"].append(entry)
            try:
                stream = stack.enter_context(Path(path).open("rb"))
                reference = inspect_reference(stream)
            except (OSError, InvalidReference) as error:
                result["errors"].append({"side": side, "message": str(error)})
                continue
            streams.append(stream)
            references.append(reference)
            entry.update({key: value for key, value in reference.items() if key != "effects"})
        if result["errors"]:
            return result
        a, b = references
        for field in IDENTITY_FIELDS:
            if a["metadata"][field] != b["metadata"][field]:
                result["identity_differences"].append(
                    {"field": field, "left": a["metadata"][field], "right": b["metadata"][field]}
                )
        try:
            result["first_state_difference"] = first_byte_difference(
                *streams, HEADER.size, a["metadata"]["state_bytes"], HEADER.size, b["metadata"]["state_bytes"]
            )
            for frame in range(max(len(a["effects"]), len(b["effects"]))):
                if frame >= min(len(a["effects"]), len(b["effects"])):
                    difference = {"byte_offset": 0, "left_byte": None, "right_byte": None, "reason": "missing_frame"}
                else:
                    left_offset, left_size = a["effects"][frame]
                    right_offset, right_size = b["effects"][frame]
                    difference = first_byte_difference(*streams, left_offset, left_size, right_offset, right_size)
                if difference is not None:
                    result["first_effect_difference"] = {
                        "frame": frame,
                        **difference,
                        "left_bytes": a["effects"][frame][1] if frame < len(a["effects"]) else None,
                        "right_bytes": b["effects"][frame][1] if frame < len(b["effects"]) else None,
                    }
                    break
        except (OSError, InvalidReference) as error:
            result["errors"].append({"message": str(error)})
            return result
        result["status"] = (
            "different"
            if any(result[key] for key in ("identity_differences", "first_state_difference", "first_effect_difference"))
            else "equal"
        )
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("left", type=Path)
    parser.add_argument("right", type=Path)
    parser.add_argument(
        "--json",
        nargs="?",
        const="-",
        metavar="PATH",
        help="write a JSON report (default stdout; '-' also selects stdout)",
    )
    args = parser.parse_args(argv)
    result = compare_references(args.left, args.right)
    if args.json is not None:
        report = json.dumps(result, indent=2) + "\n"
        try:
            if args.json == "-":
                sys.stdout.write(report)
            else:
                destination = Path(args.json)
                if any(
                    destination.resolve() == path.resolve()
                    or (destination.exists() and path.exists() and destination.samefile(path))
                    for path in (args.left, args.right)
                ):
                    raise OSError("JSON output must not overwrite an input artifact")
                destination.write_text(report, encoding="utf-8")
        except OSError as error:
            print(f"invalid: cannot write JSON report: {error}", file=sys.stderr)
            return 2
    if args.json != "-":
        print(f"DKRCOMP2: {result['status']}")
        for error in result["errors"]:
            print(f"  {error.get('side', 'comparison')}: {error['message']}")
        for difference in result["identity_differences"]:
            print(f"  identity {difference['field']}: {difference['left']} != {difference['right']}")
        for key in ("first_state_difference", "first_effect_difference"):
            if result[key] is not None:
                print(f"  {key}: {json.dumps(result[key], sort_keys=True)}")
    return {"equal": 0, "different": 1, "invalid": 2}[result["status"]]


if __name__ == "__main__":
    sys.exit(main())
