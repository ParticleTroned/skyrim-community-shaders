"""Check lossless logger normalization and inventory publication failures."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parent.parent
PWSH = shutil.which("pwsh")
MESSAGE = "Compiling Data/Shaders/RunGrass.hlsl Grass:Pixel:7 to PSHADER VR PBR_GRASS=1 GRASS_OPTIMIZATIONS "


@unittest.skipUnless(PWSH and os.name == "nt", "requires Windows PowerShell command discovery")
class ShaderConfigGenerationTests(unittest.TestCase):
    def generate(self, text, generated_entries=1):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "trace.log"
            source.write_text(text, encoding="utf-8")
            output = root / "inventory.yaml"
            output.write_text("previous inventory", encoding="utf-8")
            observed = root / "normalized.log"
            (root / "hlslkit-generate.ps1").write_text(
                "$logIndex = [Array]::IndexOf([string[]]$args, '--log')\n"
                "$outputIndex = [Array]::IndexOf([string[]]$args, '--output')\n"
                "$log = [IO.File]::ReadAllText($args[$logIndex + 1])\n"
                "[IO.File]::WriteAllText($env:TEST_NORMALIZED_LOG, $log)\n"
                "$yaml = 'shaders:' + [Environment]::NewLine\n"
                "for ($i = 0; $i -lt [int]$env:TEST_GENERATED_ENTRIES; ++$i) {\n"
                "    $yaml += '  - entry: Grass:Pixel:' + $i + [Environment]::NewLine\n"
                "}\n"
                "[IO.File]::WriteAllText($args[$outputIndex + 1], $yaml)\n"
                "exit 0\n",
                encoding="utf-8",
            )
            env = dict(os.environ, PATH=str(root) + os.pathsep + os.environ["PATH"],
                       TEST_NORMALIZED_LOG=str(observed), TEST_GENERATED_ENTRIES=str(generated_entries))
            result = subprocess.run(
                [PWSH, "-NoProfile", "-File", str(REPO / ".github/configs/generate-shader-configs.ps1"),
                 "-LogFile", str(source), "-OutputDir", str(root), "-OutputName", output.name, "-Force"],
                env=env, capture_output=True, text=True, timeout=30,
            )
            self.assertEqual(source.read_text(encoding="utf-8"), text)
            return result, output.read_text(encoding="utf-8"), observed.read_text(encoding="utf-8") if observed.exists() else None

    def test_legacy_and_current_prefixes_preserve_exact_defines(self):
        for prefix in ("[12:00:00.001] [ 42 ] [D] ",
                       "[2026-10-05 12:00:00.001] [debug] [ 42 ] [ShaderCache.cpp:5960] "):
            with self.subTest(prefix=prefix):
                result, output, normalized = self.generate(prefix + MESSAGE + "\n[ShaderTiming] remaining=0\n")
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("captured_shader_variants: 1", output)
                self.assertEqual(normalized, "[12:00:00.001] [42] [D] " + MESSAGE + "\n[ShaderTiming] remaining=0\n")

    def test_missing_queue_evidence_preserves_previous_inventory(self):
        result, output, normalized = self.generate("[12:00:00.001] [42] [D] " + MESSAGE + "\n")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("No shader queue-state records", result.stderr)
        self.assertEqual(output, "previous inventory")
        self.assertIsNone(normalized)

    def test_active_queue_preserves_previous_inventory(self):
        result, output, normalized = self.generate(
            "[12:00:00.001] [42] [D] " + MESSAGE + "\n[ShaderTiming] remaining=12\n")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("still in progress", result.stderr)
        self.assertEqual(output, "previous inventory")
        self.assertIsNone(normalized)

    def test_completed_queue_allows_generation(self):
        result, output, _ = self.generate(
            "[12:00:00.001] [42] [D] " + MESSAGE + "\n[ShaderTiming] remaining=12\n[ShaderTiming] remaining=0\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("captured_shader_variants: 1", output)

    def test_new_compile_after_completed_queue_preserves_inventory(self):
        result, output, normalized = self.generate(
            "[ShaderTiming] remaining=0\n[12:00:00.001] [42] [D] " + MESSAGE + "\n")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("still in progress", result.stderr)
        self.assertEqual(output, "previous inventory")
        self.assertIsNone(normalized)

    def test_lost_records_preserve_previous_inventory(self):
        for text, count in (("[unrecognized] " + MESSAGE + "\n", 1),
                            ("[12:00:00.001] [42] [D] " + MESSAGE + "\n", 0)):
            with self.subTest(text=text, generated=count):
                result, output, _ = self.generate(text + "[ShaderTiming] remaining=0\n", count)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(output, "previous inventory")


if __name__ == "__main__":
    unittest.main()
