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
    def test_each_character_override_participates_in_change_detection(self):
        import re

        source = read("src/Features/Upscaling/VRRenderScaleDevBenchBridge.cpp")
        assigned = set(re.findall(r"requestedSettings\.(neuralCharacter\w+)\s*=", source))
        start = source.index("const bool runtimeSettingsChanged = fovTaaDisabled")
        detection = source[start:source.index("if (!settingsChanged)", start)]
        compared = set(re.findall(
            r"previousSettings\.(neuralCharacter\w+)\s*!=\s*requestedSettings\.\1", detection))
        self.assertTrue({"neuralCharacterRenderingEnabled", "neuralCharacterFacesEnabled",
                         "neuralCharacterSkinEnabled", "neuralCharacterHairEnabled",
                         "neuralCharacterRoiMargin"}.issubset(assigned))
        self.assertEqual(assigned - compared, set(),
                         "A standalone character override would be rejected as nr_configure_noop")

    def test_atomic_lookup_and_consumers(self):
        source = read("src/Features/Upscaling/NeuralRendering/CharacterRendering.cpp")
        getter = source[source.index("CharacterPreparedSelection CharacterRendering::GetPreparedSelection("):
                        len(source)]
        self.assertEqual(getter.count("std::scoped_lock lock(state_->mutex_);"), 1)
        self.assertIn("slot->maskSrv, support, slot->computeSubrect", getter)
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

    def test_single_roi_has_no_partition_search_or_output_override(self):
        import re
        sources = [read(name) for name in (
            "src/Features/Upscaling.cpp",
            "src/Features/Upscaling/NeuralRendering/Renderer.cpp",
            "src/Features/Upscaling/NeuralRendering/CharacterRendering.cpp",
            "src/Features/Upscaling/NeuralCapture.cpp")]
        # Preprocess the real conditional blocks without unrelated include trees.
        with tempfile.TemporaryDirectory(prefix="csx-nr-search-") as directory:
            unit = Path(directory) / "search.cpp"
            unit.write_text(re.sub(r"^\s*#\s*include[^\n]*", "", "\n".join(sources), flags=re.M))
            for enabled in (False, True):
                result = subprocess.run(
                    [args.compiler, "/nologo", "/EP", "/TP",
                     "/DDEVBENCH_BRIDGE_ENABLED" if enabled else "/UDEVBENCH_BRIDGE_ENABLED", str(unit)],
                    capture_output=True, text=True, check=True)
                for token in ("completedOutputSerial", "a_outcome.outputPlans["):
                    self.assertNotIn(token, result.stdout)
                self.assertEqual("args.renderingMode" in result.stdout, enabled)
        renderer = sources[1]
        apply = renderer[renderer.index("bool Renderer::State::ApplyRegionBatchLocked("):
                         renderer.index("void Renderer::State::CaptureReplayBatch(")]
        self.assertLess(apply.index("if (quarantined_)"),
                        apply.index("NR requires a valid main or submit eye feature slot"))
        self.assertLess(apply.index("if (failureLatched_)"),
                        apply.index("NR requires a valid main or submit eye feature slot"))
        self.assertLess(apply.index("NR requires a valid main or submit eye feature slot"),
                        apply.index("std::array<ValidatedResources"))

    def test_runtime_mode_is_independent_of_capture(self):
        source = read("src/Features/Upscaling/NeuralCapture.cpp")
        setter = source[source.index("void Upscaling::SetNeuralExecutionContext("):
                        source.index("Util::PassTimingHandle Upscaling::CaptureNeuralStage(")]
        self.assertLess(setter.index("args.renderingMode = GetNeuralRenderingMode();"),
                        setter.index("CaptureEvidenceEnabled()"))
        renderer = read("src/Features/Upscaling/NeuralRendering/Renderer.cpp")
        policies = renderer[renderer.index("void Renderer::State::ApplyCompactLayoutLocked("):
                            renderer.index("bool Renderer::State::ApplyBatchLocked(")]
        self.assertNotIn("executionContext.renderingMode", policies)

    def test_baseline_transition_policy_is_unchanged(self):
        shader = read("features/Neural Rendering/Shaders/Upscaling/NeuralRendering/ColorReconstructCS.hlsl")
        self.assertIn("for (int y = -1; y <= 1; ++y)", shader)
        self.assertIn("for (int x = -1; x <= 1; ++x)", shader)
        self.assertIn("float edgeWeight = saturate(float(min(edge.x, edge.y)) / 4.0);", shader)
        self.assertIn("int2(RegionSize) - 1", shader)
        self.assertIn("Prepared.Load(int3(RegionOffset + local, 0))", shader)
        blend = read("features/Upscaling/Shaders/Upscaling/FoveatedCenterBlendCS.hlsl")
        self.assertRegex(blend, r"if \(characterWeight > 0\.0\)\s*\{\s*float4 neuralColor = CenterColor.Load")

    def test_empty_proof_is_current_and_has_no_extra_readback(self):
        source = read("src/Features/Upscaling/NeuralRendering/CharacterRendering.cpp")
        early = source[source.index("EarlyMaskReadback* FindCurrentSupport("):source.index("static void PublishMaskRoiSnapshot(")]
        self.assertNotIn("PollCharacterMaskBounds(", source)
        self.assertNotIn("TryApplyEarlyMaskBounds(", source)
        self.assertIn("readback.captureSerial == earlyMaskCaptureSerial_", early)
        self.assertIn("readback.frame == a_args.sourceWorldFrame", early)
        self.assertIn("readback.categories == GetEnabledCharacterCategoryMask(a_args.settings)", early)
        self.assertIn('slot.maskRoiStatus = authoredMode ? "cpu_geometry_single" : "disabled";', source)
        self.assertIn('slot.emptyProof = forcedEmpty', source)
        finalization = source[source.index("bool CharacterRendering::FinalizePreparedMasks("):
                              source.index("void CharacterRendering::ResolveFeature18Disposition(")]
        self.assertIn("!slot.requiresEvaluation && !state_->HasCurrentEmptyProof(slot)", finalization)
        self.assertIn("slot.prepareKey.settings != BuildSettingsKey(args.settings)", finalization)
        self.assertIn("slot.prepareKey.crop != args.viewportCrop", finalization)
        self.assertNotIn("PollCharacterMaskBounds(", finalization)
        self.assertIn("if (!a_slot.maskUniform || a_slot.uniformMaskValue != a_value)", source)

    def test_empty_episode_resets_only_evaluating_eyes(self):
        source = read("src/Features/Upscaling/NeuralRendering/CharacterRendering.cpp")
        self.assertIn("result.resetHistory = a_slot.requiresEvaluation && a_slot.resetHistoryAfterEmpty;", source)
        upscaling = read("src/Features/Upscaling.cpp")
        self.assertIn("a_args.reset = a_args.reset || result.resetHistory;", upscaling)
        self.assertIn("a_batchArgs[eye].reset = a_batchArgs[eye].reset || maskResults[eye].resetHistory;", upscaling)
        self.assertIn("globals::game::isVR && !a_results[0].bypassed && !a_results[1].bypassed", upscaling)

    def test_transaction_capture_and_budget_boundaries(self):
        source = read("src/Features/Upscaling/NeuralRendering/CharacterRendering.cpp")
        capture = source[source.index("bool CharacterRendering::CaptureAuthoredCategories("):
                         source.index("bool CharacterRendering::IsCurrentSelectionEmpty(")]
        self.assertLess(capture.index("EnsureDepthCapture("), capture.rindex("CaptureSourceGeometry("))
        projection = source[source.index("void RefreshProjectedActors("):source.index("ProjectedPlan BuildPlan(")]
        self.assertNotIn("GetProjectionEyePosition(", projection)
        self.assertNotIn("frameBufferCached", projection)
        admission = source[source.index("bool CharacterRendering::ShouldAuthorActor("):
                           source.index("bool CharacterRendering::ObserveGeometry(")]
        self.assertLess(admission.index("TryRefineCharacterActor("), admission.index("const auto projectBound"))
        budget_fallback = admission[admission.index("if (!TryRefineCharacterActor("):admission.index("const auto validBound")]
        self.assertIn("admission.admitted = true;", budget_fallback)
        self.assertIn("return true;", budget_fallback)
        finalization = source[source.index("bool CharacterRendering::FinalizePreparedMasks("):
                              source.index("void CharacterRendering::ResolveFeature18Disposition(")]
        for field in ("sourceWorldFrame", "generation", "settings", "outputIsJittered", "viewportCrop.fullInput", "viewportCrop.fullOutput"):
            self.assertIn(f"args.{field} != a_args.front().{field}", finalization)
        self.assertRegex(capture, r"#ifdef DEVBENCH_BRIDGE_ENABLED\s+if \(a_gpuMaskSupport\)\s+state_->QueueEarlyMaskBounds")
        self.assertNotIn("selected->staging", source)
        observation = source[source.index("bool CharacterRendering::ObserveGeometry("):source.index("void CharacterRendering::ObserveClassificationRejection(")]
        self.assertLess(observation.index("observationKeys_.contains("), observation.index("Util::ReadGeometryBounds("))
        self.assertIn(".bounds = bounds", observation)
        self.assertIn("const auto planningStart = evidence ?", source)

    def test_full_resolution_empty_guides_are_not_consumed(self):
        upscaling = read("src/Features/Upscaling.cpp")
        start = upscaling.index("bool Upscaling::PrepareFullResolutionNeuralInputs(")
        body = upscaling[start:upscaling.index("\n}\n", start)]
        self.assertLess(body.index("IsCurrentSelectionEmpty("), body.index("CopyRawDepthRegion("))
        self.assertIn("if (emptyEyeMask != (1u << eyeCount) - 1u && !copyGuides())", body)
        self.assertIn("args.depthGuideSRV = skippedGuides ? nullptr", upscaling)
        source = read("src/Features/Upscaling/NeuralRendering/CharacterRendering.cpp")
        self.assertIn('return fail("nonempty character selection requires current depth guides");', source)
        self.assertIn("earlySelectionPlans_ = {};", source)

    def test_empty_proof_schema(self):
        import json
        import re
        bridge = read("src/Features/Upscaling/VRRenderScaleDevBenchBridge.cpp")
        schema = json.loads(re.search(r'kNeuralRenderingDescriptor = R"nr\((.*?)\)nr"', bridge, re.S).group(1))
        self.assertNotIn("GPU-proven empty bypass remains disabled", schema["description"])
        self.assertIn("pending, stale or failed bounds never prove empty",
                      schema["description"])
        definitions = []

        def visit(value):
            if isinstance(value, dict):
                if "emptyProof" in value:
                    definitions.append(value["emptyProof"])
                for item in value.values():
                    visit(item)
            elif isinstance(value, list):
                for item in value:
                    visit(item)
        visit(schema["outputSchema"])
        self.assertEqual(len(definitions), 1)
        self.assertEqual(definitions[0]["enum"], ["none", "cpu_selection", "gpu_category_superset", "diagnostic_zero"])


if __name__ == "__main__":
    unittest.main(argv=[__file__, *remaining])
