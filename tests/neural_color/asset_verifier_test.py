"""Asset verifier failure-path fixtures (not a deployed-game inventory)."""
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/nr-color"))
import verify_assets as assets


class AssetTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "source"
        self.deployed = Path(self.temp.name) / "Data"
        self.shaders = self.root / assets.FEATURE / assets.SHADERS
        self.shaders.mkdir(parents=True)
        (self.root / assets.FEATURE / "CORE").touch()
        registry = self.root / "include/FeatureVersions.h"
        registry.parent.mkdir(parents=True)
        registry.write_text('"NeuralRendering"sv, {1,5,0}\n')
        manifest = self.root / assets.FEATURE / "Shaders/Features/NeuralRendering.ini"
        manifest.parent.mkdir(parents=True); manifest.write_text("[Info]\nVersion = 1-5-0\n")
        for name in assets.NAMES:
            (self.shaders / name).write_text('#include "Upscaling/NeuralRendering/ColorCommon.hlsli"\n' if name.endswith(".hlsl") else "// fixture\n")
        self.producers = self.root / "src/Features/Upscaling/NeuralRendering"
        self.producers.mkdir(parents=True)
        paths = ['L"Data/' + (assets.SHADERS / name).as_posix() + '"' for name in assets.NAMES if name.endswith(".hlsl")]
        (self.producers / "ColorPipeline.cpp").write_text("\n".join(paths[:-1]))
        (self.producers / "ExposureCapture.cpp").write_text(paths[-1])

    def test_complete_source_and_deployment(self):
        result = assets.verify(self.root)
        self.assertTrue(result["ok"], result)
        for row in result["assets"]:
            dest = self.deployed / row["destination"]; dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(self.root / row["source"], dest)
        self.assertTrue(assets.verify(self.root, self.deployed)["ok"])

    def test_present_but_unpackaged_transitive_include(self):
        (self.shaders / "ColorCommon.hlsli").write_text('#include "Upscaling/NeuralRendering/Hidden.hlsli"\n')
        (self.shaders / "Hidden.hlsli").write_text("// Not mapped for deployment\n")
        result = assets.verify(self.root)
        self.assertFalse(result["ok"])
        self.assertTrue(any("not packaged" in error for error in result["errors"]))

    def test_missing_include(self):
        (self.shaders / "ColorCommon.hlsli").write_text('#include "Missing.hlsli"\n')
        self.assertFalse(assets.verify(self.root)["ok"])

    def test_sibling_include_is_not_the_runtime_search_path(self):
        (self.shaders / "ColorPrepareCS.hlsl").write_text('#include "ColorCommon.hlsli"\n')
        result = assets.verify(self.root)
        self.assertFalse(result["ok"])
        self.assertTrue(any("Unresolved shader include" in error for error in result["errors"]))

    def test_non_utf8_shader_is_structured_failure(self):
        (self.shaders / "ColorPrepareCS.hlsl").write_bytes(b"\xff\xff")
        result = assets.verify(self.root)
        self.assertFalse(result["ok"])
        self.assertTrue(any("Unreadable source" in error for error in result["errors"]))

    def test_non_utf8_runtime_is_structured_failure(self):
        (self.producers / "ColorPipeline.cpp").write_bytes(b"\xff")
        self.assertFalse(assets.verify(self.root)["ok"])

    def test_missing_core_marker(self):
        (self.root / assets.FEATURE / "CORE").unlink()
        self.assertFalse(assets.verify(self.root)["ok"])

    def test_manifest_requires_one_valid_version(self):
        manifest = self.root / assets.FEATURE / "Shaders/Features/NeuralRendering.ini"
        for text in ("[Info]\n", "[Info]\nVersion = 1.5.0\n",
                     "[Info]\nVersion = 1-5-0\nVersion = 1-5-1\n"):
            with self.subTest(text=text):
                manifest.write_text(text)
                result = assets.verify(self.root)
                self.assertFalse(result["ok"])
                self.assertTrue(any("exactly one valid version" in error for error in result["errors"]))

    def test_manifest_must_match_feature_registry(self):
        manifest = self.root / assets.FEATURE / "Shaders/Features/NeuralRendering.ini"
        manifest.write_text("[Info]\nVersion = 1-4-0\n")
        result = assets.verify(self.root)
        self.assertFalse(result["ok"])
        self.assertTrue(any("differs from registry 1-5-0" in error for error in result["errors"]))

    def test_missing_runtime_reference(self):
        (self.producers / "ExposureCapture.cpp").write_text("// missing compile call\n")
        self.assertFalse(assets.verify(self.root)["ok"])

    def test_stale_deployed_asset(self):
        result = assets.verify(self.root)
        for row in result["assets"]:
            dest = self.deployed / row["destination"]; dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(self.root / row["source"], dest)
        (self.deployed / assets.SHADERS / "ColorCommon.hlsli").write_text("// stale\n")
        self.assertFalse(assets.verify(self.root, self.deployed)["ok"])

    def test_source_presence_does_not_claim_shader_compilation(self):
        result = assets.verify(self.root)
        self.assertTrue(result["ok"])
        self.assertFalse(result["shaderCompilationChecked"])
        self.assertFalse(result["deployedHashChecked"])


if __name__ == "__main__":
    unittest.main()
