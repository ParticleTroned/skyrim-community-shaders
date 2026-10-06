"""Portable API, evidence, A/B and shader-deployment regression fixtures."""
from pathlib import Path
import copy
import importlib.util
import json
import shutil
import struct
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
                       "failure": "", "effectiveMode": "managed", "rect": [0, 0, 8, 8], "sourceFormat": 26, "outputFormat": 26,
                       "profile": {"domain": "linear" if captured else "unknown", "transform": "reversible_proxy" if captured else "identity", "exposureMultiplier": 1.0, "exposureSource": "captured_hdr" if captured else "manual"},
                       "modelEditShown": True, "transportBypass": True,
                       "exposureBinding": "gpu_snapshot_queued", "exposure": {"frame": frame, "epoch": 2, "sequence": 10, "ambiguous": False}},
            "values": data}


class AssessmentTests(unittest.TestCase):
    def test_lighting_evidence_matches_runtime_float_equality(self):
        for expected, applied in ((0.0, -0.0), (-0.0, 0.0), (0.5, 0.5 + 2**-30)):
            with self.subTest(expected=expected, applied=applied):
                result = assess.lighting_evidence({"lightingPreservation": expected},
                                                 [{"lightingPreservation": applied}])
                self.assertEqual(result["reason"], "latched_observations")
        for expected, applied in ((0.5, 0.5 + 2**-24),
                                  (0.0, struct.unpack("f", struct.pack("I", 1))[0])):
            with self.subTest(expected=expected, applied=applied):
                with self.assertRaisesRegex(assess.AssessmentError, "mismatched"):
                    assess.lighting_evidence({"lightingPreservation": expected},
                                             [{"lightingPreservation": applied}])
        batch = self.batch(1, 100, (0, 1))
        for item, value in zip(batch["measurements"], (0.0, -0.0)):
            item["source"]["lightingPreservation"] = value
        status = {"apiVersion": 3, "settings": {"lightingPreservation": 0.0}, "measurementBatches": [batch]}
        self.assertEqual(len(assess.fresh_groups(status, 7, 0, 90)), 1)

    def test_neural_lighting_resolves_effective_evidence_without_mutation(self):
        saved = {"enabled": True, "mode": "neural_lighting", "appearanceMix": 0.75,
                 "lightingPreservation": 1.0}
        effective = assess.effective_reconstruction_settings(saved)
        self.assertEqual(effective["appearanceMix"], 0.0)
        self.assertEqual(effective["lightingPreservation"], 0.0)
        self.assertEqual((saved["appearanceMix"], saved["lightingPreservation"]), (0.75, 1.0))
        self.assertEqual(assess.lighting_evidence(effective, [{"lightingPreservation": 0.0}])["value"], 0.0)
        disabled = assess.effective_reconstruction_settings({**saved, "enabled": False})
        self.assertEqual(disabled["lightingPreservation"], 1.0)
        with self.assertRaisesRegex(assess.AssessmentError, "lacks derived reconstruction controls"):
            assess.effective_reconstruction_settings({"enabled": True, "mode": "neural_lighting"})

    def test_lighting_preservation_requires_sampled_value_in_every_region(self):
        batch = self.batch(1, 100, (0, 1, 4, 5))
        status = {"apiVersion": 3, "settings": {"lightingPreservation": 0.5}, "measurementBatches": [batch]}
        self.assertFalse(assess.fresh_groups(status, 7, 0, 90))
        for item in batch["measurements"]:
            item["source"]["lightingPreservation"] = 0.5
        self.assertEqual(len(assess.fresh_groups(status, 7, 0, 90)), 1)
        for invalid in (None, True, "0.5", float("nan"), 1.0):
            batch["measurements"][-1]["source"]["lightingPreservation"] = invalid
            self.assertFalse(assess.fresh_groups(status, 7, 0, 90))
        legacy = [sample(0), sample(1)]
        result = assess.assess_samples([legacy], True)
        self.assertTrue(all(row["reason"] == "absent_legacy_evidence" for row in result["lightingPreservationEvidence"]))
        self.assertFalse(assess.lighting_evidence({}, [])["available"])

    def test_previous_capture_retains_real_frame_and_exact_age(self):
        group = [sample(0, captured=True), sample(1, captured=True)]
        for item in group:
            item["source"]["profile"]["exposureSource"] = "captured_hdr_previous"
            item["source"]["exposure"]["frame"] = 99
        self.assertTrue(assess.assess_samples([group], True, True)["valid"])
        for invalid_frame in (98, 100, 101):
            bad = copy.deepcopy(group)
            for item in bad:
                item["source"]["exposure"]["frame"] = invalid_frame
            self.assertFalse(assess.assess_samples([bad], True, True)["valid"])
        strict = copy.deepcopy(group)
        for item in strict:
            item["source"]["profile"]["exposureSource"] = "captured_hdr"
        self.assertFalse(assess.assess_samples([strict], True, True)["valid"])
        self.assertEqual(len(assess.candidates(False, True)), 5)

    @staticmethod
    def batch(batch_id, frame, slots):
        manifest = {"measurementBatchId": batch_id, "expectedMeasurementSlotMask": sum(1 << s for s in slots),
                    "frame": frame, "sourceWorldFrame": frame, "generation": 4, "revision": 7,
                    "insertionPoint": 0, "atomicColourBatch": True}
        samples = [sample(s, frame) for s in slots]
        for item in samples: item["source"].update(manifest)
        return {**manifest, "measurements": samples}

    def test_v3_changing_regions_keeps_complete_batches(self):
        split = self.batch(1, 100, (0, 1, 4, 5))
        asymmetric = self.batch(2, 101, (0, 1, 4))
        single = self.batch(3, 102, (0, 1))
        status = {"apiVersion": 3, "measurementBatches": [single, asymmetric, split],
                  "slots": [i["source"] for i in split["measurements"]],
                  "measurements": single["measurements"] + split["measurements"][2:]}
        groups = assess.fresh_groups(status, 7, 0, 90)
        self.assertEqual([[i["source"]["physicalSlot"] for i in g] for g in groups],
                         [[0, 1], [0, 1, 4], [0, 1, 4, 5]])
        self.assertTrue(assess.assess_samples(groups, True)["valid"])
        self.assertEqual(len(assess.fresh_groups(status, 7, 0, 90, expected_slots={0, 1, 4, 5})), 1)

    def test_v3_cannot_fall_back_to_latest_slot_mixture(self):
        status = {"apiVersion": 3, "measurements": [sample(0), sample(1)]}
        self.assertFalse(assess.fresh_groups(status, 7, 0, 90))
        for version in (3.0, True, "3"):
            self.assertFalse(assess.fresh_groups({**status, "apiVersion": version}, 7, 0, 90))
        batch = self.batch(1, 100, (0, 1, 4, 5))
        batch["measurements"].pop()
        status["measurementBatches"] = [batch]
        self.assertFalse(assess.fresh_groups(status, 7, 0, 90))

    def test_v3_rejects_conflicting_manifest_or_region(self):
        original = self.batch(1, 100, (0, 1, 4))
        changes = {"measurementBatchId": 2, "expectedMeasurementSlotMask": 3, "frame": 101,
                   "sourceWorldFrame": 101, "generation": 5, "revision": 8, "insertionPoint": 1,
                   "atomicColourBatch": False, "physicalSlot": 5}
        for field, value in changes.items():
            bad = copy.deepcopy(original); bad["measurements"][-1]["source"][field] = value
            self.assertFalse(assess.fresh_groups({"apiVersion": 3, "measurementBatches": [bad]}, 7, 0, 90), field)
        for field in original.keys() - {"measurements"}:
            bad = copy.deepcopy(original); bad.pop(field)
            self.assertFalse(assess.fresh_groups({"apiVersion": 3, "measurementBatches": [bad]}, 7, 0, 90), field)
        for bad in (self.batch(2, 100, (0, 0, 1)), self.batch(3, 100, (0, 1, 2)),
                    self.batch(4, 100, (0, 4))):
            self.assertFalse(assess.fresh_groups({"apiVersion": 3, "measurementBatches": [bad]}, 7, 0, 90))
        self.assertFalse(assess.fresh_groups({"apiVersion": 3, "measurementBatches": [original, original]}, 7, 0, 90))
        duplicate = copy.deepcopy(original)
        duplicate["measurements"][2] = copy.deepcopy(duplicate["measurements"][0])
        self.assertFalse(assess.fresh_groups({"apiVersion": 3, "measurementBatches": [duplicate]}, 7, 0, 90))

    def test_v3_preserves_warmup_route_and_wrap_checks(self):
        batch = self.batch(1, 2, (2, 3, 6))
        status = {"apiVersion": 3, "measurementBatches": [batch]}
        self.assertEqual(len(assess.fresh_groups(status, 7, 0, 0xfffffffc, 4)), 1)
        self.assertFalse(assess.fresh_groups(status, 7, 0, 0xfffffffc, 6))
        self.assertFalse(assess.fresh_groups(status, 8, 0, 0xfffffffc))
        self.assertFalse(assess.fresh_groups(status, 7, 1, 0xfffffffc))
        self.assertFalse(assess.fresh_groups(status, 7, 0, 0xfffffffc, expected_slots={0, 1}))

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
        self.assertIn("CSSetShaderResources(0, 5", pipeline)
        self.assertIn("work.exposure.srv.Get()", pipeline)
        self.assertNotIn("captureEpoch != epoch", capture)  # Never split an active eye pair on an API toggle.
        for source in (pipeline, capture):
            self.assertIn("D3D11_ASYNC_GETDATA_DONOTFLUSH", source)
            self.assertIn("D3D11_MAP_FLAG_DO_NOT_WAIT", source)
            self.assertNotIn("->Flush(", source)
        ui = (ROOT / "src/Features/NeuralRenderingFeature.cpp").read_text()
        descriptor = json.loads(ui.split('R"schema(', 1)[1].split(')schema"', 1)[0])
        self.assertIn("captureEngineExposure", descriptor["inputSchema"]["properties"]["experiments"]["properties"])
        self.assertIn("void NeuralRenderingFeature::EarlyPrepass()", ui)
        self.assertNotIn("NRColor.ini", ui)

    def test_revisions_are_not_overwritten_after_a_conflict(self):
        class Changed:
            def call(self, *_, **__): return {"revision": 99, "slots": [], "measurements": []}
        class Args:
            timeout = 1; warmup_frames = 0; sample_frames = 1
        with self.assertRaises(assess.MutationUncertain):
            assess.collect(Changed(), 7, 0, 0, Args())


if __name__ == "__main__":
    unittest.main()
