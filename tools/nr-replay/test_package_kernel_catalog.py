"""Portable package tests use synthetic modules, never provider code or a GPU."""
from pathlib import Path
import json
import tempfile
import unittest
from unittest import mock

import package_kernel_catalog as package


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.modules = []
        offset = 0
        for index in range(9):
            data = b'\x7fELF' + bytes([index]) * 60
            path = self.root / f'original-{index}.cubin'
            path.write_bytes(data)
            count = 4 if index == 0 else 5
            functions = [dict(name=f'kernel_{value:02d}', paramSize=96, gridZ=(1, 2, 4)[value % 3])
                         for value in range(offset, offset + count)]
            offset += count
            self.modules.append(dict(originalSha256=f'{index:064x}', sha256=package.sha(data), path=str(path), functions=functions))
        self.manifest = dict(schema='nr-model-kernel-replacement-v1', batchCount=2, modules=self.modules)
        self.path = self.root / 'source.json'
        self.write()
        expected = package.semantic(self.manifest)
        self.patch = mock.patch.dict(package.CATALOGS, {'cloned': expected, 'shared': expected})
        self.patch.start()
        self.addCleanup(self.patch.stop)

    def write(self):
        self.path.write_text(json.dumps(self.manifest))

    def test_package_is_portable_hash_addressed_and_retains_semantics(self):
        output = self.root / 'payload'
        receipt = package.package(self.path, self.path, output)
        self.assertEqual(receipt['status'], 'complete')
        self.assertEqual(len(receipt['modules']), 9)
        for role in ('cloned', 'shared'):
            path = output / (role + '-n2.json')
            value, blobs, _ = package.read_catalog(path, role)
            self.assertEqual(package.semantic(value), package.CATALOGS[role])
            for module in value['modules']:
                self.assertEqual(module['path'], module['sha256'] + '.cubin')
                self.assertEqual(package.sha(blobs[module['sha256']]), module['sha256'])
        self.assertNotIn(str(self.root), json.dumps(receipt))

    def test_wrong_semantics_or_module_hash_fails_before_output(self):
        for mutation in ('semantic', 'bytes'):
            with self.subTest(mutation=mutation):
                if mutation == 'semantic':
                    self.manifest['modules'][0]['functions'][0]['gridZ'] = 4
                    self.write()
                else:
                    self.manifest['modules'][0]['functions'][0]['gridZ'] = 1
                    self.write()
                    Path(self.modules[0]['path']).write_bytes(b'wrong')
                with self.assertRaises(ValueError):
                    package.package(self.path, self.path, self.root / 'out')
                self.assertFalse((self.root / 'out').exists())

    def test_relative_paths_reject_escapes_subdirectories_and_streams(self):
        for value in ('../escape.cubin', '..\\escape.cubin', 'sub/file.cubin', '\\rooted.cubin',
                      'C:relative.cubin', 'file.cubin:stream', 'file.cubin', '/not-present/file.cubin'):
            with self.subTest(value=value), self.assertRaises((ValueError, FileNotFoundError)):
                package.source_path(value, self.path)

    def test_function_module_and_canonical_hash_contracts(self):
        original = self.path.read_bytes()
        for mutate in (
            lambda value: value.update(batchCount=True),
            lambda value: value.update(schema='other'),
            lambda value: value['modules'].reverse(),
            lambda value: value['modules'][0]['functions'].reverse(),
            lambda value: value['modules'][0].update(sha256='A' * 64),
            lambda value: value['modules'][0]['functions'][0].update(paramSize=7),
            lambda value: value['modules'][0]['functions'][0].update(gridZ=True),
        ):
            self.manifest = json.loads(original)
            mutate(self.manifest)
            self.write()
            with self.assertRaises(ValueError):
                package.read_catalog(self.path, 'shared')

    def test_existing_output_and_interruption_preserve_failure_receipt(self):
        output = self.root / 'output'
        output.mkdir()
        marker = output / 'user-file'
        marker.write_bytes(b'keep')
        with self.assertRaises(ValueError):
            package.package(self.path, self.path, output)
        self.assertEqual(marker.read_bytes(), b'keep')
        new = self.root / 'new'
        original_open = Path.open
        def fail_module(path, *args, **kwargs):
            if path.parent == new and path.suffix == '.cubin':
                raise KeyboardInterrupt
            return original_open(path, *args, **kwargs)
        with mock.patch.object(Path, 'open', fail_module), self.assertRaises(KeyboardInterrupt):
            package.package(self.path, self.path, new)
        receipt = json.loads((new / 'package-receipt.json').read_text())
        self.assertEqual(receipt['status'], 'failed')
        self.assertIn('KeyboardInterrupt', receipt['error'])


if __name__ == '__main__':
    unittest.main()
