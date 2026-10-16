"""Headless, fail-closed policy review. Inputs/ELFs are strictly read-only."""
import copy
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts'))
from legacy_memory_safety_policy import compose_memory_safety

ELF_INPUTS = []

class PolicyTests(unittest.TestCase):
    def test_requires_reviewed_elf(self):
        with self.assertRaises(ValueError):
            compose_memory_safety({},Path(__file__),'us.v77')

    def test_revisions_and_conflicts(self):
        # Explicit inputs let CI qualify hash-pinned ELFs without shipping ROMs.
        if not ELF_INPUTS:
            self.skipTest('Pass both reviewed retail ELFs to qualify revision hooks')
        for rev,elf in ELF_INPUTS:
            baseline={'schemaVersion':1,'functionHooks':[],'instructionPatches':[]}
            result=compose_memory_safety(baseline,elf,rev)
            self.assertEqual(baseline,{'schemaVersion':1,'functionHooks':[],'instructionPatches':[]})
            self.assertEqual(result,compose_memory_safety(result,elf,rev))
            self.assertEqual(len(result['functionHooks']),20)
            self.assertEqual(len(result['instructionPatches']),8)
            for hook in result['functionHooks']:
                damaged=copy.deepcopy(baseline);bad=copy.deepcopy(hook);bad['text']='return;'
                damaged['functionHooks'].append(bad)
                with self.assertRaises(ValueError):compose_memory_safety(damaged,elf,rev)

if __name__=='__main__':
    ELF_INPUTS=[('us.v77',Path(sys.argv[1])),('us.v80',Path(sys.argv[2]))] if len(sys.argv)==3 else []
    unittest.main(argv=[sys.argv[0]])
