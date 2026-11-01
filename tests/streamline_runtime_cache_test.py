"""Run the production Streamline staging module with a small local SDK fixture.

Only the pinned archive URL and digest are replaced. No network, Windows SDK,
real runtime cache or game installation is involved.
"""

import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
import zipfile


ROOT = Path(__file__).resolve().parents[1]
DX11 = {"nvngx_dlss.dll", "sl.common.dll", "sl.dlss.dll", "sl.interposer.dll", "sl.pcl.dll", "sl.reflex.dll"}
DX12 = {"nvngx_dlssg.dll", "sl.common.dll", "sl.dlss_g.dll", "sl.interposer.dll", "sl.pcl.dll", "sl.reflex.dll"}
NOTICES = {"license.txt", "3rd-party-licenses.md", "NVIDIA Nsight Graphics SDK License (Apache 2.0).txt", "nvngx_dlss.license.txt", "reflex.license.txt"}


class StreamlineRuntimeCacheTest(unittest.TestCase):
    def setUp(self):
        self.cmake = os.environ.get("CMAKE") or os.environ.get("CMAKE_COMMAND") or os.environ.get("CSX_CMAKE") or shutil.which("cmake")
        self.assertIsNotNone(self.cmake, "CMake is required for the production staging module")
        temporary = tempfile.TemporaryDirectory(prefix="csx-streamline-cache-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.build = self.root / "fixture build"
        self.modules = self.root / "modules"
        self.modules.mkdir()
        self.contents = {f"bin/x64/{name}": f"production fixture {name}" for name in DX11 | DX12}
        for name in NOTICES:
            relative = f"bin/x64/{name}" if name in {"nvngx_dlss.license.txt", "reflex.license.txt"} else name
            self.contents[relative] = f"original fixture notice {name}"
        archive = self.root / "official fixture.zip"
        with zipfile.ZipFile(archive, "w") as output:
            for name, content in self.contents.items():
                output.writestr(name, content)
        digest = hashlib.sha256(archive.read_bytes()).hexdigest().upper()
        source = (ROOT / "cmake/Streamline-Runtime.cmake").read_text()
        source, count = re.subn(r'(STREAMLINE_RUNTIME_ARCHIVE_SHA256\s+)"[0-9A-Fa-f]+"', lambda match: match[1] + f'"{digest}"', source, count=1)
        self.assertEqual(count, 1)
        source, count = re.subn(r'(STREAMLINE_RUNTIME_ARCHIVE_URL\s+)"[^"]+"', lambda match: match[1] + f'"{archive.as_uri()}"', source, count=1)
        self.assertEqual(count, 1)
        (self.modules / "Streamline-Runtime.cmake").write_text(source)
        for name in ["RuntimePayload.cmake", "CsxDownload.cmake", "RuntimePayloadInstallGuard.cmake.in", "VerifyNeuralRenderingPayload.cmake"]:
            shutil.copy2(ROOT / "cmake" / name, self.modules / name)
        self.cache = self.build / "streamline-runtime"
        self.sdk = self.cache / "sdk"
        self.payload = self.cache / "payload/Shaders/Upscaling"
        self.stamp = self.sdk / ".archive-sha256"
        self.manifest = self.sdk / ".runtime-payload-sha256.json"
        self.archive = self.cache / "streamline-sdk-v2.14.16.zip"

    def configure(self, skip):
        script = self.root / "configure.cmake"
        report = self.root / "report.txt"
        script.write_text(
            'cmake_minimum_required(VERSION 4.2)\n'
            f'set(CMAKE_CURRENT_BINARY_DIR "{self.build.as_posix()}")\n'
            f'set(SKIP_RUNTIME_DOWNLOADS {"ON" if skip else "OFF"})\n'
            f'include("{(self.modules / "Streamline-Runtime.cmake").as_posix()}")\n'
            'list(LENGTH STREAMLINE_RUNTIME_PAYLOAD_MISSING missing_count)\n'
            'list(LENGTH STREAMLINE_RUNTIME_FILES available_count)\n'
            'list(LENGTH STREAMLINE_RUNTIME_PAYLOAD_FILES expected_count)\n'
            f'file(WRITE "{report.as_posix()}" '
            '"${missing_count}\\n${available_count}\\n${expected_count}\\n${STREAMLINE_DX11_RUNTIME_FILES}\\n${STREAMLINE_DLSSG_RUNTIME_FILES}\\n")\n'
        )
        environment = os.environ.copy()
        environment.pop("CSX_ASSET_DOWNLOADER_PYTHON", None)
        environment.pop("CSX_ASSET_DOWNLOADER_SCRIPT", None)
        result = subprocess.run([self.cmake, "-P", str(script)], cwd=self.root, env=environment, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        lines = report.read_text().splitlines()
        return tuple(int(value) for value in lines[:3]), [{Path(name).name for name in line.split(";") if name} for line in lines[3:5]]

    def warm(self):
        counts, files = self.configure(False)
        self.assertEqual(counts, (0, 22, 22))
        self.assertEqual(files, [DX11 | NOTICES, DX12 | NOTICES])
        hashes = json.loads(self.manifest.read_text())
        self.assertEqual(set(hashes), set(self.contents))
        for name, content in self.contents.items():
            self.assertEqual(hashes[name], hashlib.sha256(content.encode()).hexdigest())

    def test_cold_skip_has_complete_two_directory_payload_description(self):
        counts, files = self.configure(True)
        self.assertEqual(counts, (22, 0, 22))
        self.assertEqual(files, [set(), set()])
        self.assertFalse(self.archive.exists(), "Skip mode must not fetch even an available local archive")
        self.assertFalse(self.sdk.exists(), "Skip mode must not extract SDK archives")

    def test_verified_skip_reuses_both_se_runtime_directories_and_notices(self):
        self.warm()
        unchanged = {p: (p.stat().st_mtime_ns, p.read_bytes()) for p in [self.archive, self.stamp, self.manifest]}
        counts, files = self.configure(True)
        self.assertEqual(counts, (0, 22, 22))
        self.assertEqual(files, [DX11 | NOTICES, DX12 | NOTICES])
        self.assertEqual(unchanged, {p: (p.stat().st_mtime_ns, p.read_bytes()) for p in unchanged})

    def test_modified_sdk_dll_and_notice_do_not_qualify_in_skip_mode(self):
        self.warm()
        (self.sdk / "bin/x64/sl.common.dll").write_text("modified SDK DLL")
        (self.sdk / "license.txt").write_text("modified SDK notice")
        counts, _ = self.configure(True)
        self.assertEqual(counts, (4, 18, 22))
        for directory in ["Streamline", "StreamlineDX12"]:
            self.assertEqual((self.payload / directory / "sl.common.dll").read_text(), self.contents["bin/x64/sl.common.dll"])
            self.assertEqual((self.payload / directory / "license.txt").read_text(), self.contents["license.txt"])
        self.assertEqual((self.sdk / "bin/x64/sl.common.dll").read_text(), "modified SDK DLL")

    def test_missing_hash_manifest_keeps_existing_payload_unavailable(self):
        self.warm()
        self.manifest.unlink()
        existing = (self.payload / "StreamlineDX12/nvngx_dlssg.dll").read_bytes()
        self.assertEqual(self.configure(True)[0], (22, 0, 22))
        self.assertEqual((self.payload / "StreamlineDX12/nvngx_dlssg.dll").read_bytes(), existing)
        self.assertFalse(self.manifest.exists())

    def test_invalid_hash_manifest_is_not_replaced_by_skip_mode(self):
        self.warm()
        self.manifest.write_text("not JSON")
        self.assertEqual(self.configure(True)[0], (22, 0, 22))
        self.assertEqual(self.manifest.read_text(), "not JSON")

    def test_missing_or_modified_archive_is_not_fetched_or_replaced(self):
        self.warm()
        self.archive.unlink()
        self.assertEqual(self.configure(True)[0], (22, 0, 22))
        self.assertFalse(self.archive.exists())
        self.archive.write_text("bad cached archive")
        self.assertEqual(self.configure(True)[0], (22, 0, 22))
        self.assertEqual(self.archive.read_text(), "bad cached archive")

    def test_mismatched_extraction_stamp_does_not_reextract(self):
        self.warm()
        self.stamp.write_text("wrong stamp")
        sentinel = self.sdk / "user-cache-sentinel.txt"
        sentinel.write_text("preserve")
        self.assertEqual(self.configure(True)[0], (22, 0, 22))
        self.assertEqual(self.stamp.read_text(), "wrong stamp")
        self.assertEqual(sentinel.read_text(), "preserve")

    def test_normal_mode_reextracts_modified_cache_from_verified_archive(self):
        self.warm()
        (self.sdk / "bin/x64/nvngx_dlssg.dll").write_text("bad cached FG")
        self.assertEqual(self.configure(False)[0], (0, 22, 22))
        self.assertEqual((self.sdk / "bin/x64/nvngx_dlssg.dll").read_text(), self.contents["bin/x64/nvngx_dlssg.dll"])
        self.assertEqual((self.payload / "StreamlineDX12/nvngx_dlssg.dll").read_text(), self.contents["bin/x64/nvngx_dlssg.dll"])


if __name__ == "__main__":
    unittest.main()
