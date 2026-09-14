"""Portable API, evidence, A/B and shader-deployment regression fixtures."""
from pathlib import Path
import copy
import importlib.util
import json
import shutil
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/nr-color"))
import assess
import verify_assets


def sample(slot=0, frame=100, captured=False):
    data = [0.5, 0.4, 0.3, 64, 0.5, 0.4, 0.3, 0, 0, 0, 0, 0, 0, 0, 0, 64,
            0, 0, 1, 1, 1, 1, 1, 1]
    return {"source": {"frame": frame, "sourceWorldFrame": frame, "generation": 4,
                       "physicalSlot": slot, "insertionPoint": 0, "revision": 7, "processed": True,
                       "failure": "", "profile": {"exposureSource": "captured_hdr" if captured else "manual"},
                       "modelEditShown": True, "transportBypass": True,
                       "exposureBinding": "gpu_snapshot_queued", "exposure": {"frame": frame, "epoch": 2, "sequence": 10, "ambiguous": False}},
            "values": data}


class AssessmentTests(unittest.TestCase):
    def test_explicit_semantic_success_required(self):
        receipt = {"ok": True, "transportOk": True, "semantic": {"known": True, "ok": True},
                   "data": {"content": [{"ok": True, "apiVersion": 2}]}}
        self.assertEqual(assess.unwrap(receipt)["apiVersion"], 2)
        receipt["data"]["content"] = [{"type": "text", "text": '{"ok":true,"apiVersion":2}'}]
        self.assertEqual(assess.unwrap(receipt)["apiVersion"], 2)
        for field in ("known", "ok"):
            bad = copy.deepcopy(receipt); bad["semantic"][field] = False
            with self.assertRaises(assess.AssessmentError): assess.unwrap(bad)
        bad = copy.deepcopy(receipt); bad["data"]["content"] = [{"apiVersion": 2}]
        with self.assertRaises(assess.AssessmentError): assess.unwrap(bad)

    def test_stale_or_half_eye_is_not_a_pair(self):
        left, right = sample(), sample(1)
        self.assertEqual(len(assess.fresh_groups({"measurements": [left, right]}, 7, 0, 90)), 1)
        self.assertFalse(assess.fresh_groups({"measurements": [left]}, 7, 0, 90))
        self.assertFalse(assess.fresh_groups({"measurements": [left, right]}, 8, 0, 90))
        self.assertFalse(assess.fresh_groups({"measurements": [left, right]}, 7, 0, 100))
        right["source"]["sourceWorldFrame"] = 101
        self.assertFalse(assess.fresh_groups({"measurements": [left, right]}, 7, 0, 90))

    def test_secondary_region_is_not_silently_omitted(self):
        left, right, secondary = sample(), sample(1), sample(4)
        status = {"measurements": [left, right], "slots": [x["source"] for x in (left, right, secondary)]}
        self.assertFalse(assess.fresh_groups(status, 7, 0, 90))
        status["measurements"].append(secondary)
        self.assertEqual(len(assess.fresh_groups(status, 7, 0, 90)[0]), 3)

    def test_missing_exposure_cannot_pass_as_zero_error(self):
        group = [sample(0, captured=True), sample(1, captured=True)]
        self.assertTrue(assess.assess_samples([group], True)["valid"])
        group[0]["values"][23] = 2  # Engine's unit fallback, not measured ratio.
        result = assess.assess_samples([group], True)
        self.assertFalse(result["valid"]); self.assertFalse(result["domainVerified"])

    def test_codec_invalidity_and_transport_error_rejected(self):
        for index, value in ((16, 1), (17, 1), (19, 0), (8, 0.1)):
            group = [sample(), sample(1)]
            group[0]["values"][index] = value
            self.assertFalse(assess.assess_samples([group], True)["valid"])
        group = [sample(), sample(1)]; group[0]["source"]["modelEditShown"] = False
        self.assertFalse(assess.assess_samples([group], True)["valid"])

    def test_capture_identity_disagreement_rejected(self):
        group = [sample(0, captured=True), sample(1, captured=True)]
        group[1]["source"]["exposure"]["sequence"] = 11
        self.assertFalse(assess.assess_samples([group], True)["valid"])

    def test_restore_projection_strips_observational_fields(self):
        settings = dict(zip(assess.SETTING_KEYS, (1, True, "managed", 1, 0, 1)))
        profile = {"domain": "unknown", "transform": "identity", "exposureMultiplier": 1,
                   "exposureSource": "manual", "domainOrigin": "unknown", "nrModelDomainVerified": False}
        status = {"settings": settings, "experiments": {"transportBypass": False, "diagnostics": False,
                    "captureEngineExposure": False, "applyModelEdit": True,
                    "upscaled_center": profile, "final_ldr_pre_ui": profile}}
        config = assess.editable(status)
        self.assertEqual(set(config["experiments"]["upscaled_center"]), set(assess.PROFILE_KEYS))
        self.assertNotIn("domainOrigin", json.dumps(config))

    def test_runtime_assets_and_stale_deployment(self):
        result = verify_assets.verify(ROOT)
        self.assertTrue(result["ok"], result["errors"])
        with tempfile.TemporaryDirectory() as directory:
            deployed = Path(directory)
            for row in result["assets"]:
                target = deployed / row["destination"]; target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(ROOT / row["source"], target)
            self.assertTrue(verify_assets.verify(ROOT, deployed)["ok"])
            shader = deployed / verify_assets.SHADERS / "ColorExposureCS.hlsl"
            shader.write_text("// stale shader\n", encoding="utf-8")
            self.assertFalse(verify_assets.verify(ROOT, deployed)["ok"])
            shader.unlink()
            self.assertFalse(verify_assets.verify(ROOT, deployed)["ok"])

    def test_hardware_paths_are_shared_and_nonblocking(self):
        nr = ROOT / "src/Features/Upscaling/NeuralRendering"
        pipeline = (nr / "ColorPipeline.cpp").read_text()
        capture = (nr / "ExposureCapture.cpp").read_text()
        self.assertIn("CSSetShaderResources(0, 4", pipeline)
        self.assertIn("work.exposure.srv.Get()", pipeline)
        self.assertNotIn("captureEpoch != epoch", capture)  # Never split an active eye pair on an API toggle.
        for source in (pipeline, capture):
            self.assertIn("D3D11_ASYNC_GETDATA_DONOTFLUSH", source)
            self.assertIn("D3D11_MAP_FLAG_DO_NOT_WAIT", source)
            self.assertNotIn("->Flush(", source)
        ui = (ROOT / "src/Features/NeuralColor.cpp").read_text()
        descriptor = json.loads(ui.split('R"schema(', 1)[1].split(')schema"', 1)[0])
        self.assertIn("captureEngineExposure", descriptor["inputSchema"]["properties"]["experiments"]["properties"])
        self.assertIn("void NeuralColor::EarlyPrepass()", ui)
        self.assertNotIn("NRColor.ini", ui)

    def test_revisions_are_not_overwritten_after_a_conflict(self):
        class Changed:
            def call(self, *_): return {"revision": 99}
        class Args:
            timeout = 1; warmup_frames = 0; sample_frames = 1
        with self.assertRaises(assess.MutationUncertain):
            assess.collect(Changed(), 7, 0, 0, Args())


if __name__ == "__main__":
    unittest.main()
