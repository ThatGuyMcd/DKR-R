"""Checked, opt-in timing hooks and PRIVATE scene qualification; no guest writes in release."""
import copy
import hashlib
import struct
from compose_legacy_mod_policy import elf_sections, elf_functions

NAMES = ['waves_update', 'waves_visibility', 'waves_block_hq', 'func_800B92F4',
         'func_800B97A8', 'waves_render', 'func_800BB2F4', 'func_800BDC80',
         'obj_wave_height', 'waves_get_y', 'load_level_for_menu']
HASHES = [
    ['0be5ad4f67d381a40fe42c284896ac308ce2cb0c3b018b38d0f5b0f198763139','27ff5fcb957487f23070e369b93659004bb664076d186c40868f8893da7cc026','660e7c6d4f701024bcd99946a7a53404b80e782417a2c80c7b86da8bdb38bc22','7cec4eebe04598847507c29e0f763b1ef266fd59a460835fa80f7ef1838f8445','9481d1434de804cdc562f814ff9e1f67a8d3b1aa35a5ba803814c30c55721cfa','ca06f0a082d997767983784407eb5cbb25af47b8e92d63673e76890c3f6661e6','f1761b4c091fddd4c1103cc5d678e193b0171e0cbfcd214da9599b47e33ce763','5ce68ee0a11b39ac85a9383e94479259e0d937f1b5f358aee7763e243c0c07a7','9113721a43bb596cdf15e160f197fe01fc885a18aab471f28b6806858c509bf0','3838141634a5c477c2c45058d6fd047f352853a694e8d60fbee0917cdb4d5c71','8d882096ad66310eee2c3c76eeb8352bc0ce58b268ef234e03d3ad2cc90eb027'],
    ['4cc0a30e3f6fc7afefce1a171a61815412447f911adc413ade35536127eab84d','40d51f94ec2b588a3be7741ce09d8d6629ec81d8ff00d5160cd2dafb75ff2b10','bb4849d16b18bd89b622f86e030c9f7276d2d090f15e74ac203e09e162890985','8a021c9d576192773311270af588c4ac93c3f0f4d7d4ada1c63cf3e1f5c16427','dacc90a1480a4fa309776545ed6b7567b5d334abf3a383e114893d25dfe2b077','a7e33311d3a6c2ffeb1714a79aa9b4f7ff2b2c880e2818a48c21c25797848fe7','2392ea6922def84e4a29b29d013e1579bbf90be04eefc187f9bdb34c10a400c1','99d6897f469b9dd32ac530bc3933e98326d070ba2f503b4746024a48109ca820','f5f31ea6a7b27d96ac64846b38b825c415a858f2e0bf472cb4c5a10e9088b064','cf044b459b3dccd457d26c1bd309c552f1f10948458de39461f0c98d09f77789','cad1cb56072b5d9a260d56a783fb680bd54203aab268bf8b982b2b5027f01f2e']]

def compose_water_profile(policy, elf):
    result = copy.deepcopy(policy)
    sections, functions = elf_sections(elf), elf_functions(elf)
    additions = []
    for region, name in enumerate(NAMES):
        entries = functions.get(name, set())
        if len(entries) != 1: raise ValueError(f'Ambiguous water function: {name}')
        base, size = next(iter(entries))
        bodies = [data[base-start:base-start+size] for start,data in sections if start <= base and base+size <= start+len(data)]
        if len(bodies) != 1 or hashlib.sha256(bodies[0]).hexdigest() not in [h[region] for h in HASHES]:
            raise ValueError(f'Unreviewed water function body: {name}')
        if region == 10:
            sites = [(base, 'extern void dkr_water_private_scene(uint8_t*, recomp_context*); dkr_water_private_scene(rdram, ctx);')]
        else:
            returns = [base+i for i in range(0,size,4) if struct.unpack_from('>I',bodies[0],i)[0] == 0x03E00008]
            if returns != [base+size-8]: raise ValueError(f'Water return coverage changed: {name}')
            sites = [(base, f'extern void dkr_water_profile_begin(uint32_t); dkr_water_profile_begin({region}U);'),
                     (returns[0], f'extern void dkr_water_profile_end(uint8_t*, uint32_t); dkr_water_profile_end(rdram, {region}U);')]
        for pc, text in sites:
            if any(int(p['vram'],0)==pc for p in policy.get('instructionPatches',[])):
                raise ValueError(f'Water instrumentation overlaps an instruction patch: {name}')
            existing = [h for h in result.get('functionHooks',[]) if int(h['beforeVram'],0)==pc]
            if len(existing)>1: raise ValueError(f'Ambiguous existing hook at {pc:x}')
            if existing:
                if 'dkr_water_' in existing[0]['text']: raise ValueError('Water profile already composed')
                existing[0]['text'] = text + ' ' + existing[0]['text']
            else:
                additions.append(dict(function=name,beforeVram=f'0x{pc:08X}',text=text,
                    reason='Read-only opt-in water timing; verified full MIPS body and sole return. Private scene override is compiled out of release.'))
    result.setdefault('functionHooks',[]).extend(additions)
    return result
