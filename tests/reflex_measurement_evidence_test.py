import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "reflex-measurement"))
from evidence import EvidenceError, functional_comparisons, observer_comparisons, statistics, summarize, validate  # noqa: E402
from runner import Campaign, payload, plan  # noqa: E402
from types import SimpleNamespace


def fixture(count=2):
    records = []
    for ordinal in range(count):
        records.append({"stream": "application", "ordinal": ordinal, "data": {
            "sequence": ordinal + 1, "completed": True, "completionReason": None,
            "enterQpc": 10 + ordinal * 10, "exitQpc": 11 + ordinal * 10, "frame": ordinal,
            "thread": 1, "exitReason": "updated", "updateMs": 1, "updateReason": None,
            "backend": None, "backendReason": "minimal observer",
            "intervalMs": None if ordinal == 0 else 10,
            "intervalReason": "first record" if ordinal == 0 else None}})
    common = {"captureId": 1, "schema": 1, "total": count,
              "counts": {"application": count, "native": 0, "compositor": 0, "readback": 0},
              "environment": {"producer": {"buildId": "test"}},
              "boundaries": {"applicationAdmissionStartQpc": 1, "applicationAdmissionStopQpc": 10 * count + 1,
                             "freezeQpc": 10 * count + 2, "qpcFrequency": 1000,
                             "clockRelationship": "separate clocks"},
              "condition": {"probe": True, "backend": {"generation": 1}, "requested": {}, "acceptedCondition": True,
                            "lastSettingsSubmission": None, "frameGenerationConstraint": False, "qpc": 1, "gameHour": 12},
              "quality": {key: 0 for key in ("overflow", "contention", "unfinished", "appGaps", "appDuplicates", "invalidClocks",
                         "invalidFrames", "sourceLost", "resets", "invalidCompositor", "sourceFailures", "missingCompletions")},
              "coverage": {name: {"count": count if name == "application" else 0} for name in ("application", "native", "compositor", "readback")},
              "recording": {"mode": "A"}, "preferencesChanged": False, "controlCallsDuringCapture": 0, "timingDefinitions": {},
              "termination": "completed", "workerError": "",
              "scope": {"latency": "UNSUPPORTED", "pre12Restoration": "NOT_MEASURED"}}
    common["quality"].update(complete=True, missingTail=False, postrollTimedOut=False)
    pages = []
    for offset in range(0, max(count, 1), 256):
        page = copy.deepcopy(common)
        page.update(offset=offset, records=records[offset:offset + 256], nextOffset=offset + 256 if offset + 256 < count else None)
        pages.append(page)
    return pages


