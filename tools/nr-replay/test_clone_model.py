"""Synthetic CPU contracts; no provider binary, CUDA tool or GPU is required."""
from pathlib import Path, PureWindowsPath
import argparse
import json
import struct
import subprocess
import tempfile
import unittest
from unittest import mock

import clone_model as model


class InstructionTests(unittest.TestCase):
    def test_immediate_consumer_delay_changes_only_stall1(self):
        for uniform, register in ((False, 6), (True, 16)):
            instruction = f'S2{"U" if uniform else ""}R {"U" if uniform else ""}R{register}, SR_CTAID.Z'
            low = (0x79c3 if uniform else 0x7919) | (register << 16)
            for stall in (1, 2, 3, 4, 6, 10):
                high = (0x000e220000002700 & ~model.STALL_MASK) | (stall << 41)
                before = struct.pack('<QQ', low, high)
                after = model.mask_producer(before, instruction)
                new_low, new_high = struct.unpack('<QQ', after)
                self.assertEqual(new_low, low)
                self.assertEqual(new_high & ~model.STALL_MASK, high & ~model.STALL_MASK)
                self.assertEqual((new_high >> 41) & 15, max(stall, 2))
                self.assertEqual(after != before, stall == 1)

    def test_wrong_producer_is_rejected(self):
        good = struct.pack('<QQ', 0x67919, 0x000e220000002700)
        for instruction in ('@P0 S2R R6, SR_CTAID.Z', 'S2R R6, SR_TID.Z', 'MOV R6, RZ'):
            with self.assertRaises(ValueError):
                model.mask_producer(good, instruction)
        for high in (0x000e220000002300, 0x000fc20000002700, 0x000e200000002700):
            with self.assertRaises(ValueError):
                model.mask_producer(struct.pack('<QQ', 0x67919, high), 'S2R R6, SR_CTAID.Z')

    def test_branch_relocation_retains_every_non_target_bit(self):
        original_low, original_high = 0x0000000000007947, 0x000fea000b800000
        for pc, target in ((0x30, 0x10000), (0x10000, 0x30), (0x40, 0x40)):
            low, high = model.relocate_branch(pc, original_low, original_high, target)
            self.assertEqual(model.branch_target(pc, low, high), target)
            self.assertEqual(low & ~0xfffffffc00ff0000, original_low & ~0xfffffffc00ff0000)
            self.assertEqual(high & ~0x3ffff, original_high & ~0x3ffff)
        for pc, target in ((1, 16), (16, -16), (0, 18), (0, 1 << 62)):
            with self.assertRaises(ValueError):
                model.relocate_branch(pc, original_low, original_high, target)

    def test_parameter_rebase_is_bounded_and_keeps_controls(self):
        for opcode, shift in (('LDC', 38), ('LDCU', 37)):
            high = 0x000e620008000a00
            low = 0x380 << shift
            chunk = struct.pack('<QQ', low, high)
            result = model.rebase_load(chunk, f'{opcode}.64 R4, c[0x0][0x380]', 96, 88)
            out_low, out_high = struct.unpack('<QQ', result)
            self.assertEqual((out_low >> shift) & 0x1fff, 0x3e0)
            self.assertEqual(out_high, high)
            with self.assertRaises(ValueError):
                model.rebase_load(chunk, f'{opcode}.64 R4, c[0x0][0x384]', 96, 88)
        for instruction, stride, extent in (('LDC.128 R4, c[0x0][0x3d0]', 96, 88),
                                            ('LDC R4, c[0x0][R0+0x380]', 96, 88),
                                            ('LDC R4, c[0x0][0x380]', 80, 88)):
            with self.assertRaises(ValueError):
                model.rebase_load(struct.pack('<QQ', 0x3d0 << 38, 0), instruction, stride, extent)

    def test_maps_cover_source_and_preserve_disjoint_clone_order(self):
        maps = model.pc_maps(256, [0, 128], 2)
        self.assertEqual(maps[0][0], 128)
        self.assertEqual(maps[0][16], 272)
        self.assertEqual(maps[0][128], 384)
        self.assertEqual(maps[0][144], 528)
        self.assertEqual(maps[0][256], maps[1][0])
        for mapping in maps:
            self.assertEqual(set(mapping), set(range(0, 257, 16)))
            self.assertEqual(len(set(mapping.values())), len(mapping))
        for size, sites, count in ((0, [], 1), (256, [0, 0], 2), (256, [256], 1),
                                    (256, [1], 1), (256, [], 4)):
            with self.assertRaises(ValueError):
                model.pc_maps(size, sites, count)


