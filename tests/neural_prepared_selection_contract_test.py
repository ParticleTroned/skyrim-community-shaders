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
    def test_each_neural_override_participates_in_change_detection(self):
        import re

        source = read("src/Features/Upscaling/VRRenderScaleDevBenchBridge.cpp")
        assigned = set(re.findall(r"requestedSettings\.(neural(?:Rendering|Character)\w+)\s*=", source))
        start = source.index("const bool runtimeSettingsChanged = fovTaaDisabled")
        detection = source[start:source.index("if (!settingsChanged)", start)]
        compared = set(re.findall(
            r"previousSettings\.(neural(?:Rendering|Character)\w+)\s*!=\s*requestedSettings\.\1", detection))
        self.assertTrue({"neuralCharacterRenderingEnabled", "neuralCharacterFacesEnabled",
                         "neuralCharacterSkinEnabled", "neuralCharacterHairEnabled",
                         "neuralCharacterRoiMargin"}.issubset(assigned))
        self.assertEqual(assigned - compared, set(),
                         "A standalone NR override would be rejected as nr_configure_noop")

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
        self.assertIn("UsesCharacterVisualIsolation(a_settings, a_sourceWorldFrame)", upscaling)
        self.assertIn("IsCharacterMaskActive(BuildCharacterSettings(a_settings, a_sourceWorldFrame))", upscaling)


    def test_scaled_c_selection_requires_transaction_completion(self):
        source = read("src/Features/Upscaling.cpp")
        start = source.index("bool Upscaling::PrepareReducedResolutionNeuralOutputTarget(")
        target = source[start:source.index("bool Upscaling::PrepareReducedResolutionNeuralOutput(", start)]
        self.assertIn("args.renderingMode != NeuralRendering::RenderingMode::ReducedResolution", target)
        self.assertIn("CSX::NvidiaComIdentity::IsSame(args.colorInput", target)
        self.assertIn("output.mask = selection.mask;", target)
        self.assertIn("output.providerBlending = selection.providerBlending;", target)
        self.assertLess(target.index("args.colorOutput = selected->resource.get();"),
                        target.index("foveatedCenterNeuralOut[eye].reset();"))
        self.assertNotIn("CopyResource(", target)
        self.assertNotIn("Dispatch(", target)
        start = source.index("NeuralStereoEvaluationSummary EvaluatePreparedNeuralStereo(")
        evaluation = source[start:source.index("\n\t}", start)]
        self.assertLess(evaluation.index("FinalizePreparedNeuralStereoInputs("),
                        evaluation.index("ModelResolution::Required("))
        self.assertLess(evaluation.index("PrepareReducedResolutionNeuralOutputTarget("),
                        evaluation.index("Renderer::Instance().ApplyStereo("))
        self.assertIn("args.modelOutputSelection ? outcome.WasOutputSelected(args.featureSlot)", evaluation)
        self.assertIn("a_upscaling.PrepareReducedResolutionNeuralOutput(eye, args)", evaluation)
        start = source.index("bool BindPreparedActorBlending(")
        binding = source[start:source.index("\n\t}", start)]
        self.assertLess(binding.index("args.actorSelection = selection.mask;"),
                        binding.index("if (!requested)"))
        self.assertLess(binding.index("if (!requested)"), binding.index("selection.mask->GetResource(&args.controlMask);"))


    def test_c_selected_allocation_is_admitted_before_creation(self):
        source = read("src/Features/Upscaling.cpp")
        start = source.index("bool Upscaling::EnsureReducedResolutionNeuralSelected(")
        helper = source[start:source.index("bool Upscaling::PrepareReducedResolutionNeuralOutputTarget(", start)]
        cached = helper.index("selected->desc.Format == sourceDesc.Format")
        staged = helper.index("if (validateStaged || ShouldReuseOrdinarySaveResources())")
        admission = helper.index("ReserveOutputAllocation(device, *bytes)")
        creation = helper.index("EnsureFoveatedTexture(selected, source")
        refresh = helper.index("GpuMemoryBudget::Get().Sample(device, 0, true)")
        self.assertLess(cached, staged)
        self.assertLess(staged, admission)
        self.assertLess(admission, creation)
        self.assertLess(creation, refresh)
        self.assertIn("LogicalTextureBytes(sourceDesc.Format, width, height)", helper)
        self.assertIn("if (!reservation)", helper)
        self.assertNotIn("reservation.Reset()", helper)
        self.assertNotIn("EnsureFoveatedTexture(foveatedCenterNeuralSelected", source)
        self.assertEqual(source.count("EnsureReducedResolutionNeuralSelected("), 5)
        self.assertIn("neuralOutputHeight, validateStagedResources)", source)
        self.assertIn("EnsureReducedResolutionNeuralSelected(0, color, width, height)", source)

    def test_fused_selection_diagnostics_match_the_registered_schema(self):
        import json
        import re

        bridge = read("src/Features/Upscaling/VRRenderScaleDevBenchBridge.cpp")
        descriptor = json.loads(re.search(r'kNeuralRenderingDescriptor = R"nr\((.*?)\)nr"', bridge, re.S).group(1))
        properties = descriptor["outputSchema"]["properties"]["neuralRendering"]["properties"]
        self.assertEqual(properties["apiVersion"]["const"], 11)
        last_request = properties["modelResolution"]["properties"]["lastRequest"]["properties"]
        self.assertEqual(last_request["selectedOutput"]["type"], "boolean")
        self.assertIn("actors and packed crops", last_request["sharedInputs"]["description"])
        transport = properties["sourceTransport"]["properties"]
        self.assertEqual(transport["schemaVersion"]["const"], 4)
        self.assertEqual(transport["lastModelOutputSelected"]["type"], "boolean")
        self.assertIn("API-v11 NR runtime", descriptor["description"])
        self.assertIn("latest request's completed-selection flag", descriptor["description"])
        runtime = properties["characterRendering"]["properties"]["runtime"]["properties"]
        early_bounds = runtime["earlyMaskBounds"]["description"]
        self.assertIn("Nonblocking D3D11 query polling and readback", early_bounds)
        self.assertIn("current-mask GPU coverage guard", early_bounds)
        self.assertNotIn("Production crop planning issues no reduction or readback", early_bounds)
        compatibility_fence = runtime["eyes"]["items"]["properties"]["maskRoiReadbackFenceValue"]["description"]
        self.assertIn("always zero", compatibility_fence)
        self.assertIn("D3D11 query completion separately", compatibility_fence)
        self.assertIn('{ "apiVersion", 11 }', bridge)
        self.assertIn('{ "selectedOutput", snapshot.modelOutputSelected }', bridge)
        renderer = read("src/Features/Upscaling/NeuralRendering/Renderer.cpp")
        start = renderer.index("void Renderer::State::SetRequestTelemetryLocked(")
        request = renderer[start:renderer.index("\n\t}", start)]
        self.assertIn("snapshot_.modelOutputSelected = false;", request)

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
                self.assertEqual("void Renderer::State::ApplyCompactLayoutLocked(" in result.stdout, enabled)
                self.assertIn("args.renderingMode = GetNeuralRenderingMode();", result.stdout)
                self.assertIn("args.modelResolutionPercent = NeuralRendering::EffectiveModelResolutionPercent", result.stdout)
                self.assertEqual("SetNeuralCaptureExecutionContext" in result.stdout, enabled)
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
        start = source.index("void Upscaling::SetNeuralExecutionContext(")
        setter = source[start:source.index("\n}\n", start)]
        for parameter in ("args.renderingMode = GetNeuralRenderingMode();",
                          "args.modelResolutionPercent = NeuralRendering::EffectiveModelResolutionPercent"):
            self.assertLess(setter.index(parameter), setter.index("SetNeuralCaptureExecutionContext("))
        renderer = read("src/Features/Upscaling/NeuralRendering/Renderer.cpp")
        policies = renderer[renderer.index("void Renderer::State::ApplyCompactLayoutLocked("):
                            renderer.index("bool Renderer::State::ApplyBatchLocked(")]
        self.assertNotIn("executionContext.renderingMode", policies)

    def test_baseline_transition_policy_is_unchanged(self):
        shader = read("features/Neural Rendering/Shaders/Upscaling/NeuralRendering/ColorReconstructCS.hlsl")
        self.assertIn("for (int y = -1; y <= 1; ++y)", shader)
        self.assertIn("for (int x = -1; x <= 1; ++x)", shader)
        self.assertIn("float edgeWeight = saturate(float(min(edge.x, edge.y)) / 4.0);", shader)
        self.assertIn("uint2 regionMin = 0, regionMax = RegionSize - 1u;", shader)
        self.assertIn("clamp(int2(local) + int2(x, y), int2(regionMin), int2(regionMax))", shader)
        self.assertIn("Prepared.Load(int3(RegionOffset + local, 0))", shader)
        blend = read("features/Upscaling/Shaders/Upscaling/FoveatedCenterBlendCS.hlsl")
        self.assertRegex(blend, r"if \(characterWeight > 0\.0\)\s*\{\s*float4 neuralColor = CenterColor.Load")

    def test_empty_proof_is_current_and_has_no_extra_readback(self):
        source = read("src/Features/Upscaling/NeuralRendering/CharacterRendering.cpp")
        early = source[source.index("EarlyMaskReadback* FindCurrentSupport("):source.index("CharacterTileCrops ReadTileCropProposal(")]
        empty_selection = source[source.index("bool CharacterRendering::IsCurrentSelectionEmpty("):
                                 source.index("bool CharacterRendering::PrepareMask(")]
        self.assertNotIn("PollCharacterMaskBounds(", empty_selection)
        occupancy = source[source.index("CharacterTileCrops ReadTileCropProposal("):
                           source.index("static void PublishMaskRoiSnapshot(")]
        self.assertNotIn("PollCharacterMaskBounds(", occupancy)
        self.assertIn("CharacterTileProposalFresh(readback->frame, args.sourceWorldFrame)", occupancy)
        self.assertNotIn("slot.emptyProof", occupancy)
        retirement = source[source.index("void QueueEarlyMaskBounds("):
                            source.index("EarlyMaskReadback* FindCurrentSupport(")]
        self.assertEqual(retirement.count("PollCharacterMaskBounds("), 1)
        self.assertLess(retirement.index("completedTileProposal_.cpuBounds.swap(readback.cpuBounds)"),
                        retirement.index("CopyResource(selected->staging.Get(), selected->bounds.Get())"))
        self.assertIn("readback.captureSerial > completedTileProposal_.captureSerial", retirement)
        coverage = read("features/Neural Rendering/Shaders/Upscaling/NeuralRendering/ModelResolutionCoverageCS.hlsl")
        self.assertIn("CurrentSelection.Load", coverage)
        self.assertIn("PACKED_CROP_SUPPORT_PIXELS", coverage)
        self.assertIn("InterlockedOr(PackedCoverage[0], 1u)", coverage)
        adapter = read("src/Features/Upscaling/NeuralRendering/ModelResolution.cpp")
        self.assertIn("work.constants.packedCoverageGuard = 1u;", adapter)
        self.assertIn("DispatchReconstruction<3>(args, batch)", adapter)
        self.assertIn("DispatchReconstruction<4>(args, batch)", adapter)
        self.assertIn("DispatchReconstruction<8, 3>(args, batch)", adapter)
        reconstruct = read("features/Neural Rendering/Shaders/Upscaling/NeuralRendering/ModelResolutionReconstructCS.hlsl")
        self.assertIn("#ifdef PACKED_CROP_COVERAGE_GUARD", reconstruct)
        self.assertRegex(reconstruct, r"if \(PackedPadding != 0u && PackedCoverage\[0\] != 0u\) \{\s*Result\[pixel\] = source;\s*return;")
        self.assertIn("geometry_fallback_empty_grid", occupancy)
        for blocking in ("ReadCharacterMaskBounds(", "->Flush(", "Sleep("):
            self.assertNotIn(blocking, occupancy)
        self.assertNotIn("TryApplyEarlyMaskBounds(", source)
        self.assertIn("readback.captureSerial == earlyMaskCaptureSerial_", early)
        self.assertIn("readback.frame == a_args.sourceWorldFrame", early)
        self.assertIn("readback.categories == GetEnabledCharacterCategoryMask(a_args.settings)", early)
        self.assertTrue(
            'slot.maskRoiStatus = sceneStrengths ? "scene_full_inference_region_mask" : authoredMode ? "cpu_geometry_single" : "disabled";' in " ".join(source.split()),
            "Scene adjustments must retain full inference with regional mask work",
        )
        self.assertTrue("const bool cpuProvenEmpty = !sceneStrengths && authoredMode &&" in source,
                        "An empty actor selection cannot bypass ordinary scene NR")
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
        self.assertRegex(capture, r"if \(a_gpuMaskSupport \|\| a_tileCrops\)\s+state_->QueueEarlyMaskBounds")
        self.assertIn("CopyResource(selected->staging.Get(), selected->bounds.Get())", source)
        self.assertIn("slot.tileCropReadiness.Admit(tiles.valid)", source)
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
