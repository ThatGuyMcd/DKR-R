#!/usr/bin/env python3
"""Compare the `[check][state]` lines of two or more rollback self-test logs.

Each peer of a `--self-test-owned-*` run logs, every N confirmed frames,

    [check][state] epoch=92 frame=300 hash=7DC1FBCE66916E54 us=812

(`DKR_OWNED_CHECK_STATE_EVERY`, default 30). This reads the runtime.log of each
peer and compares the hashes at the (epoch, frame) points they share. The game
itself does not cross-check state between peers mid-race; this is that check.

Exit codes: 0 = every shared frame equal, 1 = a mismatch, 2 = unusable input
(unreadable file, no state lines, no shared frames, or fewer than --minimum
frames compared).
"""

import argparse
from pathlib import Path
import re
import sys

LINE = re.compile(r"\[check\]\[state\] epoch=(\d+) frame=(\d+) hash=([0-9A-Fa-f]{16})(?:\s|$)")
DEFAULT_MINIMUM = 20


def read_states(path):
    """{(epoch, frame): hash} for one log. A repeated point must agree with itself."""
    states = {}
    with open(path, encoding="utf-8", errors="replace") as stream:
        for line in stream:
            match = LINE.search(line)
            if not match:
                continue
            key = (int(match.group(1)), int(match.group(2)))
            value = match.group(3).upper()
            if states.setdefault(key, value) != value:
                raise ValueError(f"{path}: epoch {key[0]} frame {key[1]} logged with two different hashes")
    return states


def compare(logs, minimum):
    """(exit code, report lines) for named state maps."""
    names = list(logs)
    for name in names:
        if not logs[name]:
            return 2, [f"{name}: no [check][state] lines"]
    shared = set.intersection(*(set(states) for states in logs.values()))
    lines = [f"{name}: {len(states)} samples" for name, states in logs.items()]
    if not shared:
        return 2, lines + ["no (epoch, frame) points in common"]
    points = sorted(shared)
    lines.append(f"compared {len(points)} frames, epochs {sorted({epoch for epoch, _ in points})}")
    for key in points:
        values = {name: logs[name][key] for name in names}
        if len(set(values.values())) > 1:
            lines.append(f"MISMATCH at epoch {key[0]} frame {key[1]}")
            lines += [f"  {name}: {value}" for name, value in values.items()]
            return 1, lines
    if len(points) < minimum:
        return 2, lines + [f"only {len(points)} frames compared, need at least {minimum} to call it equal"]
    lines.append(f"EQUAL, last frame epoch {points[-1][0]} frame {points[-1][1]}")
    return 0, lines


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("logs", nargs="+", type=Path, help="two or more runtime.log files")
    parser.add_argument("--minimum", type=int, default=DEFAULT_MINIMUM, help="fewest shared frames that count as equal")
    arguments = parser.parse_args(argv)
    if len(arguments.logs) < 2:
        print("need at least two logs", file=sys.stderr)
        return 2
    if len({str(path) for path in arguments.logs}) != len(arguments.logs):
        print("the same log was named twice", file=sys.stderr)
        return 2
    try:
        logs = {str(path): read_states(path) for path in arguments.logs}
    except (OSError, ValueError) as error:
        print(f"compare_state_hashes: {error}", file=sys.stderr)
        return 2
    code, lines = compare(logs, arguments.minimum)
    print("\n".join(lines))
    return code


if __name__ == "__main__":
    sys.exit(main())
