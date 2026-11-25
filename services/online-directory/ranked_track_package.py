"""Retrieve one integrity-pinned native track from a published release ZIP."""

import hashlib
import json
import re
import shutil
import stat
from urllib.parse import urlsplit
import zipfile

MAX_PACKAGE_BYTES = 256 * 1024 * 1024
MAX_TRACK_BYTES = 192 * 1024 * 1024
MAX_FILES = 4096
CHUNK = 1024 * 1024


def origin(url, allow_loopback=False):
    parsed = urlsplit(url)
    if (
        parsed.scheme != "https"
        and not (allow_loopback and parsed.scheme == "http" and parsed.hostname == "127.0.0.1")
        or not parsed.hostname
        or parsed.username is not None
        or parsed.password is not None
    ):
        raise ValueError("Invalid track archive origin.")
    return parsed.scheme, parsed.hostname, parsed.port or (443 if parsed.scheme == "https" else 80)


def safe_members(archive):
    members = {}
    folded = set()
    if len(archive.infolist()) > MAX_FILES:
        raise ValueError("Track archive contains too many entries.")
    for info in archive.infolist():
        raw = info.orig_filename.removesuffix("/")
        parts = tuple(raw.split("/"))
        key = raw.casefold()
        if (
            not raw
            or any(
                part in ("", ".", "..") or part.endswith((" ", ".")) or ":" in part or "\\" in part or "\0" in part
                for part in parts
            )
            or key in folded
            or stat.S_ISLNK(info.external_attr >> 16)
            or info.flag_bits & 1
        ):
            raise ValueError("Unsafe track archive entry.")
        folded.add(key)
        if not info.is_dir():
            members[parts] = info
    return members


def unpack_track(path, directory, track_id):
    with zipfile.ZipFile(path) as archive:
        members = safe_members(archive)
        sources = [
            parts
            for parts in members
            if parts[-1] == "manifest.json" and (len(parts) == 1 or parts[-2].lower().endswith(".dkrmap"))
        ]
        matched = []
        for parts in sources:
            info = members[parts]
            if info.file_size > CHUNK:
                raise ValueError("Track manifest exceeds its size limit.")
            manifest = json.loads(archive.read(info))
            if isinstance(manifest, dict) and manifest.get("id") == track_id:
                matched.append(parts[:-1])
        if len(matched) != 1:
            raise ValueError("The published archive needs one matching native track.")
        prefix = matched[0]
        files = {parts[len(prefix) :]: info for parts, info in members.items() if parts[: len(prefix)] == prefix}
        if sum(info.file_size for info in files.values()) > MAX_TRACK_BYTES:
            raise ValueError("Expanded native track exceeds its size limit.")
        directory.mkdir()
        for parts, info in files.items():
            output = directory.joinpath(*parts)
            output.parent.mkdir(parents=True, exist_ok=True)
            with archive.open(info) as source, output.open("xb") as target:
                shutil.copyfileobj(source, target, CHUNK)
    return directory


def retrieve_track(descriptor, directory, opener, allowed_origin, allow_loopback=False):
    """Download by trusted release metadata and validate bytes before extraction."""
    url, sha, size = (descriptor.get(key) for key in ("url", "sha256", "bytes"))
    track = descriptor.get("trackId")
    content = descriptor.get("trackHash")
    if (
        not isinstance(url, str)
        or not isinstance(sha, str)
        or not re.fullmatch(r"[a-f0-9]{64}", sha)
        or type(size) is not int
        or not 0 < size <= MAX_PACKAGE_BYTES
        or not isinstance(track, str)
        or not re.fullmatch(r"[A-Za-z0-9_-]{1,96}", track)
        or not isinstance(content, str)
        or not re.fullmatch(r"[a-f0-9]{64}", content)
    ):
        raise ValueError("Incomplete custom track qualification.")
    parsed = urlsplit(url)
    if allowed_origin is None or origin(url, allow_loopback) != allowed_origin or parsed.fragment:
        raise ValueError("The published track archive is outside the configured origin.")

    archive = directory / "release.zip"
    digest = hashlib.sha256()
    total = 0
    with opener.open(url, timeout=45) as source, archive.open("xb") as target:
        while data := source.read(CHUNK):
            total += len(data)
            if total > size:
                raise ValueError("Published archive exceeds its qualified size.")
            digest.update(data)
            target.write(data)
    if total != size or digest.hexdigest() != sha:
        raise ValueError("Published archive differs from the qualified release.")
    return unpack_track(archive, directory / "qualified.dkrmap", track)
