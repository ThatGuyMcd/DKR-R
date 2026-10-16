"""Run the renderer's separately built host file_to_c tool when cross-compiling."""
import argparse
import pathlib
import subprocess
import sys


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host", type=pathlib.Path)
    parser.add_argument("target", type=pathlib.Path)
    parser.add_argument("arguments", nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    if args.target.stem != "file_to_c":
        parser.error("Only the renderer file_to_c build tool is supported")
    try:
        host = args.host.resolve(strict=True)
        return subprocess.call([str(host), *args.arguments])
    except OSError as error:
        print(f"Cannot run host shader tool {args.host}: {error}. "
              "Build the host file_to_c target before cross-compiling.", file=sys.stderr)
        return 127


if __name__ == "__main__":
    raise SystemExit(main())
