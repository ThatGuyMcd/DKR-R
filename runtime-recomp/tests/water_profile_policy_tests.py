import copy
import json
import re
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'scripts'))
from water_profile_policy import compose_water_profile
from compose_legacy_mod_policy import elf_functions

def check_scroll_symbols(elf77, elf80):
    header=(root/'runtime-recomp/src/game/revision_addresses.hpp').read_text()
    fields=re.findall(r'std::uint32_t (\w+);',header.split('struct AddressTable {',1)[1].split('};',1)[0])
    for revision,elf in [('kUsV77',elf77),('kUsV80',elf80)]:
        table=header.split('AddressTable '+revision+'{',1)[1].split('};',1)[0]
        values=[int(v,16) for v in re.findall(r'0x([0-9A-Fa-f]+)U',table)]
        assert len(fields)==len(values)
        addresses=dict(zip(fields,values))
        symbols=elf_functions(Path(elf),(1,))
        for field in ['WaveTexUVMaskX','WaveTexUVMaskY']:
            assert symbols['g'+field]=={(addresses[field],4)}
    print('Verified both water scroll mask address tables against retail ELF symbols')

def check(elf,policy_path):
    policy=json.loads(Path(policy_path).read_text())
    original=copy.deepcopy(policy)
    result=compose_water_profile(policy,Path(elf))
    assert policy==original
    hooks=[h for h in result['functionHooks'] if 'dkr_water_' in h['text']]
    assert len(hooks)==21
    assert result.get('instructionPatches')==policy.get('instructionPatches')
    assert len({int(h['beforeVram'],0) for h in result['functionHooks']})==len(result['functionHooks'])
    try: compose_water_profile(result,Path(elf))
    except ValueError: pass
    else: raise AssertionError('Duplicate composition must fail')
    conflict=copy.deepcopy(policy)
    conflict.setdefault('instructionPatches',[]).append(dict(vram=hooks[-2]['beforeVram']))
    try: compose_water_profile(conflict,Path(elf))
    except ValueError: pass
    else: raise AssertionError('Instruction collision must fail')
    print('Verified water timing hook coverage and existing-policy preservation:',elf)

if __name__=='__main__':
    if len(sys.argv)!=5: raise SystemExit('v77 ELF policy v80 ELF policy required')
    check(sys.argv[1],sys.argv[2]);check(sys.argv[3],sys.argv[4])
    check_scroll_symbols(sys.argv[1],sys.argv[3])
