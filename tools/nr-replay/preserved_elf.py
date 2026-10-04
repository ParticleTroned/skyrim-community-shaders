"""CPU-only native CUDA ELF emitter preserving captured resource and PC metadata.

The caller owns instruction, branch and convergence-target transformation. This
module rejects unknown PC-bearing metadata instead of inferring its meaning.
"""
from pathlib import Path
import hashlib
import struct

EH = struct.Struct('<16sHHIQQQIHHHHHH')
SH = struct.Struct('<IIQQQQIIQQ')
PH = struct.Struct('<IIQQQQQQ')
SYM = struct.Struct('<IBBHQQ')
RELA = struct.Struct('<QQq')
KNOWN = {0x66, 0x37, 0x17, 0x50, 0x1b, 0x4c, 0x5f, 0x4a, 0x1c,
         0x19, 0x0a, 0x36, 0x6b, 0x6d, 0x29, 0x28, 0x1e, 0x55,
         0x31, 0x39, 0x38}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def align(value, alignment):
    require(alignment > 0 and alignment & (alignment - 1) == 0, 'invalid alignment')
    return (value + alignment - 1) & -alignment


def read_elf(source, expected_sha):
    data = Path(source).read_bytes()
    require(64 <= len(data) <= 128 * 1024 * 1024 and sha(data) == expected_sha,
            'source size or SHA256 mismatch')
    h = list(EH.unpack_from(data))
    require(h[0][:6] == b'\x7fELF\x02\x01' and h[1] == 2 and h[2] == 190 and
            h[7] == 0x6007802 and h[8] == 64 and h[9] == PH.size and h[11] == SH.size,
            'unsupported captured CUDA ELF ABI')
    require(0 < h[12] <= 4096 and h[13] < h[12] and 0 < h[10] <= 32 and
            h[6] + h[12] * SH.size <= len(data) and h[5] + h[10] * PH.size <= len(data),
            'ELF table extent invalid')
    raw = [list(SH.unpack_from(data, h[6] + i * SH.size)) for i in range(h[12])]
    names = data[raw[h[13]][4]:raw[h[13]][4] + raw[h[13]][5]]
    sections = []
    for i, sh in enumerate(raw):
        require(sh[0] < len(names) and b'\0' in names[sh[0]:], 'section name invalid')
        name = names[sh[0]:names.index(0, sh[0])].decode('utf-8', errors='strict')
        require(sh[1] == 8 or sh[4] + sh[5] <= len(data), 'section outside input')
        sections.append(dict(index=i, name=name, header=sh,
                             data=b'' if sh[1] == 8 else data[sh[4]:sh[4] + sh[5]]))
    require(len({s['name'] for s in sections}) == len(sections), 'duplicate section name')
    mercury = [s['index'] for s in sections if 'merc' in s['name']]
    limit = min(mercury) if mercury else len(sections)
    require(all('merc' in s['name'] for s in sections[limit:]), 'noncontiguous Mercury suffix')
    sections = sections[:limit]
    require(h[13] < limit, 'native name table removed')
    for s in sections:
        sh = s['header']
        require(sh[6] < limit, 'native section link targets removed section')
        if sh[2] & 64:
            require(sh[7] < limit, 'native section info targets removed section')
    programs = [list(PH.unpack_from(data, h[5] + i * PH.size)) for i in range(h[10])]
    return data, h, sections, programs


def attributes(data):
    p = 0
    while p < len(data):
        require(p + 4 <= len(data), 'truncated NV attribute')
        fmt, kind, value = struct.unpack_from('<BBH', data, p)
        require(fmt in (1, 2, 3, 4), 'unknown NV attribute format')
        end = p + 4 + (value if fmt == 4 else 0)
        require(end <= len(data), 'NV attribute payload outside section')
        yield fmt, kind, value, data[p + 4:end] if fmt == 4 else data[p + 2:p + 4]
        p = end


def encode_attribute(fmt, kind, value, payload):
    if fmt == 4:
        require(len(payload) <= 65535, 'NV attribute exceeds encoding')
        return struct.pack('<BBH', fmt, kind, len(payload)) + payload
    return struct.pack('<BBH', fmt, kind, value)


