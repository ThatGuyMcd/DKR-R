#!/usr/bin/env python3
"""Stage the Mod Store backend over the deployed library API Worker.

    build.py --worker FILE --output DIR [--frontend DIR] [--backfill JSON]

FILE is the deployed library API Worker (worker-single-file.mjs), which lives
outside this repo. The launcher sign-in (launcher-auth.inl), launcher service
(launcher-service.inl) and upload integrity checks (upload-integrity.inl) are
spliced in at fixed anchors; each must match exactly once or the stage fails.
With --frontend, the portal's assets/{app,api-store,uploads-client}.js get the
shared catalogue permissions and upload checksums (frontend.py) under
FTP-PATCH/assets. --backfill pins checksums for existing catalogue downloads
(backfill_library.py writes it).

The stage holds backend/ (the spliced Worker, the original as
worker-baseline.mjs, the schema, web.config and owner tools), FTP-PATCH/ and
mod-store-receipt.json. Nothing is deployed; OWNER-SETUP.txt is the owner's
procedure.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import frontend  # noqa: E402

COPIED = ("launcher-schema.sql", "web.config", "OWNER-SETUP.txt", "release_manifest.py", "backfill_library.py")
AUTHORED = ("launcher-auth.inl", "launcher-service.inl", "upload-integrity.inl", "upload-hash.inl", "frontend.py")
FRONTEND = ("app.js", "api-store.js", "uploads-client.js")
FETCH = "export default {\n  async fetch(request, env, ctx)"
PUBLICATION = (
    "  queries.push(statement(db, `UPDATE library_items SET current_release_id=? WHERE id=? AND ${claimGate}`,"
    " [releaseId, listingId, claim]));"
)
ROUTES = (
    "    '/auth/launcher/start': ['POST', launcherStart],\n"
    "    '/auth/launcher/authorize': ['GET', launcherAuthorize],\n"
    "    '/auth/launcher/refresh': ['POST', launcherRefresh],\n"
    "    '/auth/launcher/logout': ['POST', launcherLogout],\n"
    "    '/auth/launcher/poll': ['POST', launcherPoll],\n"
    "    '/auth/launcher/cancel': ['POST', launcherCancel],\n"
)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def once(text: str, old: str, new: str) -> str:
    if text.count(old) != 1:
        raise ValueError("Mod Store input anchor changed: " + old[:120])
    return text.replace(old, new)


def backfill_prefix(path: Path | None) -> str:
    if path is None:
        return "const launcherPinnedBackfill=[];\n"
    records = json.loads(path.read_text(encoding="utf-8"))
    if records.get("schema") != 1 or len(records.get("records", [])) > 80:
        raise ValueError("Invalid backfill records")
    for row in records["records"]:
        if (
            not re.fullmatch(r"release_[A-Za-z0-9_-]{1,100}", row["id"])
            or not re.fullmatch(r"[a-f0-9]{64}", row["sha256"])
            or not 0 < row["bytes"] <= 2 * 1024**3
            or not row["url"].startswith("https://positron-portal.com/dkr-r/downloads/")
        ):
            raise ValueError("Unsafe backfill record")
    return "const launcherPinnedBackfill=" + json.dumps(records["records"], separators=(",", ":")) + ";\n"


def splice(source: str, backfill: str) -> str:
    """The deployed library API Worker with the Mod Store backend."""
    source = once(
        source,
        "      releases: versions, version: current.version || '', fileName: current.fileName || '', fileUrl: current.fileUrl || '',",
        "      currentReleaseId: row.current_release_id || null, releases: versions, version: current.version || '', fileName: current.fileName || '', fileUrl: current.fileUrl || '',",
    )
    source = once(
        source,
        "function completeAtFrontend(frontend, clientState, values) {",
        "async function completeAtFrontend(frontend, clientState, values, env) {\n"
        '  if (clientState.startsWith("dkrnative_")) return launcherComplete(frontend, clientState, values, env);',
    )
    source = source.replace(
        "completeAtFrontend(config.frontend, flow.client_state, { error })",
        "completeAtFrontend(config.frontend, flow.client_state, { error }, env)",
    )
    source = once(
        source,
        "completeAtFrontend(config.frontend, flow.client_state, { grant })",
        "completeAtFrontend(config.frontend, flow.client_state, { grant }, env)",
    )
    auth = (HERE / "launcher-auth.inl").read_text(encoding="utf-8")
    source = once(
        source, "async function startDiscord(request, env) {", auth + "\nasync function startDiscord(request, env) {"
    )
    source = once(
        source,
        "    '/auth/discord/start': ['GET', startDiscord],",
        ROUTES + "    '/auth/discord/start': ['GET', startDiscord],",
    )
    # No cross-origin privilege bypass: native requests use the same configured
    # origin plus real opaque bearer sessions and the existing role checks.
    service = backfill + (HERE / "launcher-service.inl").read_text(encoding="utf-8")
    source = once(source, FETCH, service + "\n" + FETCH)
    source = once(
        source,
        "response = await handleAuth(request, env, ctx) ||",
        "await launcherStorage(env);\n"
        "        response = await handleLauncher(request, env) || await __dkrUploads.launcherUploadIntegrity(request, env)"
        " || await handleAuth(request, env, ctx) ||",
    )
    source = once(
        source,
        "async function handleUploads(request, env, ctx, options = {}) {",
        (HERE / "upload-integrity.inl").read_text(encoding="utf-8")
        + "\nasync function handleUploads(request, env, ctx, options = {}) {",
    )
    source = once(
        source,
        "Object.freeze({UPLOADS_REVISION,UPLOAD_CHUNK_BYTES,handleUploads,listUploadedAssets,cleanupUploads})",
        "Object.freeze({UPLOADS_REVISION,UPLOAD_CHUNK_BYTES,handleUploads,listUploadedAssets,cleanupUploads,"
        "launcherUploadIntegrity,requirePreparedUpload})",
    )
    source = once(
        source,
        "  const draft = cleanDraft(input.draft || parse(source.content_json, {}), env, true);",
        "  const draft = cleanDraft(input.draft || parse(source.content_json, {}), env, true);\n"
        "  await __dkrUploads.requirePreparedUpload(db,env,draft.fileUrl);",
    )
    source = once(
        source,
        PUBLICATION,
        "  queries.push(statement(db, `INSERT INTO launcher_release_integrity(release_id,sha256,bytes,verified_at)"
        " SELECT ?,a.sha256,a.bytes,? FROM launcher_asset_integrity a WHERE a.file_url=? AND ${claimGate}"
        " ON CONFLICT(release_id) DO NOTHING`, [releaseId,stamp,draft.fileUrl,claim]));\n" + PUBLICATION,
    )
    source = once(source, "    healthRevision:", "    launcherRevision: '20261018-beta8',\n    healthRevision:")
    source = once(
        source,
        "  if (name.startsWith('admin.') || name.startsWith('catalogue.')) requireRole(user, 'admin');",
        "  if (name.startsWith('admin.') || name === 'catalogue.archive') requireRole(user, 'admin');\n"
        "  if (name.startsWith('catalogue.') && name !== 'catalogue.archive') requireRole(user, 'moderator');",
    )
    # Publish full-file checksums to both clients. Older rows remain explicitly
    # unverified until the owner registers their real file digest.
    source = once(
        source,
        "  const releases = rowsOf(batch[0]), ratings = rowsOf(batch[1]);",
        "  const releases = rowsOf(batch[0]), ratings = rowsOf(batch[1]);\n"
        "  const integrity = await all(db, `SELECT v.* FROM launcher_release_integrity v JOIN library_releases r"
        " ON r.id=v.release_id WHERE r.item_id IN (${slots})`, ids);",
    )
    return once(
        source,
        "requirements: release.requirements, install: release.install, createdAt: iso(release.created_at) }));",
        "requirements: release.requirements, install: release.install, createdAt: iso(release.created_at),\n"
        "      ...((verified) => verified ? {sha256:verified.sha256,size:verified.bytes} : {})"
        "(integrity.find(value => value.release_id === release.id)) }));",
    )


def backfill_report(path: Path) -> str:
    records = json.loads(path.read_text(encoding="utf-8"))
    report = [
        "Existing catalogue download preparation",
        "",
        str(len(records["records"])) + " valid packages have pinned checksums in this Worker.",
        str(len(records.get("failures", []))) + " addresses could not be prepared.",
        "Unavailable entries are not marked verified or silently bypassed.",
        "",
    ]
    for failure in records.get("failures", []):
        report.extend([failure["title"], failure["url"], failure["error"], ""])
    return "\n".join(report)


def stage(worker: Path, output: Path, portal: Path | None, backfill: Path | None) -> None:
    if output.exists():
        raise ValueError("Choose a NEW stage directory")
    backend = output / "backend"
    backend.mkdir(parents=True)
    shutil.copy2(worker, backend / "worker-baseline.mjs")
    # The supplied deployed bundle is authoritative. No extracted FTPS module,
    # secret, existing account or earlier backend release is overwritten.
    source = splice(worker.read_text(encoding="utf-8-sig"), backfill_prefix(backfill))
    (backend / "worker-single-file.mjs").write_text(source, encoding="utf-8", newline="\n")
    if backfill is not None:
        shutil.copy2(backfill, backend / "BACKFILL-REPORT.json")
        (backend / "DOWNLOAD-HOSTING-REPORT.txt").write_text(backfill_report(backfill), encoding="utf-8")
    for name in COPIED:
        shutil.copy2(HERE / name, backend / name)
    portal_inputs: dict[str, str] = {}
    if portal is not None:
        for name in FRONTEND:
            path = portal / "assets" / name
            portal_inputs[name] = digest(path)
            destination = output / "FTP-PATCH/assets" / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            text = frontend.transform(name, path.read_text(encoding="utf-8"))
            destination.write_text(text, encoding="utf-8", newline="\n")
    files = {p.relative_to(output).as_posix(): digest(p) for p in sorted(output.rglob("*")) if p.is_file()}
    receipt = {
        "schema": 2,
        "worker_sha256": digest(worker),
        "frontend_inputs": portal_inputs,
        "backfill_sha256": digest(backfill) if backfill is not None else None,
        "authored": {name: digest(HERE / name) for name in AUTHORED},
        "files": files,
    }
    (output / "mod-store-receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print("Mod Store backend staged; nothing deployed:", output)


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--worker", type=Path, required=True, help="the deployed library API Worker")
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--frontend", type=Path, help="a copy of the deployed portal (with assets/)")
    p.add_argument("--backfill", type=Path, help="checksums for existing catalogue downloads")
    a = p.parse_args(argv)
    try:
        stage(
            a.worker.resolve(),
            a.output.resolve(),
            a.frontend.resolve() if a.frontend else None,
            a.backfill.resolve() if a.backfill else None,
        )
    except (ValueError, OSError, KeyError) as error:
        print(f"mod-store: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
