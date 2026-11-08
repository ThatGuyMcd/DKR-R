#!/usr/bin/env python3
"""Static ELF-to-NRM relocation checks. Never executes guest or native code.

The pinned ModTool can lose a LO16's high half after LLVM reuses a LUI and
emits R_MIPS_32 section bases without the linked pointer's addend. DKR's
interpreter needs complete targets, not those partial/addend-only records.
Resolve LOCAL records from the fully linked ELF, preserving every other byte.
No dependency, game loader, permission or memory budget is changed here.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

MAX_CODE = 4 * 1024 * 1024
MAX_FUNCTIONS = 16384
LOCAL = 0x80000000
IMPORT, EVENT = 0xfffffffe, 0xfffffffd
SUPPORTED = (0, 2, 4, 5, 6)


def require(condition, message):
    if not condition:
        raise ValueError('Static relocation inspection: ' + message)


def region(data, offset, size):
    require(0 <= offset <= len(data) and 0 <= size <= len(data)-offset, 'truncated binary table')
    return data[offset:offset+size]


def symbols(data, binary, *, targets=True):
    """Mirror the existing loader's complete symbol-wire preflight, read-only."""
    require(len(data) >= 52 and data[:8] == b'N64RSYMS', 'invalid symbol header')
    word = lambda at: struct.unpack('<I', region(data, at, 4))[0]
    require(word(8) == 1, 'unsupported symbol version')
    counts = struct.unpack('<10I', region(data, 12, 40))
    require(all(n <= (65536 if i == 9 else MAX_FUNCTIONS) for i, n in enumerate(counts)), 'symbol count budget')
    require(0 < counts[0] <= 256 and counts[1] <= 128 and counts[2] <= 4096 and counts[9] % 4 == 0, 'invalid symbol counts')
    at = 52
    strings = region(data, at, counts[9]); at += counts[9]
    sections, relocs = [], []
    functions = code = resident = reloc_count = 0

    def table(size, count):
        nonlocal at
        begin = at; region(data, begin, size*count); at += size*count
        return begin

    def name(start, length):
        require(0 < length <= 160 and start <= len(strings) and length <= len(strings)-start, 'invalid symbol name')
        value = bytes(strings[start:start+length])
        require(all(32 <= byte <= 126 for byte in value), 'nonportable symbol name')
        return value

    for index in range(counts[0]):
        h = table(28, 1)
        flags, rom, ram, size, bss, funcs, count = struct.unpack_from('<7I', data, h)
        require(not flags and size+bss <= MAX_CODE and rom <= len(binary) and size <= len(binary)-rom and
                ram+size+bss <= 0xffffffff and funcs <= MAX_FUNCTIONS-functions and
                count <= MAX_FUNCTIONS*16-reloc_count and (rom | ram) % 4 == 0 and
                size+bss <= MAX_CODE-resident, 'invalid section bounds/budget')
        require(all(not (ram < s['ram']+s['size']+s['bss'] and s['ram'] < ram+size+bss) for s in sections), 'overlapping sections')
        section = dict(index=index, ram=ram, rom=rom, size=size, bss=bss)
        sections.append(section); resident += size+bss; reloc_count += count
        f = table(8, funcs); functions += funcs; ranges = []
        for i in range(funcs):
            off, length = struct.unpack_from('<2I', data, f+8*i)
            require(length and (off | length | ram) % 4 == 0 and off <= size and length <= size-off and length <= MAX_CODE-code, 'invalid function bounds')
            code += length; ranges.append((off, off+length))
        ranges.sort()
        require(all(a[1] <= b[0] for a, b in zip(ranges, ranges[1:])), 'overlapping functions')
        r = table(16, count)
        for i in range(count):
            slot = r+16*i
            off, kind, target, owner = struct.unpack_from('<4I', data, slot)
            require(off % 4 == 0 and off <= size and size-off >= 4 and kind in SUPPORTED, 'unsupported or out-of-bounds relocation')
            if owner == IMPORT: require(kind == 4 and target < counts[2], 'invalid import relocation')
            elif owner == EVENT: require(kind == 4 and target < counts[7], 'invalid event relocation')
            elif owner & LOCAL: require(owner & ~LOCAL < counts[0], 'invalid local relocation owner')
            else: require(owner == 0x1000 and target < 0x800000, 'invalid base-game relocation')
            relocs.append(dict(section=index, off=off, kind=kind, target=target, owner=owner, slot=slot))
    if targets:
        for r in relocs:
            if r['owner'] & LOCAL and r['owner'] not in (IMPORT, EVENT):
                s = sections[r['owner'] & ~LOCAL]
                require(r['target'] <= s['size']+s['bss'], 'local relocation is outside its section at '+hex(sections[r['section']]['ram']+r['off']))
    t = table(12, counts[1]); deps = set()
    for i in range(counts[1]):
        value = name(word(t+12*i+4), word(t+12*i+8))
        require(value not in deps, 'duplicate dependency'); deps.add(value)
    for group in (2, 3):
        t = table(12, counts[group]); seen = set()
        for i in range(counts[group]):
            start, length, dep = struct.unpack_from('<3I', data, t+12*i)
            value = (dep, name(start, length))
            require(dep < counts[1] and value not in seen, 'invalid dependency symbol'); seen.add(value)
    t = table(16, counts[4])
    for i in range(counts[4]):
        func, rom, off, flags = struct.unpack_from('<4I', data, t+16*i)
        require(func < functions and rom == 0x1000 and off % 4 == 0 and flags <= 1, 'invalid replacement')
    t = table(12, counts[5]); exported = set(); names = set()
    for i in range(counts[5]):
        func, start, length = struct.unpack_from('<3I', data, t+12*i); value = name(start, length)
        require(func < functions and func not in exported and value not in names, 'invalid export')
        exported.add(func); names.add(value)
    t = table(8, counts[6])
    for i in range(counts[6]):
        event, func = struct.unpack_from('<2I', data, t+8*i)
        require(event < counts[3] and func < functions, 'invalid callback')
    t = table(8, counts[7]); seen = set()
    for i in range(counts[7]):
        value = name(word(t+8*i), word(t+8*i+4)); require(value not in seen, 'duplicate event'); seen.add(value)
    t = table(16, counts[8])
    for i in range(counts[8]):
        func, rom, off, flags = struct.unpack_from('<4I', data, t+16*i)
        require(func < functions and rom == 0x1000 and off % 4 == 0 and flags <= 1, 'invalid hook')
    require(at == len(data), 'unexpected trailing symbol bytes')
    return sections, relocs