class EvidenceTests(unittest.TestCase):
    def test_full_pagination_and_stalls(self):
        pages = fixture(600)
        self.assertTrue(validate(pages, "A", "test")["complete"])
        self.assertEqual(statistics([10, 12, 5000, None])["max"], 5000)
        self.assertEqual(statistics([10, 12, 5000])["mean"], 1674)

    def test_incomplete_overlap_and_changed_export(self):
        for mutation in (lambda p: p.pop(), lambda p: p[1].update(offset=255),
                         lambda p: p[1]["counts"].update(native=1), lambda p: p[1].update(captureId=2)):
            pages = fixture(600)
            mutation(pages)
            with self.assertRaises(EvidenceError):
                validate(pages, "A", "test")

    def test_unsupported_conclusions_rejected(self):
        pages = fixture()
        pages[0]["recording"]["mode"] = "B"
        result = validate(pages, "B", "test")
        self.assertFalse(result["complete"])
        self.assertEqual(result["pacingVerdict"], "NOT_ASSESSED")
        self.assertEqual(result["vrSmoothnessVerdict"], "NOT_ASSESSED")
        for scope, verdict in (("latency", "LOWER"), ("pre12Restoration", "RESTORED")):
            changed = fixture()
            changed[0]["scope"][scope] = verdict
            with self.assertRaises(EvidenceError):
                validate(changed, "A", "test")

    def test_clock_duplicate_missing_and_quality(self):
        pages = fixture()
        pages[0]["records"][1]["data"]["frame"] = 0
        self.assertFalse(validate(pages, "A", "test")["complete"])
        pages = fixture()
        pages[0]["records"][0]["data"]["updateMs"] = None
        with self.assertRaises(EvidenceError):
            validate(pages, "A", "test")
        for field in ("termination", "workerError"):
            pages = fixture()
            pages[0][field] = "cancelled"
            self.assertFalse(validate(pages, "A", "test")["complete"])
        pages = fixture()
        pages[0]["quality"]["complete"] = False
        pages[0]["quality"]["sourceLost"] = 1
        self.assertFalse(validate(pages, "A", "test")["complete"])

    def test_schema_build_and_missing_fields(self):
        for field, value in (("schema", 9), ("total", 7), ("counts", {})):
            pages = fixture()
            pages[0][field] = value
            with self.assertRaises(EvidenceError):
                validate(pages, "A", "test")
        with self.assertRaises(EvidenceError):
            validate(fixture(), "A", "wrong")

    def test_matrix_caps_modes_balanced_order(self):
        config = json.loads((ROOT / "tools/reflex-measurement/pacing.json").read_text())
        phases = plan(config)
        self.assertEqual(config["capsFps"], [30, 45])
        comparison = [p for p in phases if p["kind"] == "comparison"]
        self.assertEqual(len(comparison), 18)
        self.assertEqual([p["reflexMode"] for p in comparison[:3]], ["off", "on", "boost"])
        self.assertEqual([p["reflexMode"] for p in comparison[9:12]], ["boost", "on", "off"])
        self.assertEqual({p["mode"] for p in phases if p["kind"] == "observer"}, {"A", "B", "C"})
        config["settleSeconds"] = 4
        with self.assertRaises(EvidenceError):
            plan(config)

    def test_failed_transport_and_typed_payload(self):
        for result in ({"ok": False}, {"ok": True, "transportOk": False}):
            with self.assertRaises(EvidenceError):
                payload(result)
        self.assertEqual(payload({"ok": True, "transportOk": True, "data": {"content": [{"ok": True, "result": {"x": 1}}]}}), {"x": 1})

    def test_cancelled_before_admission_is_exportable_failure(self):
        pages = fixture(0)
        pages[0]["boundaries"]["applicationAdmissionStartQpc"] = None
        pages[0]["termination"] = "cancelled_before_admission"
        pages[0]["quality"].update(complete=False, invalidClocks=1, missingTail=True, postrollTimedOut=True)
        pages[0]["recording"]["mode"] = "B"
        result = validate(pages, "B", "test")
        self.assertTrue(result["schemaValid"] and result["paginationComplete"])
        self.assertFalse(result["complete"])

    def test_quality_cannot_hide_source_loss_in_empty_destination(self):
        pages = fixture()
        pages[0]["quality"]["sourceLost"] = 3
        with self.assertRaises(EvidenceError):
            validate(pages, "A", "test")
        pages[0]["quality"]["complete"] = False
        self.assertFalse(validate(pages, "A", "test")["complete"])

    def test_observed_user_changes_and_control_work_limit_verdict(self):
        for field, value in (("preferencesChanged", True), ("controlCallsDuringCapture", 1)):
            pages = fixture()
            pages[0][field] = value
            self.assertFalse(validate(pages, "A", "test")["complete"])

    def test_unfinished_entries_never_fabricate_success(self):
        pages = fixture()
        row = pages[0]["records"][1]["data"]
        row.update(completed=False, completionReason="completion not recorded", exitQpc=None,
                   exitReason=None, updateMs=None, updateReason="completion missing")
        pages[0]["quality"].update(complete=False, unfinished=1, missingCompletions=1)
        self.assertFalse(validate(pages, "A", "test")["complete"])
        row["updateMs"] = 0
        with self.assertRaises(EvidenceError):
            validate(pages, "A", "test")
        pages = fixture()
        pages[0]["recording"]["mode"] = "B"
        native = {"sequence": 3, "applicationSequence": 1, "completed": False, "completionReason": "not recorded",
                  "admittedQpc": 10, "admissionSource": "application_frame", "operationEnterQpc": 10,
                  "enterQpc": None, "exitQpc": None, "generation": 1, "device": 123, "frame": 0,
                  "operation": "settings", "result": None, "submitted": None, "preparedArguments": {"minimumIntervalUs": 50000},
                  "durationMs": None, "durationReason": "completion missing"}
        pages[0]["records"].append({"stream": "native", "ordinal": 0, "data": native})
        pages[0]["counts"]["native"] = pages[0]["coverage"]["native"]["count"] = 1
        pages[0]["total"] += 1
        pages[0]["quality"].update(complete=False, unfinished=1, missingCompletions=1)
        result = validate(pages, "B", "test")
        self.assertFalse(result["complete"])
        self.assertNotIn("native operation failure", result["limitations"])
        native["result"] = 0
        with self.assertRaises(EvidenceError):
            validate(pages, "B", "test")

    def test_interval_integrity_and_boundary_gaps(self):
        pages = fixture()
        pages[0]["records"][1]["data"]["intervalMs"] = None
        with self.assertRaises(EvidenceError):
            validate(pages, "A", "test")

        pages = fixture()
        pages[0]["boundaries"]["applicationAdmissionStopQpc"] = 5000
        pages[0]["boundaries"]["freezeQpc"] = 5010
        self.assertFalse(validate(pages, "A", "test")["complete"])
        self.assertEqual(summarize(pages)["boundaryGaps"]["lastEntryToAdmissionCloseMs"], 4980)
        pages[0]["records"][1]["data"]["sequence"] = 1
        with self.assertRaises(EvidenceError):
            validate(pages, "A", "test")

    def test_getter_success_failure_unsupported_and_pending(self):
        for result in (0, -1, -104, None):
            with self.subTest(result=result):
                pages = fixture()
                pages[0]["recording"]["mode"] = "C"
                completed = result is not None
                data = {"sequence": 3, "completed": completed, "completionReason": None if completed else "not recorded",
                        "result": result, "version": 65552, "size": 16, "generation": 1,
                        "requestedQpc": 12, "admittedQpc": 12, "enterQpc": 12 if completed else None,
                        "exitQpc": 13 if completed else None, "fields": {"bLowLatencyMode": 1} if result == 0 else None,
                        "meaning": "corroboration only", "unavailableReason": None if result == 0 else "unavailable"}
                pages[0]["records"].append({"stream": "readback", "ordinal": 0, "data": data})
                pages[0]["counts"]["readback"] = pages[0]["coverage"]["readback"]["count"] = 1
                pages[0]["total"] += 1
                if not completed:
                    pages[0]["quality"].update(complete=False, unfinished=1, missingCompletions=1)
                report = validate(pages, "C", "test")
                self.assertEqual("native readback unavailable or invalid" in report["limitations"], result != 0)
                if result != 0:
                    data["fields"] = {"bLowLatencyMode": 0}
                    with self.assertRaises(EvidenceError):
                        validate(pages, "C", "test")

    def test_functional_and_observer_reports_keep_scope(self):
        def phase(kind, mode, cap, interval, observer="B"):
            stats = statistics([interval] * 10)
            return {"condition": {"kind": kind, "reflexMode": mode, "capFps": cap, "mode": observer, "repeat": 0},
                    "validation": {"complete": True}, "statistics": {"appIntervalMs": stats, "wholeUpdateMs": stats,
                        "compositorClientMs": stats, "queryMs": None, "nativeCallMs": {"sleep": stats}}}
        rows = [phase("functional", "on", 0, 10), phase("functional", "on", 20, 50)]
        self.assertEqual(functional_comparisons(rows)[0]["status"], "CAP_RESPONSE_OBSERVED")
        rows[1]["statistics"]["appIntervalMs"] = statistics([10] * 10)
        self.assertEqual(functional_comparisons(rows)[0]["status"], "NO_EXPECTED_CAP_RESPONSE")
        rows[0]["statistics"]["appIntervalMs"] = statistics([60] * 10)
        self.assertEqual(functional_comparisons(rows)[0]["status"], "NOT_TESTABLE")
        rows = [phase("observer", "on", 0, 10, "A"), phase("observer", "on", 0, 12, "B")]
        costs = observer_comparisons(rows)
        self.assertEqual(costs[0]["materiality"], "POTENTIAL_OBSERVER_IMPACT")
        self.assertFalse(costs[0]["observerCostsSubtracted"])
        self.assertIsNone(costs[0]["displayFrameBudgetMs"])
        self.assertEqual(costs[1]["status"], "UNAVAILABLE")


