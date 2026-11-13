"""The dkr.json schema is well-formed and accepts every tracked sample.

The validator below covers only the keywords modding/schema/dkr.schema.json
uses, so the test stays stdlib-only. A new keyword in the schema fails loudly
instead of being silently ignored.
"""

import json
from pathlib import Path
import re
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCHEMA_PATH = ROOT / "modding/schema/dkr.schema.json"

ANNOTATIONS = {"$schema", "$id", "$defs", "title", "description", "default", "examples"}
TYPES = {
    "object": lambda v: isinstance(v, dict),
    "array": lambda v: isinstance(v, list),
    "string": lambda v: isinstance(v, str),
    "boolean": lambda v: isinstance(v, bool),
    # Per 2020-12, 1.0 is an integer. The loader is stricter (is_number_unsigned);
    # the schema descriptions say so because editors can't enforce it.
    "integer": lambda v: not isinstance(v, bool) and (isinstance(v, int) or (isinstance(v, float) and v.is_integer())),
}


def canonical(value):
    """JSON equality: numbers compare by value, true stays distinct from 1."""
    if isinstance(value, float) and value.is_integer():
        value = int(value)
    if isinstance(value, list):
        return "[" + ",".join(map(canonical, value)) + "]"
    if isinstance(value, dict):
        return "{" + ",".join(json.dumps(k) + ":" + canonical(v) for k, v in sorted(value.items())) + "}"
    return json.dumps(value)


def unique_object(pairs):
    out = {}
    for key, value in pairs:
        if key in out:
            raise ValueError(f"duplicate key {key!r}")
        out[key] = value
    return out


class Validator:
    def __init__(self, root):
        self.root = root

    def resolve(self, ref):
        if not ref.startswith("#/"):
            raise ValueError(f"unsupported $ref {ref}")
        node = self.root
        for part in ref[2:].split("/"):
            node = node[part]
        return node

    def errors(self, value, schema=None, path="$"):
        schema = self.root if schema is None else schema
        if schema is True:
            return []
        if schema is False:
            return [f"{path}: not allowed"]
        out = []
        for key, rule in schema.items():
            if key in ANNOTATIONS or key in ("then", "else"):
                continue
            check = getattr(self, "k_" + key.lstrip("$"), None)
            if check is None:
                raise ValueError(f"validator does not implement {key}")
            out += check(value, rule, schema, path)
        return out

    def k_ref(self, value, rule, schema, path):
        return self.errors(value, self.resolve(rule), path)

    def k_type(self, value, rule, schema, path):
        return [] if TYPES[rule](value) else [f"{path}: expected {rule}"]

    def k_const(self, value, rule, schema, path):
        return [] if canonical(value) == canonical(rule) else [f"{path}: expected {rule!r}"]

    def k_enum(self, value, rule, schema, path):
        return [] if canonical(value) in map(canonical, rule) else [f"{path}: {value!r} not in {rule}"]

    def k_required(self, value, rule, schema, path):
        if not isinstance(value, dict):
            return []
        return [f"{path}: missing {key}" for key in rule if key not in value]

    def k_properties(self, value, rule, schema, path):
        if not isinstance(value, dict):
            return []
        out = []
        for key, sub in rule.items():
            if key in value:
                out += self.errors(value[key], sub, f"{path}.{key}")
        return out

    def k_additionalProperties(self, value, rule, schema, path):
        if not isinstance(value, dict):
            return []
        named = schema.get("properties", {})
        out = []
        for key, item in value.items():
            if key not in named:
                out += self.errors(item, rule, f"{path}.{key}")
        return out

    def k_propertyNames(self, value, rule, schema, path):
        if not isinstance(value, dict):
            return []
        return [e for key in value for e in self.errors(key, rule, f"{path}[name {key!r}]")]

    def k_maxProperties(self, value, rule, schema, path):
        return [f"{path}: too many properties"] if isinstance(value, dict) and len(value) > rule else []

    def k_items(self, value, rule, schema, path):
        if not isinstance(value, list):
            return []
        return [e for i, item in enumerate(value) for e in self.errors(item, rule, f"{path}[{i}]")]

    def k_minItems(self, value, rule, schema, path):
        return [f"{path}: too few items"] if isinstance(value, list) and len(value) < rule else []

    def k_maxItems(self, value, rule, schema, path):
        return [f"{path}: too many items"] if isinstance(value, list) and len(value) > rule else []

    def k_uniqueItems(self, value, rule, schema, path):
        if not rule or not isinstance(value, list):
            return []
        seen = [canonical(item) for item in value]
        return [f"{path}: duplicate items"] if len(seen) != len(set(seen)) else []

    def k_pattern(self, value, rule, schema, path):
        return [] if not isinstance(value, str) or re.search(rule, value) else [f"{path}: {value!r} !~ {rule}"]

    def k_minLength(self, value, rule, schema, path):
        return [f"{path}: too short"] if isinstance(value, str) and len(value) < rule else []

    def k_maxLength(self, value, rule, schema, path):
        return [f"{path}: too long"] if isinstance(value, str) and len(value) > rule else []

    def k_minimum(self, value, rule, schema, path):
        return [f"{path}: below {rule}"] if TYPES["integer"](value) and value < rule else []

    def k_maximum(self, value, rule, schema, path):
        return [f"{path}: above {rule}"] if TYPES["integer"](value) and value > rule else []

    def k_allOf(self, value, rule, schema, path):
        return [e for sub in rule for e in self.errors(value, sub, path)]

    def k_if(self, value, rule, schema, path):
        branch = "then" if not self.errors(value, rule, path) else "else"
        return self.errors(value, schema.get(branch, True), path)


