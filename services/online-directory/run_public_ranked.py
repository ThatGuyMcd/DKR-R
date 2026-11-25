#!/usr/bin/env python3
"""Poll a ranked service using a credential limited to replay verification."""

import argparse
from base64 import b64encode
from concurrent.futures import ThreadPoolExecutor
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import re
import sys
from tempfile import TemporaryDirectory
from threading import Event
import time
from urllib.error import HTTPError
from urllib.parse import urlsplit
from urllib.request import HTTPRedirectHandler, Request, build_opener
import zipfile

from ranked_track_package import origin, retrieve_track

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from verify_time_trial import (  # noqa: E402
    MAX_ADVENTURE_BYTES,
    MAX_REPLAY_BYTES,
    InvalidReplay,
    file_digest,
    replay_metadata,
    replay_rom_for,
    runtime_binary,
    verify,
)  # noqa: E402


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


@contextmanager
def renew_claim(request, job):
    finished = Event()

    def renew():
        while not finished.wait(60):
            response, _ = request("renew", 1024, **job)
            if json.loads(response) != {"renewed": True}:
                raise RuntimeError("The replay claim could not be renewed.")

    with ThreadPoolExecutor(max_workers=1) as executor:
        heartbeat = executor.submit(renew)
        try:
            yield
        finally:
            finished.set()
            heartbeat.result()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("runtime", "rom", "jobs", "token-file"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--api", required=True)
    parser.add_argument("--track-origin", help="HTTPS origin serving qualified Mod Library ZIPs.")
    parser.add_argument("--once", action="store_true")
    parser.add_argument("--queue", choices=("race", "adventure"), default="race")
    parser.add_argument("--probe", action="store_true")
    parser.add_argument(
        "--backfill-run",
        help="Prepare a ghost for one verified Time Trial without changing its status.",
    )
    parser.add_argument("--local-test", action="store_true")
    parser.add_argument("--rom-v1-0", type=Path)
    args = parser.parse_args()
    if args.backfill_run and (
        args.queue != "race" or args.probe or not re.fullmatch(r"[A-Za-z0-9_-]{24}", args.backfill_run)
    ):
        parser.error("--backfill-run requires a verified Time Trial ID.")
    address = urlsplit(args.api)
    loopback = args.local_test and address.hostname == "127.0.0.1"
    if os.name != "posix" and not loopback:
        parser.error("The deployed verifier requires Linux network isolation.")
    if (
        (not loopback and address.scheme != "https")
        or address.username
        or address.password
        or (address.path not in ("", "/") or address.query or address.fragment or not address.hostname)
    ):
        parser.error("Provide the verifier's HTTPS origin.")
    allowed_origin = None
    if args.track_origin:
        track_origin_url = urlsplit(args.track_origin)
        try:
            allowed_origin = origin(args.track_origin, args.local_test)
        except ValueError as error:
            parser.error(str(error))
        if track_origin_url.path not in ("", "/") or track_origin_url.query or track_origin_url.fragment:
            parser.error("--track-origin must contain an origin.")
    secret = args.token_file.read_text(encoding="ascii").strip()
    if not re.fullmatch(r"[a-f0-9]{64}", secret):
        parser.error("Invalid verifier credential.")
    runtime = args.runtime.resolve(strict=True)
    rom = args.rom.resolve(strict=True)
    rom_v10 = args.rom_v1_0.resolve(strict=True) if args.rom_v1_0 else None
    runtime_hash = file_digest(runtime_binary(runtime))
    args.jobs.mkdir(parents=True, exist_ok=True)
    opener = build_opener(NoRedirect())
    os.environ.pop("CREDENTIALS_DIRECTORY", None)

    def request(operation, limit=MAX_REPLAY_BYTES, **body):
        url = args.api.rstrip("/") + "/v1/speedruns/verifier/" + operation
        payload = json.dumps({"runtime": runtime_hash, **body}).encode()
        req = Request(
            url,
            data=payload,
            headers={
                "Authorization": "Bearer " + secret,
                "Content-Type": "application/json",
                "User-Agent": "Mozilla/5.0 (compatible; DKR-R-Replay-Verifier/1)",
            },
        )
        with opener.open(req, timeout=50) as response:
            data = response.read(limit + 1)
            if len(data) > limit:
                raise RuntimeError("Verifier response exceeded its allowed size.")
            return data, response.headers

    if args.probe:
        result, _ = request("probe", 1024)
        if json.loads(result) != {"ready": True}:
            raise RuntimeError("Storage probe failed.")
        print("Replay storage round trip verified.", flush=True)
        return

    while True:
        try:
            selection = {"backfill": args.backfill_run} if args.backfill_run else {}
            work, _ = request("poll", 4096, queue=args.queue, **selection)
            job = json.loads(work)["job"]
            if args.backfill_run and job is None:
                raise RuntimeError("This verified run is unavailable for backfill.")
            if job is not None:
                if not all(re.fullmatch(r"[A-Za-z0-9_-]{24}", job[name]) for name in ("id", "claim")):
                    raise RuntimeError("Invalid verifier claim.")
                claim = {name: job[name] for name in ("id", "claim")}
                if args.backfill_run:
                    if job["id"] != args.backfill_run:
                        raise RuntimeError("The verifier claimed the wrong run.")
                    claim["backfill"] = True
                track = job.get("track")
                limit = MAX_ADVENTURE_BYTES if args.queue == "adventure" else MAX_REPLAY_BYTES
                replay, headers = request("evidence", limit, **claim)
                if hashlib.sha256(replay).hexdigest() != headers.get("X-DKRR-Replay-SHA256"):
                    raise RuntimeError("Replay evidence failed its storage digest.")
                with TemporaryDirectory(prefix="run-", dir=args.jobs) as folder:
                    temp = Path(folder)
                    evidence = temp / "queued.dkrreplay"
                    evidence.write_bytes(replay)
                    metadata = replay_metadata(replay)
                    ticks = metadata.get("ticks")
                    max_ticks = 648000 if args.queue == "adventure" else 54210
                    if type(ticks) is not int or not 0 < ticks <= max_ticks:
                        raise RuntimeError("Invalid replay job length.")
                    # Software rendering on a busy host may sustain fewer updates
                    # than real-time play. Claim renewal covers the longer jobs.
                    timeout = max(480, ticks // 14 + 180)
                    try:
                        with renew_claim(request, claim):
                            track_dir = (
                                retrieve_track(track, temp, opener, allowed_origin, args.local_test) if track else None
                            )
                            proof = verify(
                                runtime,
                                replay_rom_for(metadata, rom, rom_v10),
                                evidence,
                                temp / "result",
                                timeout,
                                xvfb=os.name == "posix",
                                isolate_network=not loopback or os.name == "posix",
                                track_package=track_dir,
                                emit_ghost=args.queue == "race",
                            )
                            ghost = temp / "result" / "verified.dkrghost"
                            attachment = (
                                {"ghost": b64encode(ghost.read_bytes()).decode("ascii")} if ghost.is_file() else {}
                            )
                            if track:
                                proof["releaseSha256"] = track["sha256"]
                    except InvalidReplay:
                        if args.backfill_run:
                            raise ValueError("The replay did not reverify for backfill.") from None
                        request("finish", 2048, **claim, rejected=True)
                        print("Rejected replay", job["id"], flush=True)
                    else:
                        request("finish", 2048, **claim, proof=proof, **attachment)
                        print(
                            "Backfilled ghost" if args.backfill_run else "Verified replay",
                            job["id"],
                            proof["clockTicks"],
                            flush=True,
                        )
            if args.once or args.backfill_run:
                break
            if job is None:
                time.sleep(30)
        except HTTPError as error:
            print("Verifier request failed with HTTP", error.code, file=sys.stderr, flush=True)
            if args.once or args.backfill_run:
                raise SystemExit(1)
            time.sleep(180)
        except (OSError, ValueError, RuntimeError, zipfile.BadZipFile) as error:
            print("Verifier paused after", type(error).__name__, file=sys.stderr, flush=True)
            if args.once or args.backfill_run:
                raise SystemExit(1)
            time.sleep(180)


if __name__ == "__main__":
    main()
