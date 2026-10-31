"""Prepare pinned checksums for existing public packages. No remote writes."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import time
import urllib.parse
import urllib.request

API = "https://dkr-mod-library-api.ross-macgregorbars.workers.dev"
PREFIX = "https://positron-portal.com/dkr-r/downloads/"
MAGIC = {
    ".zip": b"PK",
    ".rtz": b"PK",
    ".dkrmap": b"PK",
    ".nrm": b"PK",
    ".dkrcode": b"PK",
    ".xdelta": b"\xd6\xc3\xc4",
    ".bps": b"BPS1",
    ".ips": b"PATCH",
    ".7z": b"7z\xbc\xaf\x27\x1c",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--records", type=Path, required=True)
    parser.add_argument("--cache", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists() or args.records.exists() or args.cache.exists():
        raise ValueError("Choose NEW output files and a NEW cache directory")
    args.cache.mkdir(parents=True)
    items, cursors, cursor = [], set(), ""
    for _ in range(100):
        request = urllib.request.Request(
            API + "/v1/catalogue" + ("?cursor=" + urllib.parse.quote(cursor, safe="") if cursor else ""),
            headers={"User-Agent": "DKR-R-ModStore/1.0.7-beta8", "Accept": "application/json"},
        )
        with urllib.request.urlopen(request, timeout=25) as response:
            raw = response.read(4 * 1024 * 1024 + 1)
        if len(raw) > 4 * 1024 * 1024:
            raise ValueError("Catalogue page too large")
        page = json.loads(raw)
        items.extend(page["items"])
        cursor = page.get("nextCursor")
        if not cursor:
            break
        if cursor in cursors:
            raise ValueError("Invalid catalogue pagination")
        cursors.add(cursor)
    else:
        raise ValueError("Catalogue pagination limit exceeded")
    records, failures, seen = [], [], set()
    for item in items:
        for release in item.get("releases", []):
            identity, url = release["id"], release["fileUrl"]
            if identity in seen:
                continue
            seen.add(identity)
            try:
                if (
                    not re.fullmatch(r"release_[A-Za-z0-9_-]{1,100}", identity)
                    or not url.startswith(PREFIX)
                    or any(c in url for c in "\\?#\r\n")
                ):
                    raise ValueError("Unapproved release identity or address")
                extension = Path(urllib.parse.unquote(urllib.parse.urlsplit(url).path)).suffix.lower()
                if extension not in MAGIC:
                    raise ValueError("Unsupported package extension")
                digest, total, head = hashlib.sha256(), 0, b""
                with urllib.request.urlopen(
                    urllib.request.Request(url, headers={"User-Agent": "DKR-R-ModStore/1.0.7-beta8"}), timeout=60
                ) as response:
                    content_type = response.headers.get("Content-Type", "").lower()
                    if (
                        response.status != 200
                        or response.geturl() != url
                        or "html" in content_type
                        or content_type.startswith("text/")
                    ):
                        raise ValueError("Address returned an error page or redirect, not a package")
                    with (args.cache / (identity + extension)).open("xb") as target:
                        while chunk := response.read(256 * 1024):
                            if not head:
                                head = chunk[:64]
                                if not head.startswith(MAGIC[extension]):
                                    raise ValueError("Contents do not match the package format")
                            total += len(chunk)
                            if total > 2 * 1024**3:
                                raise ValueError("Package exceeds 2 GiB")
                            digest.update(chunk)
                            target.write(chunk)
                if not total:
                    raise ValueError("Empty package")
                sha256 = digest.hexdigest()
                if release.get("sha256") and release["sha256"] != sha256:
                    raise ValueError("File changed since its published checksum; do not overwrite that checksum")
                records.append(
                    {
                        "id": identity,
                        "url": url,
                        "sha256": sha256,
                        "bytes": total,
                        "version": release.get("version", ""),
                        "verifiedAt": int(time.time()),
                    }
                )
                print("Prepared:", item["title"], total, flush=True)
            except Exception as error:
                failures.append({"id": identity, "title": item["title"], "url": url, "error": str(error)})
                print("Not prepared:", item["title"], str(error), flush=True)

    def sql(value):
        return "'" + str(value).replace("'", "''") + "'"

    statements = []
    for row in records:
        statements.append(
            "INSERT INTO launcher_release_integrity(release_id,sha256,bytes,verified_at) SELECT id,%s,%d,%d FROM library_releases WHERE id=%s AND file_url=%s AND version=%s ON CONFLICT(release_id) DO NOTHING;"
            % (
                sql(row["sha256"]),
                row["bytes"],
                row["verifiedAt"],
                sql(row["id"]),
                sql(row["url"]),
                sql(row["version"]),
            )
        )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(statements) + "\n", encoding="utf-8")
    args.records.write_text(
        json.dumps({"schema": 1, "records": records, "failures": failures}, indent=2) + "\n", encoding="utf-8"
    )
    print(f"Prepared {len(records)} pinned records; {len(failures)} unavailable. No remote write or deployment.")


if __name__ == "__main__":
    main()