class AdmissionTests(unittest.TestCase):
    def test_capture_relocation_cannot_escape_selected_root(self):
        with tempfile.TemporaryDirectory() as directory:
            generator = object.__new__(model.Generator)
            generator.capture_root = Path(directory).resolve()
            generator.recorded_root = PureWindowsPath('C:/original/capture')
            self.assertEqual(generator.resolve_capture('C:/original/capture/module/file'),
                             Path(directory).resolve() / 'module/file')
            for path in ('D:/original/capture/file', 'C:/original/other/file',
                         'C:/original/capture/../../escape'):
                with self.assertRaises(ValueError):
                    generator.resolve_capture(path)

    def test_wrong_inventory_fails_before_output_creation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            inventory = root / 'inventory.json'
            inventory.write_text('{}')
            args = argparse.Namespace(output=root / 'out', inventory=inventory,
                                      capture_root=root, **{key: root / key for key in model.TOOL_SHA})
            with self.assertRaises(ValueError):
                model.Generator(args)
            self.assertFalse(args.output.exists())

    def test_existing_output_preserves_user_files(self):
        with tempfile.TemporaryDirectory() as directory:
            generator = object.__new__(model.Generator)
            generator.output = Path(directory)
            marker = generator.output / 'retained.txt'
            marker.write_text('retained')
            with self.assertRaises(FileExistsError):
                generator.execute()
            self.assertEqual(marker.read_text(), 'retained')

    def test_tool_timeout_preserves_partial_output_and_receipt(self):
        with tempfile.TemporaryDirectory() as directory:
            generator = object.__new__(model.Generator)
            generator.output, generator.jobs = Path(directory), []
            error = subprocess.TimeoutExpired(['tool'], 120, output=b'partial', stderr=b'diagnostic')
            with mock.patch.object(model.subprocess, 'run', side_effect=error), self.assertRaises(ValueError):
                generator.run(['tool', 'arg'], 'failed')
            self.assertTrue(generator.jobs[0]['timedOut'])
            self.assertEqual((generator.output / 'failed.stdout.txt').read_bytes(), b'partial')
            self.assertEqual((generator.output / 'failed.stderr.txt').read_bytes(), b'diagnostic')

    def test_tool_launch_failure_is_journaled_as_not_started(self):
        with tempfile.TemporaryDirectory() as directory:
            generator = object.__new__(model.Generator)
            generator.output, generator.jobs = Path(directory), []
            with mock.patch.object(model.subprocess, 'run', side_effect=FileNotFoundError('missing tool')), self.assertRaises(ValueError):
                generator.run(['missing-tool'], 'missing')
            self.assertIsNone(generator.jobs[0]['exitCode'])
            self.assertFalse(generator.jobs[0]['timedOut'])
            self.assertIn('FileNotFoundError', generator.jobs[0]['launchError'])

    def test_interruption_never_writes_success_receipt(self):
        with tempfile.TemporaryDirectory() as directory:
            generator = object.__new__(model.Generator)
            generator.output = Path(directory) / 'new'
            generator.inputs, generator.entries, generator.bundles, generator.jobs = {}, [], [], []
            with mock.patch.object(generator, 'selectors', side_effect=KeyboardInterrupt), self.assertRaises(KeyboardInterrupt):
                generator.execute()
            receipt = json.loads((generator.output / 'audit.json').read_text())
            self.assertEqual(receipt['status'], 'failed')
            self.assertIn('KeyboardInterrupt', receipt['failure'])

    def test_semantic_catalog_excludes_only_paths(self):
        source = {'batchCount': 2, 'modules': [{'path': 'one', 'sha256': 'a', 'functions': []}]}
        original = model.semantic(source)
        source['modules'][0]['path'] = 'two'
        self.assertEqual(model.semantic(source), original)
        source['modules'][0]['sha256'] = 'b'
        self.assertNotEqual(model.semantic(source), original)


if __name__ == '__main__':
    unittest.main()
