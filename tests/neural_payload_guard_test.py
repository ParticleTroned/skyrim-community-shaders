"""Exercise the distributable NR payload boundary with the real CMake guard."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "features/Neural Rendering"


class NeuralPayloadGuardTest(unittest.TestCase):
    def setUp(self):
        self.cmake = os.environ.get("CMAKE") or os.environ.get("CMAKE_COMMAND") or os.environ.get("CSX_CMAKE") or shutil.which("cmake")
        self.assertIsNotNone(self.cmake, "CMake is required for the production package guard")
        self.temporary = tempfile.TemporaryDirectory(prefix="csx-nr-payload-")
        self.addCleanup(self.temporary.cleanup)
        self.package = Path(self.temporary.name) / "package"
        self.package.mkdir()

    def validate(self, expected=True):
        result = subprocess.run(
            [self.cmake, f"-DNR_PACKAGE_ROOT={self.package}", "-P", str(ROOT / "cmake/VerifyNeuralRenderingPayload.cmake")],
            capture_output=True, text=True, timeout=30,
        )
        self.assertEqual(result.returncode == 0, expected, result.stdout + result.stderr)

    def put(self, name):
        path = self.package / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("test payload", encoding="utf-8")

    def test_complete_scalar_payload(self):
        shutil.copytree(SOURCE, self.package, dirs_exist_ok=True)
        self.validate()

    def test_optional_absence_preserves_hdr_fg_and_nvperf(self):
        for name in [
            "Shaders/Upscaling/Streamline/nvngx_dlss.dll",
            "Shaders/Upscaling/StreamlineDX12/sl.dlss_g.dll",
            "Shaders/Upscaling/StreamlineDX12/nvngx_dlssg.dll",
            "SKSE/Plugins/CommunityShaders/NvPerf/nvperf_grfx_host.dll",
            "Shaders/HDR/Display.hlsl",
        ]:
            self.put(name)
        self.validate()

    def test_missing_shader_is_rejected(self):
        shutil.copytree(SOURCE, self.package, dirs_exist_ok=True)
        (self.package / "Shaders/Upscaling/NeuralRendering/MonoPresent.hlsl").unlink()
        self.validate(False)

    def test_marker_without_shader_payload_is_rejected(self):
        self.put("Shaders/Features/NeuralRendering.ini")
        self.validate(False)

    def test_unexpected_shader_or_directory_is_rejected(self):
        shutil.copytree(SOURCE, self.package, dirs_exist_ok=True)
        self.put("Shaders/Upscaling/NeuralRendering/OtherPresentation.hlsl")
        self.validate(False)
        (self.package / "Shaders/Upscaling/NeuralRendering/OtherPresentation.hlsl").unlink()
        (self.package / "Shaders/Upscaling/NeuralRendering/nested").mkdir()
        self.validate(False)

    def test_provider_binaries_are_rejected_case_insensitively(self):
        for name in ["nvngx_dlssnr.dll", "NVNGX_DLSSNR_DEBUG.DLL", "sl.dlss_nr.dll", "SL.DLSS_NR_CUSTOM.DLL"]:
            with self.subTest(name=name):
                relative = f"SKSE/Plugins/nested/{name}"
                self.put(relative)
                self.validate(False)
                (self.package / relative).unlink()

    def test_internal_notice_is_rejected(self):
        self.put("Shaders/Upscaling/Streamline/INTERNAL-NO-REDISTRIBUTION.TXT")
        self.validate(False)


if __name__ == "__main__":
    unittest.main()
