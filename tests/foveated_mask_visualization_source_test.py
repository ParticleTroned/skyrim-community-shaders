"""Guard preview wiring that sits outside the extracted controller tests."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "src/Features/Upscaling.cpp").read_text()


def section(begin, end):
    start = SOURCE.index(begin)
    return SOURCE[start:SOURCE.index(end, start)]


class PreviewIntegration(unittest.TestCase):
    def test_profile_load_preserves_visualization(self):
        load = section("void Upscaling::LoadSettings(", "void Upscaling::RestoreDefaultSettings(")
        self.assertNotIn("settings.foveatedPeripheryMaskVisualization = false", load)
        defaults = section("void Upscaling::RestoreDefaultSettings(", "struct BSOpenVR_GetRenderTargetSize")
        self.assertIn("settings.foveatedPeripheryMaskVisualization = false", defaults)

    def test_submit_preview_uses_presentation_before_temporal_inputs(self):
        submit = section("bool Upscaling::SubmitVRUpscaledFrame(", "bool Upscaling::TryReplaceVanillaDynamicResolutionUpsample(")
        admission = submit.index("if (foveatedMaskVisualizationPreview) {")
        temporal = submit.index("SubmitTemporalInputs temporalInputs;")
        self.assertLess(admission, temporal)
        self.assertIn("presentationOnly = true;", submit[admission:admission + 160])
        self.assertIn("if (!presentationOnly)", submit[temporal:temporal + 100])
        stretch = submit[submit.index("const auto presentStretchOutput ="):submit.index("auto presentDeferredVendorOutput =")]
        self.assertIn("DispatchFoveatedMaskVisualization(eyeIndex)", stretch)
        self.assertIn("UpdateVRSubmitDesktopMirror(", stretch)
        self.assertIn("submitStageVendorEyeState = {};", stretch)
        self.assertIn("submitStageRuntimeFSRStereoState = {};", stretch)
        self.assertNotIn("VendorEvaluated", stretch)

    def test_main_preview_respects_output_ownership_and_skips_encoding(self):
        main = section("void Upscaling::Upscale()", "void Upscaling::PerformUpscaling()")
        preview = main.index("if (IsFoveatedMaskVisualizationEnabled(upscaleMethod)) {")
        self.assertLess(main.index("if (vrRenderScaleSubmitStageOwnsOutput)"), preview)
        self.assertLess(preview, main.index("auto encodeUpscalingTextures ="))
        draw = main[preview:main.index("pendingDLSSHistoryReset.exchange", preview)]
        self.assertIn("TryDrawMainFoveatedMaskVisualization(!vendorLifecycleMutationDeferred)", draw)
        self.assertIn("|| IsSubmitStageDeviceLost()", draw)
        self.assertIn("RequestHistoryReset();", draw)

    def test_failed_preview_cannot_bypass_vendor_recovery_admission(self):
        main = section("void Upscaling::Upscale()", "void Upscaling::PerformUpscaling()")
        preview = main.index("TryDrawMainFoveatedMaskVisualization(")
        guard = main.index("if (vendorLifecycleMutationDeferred && !CanDispatchExistingVRVendorEvaluation(upscaleMethod))")
        encoding = main.index("auto encodeUpscalingTextures =")
        self.assertLess(preview, guard)
        self.assertLess(guard, encoding)
        self.assertIn("return;", main[guard:encoding])

    def test_native_preview_uses_scene_format_without_widening_submit_contract(self):
        helper = section("bool Upscaling::TryDrawMainFoveatedMaskVisualization(", "bool Upscaling::DispatchFoveatedPeripheryPass(")
        self.assertIn("a_allowResourceCreation, mainDesc.Format", helper)
        allocator = section("bool Upscaling::EnsureVRPresentationTextures(", "void Upscaling::",)
        self.assertIn("colorSrcDesc.Format != colorFormat", allocator)
        header = (ROOT / "src/Features/Upscaling.h").read_text()
        self.assertIn("DXGI_FORMAT colorFormat = DXGI_FORMAT_R8G8B8A8_UNORM", header)

    def test_preview_mirror_cannot_allocate_during_protection(self):
        mirror = section("bool Upscaling::UpdateVRSubmitDesktopMirror(", "bool Upscaling::SubmitVRUpscaledFrame(")
        guard = mirror.index("ShouldReuseOrdinarySaveResources() || ShouldDeferVRVendorLifecycleMutation()")
        blit = mirror.index("BlitVRRenderScaleDesktopMirror(")
        self.assertLess(guard, blit)
        for resource in ("vrDesktopMirrorBlitTarget != sourceTexture", "!vrDesktopMirrorBlitRTV", "!vrDesktopMirrorBlitPS", "!upscaleVS"):
            self.assertIn(resource, mirror[guard:blit])
        self.assertIn("return true;", mirror[guard:blit])

    def test_eye_preview_preserves_bindings_outside_its_compute_slots(self):
        dispatch = section("bool Upscaling::DispatchFoveatedMaskVisualization(", "bool Upscaling::TryDrawMainFoveatedMaskVisualization(")
        self.assertIn("NeuralRendering::ComputeStateGuard<1>", dispatch)
        self.assertIn("CSGetSamplers", dispatch)
        self.assertIn("CSSetSamplers", dispatch)
        self.assertNotIn("UnbindUpscalingResources()", dispatch)


if __name__ == "__main__":
    unittest.main()
