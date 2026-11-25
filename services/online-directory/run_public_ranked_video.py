#!/usr/bin/env python3
"""Render verified ranked runs and archive the encrypted MP4 on the existing FTPS host."""

import argparse
from contextlib import contextmanager
import ftplib
import json
import os
from pathlib import Path
import re
import ssl
import subprocess
import sys
from tempfile import TemporaryDirectory
import time
from urllib.error import HTTPError
from urllib.request import Request, build_opener

from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from ranked_track_package import origin, retrieve_track
from render_ranked_video import MAX_MOVIE_BYTES
from run_public_ranked import (
    MAX_ADVENTURE_BYTES,
    MAX_REPLAY_BYTES,
    NoRedirect,
    file_digest,
    renew_claim,
    replay_metadata,
    replay_rom_for,
    runtime_binary,
)

CHUNK = 1024 * 1024
SITE = "https://positron-portal.com"
ARCHIVE = "/htdocs/DKR-R/downloads/Replays"
USER_AGENT = "Mozilla/5.0 (compatible; DKR-R-Replay-Verifier/1)"


def archive_name(run_id, digest):
    if not re.fullmatch(r"[A-Za-z0-9_-]{24}", run_id) or not re.fullmatch(r"[a-f0-9]{64}", digest):
        raise ValueError("Invalid video archive identity.")
    return f"dkr-video-{run_id}-{digest}.bin"


def seal(movie, target, key, run_id, replay_sha, video_sha):
    cipher = AESGCM(key)
    with movie.open("rb") as source, target.open("xb") as dest:
        index = 0
        while data := source.read(CHUNK):
            nonce = os.urandom(12)
            aad = json.dumps([run_id, replay_sha, video_sha, index, len(data)], separators=(",", ":")).encode("ascii")
            dest.write(nonce)
            dest.write(cipher.encrypt(nonce, data, aad))
            index += 1


@contextmanager
def hosting(credentials):
    with ftplib.FTP_TLS(context=ssl.create_default_context(), timeout=45) as ftp:
        ftp.connect("ftp.fasthosts.co.uk", 21)
        ftp.login(credentials["user"], credentials["password"])
        ftp.prot_p()
        ftp.cwd(ARCHIVE)
        ftp.voidcmd("TYPE I")
        yield ftp


def size_on_host(ftp, name):
    try:
        return ftp.size(name)
    except ftplib.error_perm as error:
        if not str(error).startswith("550 "):
            raise
        return None


def publish(archive, credentials):
    name = archive.name
    temporary = name + "." + os.urandom(6).hex() + ".part"
    expected = archive.stat().st_size
    with hosting(credentials) as ftp:
        existing = size_on_host(ftp, name)
        if existing == expected:
            return
        if existing is not None:
            raise RuntimeError("Existing encrypted archive has the wrong length.")
        try:
            with archive.open("rb") as source:
                ftp.storbinary("STOR " + temporary, source, blocksize=256 * 1024)
            if size_on_host(ftp, temporary) != expected:
                raise RuntimeError("The encrypted upload is incomplete.")
            ftp.rename(temporary, name)
        finally:
            if size_on_host(ftp, temporary) is not None:
                ftp.delete(temporary)


