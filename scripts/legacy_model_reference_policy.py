"""Bounds/lifetime fences for the hash-pinned retail model dereferences.

This layer preserves valid retail loaders and shading. Corrupt allocations are
never guessed/rebound; their native owner reports the context and ends safely.
"""
import copy
import hashlib
import struct
from legacy_character_presentation_policy import ELFS
from compose_legacy_mod_policy import elf_sections, elf_functions


def compose_model_references(policy, elf, revision):
    if revision not in ELFS or hashlib.sha256(elf.read_bytes()).hexdigest()!=ELFS[revision]:
        raise ValueError('Model reference fences require a reviewed retail ELF')
    result=copy.deepcopy(policy)
    words={base+i:struct.unpack_from('>I',data,i)[0] for base,data in elf_sections(elf)
           for i in range(0,len(data),4)}
    symbols=elf_functions(elf)
    declaration='extern int dkr_legacy_model_safety(uint8_t*, recomp_context*, unsigned, uint32_t); '

    def hook(name,pc,word,text):
        matches=symbols.get(name,set())
        if len(matches)!=1:raise ValueError('Ambiguous model reference function '+name)
        begin,size=next(iter(matches))
        if not begin<=pc<begin+size or words.get(pc)!=word:
            raise ValueError('Model reference instruction changed '+name+' '+hex(pc))
        entry=dict(function=name,beforeVram=hex(pc),text=declaration+text,
                   reason='Bound sparse model slots and validate allocation references before retail dereferences; no pointer masking or speculative rebinding.')
        owners=[h for h in result.setdefault('functionHooks',[]) if int(h['beforeVram'],0)==pc]
        if owners:
            if owners!=[entry]:raise ValueError('Conflicting model reference hook')
        elif any(int(p['vram'],0)==pc for p in result.get('instructionPatches',[])):
            raise ValueError('Conflicting model reference instruction')
        else:result['functionHooks'].append(entry)

    # All-NULL is a valid unshaded result, but retail's search otherwise runs
    # into the following behaviour allocation and interprets it as an instance.
    hook('init_object_shading',0x8000f7ec,0x27bdffd8,
         'if (dkr_legacy_model_safety(rdram, ctx, 0U, (uint32_t)ctx->r4)) { MEM_W(0x54, ctx->r4) = 0; ctx->r2 = 4; return; }')
    # Observe every loaded slot BEFORE behaviour initialization can obscure
    # where a reference was damaged. Slots intentionally skipped stay NULL.
    hook('spawn_object',0x8000f0ec,0x8e480040,
         'dkr_legacy_model_safety(rdram, ctx, 1U, (uint32_t)ctx->r18);')
    instance=0x8005fcd0 if revision=='us.v77' else 0x8005ff10
    hook('model_instance_init',instance+8,0x848e0048,
         'dkr_legacy_model_safety(rdram, ctx, 2U, (uint32_t)ctx->r4);')
    hook('model_instance_init',instance+0x260,0x8fbf0014,
         'if (ctx->r2) dkr_legacy_model_safety(rdram, ctx, 3U, (uint32_t)ctx->r2);')
    return result