def tracked_samples():
    listed = subprocess.run(
        ["git", "ls-files", "-z", "--", "*dkr.json"], cwd=ROOT, capture_output=True, check=True
    ).stdout.decode()
    return sorted(ROOT / name for name in listed.split("\0") if name and Path(name).name == "dkr.json")


class DkrJsonSchemaTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"), object_pairs_hook=unique_object)
        cls.validator = Validator(cls.schema)

    def base(self, **changes):
        meta = {"schema": 1, "api": 2, "online": "deterministic", "revisions": ["us.v77", "us.v80"]}
        meta.update(changes)
        return meta

    def assertRejected(self, meta):
        self.assertTrue(self.validator.errors(meta), f"schema accepted {meta}")

    def test_top_level_shape(self):
        self.assertEqual(self.schema["$schema"], "https://json-schema.org/draft/2020-12/schema")
        self.assertEqual(self.schema["type"], "object")
        self.assertEqual(self.schema["required"], ["schema", "api", "revisions"])
        self.assertNotIn("additionalProperties", self.schema, "the inspector ignores unknown keys")
        for name, prop in self.schema["properties"].items():
            self.assertTrue(prop.get("title") and prop.get("description"), name)
        for example in self.schema["examples"]:
            self.assertEqual(self.validator.errors(example), [])

    def test_tracked_samples_validate(self):
        samples = tracked_samples()
        self.assertTrue(any("modding/examples" in str(path) for path in samples))
        for path in samples:
            with self.subTest(path=path.relative_to(ROOT).as_posix()):
                meta = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique_object)
                self.assertEqual(self.validator.errors(meta), [])

    def test_editor_schema_key_is_allowed(self):
        self.assertEqual(self.validator.errors(self.base(**{"$schema": "../../schema/dkr.schema.json"})), [])

    def test_rejects_what_the_inspector_rejects(self):
        self.assertRejected(self.base(online="netplay-safe"))
        self.assertRejected(self.base(api="2"))
        self.assertRejected(self.base(schema=2))
        self.assertRejected(self.base(api=3))
        self.assertRejected(self.base(activation="live"))
        self.assertRejected(self.base(revisions=[]))
        self.assertRejected(self.base(revisions=["us.v77", "us.v77"]))
        self.assertRejected(self.base(revisions=["eu.v77"]))
        self.assertRejected(self.base(requires={"Core": 1}))
        self.assertRejected(self.base(api=1, requires={"entities2": 2}))
        self.assertRejected(self.base(development="yes"))
        self.assertRejected(self.base(profile="huge"))
        self.assertRejected(self.base(live_reload="instruction-only", state_schema=1))
        self.assertRejected(self.base(development=True, live_reload="instruction-only", state_schema=0))
        self.assertRejected(self.base(api=1, live_options=["pace"], requires={"live-settings2": 1}))
        self.assertRejected(self.base(live_options=["pace"], requires={"core": 1}))
        self.assertRejected(self.base(systems=["a", "a"]))
        for path in (
            "con.txt",
            "d/COM1.bin",
            "con/x.txt",
            "a/nul/b.txt",
            "/abs.json",
            "../up.json",
            "a\\b.json",
            "c:x",
            "dir/",
            "x.",
            "dkr.json",
            "café.json",
        ):
            self.assertRejected(self.base(resources={"data": path}))
        meta = self.base()
        del meta["revisions"]
        self.assertRejected(meta)

    def test_accepts_what_the_inspector_tolerates(self):
        self.assertEqual(self.validator.errors(self.base(api=1, profile="anything")), [])
        self.assertEqual(self.validator.errors(self.base(api=1, development=True)), [])
        self.assertEqual(self.validator.errors(self.base(future_key={"nested": True})), [])
        self.assertEqual(self.validator.errors(self.base(resources={"tools-ui": "ui/tools.json"})), [])
        for path in ("console.txt", "com10.txt", "comx.txt"):
            self.assertEqual(self.validator.errors(self.base(resources={"data": path})), [])
        # state_schema is only read inside the live_reload branch.
        self.assertEqual(self.validator.errors(self.base(state_schema=0)), [])
        # 2020-12 integers compare by value; the loader is stricter (see descriptions).
        self.assertEqual(self.validator.errors(self.base(schema=1.0, requires={"core": 1.0})), [])
        del_online = self.base()
        del del_online["online"]
        self.assertEqual(self.validator.errors(del_online), [])


if __name__ == "__main__":
    unittest.main()
