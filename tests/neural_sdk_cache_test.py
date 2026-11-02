"""Run the production NR SDK staging module against a local, hash-pinned archive fixture."""

import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]


class NeuralSDKCacheTest(unittest.TestCase):
    def setUp(self):
        self.cmake = os.environ.get("CMAKE") or shutil.which("cmake")
        self.assertIsNotNone(self.cmake, "CMake is required for SDK staging tests")
        temporary = tempfile.TemporaryDirectory(prefix="csx-nr-sdk-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.source = self.root / "source"
        self.build = self.root / "build"
        self.modules = self.source / "cmake"
        self.modules.mkdir(parents=True)
        for name in ["NeuralRendering-Runtime.cmake", "RuntimePayload.cmake",
                     "CsxDownload.cmake", "VerifyNeuralRenderingPayload.cmake"]:
            shutil.copy2(ROOT / "cmake" / name, self.modules / name)
        self.license = self.source / "extern/NeuralRenderingSDK/LICENSE.txt"
        self.license.parent.mkdir(parents=True)
        self.license.write_bytes(b"Original fixture license\r\n")
        self.header = b"#define NVSDK_NGX_Parameter_NRFixture \"SDK parameter\"\n"
        archive = self.root / "official-sdk.zip"
        with zipfile.ZipFile(archive, "w") as output:
            output.writestr("LICENSE.txt", self.license.read_bytes())
            output.writestr("include/nvsdk_ngx_defs_dlssnr.h", self.header)
            output.writestr("include/other_original.h", b"Original companion header\n")
            output.writestr("bin/nvngx_dlssnr.dll", b"Never extract or distribute this fixture runtime")
        values = {
            "NEURAL_RENDERING_SDK_PACKAGE": archive.name,
            "NEURAL_RENDERING_SDK_URL": archive.as_uri(),
            "NEURAL_RENDERING_SDK_ARCHIVE_SHA256": hashlib.sha256(archive.read_bytes()).hexdigest(),
            "NEURAL_RENDERING_SDK_LICENSE_SHA256": hashlib.sha256(self.license.read_bytes()).hexdigest(),
            "NEURAL_RENDERING_SDK_HEADER_SHA256": hashlib.sha256(self.header).hexdigest(),
        }
        (self.modules / "NeuralRenderingSDKPin.cmake").write_text(
            "".join(f'set({key} "{value}")\n' for key, value in values.items()), encoding="utf-8")
        (self.source / "stub.cpp").write_text("void fixture() {}\n", encoding="utf-8")
        (self.source / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 4.2)\nproject(NRSDKFixture LANGUAGES CXX)\n'
            'add_library(NRSDKFixture STATIC stub.cpp)\nadd_library(NgxSdk INTERFACE)\n'
            'set(STREAMLINE_RUNTIME_FEATURE_ROOT "${CMAKE_BINARY_DIR}/payload")\n'
            'set(STREAMLINE_RUNTIME_DIRECTORY "${STREAMLINE_RUNTIME_FEATURE_ROOT}/Shaders/Upscaling/Streamline")\n'
            'file(MAKE_DIRECTORY "${STREAMLINE_RUNTIME_DIRECTORY}")\n'
            'set(STREAMLINE_DLSSG_RUNTIME_FILES "existing-se-frame-generation")\n'
            'include(cmake/NeuralRendering-Runtime.cmake)\n'
            'file(WRITE "${CMAKE_BINARY_DIR}/report.txt" '
            '"${STREAMLINE_RUNTIME_FILES}\\n${STREAMLINE_DX11_RUNTIME_FILES}\\n${STREAMLINE_DLSSG_RUNTIME_FILES}\\n")\n',
            encoding="utf-8")
        self.cache = self.build / "neural-rendering-sdk"
        self.extracted = self.cache / "sdk"
        self.archive = self.cache / archive.name

    def configure(self, *, skip=False, expected=True):
        result = subprocess.run(
            [self.cmake, "-S", str(self.source), "-B", str(self.build),
             f"-DSKIP_RUNTIME_DOWNLOADS={'ON' if skip else 'OFF'}"],
            capture_output=True, text=True, timeout=40,
        )
        self.assertEqual(result.returncode == 0, expected, result.stdout + result.stderr)
        return result

    def test_warm_cache_extracts_only_headers_and_stages_original_license(self):
        self.configure()
        self.assertEqual((self.extracted / "include/nvsdk_ngx_defs_dlssnr.h").read_bytes(), self.header)
        self.assertTrue((self.extracted / "include/other_original.h").is_file())
        self.assertEqual(list(self.extracted.rglob("*.dll")), [])
        notices = list((self.build / "payload").rglob("nvngx_dlssnr.license.txt"))
        self.assertEqual(len(notices), 1)
        self.assertEqual(notices[0].read_bytes(), self.license.read_bytes())
        report = (self.build / "report.txt").read_text().splitlines()
        self.assertEqual(report[0], report[1])
        self.assertEqual(report[2], "existing-se-frame-generation")
        unchanged = {path: (path.stat().st_mtime_ns, path.read_bytes()) for path in
                     [self.archive, self.extracted / ".archive-sha256",
                      self.extracted / "include/nvsdk_ngx_defs_dlssnr.h"]}
        self.configure(skip=True)
        self.assertEqual(unchanged, {path: (path.stat().st_mtime_ns, path.read_bytes()) for path in unchanged})

    def test_cold_offline_configure_does_not_download_or_extract(self):
        result = self.configure(skip=True, expected=False)
        self.assertIn("Official NR SDK headers are unavailable", result.stdout + result.stderr)
        self.assertFalse(self.archive.exists())
        self.assertFalse(self.extracted.exists())

    def test_modified_cache_is_rejected_without_overwriting_it_offline(self):
        self.configure()
        for path in [self.archive, self.extracted / ".archive-sha256",
                     self.extracted / "include/nvsdk_ngx_defs_dlssnr.h"]:
            with self.subTest(path=path.name):
                original = path.read_bytes()
                path.write_bytes(b"changed cached bytes")
                self.configure(skip=True, expected=False)
                self.assertEqual(path.read_bytes(), b"changed cached bytes")
                path.write_bytes(original)

    def test_modified_source_license_is_rejected(self):
        self.license.write_bytes(b"Rewritten notice")
        result = self.configure(expected=False)
        self.assertIn("license differs from its original", result.stdout + result.stderr)
        self.assertFalse(self.archive.exists())

    def test_stale_runtime_in_staging_is_rejected(self):
        self.configure()
        runtime = self.build / "payload/Shaders/Upscaling/Streamline/nvngx_dlssnr.dll"
        runtime.write_bytes(b"stale runtime")
        self.configure(skip=True, expected=False)
        self.assertEqual(runtime.read_bytes(), b"stale runtime")


if __name__ == "__main__":
    unittest.main()