class InterruptedCampaign(Campaign):
    def __init__(self, output, stage, conflict=False):
        config = json.loads((ROOT / "tools/reflex-measurement/pacing.json").read_text())
        super().__init__(SimpleNamespace(output=output, build_id="test"), config)
        self.stage, self.conflict, self.interrupted = stage, conflict, False
        self.calls = []

    def controller(self, command, label, arguments=None):
        return {"ok": True, "data": {"tools": [{"name": "communityshaders.reflex_measurement",
                "inputSchema": {"properties": {"action": {"enum": ["recover_and_stop"]}}}}]}}

    def call(self, action, **arguments):
        self.calls.append(action)
        trigger = {"settling": "set_noon", "capture": "stop", "export": "export"}[self.stage]
        if not self.interrupted and action == trigger and (self.stage != "export" or arguments["offset"] > 0):
            self.interrupted = True
            raise KeyboardInterrupt(self.stage)
        if action == "prepare":
            return {"sessionId": 1}
        if action == "configure" or action == "set_noon" or action == "acknowledge_export":
            return {}
        if action == "condition":
            return {"acceptedCondition": True, "frameGenerationConstraint": False, "gameHour": 12}
        if action == "start":
            return {"captureId": 1, "admissionDelayMs": 2500, "condition": {}}
        if action == "stop" or action == "recover_and_stop":
            return {"sessionId": 1, "captureId": 1, "termination": "cancelled", "frozen": True}
        if action == "export":
            return fixture(600)[arguments["offset"] // 256]
        if action == "restore" or action == "restoration":
            return {"restoration": {"requestedRestored": not self.conflict, "acceptedRestored": not self.conflict, "conflict": self.conflict},
                    "remaining": {"requested": {"fps": 45}, "backend": {"accepted": {"mode": 1}}}}
        raise AssertionError(action)


class CancellationTests(unittest.TestCase):
    def test_optional_readback_failure_keeps_passive_campaign_and_exit_status(self):
        parent = ROOT / "build" / "reflex-offline-tests"
        parent.mkdir(parents=True, exist_ok=True)
        for status, expected_exit in (("UNSUPPORTED", 0), ("FAILED", 1)):
            with self.subTest(status=status), tempfile.TemporaryDirectory(dir=parent) as temporary:
                campaign = InterruptedCampaign(str(Path(temporary) / "run"), "settling")
                campaign.config["modes"] = ["boost"]

                def call(action, **arguments):
                    if action == "prepare":
                        return {"sessionId": 1}
                    if action == "readiness":
                        names = ("build", "device", "captureGeneration", "nativeFunctions", "nativeExecution", "application",
                                 "compositor", "captureQuality", "capacity", "export", "restoration")
                        items = {name: {"status": "READY"} for name in names}
                        items["readback"] = {"status": status, "reason": "synthetic getter result"}
                        return {"items": items}
                    raise AssertionError(action)

                def phase(condition):
                    stats = statistics([10] * 10)
                    summary = {"condition": condition, "validation": {"complete": condition["kind"] != "optional_readiness", "paginationComplete": True},
                               "statistics": {"appIntervalMs": stats, "wholeUpdateMs": stats, "compositorClientMs": stats,
                                              "nativeCallMs": {"sleep": stats}, "queryMs": None}}
                    campaign.completed.append(summary)
                    return summary

                with patch.object(campaign, "call", side_effect=call), patch.object(campaign, "phase", side_effect=phase), patch.object(campaign, "restore"):
                    self.assertEqual(campaign.run(), expected_exit)
                comparisons = [p for p in campaign.completed if p["condition"]["kind"] == "comparison"]
                self.assertEqual(len(comparisons), 6)
                self.assertTrue(all(p["condition"]["reflexMode"] == "boost" for p in comparisons))
                self.assertFalse(any(p["condition"]["kind"] == "observer" and p["condition"]["mode"] == "C" for p in campaign.completed))

    def test_settling_capture_export_always_restore_and_preserve_partial(self):
        parent = ROOT / "build" / "reflex-offline-tests"
        parent.mkdir(parents=True, exist_ok=True)
        for stage in ("settling", "capture", "export"):
            with self.subTest(stage=stage), tempfile.TemporaryDirectory(dir=parent) as temporary:
                campaign = InterruptedCampaign(str(Path(temporary) / "run"), stage)
                with patch("runner.time.sleep", lambda _: None), patch("runner.qpc_now", return_value=0):
                    self.assertEqual(campaign.run(), 1)
                self.assertIn("recover_and_stop", campaign.calls)
                self.assertIn("restore", campaign.calls)
                self.assertTrue(campaign.cleanup["confirmation"]["restoration"]["acceptedRestored"])
                self.assertTrue((campaign.root / "campaign.json").is_file())
                if stage == "export":
                    self.assertTrue((campaign.root / "readiness-page-00000000.json").is_file())
                    self.assertTrue((campaign.root / "interrupted-page-00000000.json").is_file())

    def test_conflicting_restoration_is_prominent_failure(self):
        parent = ROOT / "build" / "reflex-offline-tests"
        parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=parent) as temporary:
            campaign = InterruptedCampaign(str(Path(temporary) / "run"), "settling", conflict=True)
            with patch("runner.time.sleep", lambda _: None), patch("runner.qpc_now", return_value=0), patch("runner.sys.stderr") as stderr:
                self.assertEqual(campaign.run(), 1)
                self.assertTrue(stderr.write.called)
            self.assertIn("restorationFailure", campaign.cleanup)
            self.assertFalse(campaign.cleanup["confirmation"]["restoration"]["requestedRestored"])


if __name__ == "__main__":
    unittest.main()
