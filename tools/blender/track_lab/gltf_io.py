"""Reader and writer for DKR level object-map glTFs.

These files are not general glTF. Every retail object map is a flat node tree
with no meshes, buffers, materials or animations: one root node named
``objects`` whose children each carry a ``translation`` and an ``extras`` dict
holding the decoded ``LevelObjectEntry``. That is the whole format.

Blender's stock glTF importer/exporter is therefore the wrong tool. It rewrites
the document (axis conversion, node reordering, mesh and buffer boilerplate) and
coerces integer ``extras`` to float, which is exactly the fidelity the object map
depends on. This module reads and writes the documents directly instead.

The serialisation below reproduces all 136 retail ``us.v77`` object maps
byte-for-byte; ``tests/test_roundtrip.py`` asserts that and is the regression
gate for any change here.

Deliberately free of ``bpy`` so it can be exercised outside Blender.
"""

from __future__ import annotations

import json
import os
from dataclasses import dataclass, field
from typing import Any, Dict, List

#: Serialisation used by ``dkr_assets_tool``. Established empirically against
#: every retail object map: two-space indent, sorted keys, trailing newline.
JSON_INDENT = 2
JSON_SORT_KEYS = True
JSON_TRAILING_NEWLINE = True

ROOT_NODE_NAME = "objects"
GLTF_VERSION = "2.0"

#: Key inside ``extras`` naming the object type, e.g. ``ASSET_OBJECT_GROUNDZIPPER``.
ID_KEY = "id"


class ObjectMapError(Exception):
    """Raised when a document does not match the object-map shape."""


@dataclass
class MapObject:
    """One placed object: a glTF node with a translation and decoded fields."""

    object_id: str
    name: str
    translation: List[float]
    #: Decoded ``LevelObjectEntry`` fields, minus ``id``. Values are ints,
    #: floats, strings (enum members) or lists of ints, matching the JSON.
    fields: Dict[str, Any] = field(default_factory=dict)

    @property
    def extras(self) -> Dict[str, Any]:
        """The ``extras`` dict as it is written to the document."""
        out = dict(self.fields)
        out[ID_KEY] = self.object_id
        return out


@dataclass
class ObjectMap:
    """A whole object map, in document order."""

    objects: List[MapObject] = field(default_factory=list)
    root_name: str = ROOT_NODE_NAME

    def by_id(self, object_id: str) -> List[MapObject]:
        return [o for o in self.objects if o.object_id == object_id]

    def count_of(self, object_id: str) -> int:
        return sum(1 for o in self.objects if o.object_id == object_id)


def parse(document: Dict[str, Any]) -> ObjectMap:
    """Convert a parsed glTF document into an :class:`ObjectMap`."""
    nodes = document.get("nodes")
    if not isinstance(nodes, list) or not nodes:
        raise ObjectMapError("document has no nodes")

    root = nodes[0]
    children = root.get("children", [])
    if children and children != list(range(1, len(nodes))):
        raise ObjectMapError(
            "root children are not the remaining nodes in order; this is not a "
            "DKR object map"
        )

    objects: List[MapObject] = []
    for index, node in enumerate(nodes[1:], start=1):
        extras = node.get("extras")
        if not isinstance(extras, dict):
            raise ObjectMapError("node %d has no extras" % index)
        object_id = extras.get(ID_KEY)
        if not isinstance(object_id, str):
            raise ObjectMapError("node %d has no string %r in extras" % (index, ID_KEY))
        translation = node.get("translation")
        if not isinstance(translation, list) or len(translation) != 3:
            raise ObjectMapError("node %d has no 3-component translation" % index)
        fields = {k: v for k, v in extras.items() if k != ID_KEY}
        objects.append(
            MapObject(
                object_id=object_id,
                name=node.get("name", ""),
                translation=[float(c) for c in translation],
                fields=fields,
            )
        )

    return ObjectMap(objects=objects, root_name=root.get("name", ROOT_NODE_NAME))


def build(object_map: ObjectMap) -> Dict[str, Any]:
    """Convert an :class:`ObjectMap` back into a glTF document."""
    nodes: List[Dict[str, Any]] = []
    root: Dict[str, Any] = {"name": object_map.root_name}
    nodes.append(root)

    for obj in object_map.objects:
        nodes.append(
            {
                "extras": obj.extras,
                "name": obj.name,
                "translation": _normalise_translation(obj.translation),
            }
        )

    # Retail maps with no objects omit ``children`` rather than writing an empty
    # list, so match that.
    if len(nodes) > 1:
        root["children"] = list(range(1, len(nodes)))

    return {
        "asset": {"version": GLTF_VERSION},
        "nodes": nodes,
        "scenes": [{"nodes": [0]}],
    }


def _normalise_translation(translation) -> List[float]:
    """Positions are whole-number world units in every retail map.

    They are still written as JSON floats (``3133.0``), so keep the float type
    but drop any epsilon Blender's transform stack introduced, which would
    otherwise turn ``449.0`` into ``448.99999237060547``.
    """
    out = []
    for component in translation:
        value = float(component)
        rounded = round(value)
        out.append(float(rounded) if abs(value - rounded) < 1e-4 else value)
    return out


def dumps(object_map: ObjectMap) -> str:
    """Serialise to the exact text form ``dkr_assets_tool`` produces."""
    text = json.dumps(build(object_map), indent=JSON_INDENT, sort_keys=JSON_SORT_KEYS)
    return text + "\n" if JSON_TRAILING_NEWLINE else text


def load(path: str) -> ObjectMap:
    with open(path, "r", encoding="utf-8") as handle:
        return parse(json.load(handle))


def save(object_map: ObjectMap, path: str) -> None:
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(dumps(object_map))


#: The two-line sidecar that names the glTF, e.g.
#: ``{"objects": "asset_level_object_maps_0.gltf", "type": "LevelObjectMap"}``.
SIDECAR_TYPE = "LevelObjectMap"


def load_sidecar(path: str) -> str:
    """Return the glTF path a ``*.json`` object-map sidecar points at."""
    with open(path, "r", encoding="utf-8") as handle:
        data = json.load(handle)
    if data.get("type") != SIDECAR_TYPE:
        raise ObjectMapError("%s is not a %s sidecar" % (path, SIDECAR_TYPE))
    return os.path.join(os.path.dirname(path), data["objects"])


def save_sidecar(path: str, gltf_name: str) -> None:
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(
            json.dumps(
                {"objects": gltf_name, "type": SIDECAR_TYPE},
                indent=4,
                sort_keys=True,
            )
            + "\n"
        )
