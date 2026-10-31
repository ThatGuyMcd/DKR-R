"""Bundle the maintained API modules for owner deployment; never deploy them."""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

AUTH = Path(__file__).resolve().parent
ROOT = AUTH.parents[1]
API = AUTH / "api"


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def compose(output, esbuild, rollback_api=None, rollback_directory=None):
    output = output.resolve()
    if output.exists():
        raise ValueError("Choose a fresh owner bundle directory")
    if not esbuild or not esbuild.is_file():
        raise ValueError("Pass --esbuild with the esbuild 0.28.2 executable")
    version = subprocess.check_output([str(esbuild), "--version"], text=True).strip()
    if version != "0.28.2":
        raise ValueError("Use the pinned esbuild 0.28.2 bundler")
    output.mkdir(parents=True)
    for name in ("PRIVATE-API", "ONLINE-WORKER", "ROLLBACK"):
        (output / name).mkdir()
    # Ordinary ES modules are the source of truth. Bundling does not patch a
    # frozen Worker, rewrite authored source, or run a deployed service.
    subprocess.run(
        [
            str(esbuild),
            str(API / "worker.mjs"),
            "--bundle",
            "--format=esm",
            "--platform=neutral",
            "--target=es2022",
            "--legal-comments=inline",
            "--external:cloudflare:*",
            "--outfile=" + str(output / "PRIVATE-API/worker-single-file.mjs"),
            "--metafile=" + str(output / "API-BUILD-INPUTS.json"),
        ],
        check=True,
        cwd=ROOT,
    )
    shutil.copy2(API / "discord_schema.sql", output / "PRIVATE-API/001-launcher-discord-profiles.sql")
    shutil.copy2(API / "profile_schema.sql", output / "PRIVATE-API/002-launcher-profiles.sql")
    for name in ("directory.mjs", "wrangler-online.jsonc"):
        shutil.copy2(AUTH / name, output / "ONLINE-WORKER" / name)
    for source, name in ((rollback_api, "worker-single-file.mjs"), (rollback_directory, "directory.mjs")):
        if source:
            if not source.is_file():
                raise ValueError("Rollback source is missing: " + str(source))
            shutil.copy2(source, output / "ROLLBACK" / name)
    (output / "DISCORD-PROFILE-DEPLOYMENT.txt").write_text(
        """Friends and profile pictures update

1. Back up the currently deployed private API Worker, online-directory Worker
   and D1 database. Keep these backups for rollback.
2. Run PRIVATE-API/001-launcher-discord-profiles.sql, then 002-launcher-profiles.sql
   against the existing private API D1 database. Both migrations are idempotent.
3. Update the code of the existing online-directory Worker with
   ONLINE-WORKER/directory.mjs. Keep its bindings, Durable Object migration and
   current ONLINE_ENABLED and RUNNING_SPECTATORS_ENABLED values.
4. Update the existing private API Worker with PRIVATE-API/worker-single-file.mjs.
   Keep all current bindings, variables and secrets. No new service binding is needed.
5. Use the matching test launchers. Sign out and back in with Discord once to
   populate its name and image metadata. The launcher offers separate choices.
6. Online Profile now lets you choose and preview a PNG or JPG, select Discord's
   image, or use initials. Save Picture applies the picture and sharing choice.

No FTP website files are needed. The current mod upload method is unchanged.
The API stores one small avatar per account and a separate verified friend link
for each launcher identity. Original image files and metadata are not uploaded.
Friends and lobby players only receive the selected picture when sharing is on.
Without the API update, existing online play remains available and profiles explain
why picture import is unavailable. Gameplay transport and rollback are unchanged.

For rollback, restore the private API and online-directory code from your backups
or the supplied ROLLBACK files. The added tables can remain. Keep current bindings
and feature settings. A new installation still needs the existing directory setup.
""",
        encoding="utf-8",
    )
    docs = [AUTH / name for name in ("DEPLOYMENT.md", "PLAYTEST.txt", "IMPLEMENTATION.txt", "QA.md")]
    for path in docs:
        shutil.copy2(path, output / path.name)
    sources = [
        *sorted(API.glob("*")),
        AUTH / "directory.mjs",
        AUTH / "wrangler-online.jsonc",
        *docs,
        AUTH.parent / "test-tools/package-lock.json",
    ]
    receipt = {
        "schema": 2,
        "feature": "dkrr-friends-profile-pictures",
        "published": False,
        "bundler": {"version": version, "sha256": sha(esbuild.read_bytes())},
        "authoring_sha256": {p.relative_to(ROOT).as_posix(): sha(p.read_bytes()) for p in sources if p.is_file()},
        "files": {
            p.relative_to(output).as_posix(): sha(p.read_bytes()) for p in sorted(output.rglob("*")) if p.is_file()
        },
    }
    (output / "BUNDLE-RECEIPT.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print("Owner API bundle composed; no FTP, database or service deployment performed:", output)


def verify(output):
    receipt = json.loads((output / "BUNDLE-RECEIPT.json").read_text(encoding="utf-8"))
    for name, digest in receipt["files"].items():
        if sha((output / name).read_bytes()) != digest:
            raise ValueError("Bundle file changed: " + name)
    if receipt["published"]:
        raise ValueError("Unexpected deployment claim")
    print("Owner bundle hashes verified.")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--esbuild", type=Path, default=AUTH.parent / "test-tools/node_modules/.bin/esbuild")
    parser.add_argument("--rollback-api", type=Path)
    parser.add_argument("--rollback-directory", type=Path)
    parser.add_argument("--archive", type=Path)
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args(argv)
    try:
        if not args.verify:
            compose(args.output, args.esbuild.resolve(), args.rollback_api, args.rollback_directory)
        verify(args.output)
        if args.archive:
            if args.archive.exists():
                raise ValueError("Choose a fresh bundle archive")
            args.archive.parent.mkdir(parents=True, exist_ok=True)
            with zipfile.ZipFile(args.archive, "x", zipfile.ZIP_DEFLATED) as archive:
                for path in sorted(args.output.rglob("*")):
                    if path.is_file():
                        archive.write(path, path.relative_to(args.output).as_posix())
            print("Owner bundle archive:", args.archive, sha(args.archive.read_bytes()))
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f"online-directory: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
