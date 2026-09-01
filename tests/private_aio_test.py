from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location(
    "private_aio", ROOT / "tools/private_aio.py"
)
assert SPEC and SPEC.loader
PRIVATE_AIO = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PRIVATE_AIO)


class PrivateAioTests(unittest.TestCase):
    def test_runtime_contract_contains_only_pinned_public_metadata(self) -> None:
        contract = PRIVATE_AIO.load_contract(
            ROOT / "cmake/PrivateNvidiaRuntime.json"
        )
        safe = PRIVATE_AIO.safe_runtime_manifest(contract)
        encoded = json.dumps(safe)
        self.assertNotIn("source", encoded)
        self.assertNotRegex(encoded, r"[A-Za-z]:[\\/]")
        self.assertEqual(len(safe["files"]), 7)

    def test_privacy_scan_rejects_ascii_and_utf16_machine_values(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            source.mkdir()
            for index, data in enumerate(
                (
                    str(source).encode("utf-8"),
                    str(source).encode("utf-16-le"),
                    b"github_pat_example-secret-value",
                    b"C:\\Users\\developer\\source.cpp",
                )
            ):
                candidate = root / f"candidate-{index}.bin"
                candidate.write_bytes(data)
                with self.subTest(index=index):
                    with self.assertRaises(PRIVATE_AIO.PrivateAioError):
                        PRIVATE_AIO.scan_file(candidate, source)

    def test_privacy_scan_accepts_path_free_artifact(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            candidate = root / "manifest.json"
            candidate.write_text(
                '{"buildId":"0123456789abcdef","runtime":"SE"}',
                encoding="utf-8",
            )
            PRIVATE_AIO.scan_file(candidate, root / "source")

    def test_privacy_scan_uses_complete_embedded_strings(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            source.mkdir()
            candidate = root / "candidate.bin"

            candidate.write_bytes(b"\\\\server\x00\\share")
            PRIVATE_AIO.scan_file(candidate, source)

            for data in (
                b"\\\\server\\share\\artifact.pdb",
                "C:\\Users\\developer\\artifact.pdb".encode("utf-16-le"),
            ):
                candidate.write_bytes(data)
                with self.assertRaises(PRIVATE_AIO.PrivateAioError):
                    PRIVATE_AIO.scan_file(candidate, source)

    def test_privacy_scan_checks_nested_staging_tree(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            nested = root / "CacheProfiles" / "standard"
            nested.mkdir(parents=True)
            (nested / "Manifest.json").write_bytes(
                b'{"compilerPath":"C:\\Users\\developer\\fxc.exe"}'
            )
            with self.assertRaises(PRIVATE_AIO.PrivateAioError):
                PRIVATE_AIO.scan_staging_tree(root, root / "source")

    def test_opaque_assets_keep_exact_and_credential_scans(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            source.mkdir()
            candidate = root / "publisher-font.ttf"

            candidate.write_bytes(b"C:\\Users\\publisher\\font-build.pdb")
            PRIVATE_AIO.scan_staging_tree(root, source)

            candidate.write_bytes(b"Token::Process.Load(object[0])")
            PRIVATE_AIO.scan_staging_tree(root, source)

            candidate.write_bytes(b"Token: {}")
            PRIVATE_AIO.scan_staging_tree(root, source)

            candidate.write_bytes(b"token: actual-secret-value")
            with self.assertRaises(PRIVATE_AIO.PrivateAioError):
                PRIVATE_AIO.scan_staging_tree(root, source)

            candidate.write_bytes(
                b"https://scripts.sil.org/OFL|authors@example.invalid"
            )
            PRIVATE_AIO.scan_staging_tree(root, source)

            candidate.write_bytes(b"https://developer:secret@example.invalid")
            with self.assertRaises(PRIVATE_AIO.PrivateAioError):
                PRIVATE_AIO.scan_staging_tree(root, source)

            candidate.write_bytes(b"github_pat_example-secret-value")
            with self.assertRaises(PRIVATE_AIO.PrivateAioError):
                PRIVATE_AIO.scan_staging_tree(root, source)

            candidate.write_bytes(str(source).encode("utf-8"))
            with self.assertRaises(PRIVATE_AIO.PrivateAioError):
                PRIVATE_AIO.scan_staging_tree(root, source)

    def test_private_labels_are_exact(self) -> None:
        self.assertEqual(
            PRIVATE_AIO.EXPECTED_LABELS,
            frozenset({"Vincent-se", "Gogh-se"}),
        )

    def test_shader_cache_packager_imports_on_python_312(self) -> None:
        packager = PRIVATE_AIO.import_shader_packager(ROOT)
        self.assertTrue(callable(packager.prepare_aio_archive))


if __name__ == "__main__":
    unittest.main()
