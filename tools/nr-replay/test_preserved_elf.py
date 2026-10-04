"""CPU-only metadata tests; synthetic fixtures contain no provider instructions."""

import hashlib
from pathlib import Path
import struct
import tempfile
import unittest

import preserved_elf as elf


def attribute(kind, values):
    payload = struct.pack('<' + 'I' * len(values), *values)
    return struct.pack('<BBH', 4, kind, len(payload)) + payload


def dwarf_unit(body):
    result = bytearray(struct.pack('<IQ', 0xffffffff, 0) + body)
    result += bytes((-len(result)) % 8)
    struct.pack_into('<Q', result, 4, len(result) - 12)
    return bytes(result)


def synthetic_elf():
    names = ['', '.shstrtab', '.strtab', '.symtab', '.debug_frame', '.rela.debug_frame',
             '.nv.info', '.nv.info.fixture', '.text.fixture', '.nv.shared.fixture',
             '.nv.constant0.fixture', '.nv.merc.fixture']
    name_table = b'\0'
    name_offsets = [0]
    for name in names[1:]:
        name_offsets.append(len(name_table))
        name_table += name.encode() + b'\0'
    symbols = (bytes(24) + struct.pack('<IBBHQQ', 0, 3, 0, 8, 0, 0) +
               struct.pack('<IBBHQQ', 0, 3, 0, 10, 0, 0) +
               struct.pack('<IBBHQQ', 1, 0x12, 0, 8, 0, 128))
    cie = dwarf_unit(struct.pack('<Q', 0xffffffffffffffff) + b'\x03\0\x04\x7c\0\x0c\x01\0')
    cfi = b'\x04\x04\0\0\0\x0c\x01\x08\x04\x14\0\0\0\x0e\0'
    fde = dwarf_unit(struct.pack('<QQQ', 0, 0, 128) + cfi)
    metadata = (attribute(0x66, [3]) + attribute(0x37, [134]) +
                attribute(0x17, [0, 0, (24 << 18) | 0x1f000]) +
                struct.pack('<BBH', 3, 0x19, 24) + attribute(0x0a, [2, (24 << 16) | 0x380]) +
                struct.pack('<BBH', 3, 0x1b, 255) + attribute(0x1c, [96]) +
                attribute(0x28, [32, 64]) + attribute(0x29, [2, 4]) +
                attribute(0x55, [1, 80]) + attribute(0x39, [48, 2, 0, 4]) +
                attribute(0x1e, [0]))
    payloads = [b'', name_table, b'\0fixture\0', symbols, cie + fde,
                struct.pack('<QQq', len(cie) + 20, (3 << 32) | 2, 0),
                attribute(0x2f, [3, 32]) + attribute(0x11, [3, 8]) + attribute(0x12, [3, 8]),
                metadata, bytes(range(128)), b'', bytes(0x380 + 24), b'mercury fixture']
    headers = [[name_offsets[i], 1, 0, 0, 0, len(payloads[i]), 0, 0, 1, 0] for i in range(len(names))]
    headers[0] = [0] * 10
    headers[1][1] = headers[2][1] = 3
    headers[3][1] = 2; headers[3][6:10] = [2, 3, 8, 24]
    headers[5][1:3] = [4, 64]; headers[5][6:10] = [3, 4, 8, 24]
    headers[6][1] = headers[7][1] = 0x70000000
    headers[6][6] = headers[7][6] = 3
    headers[7][2] = 64; headers[7][7] = 8
    headers[8][2] = 6; headers[8][6:9] = [3, 3, 128]
    headers[9][1:3] = [8, 67]; headers[9][5] = 64; headers[9][7:9] = [8, 16]
    headers[10][2] = 66; headers[10][7:9] = [8, 4]
    data = bytearray(64)
    for index in range(1, len(headers)):
        header = headers[index]
        header[4] = (len(data) + header[8] - 1) & -header[8]
        data += bytes(header[4] - len(data))
        if header[1] != 8:
            data += payloads[index]
    shoff = (len(data) + 7) & -8
    phoff = shoff + len(headers) * 64
    programs = [[6, 4, phoff, 0, 0, 280, 280, 8], [1, 4, phoff, 0, 0, 280, 280, 8],
                [1, 5, headers[8][4], 0, 0, 128, 128, 8],
                [1, 6, headers[9][4], 0, 0, 0, 64, 8],
                [1, 4, headers[10][4], 0, 0, 0x380 + 24, 0x380 + 24, 8]]
    data += bytes(shoff - len(data))
    data += b''.join(struct.pack('<IIQQQQIIQQ', *header) for header in headers)
    data += b''.join(struct.pack('<IIQQQQQQ', *program) for program in programs)
    ident = b'\x7fELF\x02\x01\x01\x41\x08' + bytes(7)
    struct.pack_into('<16sHHIQQQIHHHHHH', data, 0, ident, 2, 190, 1, 0, phoff, shoff,
                     0x6007802, 64, 56, len(programs), 64, len(headers), 1)
    return bytes(data)


class PreservedElfTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='nr-elf-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / 'source.cubin'
        self.original = synthetic_elf()
        self.source.write_bytes(self.original)
        self.source_hash = hashlib.sha256(self.original).hexdigest()
        self.output = self.root / 'output.cubin'

    def sections(self, path, digest):
        return {section['name']: section for section in elf.read_elf(path, digest)[2]}

    def identity(self):
        return dict(code=bytes(range(128)), pc_maps=[{p: p for p in range(0, 129, 16)}], parameter_bytes=24)

    def emit(self, transforms):
        return elf.emit_module(self.source, self.source_hash, self.output, transforms)

    def test_identity_preserves_native_payloads_and_resources(self):
        receipt = self.emit({'fixture': self.identity()})
        before, after = self.sections(self.source, self.source_hash), self.sections(self.output, receipt['sha256'])
        self.assertEqual(set(before), set(after))
        self.assertNotIn('.nv.merc.fixture', after)
        for name, section in before.items():
            self.assertEqual(section['data'], after[name]['data'], name)
            for field in (0, 1, 2, 3, 5, 6, 7, 8, 9):
                self.assertEqual(section['header'][field], after[name]['header'][field], (name, field))

    def test_two_clone_packet_growth_pc_metadata_and_fde_ranges(self):
        first = {p: 128 + p + (128 if p > 32 else 0) for p in range(0, 129, 16)}
        second = {p: 384 + p + (128 if p > 32 else 0) for p in range(0, 129, 16)}
        receipt = self.emit({'fixture': dict(code=bytes(640), pc_maps=[first, second], parameter_bytes=64)})
        before, after = self.sections(self.source, self.source_hash), self.sections(self.output, receipt['sha256'])
        self.assertEqual(after['.nv.info']['data'], before['.nv.info']['data'])
        self.assertEqual(after['.nv.shared.fixture']['header'][5], 64)
        self.assertEqual(len(after['.nv.constant0.fixture']['data']), 0x3c0)
        attrs = {kind: (fmt, value, payload) for fmt, kind, value, payload in elf.attributes(after['.nv.info.fixture']['data'])}
        self.assertEqual(attrs[0x19][1], 64)
        self.assertEqual(struct.unpack('<III', attrs[0x17][2])[2] >> 18, 64)
        self.assertEqual(struct.unpack('<II', attrs[0x0a][2]), (2, 0x400380))
        self.assertEqual(struct.unpack('<II', attrs[0x1c][2]), (352, 608))
        self.assertEqual(struct.unpack('<IIII', attrs[0x28][2]), (160, 320, 416, 576))
        self.assertEqual(struct.unpack('<IIII', attrs[0x29][2]), (2, 4, 2, 4))
        self.assertEqual(struct.unpack('<IIII', attrs[0x55][2]), (1, 336, 1, 592))
        self.assertEqual(struct.unpack('<IIIIIIII', attrs[0x39][2]), (304, 2, 0, 4, 560, 2, 0, 4))
        relocations = after['.rela.debug_frame']['data']
        self.assertEqual(len(relocations), 48)
        frame = after['.debug_frame']['data']
        for offset, base in ((0, 128), (24, 384)):
            location, info, addend = struct.unpack_from('<QQq', relocations, offset)
            self.assertEqual((info, addend), ((3 << 32) | 2, base))
            self.assertEqual(struct.unpack_from('<QQ', frame, location), (0, 256))
            body = frame[location + 16:location + 16 + 15]
            self.assertEqual(body, b'\x04\x04\0\0\0\x0c\x01\x08\x04\x34\0\0\0\x0e\0')
        symbol = struct.unpack_from('<IBBHQQ', after['.symtab']['data'], 3 * 24)
        self.assertEqual(symbol[-1], 640)

    def test_all_complete_map_invariants(self):
        variants = []
        missing = self.identity(); del missing['pc_maps'][0][16]; variants.append(missing)
        endpoint = self.identity(); del endpoint['pc_maps'][0][128]; variants.append(endpoint)
        duplicate = self.identity(); duplicate['pc_maps'][0][32] = 16; variants.append(duplicate)
        unaligned = self.identity(); unaligned['pc_maps'][0][32] = 33; variants.append(unaligned)
        outside = self.identity(); outside['pc_maps'][0][128] = 144; variants.append(outside)
        overlap = self.identity(); overlap['pc_maps'] *= 2; variants.append(overlap)
        for transform in variants:
            with self.subTest(transform=transform), self.assertRaises(ValueError):
                self.emit({'fixture': transform})
            self.assertFalse(self.output.exists())

    def test_unknown_attributes_cfi_and_pc_records_fail_closed(self):
        transform = self.identity()
        metadata = self.sections(self.source, self.source_hash)['.nv.info.fixture']['data']
        for suffix in (b'\x03\xfe\0\0', attribute(0x55, [9, 16]), attribute(0x31, [17])):
            with self.subTest(suffix=suffix), self.assertRaises(ValueError):
                elf.relocate_attributes(metadata + suffix, transform)
        with self.assertRaisesRegex(ValueError, 'unsupported DWARF CFI'):
            list(elf.cfi_tokens(b'\x01'))
        with self.assertRaisesRegex(ValueError, 'absent'):
            elf.relocate_cfi(b'\x04\x01\0\0\0', transform['pc_maps'][0], 4)

    def test_cfi_variable_forms_and_truncated_operands(self):
        self.assertEqual(list(elf.cfi_tokens(b'\x42\x02\x03\x03\x04\0')), [(b'\x42', 2), (b'\x02\x03', 3), (b'\x03\x04\0', 4)])
        for value in (b'\x04\0', b'\x0c\x80', b'\x08\x80'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                list(elf.cfi_tokens(value))

    def test_source_pin_and_immutable_output(self):
        with self.assertRaisesRegex(ValueError, 'SHA256'):
            elf.emit_module(self.source, '0' * 64, self.output, {})
        self.output.write_bytes(b'prior artifact')
        with self.assertRaisesRegex(ValueError, 'immutable'):
            self.emit({})
        self.assertEqual(self.output.read_bytes(), b'prior artifact')

    def test_parameter_code_and_extra_field_bounds(self):
        for field, value in (('parameter_bytes', 0), ('parameter_bytes', 4097), ('code', b'bad'),
                             ('register_count', 256), ('register_count', True), ('unknown', True)):
            transform = self.identity(); transform[field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                self.emit({'fixture': transform})
            self.assertFalse(self.output.exists())

    def test_native_text_relocations_require_explicit_support(self):
        # Reuse the existing relocation section as a nonempty text relocation.
        data = bytearray(self.original)
        shoff = struct.unpack_from('<Q', data, 40)[0]
        struct.pack_into('<I', data, shoff + 5 * 64 + 44, 8)
        self.source.write_bytes(data)
        self.source_hash = hashlib.sha256(data).hexdigest()
        with self.assertRaisesRegex(ValueError, 'nonempty text relocations'):
            self.emit({'fixture': self.identity()})
        self.assertFalse(self.output.exists())

    def test_register_extension_requires_explicit_cap_and_preserves_frame(self):
        sections = self.sections(self.source, self.source_hash)
        metadata_offset = sections['.nv.info.fixture']['header'][4]
        data = bytearray(self.original)
        marker = bytes(data).index(struct.pack('<BBH', 3, 0x1b, 255), metadata_offset)
        struct.pack_into('<H', data, marker + 2, 32)
        self.source.write_bytes(data)
        self.source_hash = elf.sha(data)
        transform = self.identity()
        transform['register_count'] = 33
        with self.assertRaisesRegex(ValueError, 'exceeds original cap'):
            self.emit({'fixture': transform})
        transform['register_cap'] = 33
        receipt = self.emit({'fixture': transform})
        after = self.sections(self.output, receipt['sha256'])
        attrs = {kind: (fmt, value, payload) for fmt, kind, value, payload in elf.attributes(after['.nv.info.fixture']['data'])}
        self.assertEqual(attrs[0x1b][1], 33)
        global_attrs = {kind: struct.unpack('<II', payload) for _, kind, _, payload in elf.attributes(after['.nv.info']['data'])}
        self.assertEqual(global_attrs[0x2f], (3, 33))
        self.assertEqual(global_attrs[0x11], (3, 8))
        self.assertEqual(global_attrs[0x12], (3, 8))
        self.assertEqual(after['.text.fixture']['data'], bytes(range(128)))
        self.assertEqual(after['.text.fixture']['header'][7], 3)

    def test_register_cap_rejects_implicit_reduction_missing_count_and_overflow(self):
        for count, cap in ((None, 255), (32, 31), (33, 256), (32, 32), (33, True)):
            transform = self.identity()
            if count is not None:
                transform['register_count'] = count
            transform['register_cap'] = cap
            with self.subTest(count=count, cap=cap), self.assertRaises(ValueError):
                self.emit({'fixture': transform})
            self.assertFalse(self.output.exists())

    def test_empty_transform_still_checks_native_symbol_targets_and_names(self):
        shoff = struct.unpack_from('<Q', self.original, 40)[0]
        symoff = struct.unpack_from('<Q', self.original, shoff + 3 * 64 + 24)[0]
        for offset, fmt, value in ((symoff + 3 * 24 + 6, '<H', 11), (symoff + 3 * 24, '<I', 999)):
            data = bytearray(self.original)
            struct.pack_into(fmt, data, offset, value)
            self.source.write_bytes(data)
            self.source_hash = hashlib.sha256(data).hexdigest()
            with self.subTest(offset=offset), self.assertRaisesRegex(ValueError, 'native symbol'):
                self.emit({})
            self.assertFalse(self.output.exists())

    def test_cie_location_advance_requires_explicit_support(self):
        data = bytearray(self.original)
        shoff = struct.unpack_from('<Q', data, 40)[0]
        frame_offset = struct.unpack_from('<Q', data, shoff + 4 * 64 + 24)[0]
        data[frame_offset + 29] = 0x41
        self.source.write_bytes(data)
        self.source_hash = hashlib.sha256(data).hexdigest()
        with self.assertRaisesRegex(ValueError, 'CIE location advances'):
            self.emit({'fixture': self.identity()})
        self.assertFalse(self.output.exists())

    def test_register_count_updates_global_metadata_not_function_symbol_identity(self):
        transform = self.identity()
        transform['register_count'] = 40
        receipt = self.emit({'fixture': transform})
        after = self.sections(self.output, receipt['sha256'])
        global_attributes = {kind: struct.unpack('<II', payload) for _, kind, _, payload in elf.attributes(after['.nv.info']['data'])}
        self.assertEqual(global_attributes, {0x2f: (3, 40), 0x11: (3, 8), 0x12: (3, 8)})
        self.assertEqual(after['.text.fixture']['header'][7], 3)
        function_attributes = {kind: value for _, kind, value, _ in elf.attributes(after['.nv.info.fixture']['data'])}
        self.assertEqual(function_attributes[0x1b], 255)


if __name__ == '__main__':
    unittest.main(verbosity=2)
