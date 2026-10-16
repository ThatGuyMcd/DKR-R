"""No-ROM signature/bounds/conflict regressions for the intro-skip pipeline."""
import sys
import unittest
from pathlib import Path
from unittest.mock import patch
import struct

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
import startup_flow_policy as pipeline


class StartupPipelineTests(unittest.TestCase):
    def compose(self, revision, *, opcode_changed=False, outside=False, bad_store=False, policy=None):
        sites = pipeline.SITES[revision]
        symbols = {}
        for name, address, _, _ in sites:
            if name not in symbols: symbols[name] = {(address+4 if outside else address-4,16)}
            elif not outside:
                begin=next(iter(symbols[name]))[0]
                symbols[name]={(begin,address+16-begin)}
        sections = []
        for name, address, word, _ in sites:
            sections.append((address - 4, struct.pack('>II', 0 if bad_store else 0xAC820000, word ^ int(opcode_changed))))
        with patch.object(pipeline, 'reviewed_revision', return_value=revision), \
             patch.object(pipeline, 'elf_functions', return_value=symbols), \
             patch.object(pipeline, 'elf_sections', return_value=sections):
            return pipeline.compose_startup_flow(policy or {}, Path('not-a-ROM'), revision)

    def test_both_revisions_and_idempotence(self):
        for revision in pipeline.SITES:
            result = self.compose(revision)
            self.assertEqual(len(result['functionHooks']), 4)
            self.assertEqual(result, self.compose(revision, policy=result))

    def test_opcode_and_function_bounds_fail_closed(self):
        for revision in pipeline.SITES:
            with self.assertRaises(ValueError): self.compose(revision, opcode_changed=True)
            with self.assertRaises(ValueError): self.compose(revision, outside=True)
            with self.assertRaises(ValueError): self.compose(revision, bad_store=True)

    def test_conflicting_hook_is_rejected(self):
        result = self.compose('us.v77')
        result['functionHooks'][0]['text'] = 'wrong callback'
        with self.assertRaises(ValueError): self.compose('us.v77', policy=result)

    def test_unreviewed_image_is_rejected(self):
        with patch.object(pipeline, 'reviewed_revision', return_value=None):
            with self.assertRaises(ValueError):
                pipeline.compose_startup_flow({}, Path('not-a-ROM'), 'us.v77')


if __name__ == '__main__':
    unittest.main()