def validate_transform(transform, old_bytes):
    require(set(transform) <= {'code', 'pc_maps', 'parameter_bytes', 'register_count'},
            'unknown transform field')
    code, maps, parameter = transform['code'], transform['pc_maps'], transform['parameter_bytes']
    require(isinstance(code, bytes) and 0 < len(code) <= 4 * 1024 * 1024 and len(code) % 128 == 0,
            'transformed native code extent invalid')
    require(isinstance(parameter, int) and 0 < parameter <= 4096 and parameter % 4 == 0,
            'parameter extent invalid')
    require(isinstance(maps, list) and 1 <= len(maps) <= 2, 'one or two complete PC maps required')
    ranges = []
    for mapping in maps:
        require(isinstance(mapping, dict) and set(mapping) == set(range(0, old_bytes + 1, 16)),
                'PC map does not cover every source instruction and exclusive end')
        values = [mapping[p] for p in range(0, old_bytes + 1, 16)]
        require(all(isinstance(p, int) and 0 <= p <= len(code) and p % 16 == 0 for p in values),
                'PC map output outside aligned code')
        require(all(b >= a + 16 for a, b in zip(values, values[1:])), 'PC map is not strictly ordered')
        ranges.append((values[0], values[-1]))
    ranges.sort()
    require(all(b[0] >= a[1] for a, b in zip(ranges, ranges[1:])), 'clone PC ranges overlap')
    if 'register_count' in transform:
        require(isinstance(transform['register_count'], int) and 1 <= transform['register_count'] <= 255,
                'register count invalid')


