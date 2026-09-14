"""Exercise the production CMake guard against complete registration fixtures."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CMAKE = sys.argv.pop(1)
MANIFEST = Path("features/Neural Rendering Colour/Shaders/Features/NeuralColor.ini")
CORE = Path("features/Neural Rendering Colour/CORE")
REGISTRY = Path("include/FeatureVersions.h")


class RegistrationTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        for relative in (MANIFEST, CORE, REGISTRY):
            target = self.root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / relative, target)

    def validate(self):
        return subprocess.run(
            [CMAKE, "-D", f"CSX_NEURAL_COLOR_ROOT={self.root}", "-P",
             str(ROOT / "cmake/ValidateNeuralColor.cmake")],
            capture_output=True, text=True, timeout=15)

    def test_complete_registration(self):
        result = self.validate()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_generated_header_cannot_mask_missing_runtime_entry(self):
        registry = self.root / REGISTRY
        text = registry.read_text()
        generated = self.root / "build/cmake/FeatureVersions.h"
        generated.parent.mkdir(parents=True)
        generated.write_text(text)
        registry.write_text("\n".join(line for line in text.splitlines()
                                      if not ('"NeuralColor"sv' in line and '{' in line)))
        result = self.validate()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing from include/FeatureVersions.h", result.stderr)

    def test_missing_core_registration(self):
        registry = self.root / REGISTRY
        text = registry.read_text()
        registry.write_text("\n".join(line for line in text.splitlines()
                                      if line.strip() != '"NeuralColor"sv,'))
        result = self.validate()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing from FEATURE_CORE_NAMES", result.stderr)

    def test_version_mismatch(self):
        for version in ("1-1-0", "1-3-0"):
            with self.subTest(version=version):
                (self.root / MANIFEST).write_text(f"[Info]\nVersion = {version}\n")
                result = self.validate()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("differs from manifest", result.stderr)

    def test_missing_required_files(self):
        for relative in (MANIFEST, CORE, REGISTRY):
            with self.subTest(path=relative):
                (self.root / relative).unlink()
                result = self.validate()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("registration requires", result.stderr)
                shutil.copyfile(ROOT / relative, self.root / relative)

    def test_invalid_manifest(self):
        (self.root / MANIFEST).write_text("[Info]\nVersion = invalid\n")
        result = self.validate()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("must declare a valid feature version", result.stderr)


if __name__ == "__main__":
    unittest.main()
