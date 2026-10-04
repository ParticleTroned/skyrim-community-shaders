"""CPU-only shared-model index and register-allocation contract tests."""
import struct
import unittest

import indexed_full_model as model


class FullModelTests(unittest.TestCase):
    def test_catalog_uses_host_schema_and_deterministic_module_order(self):
        modules = [dict(originalSha256=f'{index:064x}', path=str(index), sha256='a' * 64, functions=[])
                   for index in reversed(range(9))]
        result = model.catalog(2, modules)
        self.assertEqual(result['schema'], 'nr-model-kernel-replacement-v1')
        self.assertEqual([item['originalSha256'] for item in result['modules']], sorted(item['originalSha256'] for item in modules))
        self.assertEqual(modules[0]['path'], '8')
        with self.assertRaises(ValueError):
            model.catalog(2, modules[:-1])
        with self.assertRaises(ValueError):
            model.catalog(2, [modules[0]] * 9)

    def test_uniform_gap_is_bounded_for_special_and_wide_operands(self):
        model.admit_uniform_gap({0: 'TEX R4, R6, UR28', 16: 'UBLKCP.S.G [R2], [UR22]',
                                 32: 'LDCU.128 UR40, c[0x0][0x380]', 48: 'ELECT P0, UR79, PT',
                                 64: 'MOV R74, UR79'})
        for text in ('LDCU UR48, c[0x0][0x380]', 'TEX R2, R4, UR40', 'MOV UR79, UR4',
                     'LDCU.64 UR47, c[0x0][0x380]'):
            with self.subTest(text=text), self.assertRaises(ValueError):
                model.admit_uniform_gap({0: text})

    def test_prefix_selects_item_for_all_original_z_shapes(self):
        optimized = {'S2R R0, SR_CTAID.Z': struct.pack('<QQ', 0x7919, 0x000e220000002700),
                     'IMAD R0, R0, 0x60, RZ': struct.pack('<QQ', 0x0000006000007824, 0x001fca00078e02ff)}
        unoptimized = {'R2UR UR4, R6': struct.pack('<QQ', 0x00000000060472ca, 0x000fc000000e0000)}
        geometry = {'SHF.R.U32.HI R0, RZ, 0x1, R0': struct.pack('<QQ', 0x00000001ff007819, 0x001fca0000011600)}
        for z in (1, 2, 4):
            for register in (10, 128, 168):
                for stride in (16, 80, 96, 384):
                    code = model.prefix_words(optimized, unoptimized, geometry, register, stride, z)
                    self.assertEqual(len(code), 128)
                    low, high = struct.unpack_from('<QQ', code)
                    self.assertEqual((low >> 16) & 255, register)
                    self.assertEqual((high >> 41) & 15, 2)
                    low, high = struct.unpack_from('<QQ', code, 16)
                    shift = (low >> 32) & 0xffffffff
                    self.assertEqual((low >> 16) & 255, register)
                    self.assertEqual(high & 255, register)
                    self.assertEqual((high >> 52) & 63, 1)
                    for physical in range(z * 2):
                        self.assertEqual((physical >> shift) * stride, (physical // z) * stride)
                    low, _ = struct.unpack_from('<QQ', code, 32)
                    self.assertEqual((low >> 32) & 0xffffffff, stride)
                    self.assertEqual((low >> 16) & 0xffff, register | (register << 8))
        for register, stride, z in ((255, 96, 1), (168, 17, 1), (168, 96, 3)):
            with self.assertRaises(ValueError):
                model.prefix_words(optimized, unoptimized, geometry, register, stride, z)

    def test_indexing_preserves_runtime_constants_stack_and_all_control_words(self):
        runtime = struct.pack('<QQ', (0x37c << 38) | (255 << 24) | (1 << 16), 0x000fe20000000800)
        wide = struct.pack('<QQ', (0x380 << 37) | (255 << 24) | (8 << 16), 0x001e620008000c00)
        logical = runtime + wide
        code, loads = model.index_body(logical, {0: 'LDC R1, c[0x0][0x37c]', 16: 'LDCU.128 UR8, c[0x0][0x380]'}, bytes(128), 16, 168)
        self.assertEqual(code[128:144], runtime)
        self.assertEqual(code[144:147], wide[:3])
        self.assertEqual(code[147], 48)
        self.assertEqual(code[148:], wide[4:])
        self.assertEqual(loads[0]['indexed'], 'LDCU.128 UR8, c[0x0][UR48+0x380]')


if __name__ == '__main__':
    unittest.main()