def linked_elf(data):
    require(data[:7] == b'\x7fELF\x01\x02\x01', 'expected big-endian ELF32')
    header = struct.unpack('>HHIIIIIHHHHHH', region(data, 16, 36))
    require(header[0:2] == (2, 8) and header[10] == 40 and 0 < header[11] < 65536, 'expected linked MIPS ELF')
    sections = []
    for i in range(header[11]):
        values = struct.unpack('>10I', region(data, header[5]+40*i, 40))
        section = dict(zip(('name','type','flags','addr','offset','size','link','info','align','entsize'), values))
        if section['type'] != 8: region(data, section['offset'], section['size'])
        require(section['addr']+section['size'] <= 0x100000000, 'ELF address overflow')
        sections.append(section)
    result = {}; pairs = []; orphan_lows = []
    for rel in sections:
        if rel['type'] != 9: continue
        require(rel['info'] < len(sections) and rel['link'] < len(sections) and rel['entsize'] == 8 and rel['size'] % 8 == 0, 'invalid ELF relocation table')
        source = sections[rel['info']]
        if not source['flags'] & 2: continue  # Debug-only relocations are not loaded.
        syms = sections[rel['link']]
        require(source['type'] != 8 and syms['type'] == 2 and syms['entsize'] == 16 and syms['size'] % 16 == 0, 'invalid ELF symbol/source table')
        pending = []
        for at in range(rel['offset'], rel['offset']+rel['size'], 8):
            address, info = struct.unpack_from('>2I', data, at); kind = info & 255; symbol_index = info >> 8
            require(symbol_index < syms['size']//16 and kind in SUPPORTED, 'unsupported ELF relocation')
            require(source['addr'] <= address and address+4 <= source['addr']+source['size'] and address % 4 == 0, 'ELF relocation outside initialized section')
            value = struct.unpack_from('>IIIBBH', data, syms['offset']+16*symbol_index)
            symbol = dict(value=value[1], size=value[2], type=value[3] & 15, section=value[5])
            word = struct.unpack_from('>I', data, source['offset']+address-source['addr'])[0]
            key = (address, kind)
            require(key not in result, 'duplicate ELF relocation address/type')
            row = dict(address=address, kind=kind, symbol=symbol, word=word)
            result[key] = row
            if kind == 5:
                require(not pending or pending[0]['symbol'] == symbol, 'mixed-symbol pending HI16 group')
                pending.append(row)
            elif kind == 6:
                if pending:
                    require(pending[0]['symbol'] == symbol, 'HI16/LO16 symbol disagreement')
                    low = (word & 0xffff) - (0x10000 if word & 0x8000 else 0)
                    for high in pending:
                        target = (((high['word'] & 0xffff) << 16)+low) & 0xffffffff
                        high['resolved'] = target; pairs.append((high['address'], address))
                    pending = []
                else: orphan_lows.append(address)
            else:
                require(not pending, 'unpaired HI16 before another relocation')
        require(not pending, 'unterminated HI16 group')
    return sections, result, pairs, orphan_lows


def normalize(elf, syms, binary):
    """Return proven complete local targets; never infer an ambiguous address."""
    sections, relocs = symbols(syms, binary, targets=False)
    elf_sections, entries, pairs, orphan_lows = linked_elf(elf)
    out = bytearray(syms); changes = []; local = 0; addresses = set()
    highs = {}
    for entry in entries.values():
        if entry['kind'] == 5 and 'resolved' in entry:
            highs.setdefault(tuple(entry['symbol'].items()), []).append(entry['resolved'])
    checked_orphans = 0
    for row in relocs:
        source = sections[row['section']]; address = source['ram']+row['off']; key = (address, row['kind'])
        require(key not in addresses, 'duplicate output relocation'); addresses.add(key)
        require(key in entries, 'output relocation has no ELF provenance at '+hex(address))
        entry = entries[key]; symbol = entry['symbol']; kind = row['kind']
        if row['owner'] in (IMPORT, EVENT) or not row['owner'] & LOCAL:
            continue  # The pinned tool owns imports/events/per-ROM references.
        local += 1
        require(0 < symbol['section'] < len(elf_sections), 'invalid local ELF target section')
        target_section = elf_sections[symbol['section']]
        require(symbol['section'] != 0 and target_section['flags'] & 2 and target_section['size'], 'local record does not reference an allocated ELF section')
        lo = target_section['addr']; end = lo+target_section['size']; word = entry['word']
        if kind == 0: continue
        if kind == 2:
            target = word  # Fully linked pointer, INCLUDING its addend.
        elif kind == 4:
            target = ((address+4) & 0xf0000000) | ((word & 0x3ffffff) << 2)
        elif kind == 5:
            require('resolved' in entry, 'HI16 has no checked LO16 pair')
            target = entry['resolved']
        else:
            # A LO16's encoded low half is exact even if an unrelated LUI was
            # encountered since its matching high relocation. A small original
            # ELF data section makes the full address uniquely recoverable.
            first = lo+(((word & 0xffff)-lo) & 0xffff)
            candidates = list(range(first, end+1, 0x10000))
            require(len(candidates) == 1, 'ambiguous LO16 target at '+hex(address)+'; split oversized data sections (use -fdata-sections)')
            target = candidates[0]
        require(lo <= target <= end, 'linked target outside its original ELF section at '+hex(address))
        dest = sections[row['owner'] & ~LOCAL]
        require(dest['ram'] <= lo and end <= dest['ram']+dest['size']+dest['bss'], 'ELF target section not contained in declared NRM owner')
        require(region(binary, source['rom']+row['off'], 4) == struct.pack('>I', word), 'tool modified a local relocation instruction/pointer')
        corrected = target-dest['ram']
        if kind == 6 and address in orphan_lows:
            # LLVM may reuse the same symbol's LUI inside a basic block even
            # with MachineCSE disabled. This is safe at EVERY runtime base
            # aligned to 16 bytes only if all of that symbol's high targets
            # have the same carry for every such base. Prove the stronger,
            # simple condition: all offsets lie in the same 16-byte cell.
            # Otherwise refuse to publish, rather than depend on mod order.
            candidates = highs.get(tuple(symbol.items()), [])
            require(candidates and all((h-dest['ram'])//16 == corrected//16 for h in candidates),
                    'shared LUI has an ambiguous relocation carry at '+hex(address))
            checked_orphans += 1
        if corrected != row['target']:
            changes.append(dict(address=hex(address), kind=kind, before=hex(row['target']), after=hex(corrected)))
            struct.pack_into('<I', out, row['slot']+8, corrected)
    require(checked_orphans == len(orphan_lows), 'unverified orphan LO16 relocation')
    symbols(out, binary)
    report = dict(schema=1, mode='static-only; no game/mod execution', elf_sha256=hashlib.sha256(elf).hexdigest(),
                  binary_sha256=hashlib.sha256(binary).hexdigest(), symbols_sha256=hashlib.sha256(out).hexdigest(),
                  sections=len(sections), relocations=len(relocs), local_relocations=local, paired_highs=len(pairs),
                  carry_checked_shared_lows=checked_orphans, changes=changes)
    return bytes(out), report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('elf', 'symbols', 'binary', 'report'): parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    require(not args.report.exists(), 'choose a new report file')
    original = args.symbols.read_bytes()
    updated, report = normalize(args.elf.read_bytes(), original, args.binary.read_bytes())
    # Only the builder's NEW, intermediate symbol file is canonicalized. Its
    # game binary, input ELF, package resources and imports are left untouched.
    args.symbols.write_bytes(updated)
    args.report.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print('Static relocation inspection: '+str(report['relocations'])+' records, '+str(len(report['changes']))+' complete-target corrections; no execution.')


if __name__ == '__main__': main()