def secret(path):
    text = path.read_text(encoding="ascii").strip()
    if not re.fullmatch(r"[a-f0-9]{64}", text):
        raise ValueError("Invalid video service credential.")
    return text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("runtime", "rom", "jobs", "token-file", "key-file", "ftp-file"):
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--api", required=True)
    parser.add_argument("--rom-v1-0", type=Path)
    parser.add_argument("--queue", choices=("race", "adventure"), default="race")
    parser.add_argument("--once", action="store_true")
    args = parser.parse_args()
    origin(args.api)
    api = args.api.rstrip("/") + "/v1/speedruns/"
    runtime = args.runtime.resolve(strict=True)
    rom = args.rom.resolve(strict=True)
    rom_v10 = args.rom_v1_0.resolve(strict=True) if args.rom_v1_0 else None
    stamp = file_digest(runtime_binary(runtime))
    token = secret(args.token_file)
    key = bytes.fromhex(secret(args.key_file))
    credentials = json.loads(args.ftp_file.read_text(encoding="utf-8"))
    opener = build_opener(NoRedirect())
    os.environ.pop("CREDENTIALS_DIRECTORY", None)
    args.jobs.mkdir(parents=True, exist_ok=True)

    def verifier_request(action, **body):
        payload = json.dumps({"runtime": stamp, **body}).encode()
        headers = {"User-Agent": USER_AGENT, "Content-Type": "application/json", "Authorization": "Bearer " + token}
        return Request(api + "verifier/video-" + action, data=payload, headers=headers)

    def request(action, **body):
        with opener.open(verifier_request(action, **body), timeout=70) as response:
            return json.loads(response.read(65_536))

    def work(item):
        run_id, digest = (item[name] for name in ("id", "sha256"))
        adventure = args.queue == "adventure"
        maximum = 648000 if adventure else 54210
        ticks = item["ticks"]
        if type(ticks) is not int or not 0 < ticks <= maximum:
            raise ValueError("The video queue returned an invalid replay length.")
        # Software 1080p60 can take longer than four updates per second
        # when the host is busy. Keep the existing adaptive Full Game budget.
        timeout = max(1200, ticks // 4 + 300) if adventure else max(1500, ticks // 2 + 300)
        limit = MAX_ADVENTURE_BYTES if adventure else MAX_REPLAY_BYTES
        archive_name(run_id, digest)
        with TemporaryDirectory(prefix="video-", dir=args.jobs) as temporary:
            job = Path(temporary)
            replay = job / "source.dkrreplay"
            evidence = verifier_request("evidence", id=run_id, claim=item["claim"])
            with opener.open(evidence, timeout=35) as response:
                if response.headers.get("X-DKRR-Replay-SHA256") != digest:
                    raise ValueError("The verifier returned a different replay.")
                replay.write_bytes(response.read(limit + 1))
            if replay.stat().st_size > limit or file_digest(replay) != digest:
                raise ValueError("The private replay did not match the claimed evidence.")
            replay_rom = replay_rom_for(replay_metadata(replay.read_bytes()), rom, rom_v10)
            output = job / "movie"
            render = Path(__file__).with_name("render_ranked_video.py")
            command = [
                sys.executable,
                str(render),
                "--runtime",
                str(runtime),
                "--rom",
                str(replay_rom),
                "--replay",
                str(replay),
                "--output",
                str(output),
                "--timeout",
                str(timeout),
                "--racer-credit=" + item["racerCredit"],
            ]
            if item.get("track"):
                package = retrieve_track(item["track"], job, opener, origin(SITE))
                command += ["--track-package", str(package)]
            capture_log = job / "renderer.log"
            try:
                with capture_log.open("wb") as log:
                    subprocess.run(command, check=True, timeout=timeout + 300, stdout=log, stderr=subprocess.STDOUT)
            except subprocess.SubprocessError:
                with capture_log.open("rb") as log:
                    log.seek(-min(capture_log.stat().st_size, 2048), os.SEEK_END)
                    print(log.read().decode("utf-8", errors="replace").strip(), file=sys.stderr, flush=True)
                raise
            proof = json.loads((output / "proof.json").read_text(encoding="utf-8"))
            movie = output / "verified.mp4"
            size = movie.stat().st_size
            if (
                not 0 < size <= MAX_MOVIE_BYTES
                or proof["videoSha256"] != file_digest(movie)
                or proof["replaySha256"] != digest
                or proof.get("videoRacerCredit") != item["racerCredit"]
            ):
                raise ValueError("The movie did not match the completed capture proof.")
            archive = job / archive_name(run_id, proof["videoSha256"])
            seal(movie, archive, key, run_id, digest, proof["videoSha256"])
            publish(archive, credentials)
            return size, proof

    def keep_claim(action, _limit, **body):
        return json.dumps(request(action, **body)).encode(), None

    while True:
        item = None
        try:
            queue = request("claim", queue=args.queue)
            item = queue["job"]
            for expired in queue["deletions"]:
                name = archive_name(expired["id"], expired["sha"])
                with hosting(credentials) as ftp:
                    if size_on_host(ftp, name) is not None:
                        ftp.delete(name)
                request("prune", **expired)
            if item:
                claim = {name: item[name] for name in ("id", "claim")}
                with renew_claim(keep_claim, claim):
                    size, proof = work(item)
                if request("complete", **claim, bytes=size, proof=proof) != {"ready": True}:
                    raise RuntimeError("The hosted video could not be registered.")
                print(f"Prepared verified video {claim['id']} ({size} bytes).", flush=True)
        except (ftplib.Error, ValueError, OSError, RuntimeError, subprocess.SubprocessError) as error:
            detail = f"HTTP {error.code}" if isinstance(error, HTTPError) else type(error).__name__
            print(f"Video job failed ({detail}).", file=sys.stderr, flush=True)
            if item:
                try:
                    request("fail", id=item["id"], claim=item["claim"])
                except OSError:
                    pass
            if args.once:
                return 1
        if args.once:
            return 0
        if item is None:
            time.sleep(30)


if __name__ == "__main__":
    sys.exit(main())
