#!/usr/bin/env python3
"""Exercise the mono receipt verifier against complete and damaged native files."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zlib

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('screenshot_verify', root / 'tools/screenshot_verify.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def png():
    def chunk(tag, value):
        return struct.pack('>I', len(value)) + tag + value + struct.pack('>I', zlib.crc32(tag + value))
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 2, 1, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(b'\0' + b'\x80\x40\x20\xff' * 2)) + chunk(b'IEND', b'')


class Verification(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.image = png()
        (self.directory / 'frame.png').write_bytes(self.image)
        self.manifest = self.directory / 'sequence.json'
        self.document = {'contract': {'name': 'csx.screenshot', 'major': 1}, 'state': 'final', 'terminalOutcome': 'completed',
                         'counts': {'requested': 1, 'written': 1, 'acquired': 1, 'failed': 0, 'cancelled': 0, 'dropped': 0, 'inFlight': 0},
                         'children': [{'ordinal': 1, 'state': 'completed', 'actual': {'acquisition': {'source': 'desktop_mirror',
                                      'generatedFrame': False, 'engineFrame': 41, 'planes': [{'sourceWidth': 2, 'sourceHeight': 1, 'samples': 1}]}},
                                       'artifacts': [{'path': 'frame.png', 'bytes': len(self.image), 'sha256': hashlib.sha256(self.image).hexdigest(),
                                       'committed': True, 'actual': {'view': 'source_native', 'generatedFrame': False, 'engineFrame': 41,
                                       'width': 2, 'height': 1, 'format': 'png', 'colourContract': 'sdr_srgb',
                                       'crop': {'x': 0, 'y': 0, 'width': 2, 'height': 1}}}]}]}

    def run_document(self, **options):
        self.manifest.write_text(json.dumps(self.document), encoding='utf-8')
        return module.verify(self.manifest, **options)

    def test_complete(self):
        self.assertEqual(self.run_document()['artifacts'], 1)

    def test_changed_bytes(self):
        (self.directory / 'frame.png').write_bytes(self.image[:-1] + b'!')
        with self.assertRaisesRegex(module.VerificationError, 'SHA-256'): self.run_document()

    def test_escaped_path(self):
        for value in ('../frame.png', '/frame.png', 'C:\\frame.png', 'a/../frame.png', '\\\\host\\frame.png'):
            with self.subTest(value=value):
                self.document['children'][0]['artifacts'][0]['path'] = value
                with self.assertRaises(module.VerificationError): self.run_document()

    def test_changed_dimensions(self):
        self.document['children'][0]['artifacts'][0]['actual']['width'] = 3
        with self.assertRaisesRegex(module.VerificationError, 'dimensions'): self.run_document()

    def test_generated_frame(self):
        self.document['children'][0]['actual']['acquisition']['generatedFrame'] = True
        with self.assertRaisesRegex(module.VerificationError, 'accepted mono'): self.run_document()

    def test_neural_frame_identity(self):
        evidence = {'available': True, 'viewCount': 1, 'sourceWorldFrame': 41, 'evaluationFrame': 41,
                    'presentationFrame': 41, 'modelWidth': 128, 'modelHeight': 128}
        self.document['children'][0]['actual']['acquisition']['nrEvidence'] = evidence
        self.assertTrue(self.run_document(require_neural=True)['neuralEvidenceRequired'])
        evidence['presentationFrame'] = 40
        with self.assertRaisesRegex(module.VerificationError, 'another frame'): self.run_document(require_neural=True)

    def test_exact_exposure(self):
        key = {'frame': 40, 'epoch': 7, 'sequence': 19, 'ambiguous': False}
        actual = self.document['children'][0]['actual']
        actual['acquisition']['nrEvidence'] = {'available': True, 'viewCount': 1, 'sourceWorldFrame': 41,
            'evaluationFrame': 41, 'presentationFrame': 41, 'modelWidth': 128, 'modelHeight': 128, 'neuralExposure': key}
        actual['captureDiagnostics'] = {'finalized': True, 'exposures': [{'key': copy.deepcopy(key),
            'available': True, 'sample': {'readbackComplete': True}}]}
        self.assertTrue(self.run_document(require_exposure=True)['exposureEvidenceRequired'])
        actual['captureDiagnostics']['exposures'][0]['key']['sequence'] = 20
        with self.assertRaisesRegex(module.VerificationError, 'exact captured'): self.run_document(require_exposure=True)

    def test_continuity(self):
        second = copy.deepcopy(self.document['children'][0])
        second['ordinal'] = 2
        second['actual']['acquisition']['engineFrame'] = 43
        second['artifacts'][0]['actual']['engineFrame'] = 43
        second['artifacts'][0]['path'] = 'second.png'
        (self.directory / 'second.png').write_bytes(self.image)
        self.document['children'].append(second)
        self.document['counts'].update(requested=2, written=2, acquired=2)
        self.assertEqual(self.run_document()['frames'], 2)
        with self.assertRaisesRegex(module.VerificationError, 'contiguous'): self.run_document(require_contiguous=True)

    def test_missing_frame(self):
        self.document['counts']['requested'] = 2
        with self.assertRaisesRegex(module.VerificationError, 'every requested'): self.run_document()


if __name__ == '__main__':
    unittest.main()
