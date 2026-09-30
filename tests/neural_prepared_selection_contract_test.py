"""Source and preprocessing checks; no native shader or DLL execution."""

import argparse
from pathlib import Path
import subprocess
import tempfile
import unittest

parser = argparse.ArgumentParser()
parser.add_argument("--compiler", required=True)
args, remaining = parser.parse_known_args()
ROOT = Path(__file__).resolve().parents[1]


def read(name):
    return (ROOT / name).read_text()


class PreparedSelectionContract(unittest.TestCase):
    def test_atomic_lookup_and_consumers(self):
        source = read("src/Features/Upscaling/NeuralRendering/CharacterRendering.cpp")
        getter = source[source.index("CharacterPreparedSelection CharacterRendering::GetPreparedSelection("):]
        self.assertEqual(getter.count("std::scoped_lock lock(state_->mutex_);"), 1)
        self.assertIn("slot->maskSrv, support, slot->computeSubrect, slot->computeRegions", getter)
        upscaling = read("src/Features/Upscaling.cpp")
        for removed in ("GetPreparedMaskSrv", "GetMaskSupportRect", "GetPreparedComputeSubrect", "GetPreparedComputeRegions"):
            self.assertNotIn(removed, source + upscaling)
        self.assertEqual(upscaling.count("&characterComposite.maskSupport"), 2)
        self.assertIn("&selection.maskSupport", upscaling)
        self.assertIn("&characterSelections[eye].maskSupport", upscaling)

    def test_finalization_evidence_is_bridge_and_capture_gated(self):
        source = read("src/Features/Upscaling/NeuralRendering/CharacterRendering.cpp")
        finalization = source[source.index("bool CharacterRendering::FinalizePreparedMasks("):
                              source.index("void CharacterRendering::ResolveFeature18Disposition(")]
        self.assertIn("a_results[index] = state_->BuildPreparedResult(args, slot);", finalization)
        source = source[source.index("[[nodiscard]] CharacterMaskPrepareResult BuildPreparedResult("):
                        source.index("[[nodiscard]] bool BeginObservationFrame(")]
        with tempfile.TemporaryDirectory(prefix="csx-nr-selection-") as directory:
            unit = Path(directory) / "selection.cpp"
            unit.write_text(source)
            for enabled in (False, True):
                result = subprocess.run(
                    [args.compiler, "/nologo", "/EP", "/TP",
                     "/DDEVBENCH_BRIDGE_ENABLED" if enabled else "/UDEVBENCH_BRIDGE_ENABLED", str(unit)],
                    capture_output=True, text=True, check=True)
                self.assertEqual("FindPreparationEvidence(" in result.stdout, enabled)
                self.assertEqual("CaptureEvidenceEnabled()" in result.stdout, enabled)

    def test_baseline_transition_policy_is_unchanged(self):
        shader = read("features/Neural Rendering/Shaders/Upscaling/NeuralRendering/ColorReconstructCS.hlsl")
        self.assertIn("for (int y = -1; y <= 1; ++y)", shader)
        self.assertIn("for (int x = -1; x <= 1; ++x)", shader)
        self.assertIn("float edgeWeight = saturate(float(min(edge.x, edge.y)) / 4.0);", shader)
        self.assertIn("int2(RegionSize) - 1", shader)
        self.assertIn("Prepared.Load(int3(RegionOffset + local, 0))", shader)
        blend = read("features/Upscaling/Shaders/Upscaling/FoveatedCenterBlendCS.hlsl")
        self.assertRegex(blend, r"if \(characterWeight > 0\.0\)\s*\{\s*float4 neuralColor = CenterColor.Load")


if __name__ == "__main__":
    unittest.main(argv=[__file__, *remaining])