def relocate_attributes(data, transform):
    maps, parameter = transform['pc_maps'], transform['parameter_bytes']
    result = bytearray()
    old_parameter = None
    seen = set()
    for fmt, kind, value, payload in attributes(data):
        require(kind in KNOWN and kind not in seen, f'unknown or repeated per-function attribute {kind:#x}')
        seen.add(kind)
        if kind in (0x1c, 0x28, 0x31):
            require(fmt == 4 and len(payload) % 4 == 0, 'PC metadata extent invalid')
            pcs = struct.unpack('<' + 'I' * (len(payload) // 4), payload)
            require(all(pc in m and pc != max(m) for m in maps for pc in pcs), 'metadata PC absent from map')
            payload = b''.join(struct.pack('<I', m[pc]) for m in maps for pc in pcs)
        elif kind == 0x29:
            require(fmt == 4 and len(payload) % 4 == 0, 'cooperative mask extent invalid')
            payload *= len(maps)
        elif kind in (0x55, 0x39):
            stride, offset = (8, 4) if kind == 0x55 else (16, 0)
            require(fmt == 4 and len(payload) % stride == 0, 'instruction record extent invalid')
            records = []
            for m in maps:
                for p in range(0, len(payload), stride):
                    record = bytearray(payload[p:p + stride])
                    if kind == 0x55:
                        require(struct.unpack_from('<I', record)[0] == 1, 'unknown annotation kind')
                    pc = struct.unpack_from('<I', record, offset)[0]
                    require(pc in m and pc != max(m), 'record PC absent from map')
                    struct.pack_into('<I', record, offset, m[pc])
                    records.append(record)
            payload = b''.join(records)
        elif kind == 0x19:
            require(fmt == 3 and value > 0, 'parameter-size attribute invalid')
            old_parameter, value = value, parameter
        elif kind == 0x0a:
            require(fmt == 4 and len(payload) == 8, 'parameter-bank attribute invalid')
            symbol, packed = struct.unpack('<II', payload)
            require(packed & 0xffff == 0x380, 'unexpected packet base')
            payload = struct.pack('<II', symbol, (parameter << 16) | 0x380)
        elif kind == 0x17:
            require(fmt == 4 and len(payload) == 12, 'parameter definition invalid')
            index, offset, packed = struct.unpack('<III', payload)
            require(index == 0 and offset == 0 and packed >> 18 > 0, 'only one aggregate parameter supported')
            payload = struct.pack('<III', index, offset, (packed & 0x3ffff) | (parameter << 18))
        elif kind == 0x1b and 'register_count' in transform:
            require(fmt == 3 and transform['register_count'] <= value, 'register count exceeds original cap')
        result += encode_attribute(fmt, kind, value, payload)
    require({0x19, 0x0a, 0x17} <= seen and old_parameter is not None, 'packet metadata missing')
    return bytes(result), old_parameter


def read_leb(data, p, signed=False):
    start, value, shift = p, 0, 0
    while True:
        require(p < len(data) and p - start < 10, 'invalid LEB128')
        byte = data[p]; p += 1
        value |= (byte & 0x7f) << shift; shift += 7
        if not byte & 0x80:
            if signed and byte & 0x40:
                value -= 1 << shift
            return value, p


def cfi_tokens(data):
    """Decodes the exact supported DWARF CFA operand grammar, retaining raw bytes."""
    p = 0
    while p < len(data):
        start, opcode = p, data[p]; p += 1
        advance = None
        if opcode & 0xc0 == 0x40:
            advance = opcode & 0x3f
        elif opcode & 0xc0 == 0x80:
            _, p = read_leb(data, p)
        elif opcode & 0xc0 == 0xc0:
            pass
        elif opcode in (2, 3, 4):
            size = {2: 1, 3: 2, 4: 4}[opcode]
            require(p + size <= len(data), 'truncated CFI advance')
            advance = int.from_bytes(data[p:p + size], 'little'); p += size
        elif opcode in (0, 10, 11):
            pass
        elif opcode in (5, 9, 12, 20):
            _, p = read_leb(data, p); _, p = read_leb(data, p)
        elif opcode in (6, 7, 8, 13, 14):
            _, p = read_leb(data, p)
        elif opcode in (17, 18, 21):
            _, p = read_leb(data, p); _, p = read_leb(data, p, True)
        elif opcode == 19:
            _, p = read_leb(data, p, True)
        else:
            raise ValueError(f'unsupported DWARF CFI opcode {opcode:#x}')
        yield data[start:p], advance


def relocate_cfi(data, mapping, code_alignment):
    old_pc, new_pc = 0, mapping[0]
    output = bytearray()
    for raw, advance in cfi_tokens(data):
        if advance is None:
            output += raw
        else:
            old_pc += advance * code_alignment
            require(old_pc in mapping, 'CFI row location absent from complete instruction map')
            wanted = mapping[old_pc]
            delta = wanted - new_pc
            require(delta >= 0 and delta % code_alignment == 0 and delta // code_alignment <= 0xffffffff,
                    'CFI advance cannot represent transformed location')
            output += b'\x04' + struct.pack('<I', delta // code_alignment)
            new_pc = wanted
    return bytes(output)


def relocate_debug_frame(data, reloc_data, transformed_symbols):
    require(len(reloc_data) % RELA.size == 0, 'debug relocation table extent invalid')
    relocations = {}
    for p in range(0, len(reloc_data), RELA.size):
        off, info, addend = RELA.unpack_from(reloc_data, p)
        require(off not in relocations and info & 0xffffffff == 2 and addend == 0,
                'unsupported debug relocation')
        relocations[off] = (info, addend)
    output, new_reloc, cie_map, code_alignments, consumed = bytearray(), {}, {}, {}, set()
    p = 0
    while p < len(data):
        require(p + 20 <= len(data), 'truncated DWARF64 unit')
        marker, length, cie = struct.unpack_from('<IQQ', data, p)
        end = p + 12 + length
        require(marker == 0xffffffff and length >= 8 and end <= len(data), 'unsupported DWARF frame unit')
        if cie == 0xffffffffffffffff:
            require(data[p + 20:p + 22] == b'\x03\0', 'unsupported DWARF CIE version/augmentation')
            code_alignment, q = read_leb(data, p + 22)
            _, q = read_leb(data, q, True); _, q = read_leb(data, q)
            require(code_alignment > 0, 'invalid CIE code alignment')
            require(all(advance is None for _, advance in cfi_tokens(data[q:end])),
                    'CIE location advances require explicit relocation support')
            cie_map[p], code_alignments[p] = len(output), code_alignment
            output += data[p:end]
        else:
            require(cie in cie_map and p + 36 <= end and p + 20 in relocations,
                    'FDE CIE or address relocation missing')
            info, addend = relocations[p + 20]; consumed.add(p + 20)
            initial, extent = struct.unpack_from('<QQ', data, p + 20)
            require(initial == 0 and extent > 0, 'unsupported FDE address/range')
            transform = transformed_symbols.get(info >> 32)
            if transform is None:
                record = bytearray(data[p:end]); struct.pack_into('<Q', record, 12, cie_map[cie])
                new_reloc[p + 20] = [RELA.pack(len(output) + 20, info, addend)]
                output += record
            else:
                require(extent == max(transform['pc_maps'][0]), 'FDE range differs from original native text')
                new_reloc[p + 20] = []
                for mapping in transform['pc_maps']:
                    body = relocate_cfi(data[p + 36:end], mapping, code_alignments[cie])
                    record = bytearray(struct.pack('<IQQQQ', 0xffffffff, 0, cie_map[cie], 0,
                                                   mapping[extent] - mapping[0]) + body)
                    record += bytes(align(len(record), 8) - len(record))
                    struct.pack_into('<Q', record, 4, len(record) - 12)
                    new_reloc[p + 20].append(RELA.pack(len(output) + 20, info, mapping[0]))
                    output += record
        p = end
    require(consumed == set(relocations), 'unhandled debug frame relocation')
    return bytes(output), b''.join(record for off in relocations for record in new_reloc[off])


def emit_module(source, pinned_sha, destination, transforms):
    """Writes a new native-only ELF; never loads or executes a CUDA module.

    Each transform provides code bytes, one/two complete original-to-new PC maps
    (including exclusive end), parameter_bytes, and optional register_count.
    """
    destination = Path(destination)
    require(not destination.exists(), 'destination must be a new immutable artifact')
    data, h, sections, programs = read_elf(source, pinned_sha)
    by_name = {s['name']: s for s in sections}
    require(isinstance(transforms, dict) and len(transforms) <= 128, 'transform table invalid')
    symbols = bytearray(by_name['.symtab']['data'])
    require(len(symbols) % SYM.size == 0, 'symbol extent invalid')
    original_symbol_names = by_name['.strtab']['data']
    for offset in range(0, len(symbols), SYM.size):
        entry = SYM.unpack_from(symbols, offset)
        require(entry[0] < len(original_symbol_names) and b'\0' in original_symbol_names[entry[0]:],
                'native symbol name outside string table')
        require(entry[3] < len(sections) or entry[3] >= 0xff00, 'native symbol targets removed section')
    transformed_symbols, changes = {}, []
    for name, transform in transforms.items():
        require(all(prefix + name in by_name for prefix in ('.text.', '.nv.info.', '.nv.constant0.')),
                'entry is not a complete native function')
        text, meta, bank = (by_name[prefix + name] for prefix in ('.text.', '.nv.info.', '.nv.constant0.'))
        validate_transform(transform, len(text['data']))
        for s in sections:
            if s['header'][1] in (4, 9) and s['header'][7] == text['index']:
                require(not s['data'], 'nonempty text relocations require instruction-specific handling')
        meta['data'], old_parameter = relocate_attributes(meta['data'], transform)
        new_parameter = transform['parameter_bytes']
        require(len(bank['data']) == 0x380 + old_parameter and not any(bank['data']),
                'constant bank contains static data or an unsupported extent')
        bank['data'] = bytes(0x380 + new_parameter)
        text['data'] = transform['code']
        found = []
        for offset in range(0, len(symbols), SYM.size):
            entry = list(SYM.unpack_from(symbols, offset))
            if entry[3] != text['index'] or entry[1] & 15 != 2:
                continue
            symbol_name = original_symbol_names[entry[0]:original_symbol_names.index(0, entry[0])].decode()
            require(symbol_name == name and entry[4] == 0 and entry[5] == max(transform['pc_maps'][0]),
                    'unsupported native function symbol')
            entry[5] = len(transform['code']); SYM.pack_into(symbols, offset, *entry)
            found.append(offset // SYM.size)
        require(len(found) == 1, 'native function symbol missing or ambiguous')
        require(text['header'][7] == found[0], 'native text info is not the preserved function symbol identity')
        transformed_symbols[found[0]] = transform
        changes.append(dict(entry=name, originalParameterBytes=old_parameter, parameterBytes=new_parameter,
                            nativeBytes=len(transform['code']), nativeSha256=sha(transform['code']),
                            cloneCount=len(transform['pc_maps']), functionSymbol=found[0]))
    by_name['.symtab']['data'] = bytes(symbols)
    global_info = bytearray()
    for fmt, kind, value, payload in attributes(by_name['.nv.info']['data']):
        require(kind in (0x2f, 0x11, 0x12) and fmt == 4 and len(payload) == 8,
                'unknown global resource metadata')
        symbol, resource = struct.unpack('<II', payload)
        if kind == 0x2f and symbol in transformed_symbols and 'register_count' in transformed_symbols[symbol]:
            payload = struct.pack('<II', symbol, transformed_symbols[symbol]['register_count'])
        global_info += encode_attribute(fmt, kind, value, payload)
    by_name['.nv.info']['data'] = bytes(global_info)
    if transformed_symbols:
        require('.debug_frame' in by_name and '.rela.debug_frame' in by_name, 'debug frame provenance missing')
        debug, reloc = relocate_debug_frame(by_name['.debug_frame']['data'], by_name['.rela.debug_frame']['data'],
                                            transformed_symbols)
        by_name['.debug_frame']['data'], by_name['.rela.debug_frame']['data'] = debug, reloc

    output = bytearray(data[:64])
    new_headers = []
    for s in sections:
        sh = s['header'].copy()
        if s['index'] == 0:
            require(sh == [0] * 10, 'nonzero ELF null section')
        else:
            sh[4] = align(len(output), max(1, sh[8]))
            output += bytes(sh[4] - len(output))
            if sh[1] != 8:
                sh[5] = len(s['data']); output += s['data']
        new_headers.append(sh)
    section_offset = align(len(output), 8)
    program_offset = section_offset + len(sections) * SH.size
    new_programs, table_count = [], 0
    for original in programs:
        ph = original.copy()
        require(ph[0] in (1, 6) and ph[3] == 0 and ph[4] == 0, 'unknown program-header ABI')
        if ph[2] == h[5] and ph[5] == h[10] * PH.size:
            require(ph[6] == ph[5], 'program-table memory extent differs')
            ph[2] = program_offset; table_count += 1
        elif ph[1] == 6:
            candidates = [s for s in sections if s['header'][1] == 8 and
                          ph[2] <= s['header'][4] <= ph[2] + ph[5]]
            require(ph[0] == 1 and candidates and not any(data[ph[2]:ph[2] + ph[5]]) and
                    not any(s['header'][1] != 8 and s['header'][2] & 2 and s['header'][5] and
                            ph[2] <= s['header'][4] < ph[2] + ph[5] for s in sections),
                    'writable load segment is not only native NOBITS alignment padding')
            tail = ph[6] - ph[5]
            require(tail > 0, 'native NOBITS segment has no memory extent')
            ph[2] = min(new_headers[s['index']][4] for s in candidates)
            ph[5] = max(new_headers[s['index']][4] for s in candidates) - ph[2]
            ph[6] = ph[5] + tail
        else:
            members = [s for s in sections if s['header'][2] & 2 and s['header'][1] != 8 and
                       s['header'][5] and s['header'][4] >= ph[2] and
                       s['header'][4] + s['header'][5] <= ph[2] + ph[5]]
            require(ph[0] == 1 and members and ph[5] == ph[6], 'unsupported native load segment')
            require(min(s['header'][4] for s in members) == ph[2] and
                    max(s['header'][4] + s['header'][5] for s in members) == ph[2] + ph[5],
                    'load segment includes unknown leading/trailing data')
            ph[2] = min(new_headers[s['index']][4] for s in members)
            ph[5] = max(new_headers[s['index']][4] + new_headers[s['index']][5] for s in members) - ph[2]
            ph[6] = ph[5]
        new_programs.append(ph)
    require(table_count == 2, 'expected PHDR and read-only table LOAD pair')
    output += bytes(section_offset - len(output))
    output += b''.join(SH.pack(*s) for s in new_headers)
    output += b''.join(PH.pack(*p) for p in new_programs)
    h[5], h[6], h[12] = program_offset, section_offset, len(sections)
    EH.pack_into(output, 0, *h)
    with destination.open('xb') as stream:
        stream.write(output)
    return dict(schema='nr-preserved-native-ELF-v1', source=str(source), sourceSha256=pinned_sha,
                path=str(destination), sha256=sha(output), bytes=len(output), entries=changes,
                sectionCount=len(sections), gpuExecuted=False, driverLoadValidated=False,
                instructionControlFlowValidatedByEmitter=False,
                preserved=['original ELF ABI', 'native section/symbol identities', 'shared resource extents',
                           'frame and minimum stack sizes', 'global resource metadata', 'non-PC native attributes'],
                relocated=['instruction-location attributes', 'function symbol extent', 'parameter bank/aggregate extent',
                           'DWARF FDE initial locations/ranges/CFI rows', 'ELF payload and program file offsets'])
