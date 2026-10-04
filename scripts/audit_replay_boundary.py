"""Read-only direct-call audit of pipeline-generated CPU code.

This does not edit generated files and is NOT a replay-safety proof. Dynamic
dispatch and guest memory aliasing require runtime/ownership qualification.
It identifies native call boundaries the experimental adapter must account for.
"""

import argparse
import collections
import json
from pathlib import Path
import re


FUNCTION = re.compile(r"RECOMP_FUNC\s+void\s+(\w+)\s*\(")
CALL = re.compile(r"\b(\w+)\s*\(\s*rdram\s*[,)]")


def audit(directory, root):
    functions = {}
    sources = {}
    for file in sorted(directory.glob("*.c")):
        source = file.read_text(encoding="utf-8")
        matches = list(FUNCTION.finditer(source))
        for index, match in enumerate(matches):
            body = source[match.end(): matches[index + 1].start() if index + 1 < len(matches) else len(source)]
            body = re.sub(r"/\*.*?\*/|//[^\n]*", "", body, flags=re.S)
            # Function declarations inside hooks are not invocations because
            # their first parameter is a type, not the rdram variable.
            functions[match[1]] = (sorted(set(CALL.findall(body))), "LOOKUP_FUNC(" in body)
            sources[match[1]] = {"file": str(file.resolve()), "line": source.count("\n", 0, match.start()) + 1}
    if root not in functions:
        raise ValueError(f"Missing root function {root}")
    queue = collections.deque([(root, [root])])
    seen, native, indirect = set(), {}, {}
    while queue:
        name, path = queue.popleft()
        if name in seen:
            continue
        seen.add(name)
        if name not in functions:
            native[name] = {"shortest_direct_path": path, "caller": sources[path[-2]]}
            continue
        calls, dynamic = functions[name]
        if dynamic:
            indirect[name] = {"shortest_direct_path": path, "source": sources[name]}
        for call in calls:
            queue.append((call, path + [call]))
    return {
        "root": root,
        "generated_directory": str(directory.resolve()),
        "directly_reachable_guest_functions": sum(name in functions for name in seen),
        "native_boundaries": dict(sorted(native.items())),
        "indirect_dispatch": dict(sorted(indirect.items())),
        "limitations": ["Only direct calls whose first argument is rdram are followed.",
                        "Indirect calls are reported, not assumed safe or fully enumerated.",
                        "Native helper calls without rdram and shared-memory ownership need a separate audit.",
                        "Reachability includes branches, not a claim that every call runs on every frame."]
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("generated", type=Path)
    parser.add_argument("--root", default="main_game_loop")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = audit(args.generated, args.root)
    encoded = json.dumps(result, indent=2)
    if args.output:
        # This is a derived audit artifact, never a generated-code rewrite.
        args.output.write_text(encoded + "\n", encoding="utf-8")
        print(f"{args.root}: {result['directly_reachable_guest_functions']} directly reachable guest functions; "
              f"{len(result['native_boundaries'])} native boundaries; "
              f"{len(result['indirect_dispatch'])} indirect-dispatch functions")
    else:
        print(encoded)


if __name__ == "__main__":
    main()
