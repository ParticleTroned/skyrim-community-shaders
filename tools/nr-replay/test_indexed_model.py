"""Shared-body CPU admission tests without captured binaries or a GPU."""
from pathlib import Path
import json
import struct
import tempfile
import unittest
from unittest import mock

import indexed_model as model


class IndexedTests(unittest.TestCase):
    def test_packet_index_preserves_opcode_predicate_and_dependency_bits(self):
        for opcode, shift, destination, index in (('LDC', 38, 'R4', 1), ('LDCU', 37, 'UR6', 19)):
            for width in ('', '.64'):
                before = struct.pack('<QQ', (0x380 << shift) | (255 << 24) | 0x40000 | 0x7ab9, 0x001e620008000a00)
                instruction = f'@P3 {opcode}{width} {destination}, c[0x0][0x380]'
                after, decoded = model.address_load(before, instruction)
                self.assertEqual(after[:3], before[:3])
                self.assertEqual(after[4:], before[4:])
                self.assertEqual(after[3], index)
                self.assertEqual(decoded, instruction.replace('[0x380]', f'[{"UR" if opcode == "LDCU" else "R"}{index}+0x380]'))

    def test_packet_index_rejects_bounds_existing_index_and_unqualified_width(self):
        for instruction, offset, index in (
            ('LDC R4, c[0x0][0x37c]', 0x37c, 255),
            ('LDC.64 R4, c[0x0][0x3dc]', 0x3dc, 255),
            ('LDC R4, c[0x0][0x380]', 0x380, 4),
            ('LDC R4, c[0x0][0x384]', 0x380, 255),
            ('LDCU.128 UR4, c[0x0][0x380]', 0x380, 255),
        ):
            with self.assertRaises(ValueError):
                model.address_load(struct.pack('<QQ', (offset << 38) | (index << 24), 0), instruction)

    def test_register_admission_understands_scalar_wide_multiply_operand(self):
        model.admit_registers({0: 'LDC R1, c[0x0][0x37c]', 16: 'IMAD.WIDE R6, R0, 0x10, R120'})
        for instruction in ('IMAD.WIDE R0, R6, 0x10, R120', 'IMAD.WIDE R6, R2, 0x10, R0',
                            'LDC.64 R0, c[0x0][0x380]', 'LDG.E R4, [R0.64]',
                            'HMMA.1688.F32 R0, R8, R12, R16', 'LDCU.64 UR18, c[0x0][0x380]',
                            'LDCU.128 UR16, c[0x0][0x380]', 'MOV UR19, 0x1', 'MOV R1, R2',
                            'STL [R4], R6', 'CALL 0x40', 'S2R R6, SR_CTAID.Z'):
            with self.subTest(instruction=instruction), self.assertRaises(ValueError):
                model.admit_registers({0: 'LDC R1, c[0x0][0x37c]', 16: instruction})

    def test_prefix_fixes_immediate_source_dependency_and_keeps_wait(self):
        source = struct.pack('<QQ', 0x7919, 0x000e220000002700)
        multiply = struct.pack('<QQ', 0x0000006000007824, 0x001fca00078e02ff)
        transfer = struct.pack('<QQ', 0x00000000060472ca, 0x000fc000000e0000)
        prefix = model.make_prefix({'S2R R0, SR_CTAID.Z': source, 'IMAD R0, R0, 0x60, RZ': multiply}, {'R2UR UR4, R6': transfer})
        self.assertEqual(len(prefix), 128)
        low, high = struct.unpack_from('<QQ', prefix)
        self.assertEqual((low >> 16) & 255, 1)
        self.assertEqual((high >> 41) & 15, 2)
        self.assertEqual((high >> 46) & 7, 0)
        self.assertEqual(high & ~model.clone.STALL_MASK, struct.unpack('<QQ', source)[1] & ~model.clone.STALL_MASK)
        self.assertEqual((struct.unpack_from('<Q', prefix, 24)[0] >> 52) & 63, 1)
        for pc in range(32, 128, 16):
            self.assertEqual((struct.unpack_from('<Q', prefix, pc + 8)[0] >> 41) & 15, 15)
        bad_multiply = bytearray(multiply)
        struct.pack_into('<Q', bad_multiply, 8, 0x000fca00078e02ff)
        with self.assertRaises(ValueError):
            model.make_prefix({'S2R R0, SR_CTAID.Z': source, 'IMAD R0, R0, 0x60, RZ': bytes(bad_multiply)}, {'R2UR UR4, R6': transfer})

    def test_source_identity_fails_before_transforming_an_unknown_kernel(self):
        with self.assertRaises(ValueError):
            model.transform(bytes(45056), {}, bytes(128))

    def test_interruption_and_existing_outputs_cannot_report_success(self):
        with tempfile.TemporaryDirectory() as directory:
            generator = object.__new__(model.Generator)
            generator.output = Path(directory) / 'new'
            generator.inputs, generator.entries, generator.bundles, generator.jobs = {}, [], [], []
            with mock.patch.object(generator, 'generate', side_effect=KeyboardInterrupt), self.assertRaises(KeyboardInterrupt):
                generator.execute()
            receipt = json.loads((generator.output / 'audit.json').read_text())
            self.assertEqual(receipt['status'], 'failed')
            self.assertFalse(receipt['gpuExecuted'])
            before = (generator.output / 'audit.json').read_bytes()
            with self.assertRaises(FileExistsError):
                generator.execute()
            self.assertEqual((generator.output / 'audit.json').read_bytes(), before)


if __name__ == '__main__':
    unittest.main()
