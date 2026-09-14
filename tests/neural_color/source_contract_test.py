"""Supplement executable math/WARP tests with shared-route wiring contracts."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
NR = ROOT / "src/Features/Upscaling/NeuralRendering"
SHADERS = ROOT / "features/Neural Rendering Colour/Shaders/Upscaling/NeuralRendering"


class Contracts(unittest.TestCase):
    def test_shared_order_and_transaction(self):
        source = (NR / "Renderer.cpp").read_text(encoding="utf-8-sig")
        self.assertEqual(source.count("state_->CaptureColorConfiguration("), 3)
        self.assertLess(source.index("colorPipeline_.Prepare("), source.index("Runtime::Instance().Execute("))
        self.assertLess(source.index("colorPipeline_.Reconstruct("), source.index("colorPipeline_.Commit("))
        self.assertIn("colorConfiguration_.Enabled() ||", source)
        self.assertIn("colorInputEpoch = colorConfiguration_.inputEpoch", source)
        self.assertIn("historyValid = !colorConfiguration_.experiments.transportBypass", source)
        self.assertIn("context.timingPending = false", (NR / "ColorTransport.cpp").read_text())

    def test_baseline_selection_not_duplicated(self):
        source = (SHADERS / "ColorReconstructCS.hlsl").read_text()
        self.assertNotIn("CharacterMask", source)
        self.assertIn("RegionSize) - 1", source)
        self.assertIn("DetailStrength == 0.0 && AppearanceMix == 0.0", source)
        self.assertIn("baseline.a", source)
        self.assertIn("neuralSource - originalProxy", (SHADERS / "ColorCommon.hlsli").read_text())

    def test_async_measurement_and_resource_lifetime(self):
        source = (NR / "ColorPipeline.cpp").read_text()
        self.assertIn("D3D11_ASYNC_GETDATA_DONOTFLUSH", source)
        self.assertIn("D3D11_MAP_FLAG_DO_NOT_WAIT", source)
        self.assertNotIn("->Flush(", source)
        self.assertIn("measurement.source = readback.source", source)
        guard = (NR / "ComputeStateGuard.h").read_text()
        self.assertIn("SetPredication(nullptr, FALSE)", guard)
        self.assertIn("SetPredication(predicate_.Get(), predicateValue_)", guard)
        self.assertIn("for (auto& readback : readbacks) readback.Abandon()", source)

    def test_feature_and_packaging(self):
        source = (ROOT / "src/Feature.cpp").read_text()
        self.assertIn("&NeuralColor::Instance()", source)
        self.assertTrue((ROOT / "features/Neural Rendering Colour/CORE").exists())
        ini = ROOT / "features/Neural Rendering Colour/Shaders/Features/NeuralColor.ini"
        self.assertIn("Version = 1-2-0", ini.read_text())
        ui = (ROOT / "src/Features/NeuralColor.cpp").read_text()
        self.assertIn('"communityshaders.nr_color"', ui)
        save = ui[ui.index("void NeuralColor::SaveSettings"):ui.index("void NeuralColor::RestoreDefaultSettings")]
        self.assertNotIn("experiments", save)


if __name__ == "__main__":
    unittest.main()
