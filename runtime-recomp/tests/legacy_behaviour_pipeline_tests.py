"""Read-only qualification of the closed behaviour hook ABI."""
import copy
import sys
import unittest
from pathlib import Path
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
from legacy_behaviour_policy import compose_behaviour

INPUTS = []

class PolicyTests(unittest.TestCase):
    def test_unreviewed_source(self):
        with self.assertRaises(ValueError):
            compose_behaviour({}, Path(__file__), 'us.v77')
        with self.assertRaises(ValueError):
            compose_behaviour({}, Path(__file__), 'unknown')

    def test_revisions_conflicts_and_idempotence(self):
        if not INPUTS:
            self.skipTest('Pass both retail ELFs for private revision qualification')
        for revision, path in INPUTS:
            baseline = {'functionHooks': [], 'instructionPatches': []}
            output = compose_behaviour(baseline, path, revision)
            self.assertEqual(baseline, {'functionHooks': [], 'instructionPatches': []})
            self.assertEqual(len(output['functionHooks']), 10)
            self.assertEqual(compose_behaviour(output, path, revision), output)
            for hook in output['functionHooks']:
                damaged = copy.deepcopy(baseline)
                damaged['functionHooks'] = [dict(hook, text='return;')]
                with self.assertRaises(ValueError):
                    compose_behaviour(damaged, path, revision)
                damaged = copy.deepcopy(baseline)
                damaged['instructionPatches'] = [{'vram': hook['beforeVram']}]
                with self.assertRaises(ValueError):
                    compose_behaviour(damaged, path, revision)

if __name__ == '__main__':
    INPUTS = [('us.v77', Path(sys.argv[1])), ('us.v80', Path(sys.argv[2]))] if len(sys.argv) == 3 else []
    unittest.main(argv=[sys.argv[0]])
