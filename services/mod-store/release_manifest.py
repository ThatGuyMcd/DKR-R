"""Read public release files and emit reviewed additive D1 integrity records."""

import argparse
import hashlib
import json
from pathlib import Path
import time
import urllib.parse
import urllib.request

API = "https://dkr-mod-library-api.ross-macgregorbars.workers.dev"
PREFIX = "https://positron-portal.com/dkr-r/downloads/"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise ValueError("Choose a new output file")
    items = []
    cursor = ""
    for _ in range(100):
        url = API + "/v1/catalogue" + ("?cursor=" + urllib.parse.quote(cursor, safe="") if cursor else "")
        with urllib.request.urlopen(url, timeout=25) as response:
            raw = response.read(4 * 1024 * 1024 + 1)
            if len(raw) > 4 * 1024 * 1024:
                raise ValueError("Catalogue page too large")
            page = json.loads(raw)
        items.extend(page["items"])
        cursor = page.get("nextCursor")
        if not cursor:
            break
    else:
        raise ValueError("Catalogue pagination limit exceeded")
    statements = []
    seen = set()
    for item in items:
        for release in item.get("releases", []):
            identity = release["id"]
            url = release["fileUrl"]
            if identity in seen:
                continue
            seen.add(identity)
            if not identity.startswith("release_") or not all(c.isalnum() or c in "_-" for c in identity):
                raise ValueError("Invalid release ID")
            if not url.startswith(PREFIX) or any(c in url for c in "\\?#\r\n"):
                raise ValueError("Unapproved download address")
            digest = hashlib.sha256()
            total = 0
            head = b""
            with urllib.request.urlopen(url, timeout=60) as response:
                if response.status != 200 or response.geturl() != url:
                    raise ValueError("Unexpected download response")
                if "html" in response.headers.get("Content-Type", "").lower():
                    raise ValueError("Error page instead of package: " + identity)
                while True:
                    chunk = response.read(256 * 1024)
                    if not chunk:
                        break
                    if not head:
                        head = chunk[:64]
                    total += len(chunk)
                    if total > 2 * 1024**3:
                        raise ValueError("Package exceeds 2 GiB")
                    digest.update(chunk)
            extension = Path(urllib.parse.unquote(urllib.parse.urlsplit(url).path)).suffix.lower()
            expected = {
                "zip": b"PK",
                "rtz": b"PK",
                "dkrmap": b"PK",
                "nrm": b"PK",
                "dkrcode": b"PK",
                "xdelta": b"\xd6\xc3\xc4",
                "bps": b"BPS1",
                "ips": b"PATCH",
                "7z": b"7z\xbc\xaf\x27\x1c",
            }
            if not total or not head.startswith(expected.get(extension.lstrip("."), b"\xff-invalid")):
                raise ValueError("Package signature does not match " + identity)
            statements.append(
                "INSERT INTO launcher_release_integrity(release_id,sha256,bytes,verified_at) VALUES('%s','%s',%d,%d) ON CONFLICT(release_id) DO NOTHING;"
                % (identity, digest.hexdigest(), total, int(time.time()))
            )
            print(identity, total, digest.hexdigest())
    args.output.write_text("\n".join(statements) + "\n", encoding="utf-8")
    print("Prepared integrity records only. No remote write or deployment.")


if __name__ == "__main__":
    main()
