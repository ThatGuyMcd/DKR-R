"""Write the texture pack that gives a track's own artwork its resolution back.

A ``.dkrmap`` carries each of the track's own textures at the size the RDP can
load - 64x32 for a colour image, from a photograph of millions of pixels. This
writes the other half: a ``.zip`` in Rice's layout holding the author's
full-resolution originals, each named by the identity RT64 computes for the
64x32 the package ships (see :mod:`.rice_identity`). DKR-R's Graphics > Custom
Texture Packs imports it like any Rice pack, and the game draws the original
where it loaded the reduction.

**Beside the ``.dkrmap``, never inside it.** The runtime serves a track's
payloads byte for byte and would carry a zip as dead weight; packs and tracks
are installed through different doors; and an author may want to hand out the
track without the pack. The track is complete and correct without it.

**What is in the zip.** One ``Diddy Kong Racing#<identity>_all.png`` per
texture, at the root - ``_all`` being Rice's name for colour and alpha in one
image, which is the only variant written. The importer ignores everything
before the first ``#`` and every file that is not a Rice PNG name, so the
:data:`STAMP_NAME` beside them is invisible to it: it records which export the
pack belongs to, and the manifest carries the same digest, so a pack and a
``.dkrmap`` from two different exports can be told apart.

Deliberately free of ``bpy``.
"""

from __future__ import annotations

import hashlib
import json
import os
import re
import tempfile
import zipfile
from typing import Dict, Iterable, Optional, Sequence, Tuple

from .textures import png_size

#: What the importer skips before the first ``#``. Rice packs name the game.
PREFIX = "Diddy Kong Racing"

#: Colour and alpha in one image.
SUFFIX = "_all.png"

#: Which export this pack was written by, and for which track.
STAMP_NAME = "dkr-r-track.json"
STAMP_SCHEMA = 1

#: The importer's own limits (``runtime_rice_texture_import.cpp``). One image
#: over either fails the **whole** import, so an original past them is left out
#: of the pack rather than allowed to take the others down with it.
MAX_IMAGE_BYTES = 256 * 1024 * 1024
MAX_IMAGE_PIXELS = 64 * 1024 * 1024

#: When the author is told the pack is large. Not a limit - twenty photographs
#: of four megabytes are an 80 MB pack and that is allowed - only worth saying.
LARGE_PACK_BYTES = 64 * 1024 * 1024

_IDENTITY_RE = re.compile(r"^[0-9a-f]{8}#[0-9]+#[0-9]+$")


class PackError(Exception):
    """The pack cannot be written as asked, with the reason."""


def entry_name(identity: str) -> str:
    """The file name a replacement for ``identity`` is stored under."""
    if not _IDENTITY_RE.match(identity or ""):
        raise PackError("%r is not a three-part Rice identity" % (identity,))
    return "%s#%s%s" % (PREFIX, identity, SUFFIX)


def image_problem(path: str) -> Optional[str]:
    """Why an original cannot go in the pack, or ``None`` if it can."""
    if not path or not os.path.isfile(path):
        return "the full-resolution image is not on disk (%s)" % (path or "none")
    size = png_size(path)
    if size is None:
        return "%s is not a PNG" % os.path.basename(path)
    if os.path.getsize(path) > MAX_IMAGE_BYTES:
        return ("%s is over the %d MB the pack importer reads for one image"
                % (os.path.basename(path), MAX_IMAGE_BYTES // (1024 * 1024)))
    if size[0] * size[1] > MAX_IMAGE_PIXELS:
        return ("%s is %dx%d, over the %d megapixels the pack importer decodes"
                % (os.path.basename(path), size[0], size[1],
                   MAX_IMAGE_PIXELS // (1024 * 1024)))
    return None


def texture_digest(payloads: Iterable[bytes]) -> str:
    """One short digest over a track's texture payloads, in ordinal order.

    It says which export a pack belongs to; it does not say two packs are the
    same. Identities are a function of the texels *and* of how they are
    hashed, and the hashing once changed (see rice_identity.rice_word_order)
    without a single payload changing. DKR-R therefore tells packs apart by
    the archive's own entries and uses this only to pair a pack with its
    package.
    """
    digest = hashlib.sha256()
    for payload in payloads:
        digest.update(len(payload).to_bytes(4, "big"))
        digest.update(payload)
    return digest.hexdigest()[:16]


def _file_digest(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def write_pack(path: str, entries: Sequence[Tuple[str, str]],
               stamp: Optional[Dict] = None) -> Dict[str, object]:
    """Write ``entries`` - ``(identity, png path)`` - as a Rice pack at ``path``.

    Two entries with one identity are the same texture, and are written once -
    unless their originals differ, which would have one silently win in game,
    so that is refused. (The export separates such pairs before it gets here;
    see :func:`..textures.nudge_texels`.)

    Written to a temporary file and moved into place, so a failure halfway
    leaves the previous pack rather than half of a new one.
    """
    chosen: Dict[str, str] = {}
    for identity, png in entries:
        name = entry_name(identity)
        problem = image_problem(png)
        if problem:
            raise PackError(problem)
        earlier = chosen.get(name)
        if earlier is None:
            chosen[name] = png
        elif (os.path.normcase(os.path.abspath(earlier))
              != os.path.normcase(os.path.abspath(png))
              and _file_digest(earlier) != _file_digest(png)):
            raise PackError(
                "%s and %s would both replace the texture %s, and only one of "
                "them can" % (os.path.basename(earlier), os.path.basename(png),
                              identity)
            )
    if not chosen:
        raise PackError("there is nothing to put in the pack")

    directory = os.path.dirname(os.path.abspath(path)) or "."
    os.makedirs(directory, exist_ok=True)
    handle, temporary = tempfile.mkstemp(prefix=".dkr-hd-", suffix=".zip",
                                         dir=directory)
    os.close(handle)
    try:
        with zipfile.ZipFile(temporary, "w", zipfile.ZIP_DEFLATED) as archive:
            for name in sorted(chosen):
                archive.write(chosen[name], name)
            if stamp is not None:
                document = dict(stamp)
                document.setdefault("schemaVersion", STAMP_SCHEMA)
                archive.writestr(STAMP_NAME, json.dumps(
                    document, indent=2, sort_keys=True) + "\n")
        os.replace(temporary, path)
    except BaseException:
        try:
            os.remove(temporary)
        except OSError:
            pass
        raise
    return {"file": path, "count": len(chosen), "bytes": os.path.getsize(path)}


def read_stamp(path: str) -> Optional[Dict]:
    """The stamp inside a pack, or ``None`` if it has none or is unreadable."""
    try:
        with zipfile.ZipFile(path) as archive:
            return json.loads(archive.read(STAMP_NAME).decode("utf-8"))
    except (OSError, KeyError, ValueError, zipfile.BadZipFile):
        return None
