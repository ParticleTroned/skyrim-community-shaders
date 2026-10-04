"""CPU admission checks for resource-neutral constant-packet indexing."""
import struct
import unittest

import indexed_neutral_model as model


class NeutralModelTests(unittest.TestCase):
    def setUp(self):
        self.words = {
            'S2UR UR5, SR_CTAID.Z': struct.pack('<QQ', 0x579c3, 0x000e240000002700),
            'USHF.R.U32.HI UR4, URZ, 0x1, UR5': struct.pack('<QQ', 0x00000001ff047899, 0x001fc80008011605),
            'UIMAD UR4, UR4, 0x60, URZ': struct.pack('<QQ', 0x00000060040478a4, 0x000fc8000f8e02ff),
        }

    def test_uniform_prefix_keeps_producer_wait_and_selects_packet(self):
        for z in (1, 2, 4):
            for stride in (16, 80, 96, 384):
                code = model.uniform_prefix(self.words, stride, z)
                self.assertEqual(len(code), 128)
                source, control = struct.unpack_from('<QQ', code)
                self.assertEqual((source >> 16) & 255, 48)
                self.assertEqual((control >> 41) & 15, 2)
                divide, control = struct.unpack_from('<QQ', code, 16)
                self.assertEqual((control >> 52) & 63, 1)
                self.assertEqual(control & 255, 48)
                shift = divide >> 32
                multiply, control = struct.unpack_from('<QQ', code, 32)
                self.assertEqual((multiply >> 16) & 65535, 48 | (48 << 8))
                self.assertEqual(multiply >> 32, stride)
                self.assertEqual((control >> 41) & 15, 15)
                for physical in range(2 * z):
                    self.assertEqual((physical >> shift) * stride, physical // z * stride)
        for stride, z in ((0, 1), (17, 1), (4096, 1), (96, 3)):
            with self.assertRaises(ValueError):
                model.uniform_prefix(self.words, stride, z)
        self.words['S2UR UR5, SR_CTAID.Z'] = bytes(16)
        with self.assertRaises(ValueError):
            model.uniform_prefix(self.words, 96, 1)

    def test_destination_reuse_preserves_every_original_predicate(self):
        for destination in (0, 1, 127, 167, 254):
            for predicate in range(16):
                source = struct.pack('<QQ', (predicate << 12) | 0x802, 0)
                prelude = model.load_prelude(source, destination)
                _, control = struct.unpack_from('<QQ', prelude)
                self.assertEqual((control >> 52) & 63, 63)
                self.assertEqual((control >> 41) & 15, 15)
                move, control = struct.unpack_from('<QQ', prelude, 16)
                self.assertEqual((move >> 12) & 15, predicate)
                self.assertEqual((move >> 16) & 255, destination)
                self.assertEqual((move >> 32) & 255, 48)
                self.assertEqual((control >> 41) & 15, 15)
        for destination in (-1, 255, True):
            with self.assertRaises(ValueError):
                model.load_prelude(bytes(16), destination)

    def test_branch_target_map_includes_preludes_and_exclusive_end(self):
        sites = {16: {}, 32: {}}
        mapping = model.pc_map(64, sites)
        self.assertEqual(mapping, {0: 128, 16: 144, 32: 192, 48: 240, 64: 256})
        self.assertEqual(mapping[32] + model.PRELUDE_BYTES, 224)
        for size, invalid in ((0, []), (17, []), (64, [3]), (64, [-16]), (64, [64]), (64, [16, 16])):
            with self.assertRaises(ValueError):
                model.pc_map(size, invalid)

    def test_only_user_packet_gpr_loads_receive_destination_prelude(self):
        code = {0: 'LDC R1, c[0x0][0x37c]', 16: '@!P4 LDC.64 R16, c[0x0][0x388]',
                32: 'LDCU.128 UR8, c[0x0][0x380]', 48: '@P0 LDC R0, c[0x0][0x380]',
                64: 'LDC R2, c[0x0][0x3e0]'}
        self.assertEqual(model.packet_loads(code, 96), {
            16: {'predicate': '@!P4 ', 'destination': 16},
            48: {'predicate': '@P0 ', 'destination': 0},
        })


if __name__ == '__main__':
    unittest.main()
