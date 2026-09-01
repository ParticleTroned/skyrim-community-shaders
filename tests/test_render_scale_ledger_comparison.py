"""Regression tests for retained timing coverage in the canonical ledger."""

import csv
import importlib.util
import json
import os
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location(
    "comparison", Path(__file__).resolve().parents[1] / "tools/compare-render-scale-ledger.py"
)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class LedgerComparisonTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="csx-ledger-comparison-")
        self.addCleanup(self.temp.cleanup)
        self.ledger = Path(self.temp.name) / "ledger.csv"
        self.comparison = {side: {"runId": side + "-run", "rows": [
            {"lane": "nvidia", "pass": 1, "ordinal": 1,
             "switchTimings": {"strictMs": 123.25 if side == "baseline" else 100.5}}
        ]} for side in ("baseline", "candidate")}
        self.write("123.25", "100.5")

    def write(self, baseline, candidate):
        with self.ledger.open("w", newline="", encoding="utf-8") as stream:
            csv.writer(stream).writerows([
                ["metric", "baseline-run build B", "candidate-run build C", "historic"],
                ["tuning_pass1_transition_01_taa_to_none_strict_ms", baseline, candidate, "retained"],
            ])

    def test_all_measured_values_verified_without_mutation(self):
        before = self.ledger.read_bytes()
        result = MODULE.audit_ledger(self.ledger, self.comparison)
        self.assertEqual(result["verifiedNumericCells"], 2)
        self.assertEqual(len(result["unavailable"]), 14)
        self.assertEqual(before, self.ledger.read_bytes())

    def test_cross_partition_comparison_keeps_all_cells_unchanged(self):
        table = MODULE.read_ledger(self.ledger)
        archive = self.ledger.with_name('archive.csv')
        for path, columns in ((self.ledger, (0, 2)), (archive, (0, 1, 3))):
            with path.open('w', newline='', encoding='utf-8') as stream:
                csv.writer(stream).writerows([[row[i] for i in columns] for row in table])
        original = [path.read_bytes() for path in (self.ledger, archive)]
        result = MODULE.audit_ledger(self.ledger, self.comparison, [archive])
        self.assertEqual(result['verifiedNumericCells'], 2)
        self.assertEqual(original, [path.read_bytes() for path in (self.ledger, archive)])

    def test_ambiguous_run_across_partitions_is_rejected(self):
        archive = self.ledger.with_name('archive.csv')
        archive.write_bytes(self.ledger.read_bytes())
        with self.assertRaisesRegex(ValueError, 'duplicate run columns'):
            MODULE.audit_ledger(self.ledger, self.comparison, [archive])

    def test_latest_version_selection_rejects_duplicate_numbers(self):
        root = self.ledger.parent
        for name in ('vr-render-scale-ledger-0009-pr73.csv', 'vr-render-scale-ledger-0010-pr74.csv'):
            (root / name).write_bytes(self.ledger.read_bytes())
        self.assertEqual(MODULE.latest_ledger(root).name, 'vr-render-scale-ledger-0010-pr74.csv')
        (root / 'vr-render-scale-ledger-0010-pr75.csv').write_bytes(self.ledger.read_bytes())
        with self.assertRaisesRegex(ValueError, 'duplicate ledger version'):
            MODULE.latest_ledger(root)

    def test_large_structured_detail_is_lossless_and_restores_csv_limit(self):
        detail = json.dumps({'trace': '\u03bb,"\n' * 40000, 'values': [False, 0, None, [], {}]}, ensure_ascii=False)
        with self.ledger.open('a', encoding='utf-8', newline='') as stream:
            csv.writer(stream).writerow(['tuning_detail_summary_json', detail, detail, 'retained'])
        before = self.ledger.read_bytes()
        previous_limit = csv.field_size_limit(131072)
        self.addCleanup(csv.field_size_limit, previous_limit)
        self.assertEqual(MODULE.read_ledger(self.ledger)[-1][1], detail)
        self.assertEqual(MODULE.audit_ledger(self.ledger, self.comparison)['verifiedNumericCells'], 2)
        self.assertEqual(csv.field_size_limit(), 131072)
        self.assertEqual(self.ledger.read_bytes(), before)

    def test_csv_limit_restored_after_parser_failure(self):
        previous_limit = csv.field_size_limit(64)
        self.addCleanup(csv.field_size_limit, previous_limit)
        with patch.object(MODULE.csv, 'reader', side_effect=csv.Error('invalid CSV')):
            with self.assertRaisesRegex(csv.Error, 'invalid CSV'):
                MODULE.read_ledger(self.ledger)
        self.assertEqual(csv.field_size_limit(), 64)

    def test_single_lane_and_partial_runs_ignore_other_lane_history(self):
        for lane in ("nvidia", "explicit_fsr3", "explicit_fsr4", "fsr4_to_fsr3_fallback"):
            with self.subTest(lane=lane):
                for run in self.comparison.values():
                    run["rows"][0]["lane"] = lane
                with self.ledger.open("w", newline="", encoding="utf-8") as stream:
                    csv.writer(stream).writerows([
                        ["metric", "baseline-run", "candidate-run", "historic"],
                        *[[f"tuning_{name}_pass1_transition_01_taa_to_none_strict_ms",
                           "123.25" if name == lane else "", "100.5" if name == lane else "", "9"]
                          for name in ("nvidia", "explicit_fsr3", "explicit_fsr4", "fsr4_to_fsr3_fallback")]])
                before = self.ledger.read_bytes()
                self.assertEqual(MODULE.audit_ledger(self.ledger, self.comparison)["verifiedNumericCells"], 2)
                self.assertEqual(before, self.ledger.read_bytes())

    def test_missing_measurement_is_not_zero(self):
        self.comparison["candidate"]["rows"][0]["switchTimings"]["strictMs"] = None
        self.assertEqual(MODULE.audit_ledger(self.ledger, self.comparison)["verifiedNumericCells"], 1)

    def test_missing_or_incorrect_ledger_value_rejected(self):
        for value in ("n/a", "0", "100.6"):
            with self.subTest(value=value):
                self.write("123.25", value)
                with self.assertRaises(ValueError):
                    MODULE.audit_ledger(self.ledger, self.comparison)

    def test_missing_column_rejected(self):
        self.comparison["candidate"]["runId"] = "not-in-ledger"
        with self.assertRaisesRegex(ValueError, "one canonical ledger column"):
            MODULE.audit_ledger(self.ledger, self.comparison)

    def test_ambiguous_timing_rows_rejected(self):
        with self.ledger.open('a', encoding='utf-8', newline='') as stream:
            csv.writer(stream).writerow([
                'different_pass1_transition_01_taa_to_none_strict_ms', '123.25', '100.5', 'retained'])
        with self.assertRaisesRegex(ValueError, 'ambiguous ledger metric'):
            MODULE.audit_ledger(self.ledger, self.comparison)


class ReportingPipelineTest(unittest.TestCase):
    write = LedgerComparisonTest.write

    def setUp(self):
        LedgerComparisonTest.setUp(self)
        root = Path(self.temp.name)
        self.args = SimpleNamespace(
            toolkit_root=root / 'toolkit', node=root / 'node.exe',
            baseline_root=root / 'baseline', candidate_root=root / 'candidate',
            output_root=root / 'output', provenance_path=None, policy_path=None,
            finalize_candidate=None, ledger_candidate=None, expected_ledger_sha256=None)
        self.args.node.write_bytes(b'node test identity')
        script = self.args.toolkit_root / 'tools/renderscale-tuning-finalizer/comparison.js'
        script.parent.mkdir(parents=True)
        script.write_text('fixture', encoding='utf-8')
        for run in (self.args.baseline_root, self.args.candidate_root):
            (run / 'raw').mkdir(parents=True)
            (run / 'raw/receipt.json').write_text('{"count":1}', encoding='utf-8')
        self.comparison['changeAssessment'] = {'status': 'INCONCLUSIVE', 'changesTestResult': False}
        self.invocations = []

    def invoke(self, command):
        self.invocations.append(command)
        if command[1].endswith('finalizer.js'):
            for name in ('summary.json', 'transitions.csv', 'report.md', 'receipt-index.json', 'evidence-values.csv'):
                target = self.args.candidate_root / name
                target.write_text(json.dumps({'reporting': {'status': 'INCOMPLETE', 'reasons': ['retry_telemetry_incomplete']},
                                              'assayExecution': {'status': 'COMPLETE'}}), encoding='utf-8')
        else:
            for extension in ('json', 'md', 'csv'):
                (self.args.output_root / ('comparison.' + extension)).write_text(json.dumps(self.comparison), encoding='utf-8')

    def run_pipeline(self):
        with patch.object(MODULE, 'invoke', side_effect=self.invoke):
            return MODULE.complete_reporting(self.args, self.ledger)

    def test_numbered_snapshot_cannot_be_overwritten(self):
        self.ledger = self.ledger.with_name('vr-render-scale-ledger-0003-pr73.csv')
        self.write('123.25', '100.5')
        before = self.ledger.read_bytes()
        self.args.ledger_candidate = self.ledger.with_name('prepared.csv')
        with self.assertRaisesRegex(ValueError, 'numbered ledgers are immutable'):
            self.run_pipeline()
        self.assertEqual(self.ledger.read_bytes(), before)
        self.assertEqual(self.invocations, [])

    def test_reuse_preserves_outputs_but_still_rejects_bad_ledger(self):
        self.assertFalse(self.run_pipeline()['comparisonReused'])
        report = self.args.output_root / 'comparison.md'
        previous = report.stat().st_mtime_ns
        self.assertTrue(self.run_pipeline()['comparisonReused'])
        self.assertEqual(report.stat().st_mtime_ns, previous)
        self.assertEqual(len(self.invocations), 1)
        self.write('123.25', '0')
        with self.assertRaisesRegex(ValueError, 'disagrees'):
            self.run_pipeline()
        self.assertEqual(len(self.invocations), 1)
        validation = json.loads((self.args.output_root / 'ledger-validation.json').read_text())
        self.assertEqual(validation['status'], 'INCOMPLETE')

    def test_same_size_same_timestamp_input_change_forces_regeneration(self):
        self.run_pipeline()
        receipt = self.args.candidate_root / 'raw/receipt.json'
        previous = receipt.stat()
        receipt.write_text('{"count":2}', encoding='utf-8')
        os.utime(receipt, ns=(previous.st_atime_ns, previous.st_mtime_ns))
        self.assertFalse(self.run_pipeline()['comparisonReused'])
        self.assertEqual(len(self.invocations), 2)

    def test_corrupt_output_and_changed_dependency_force_regeneration(self):
        self.run_pipeline()
        (self.args.output_root / 'comparison.md').write_text('corrupt', encoding='utf-8')
        self.assertFalse(self.run_pipeline()['comparisonReused'])
        dependency = self.args.toolkit_root / 'tools/helper.js'
        dependency.write_text('new dependency', encoding='utf-8')
        self.assertFalse(self.run_pipeline()['comparisonReused'])
        self.assertEqual(len(self.invocations), 3)

    def test_matrix_or_option_role_changes_invalidate_cache(self):
        self.args.provenance_path = Path(self.temp.name) / 'provenance.json'
        self.args.policy_path = Path(self.temp.name) / 'policy.json'
        for path in (self.args.provenance_path, self.args.policy_path):
            path.write_text('{}', encoding='utf-8')
        self.run_pipeline()
        self.args.provenance_path, self.args.policy_path = self.args.policy_path, self.args.provenance_path
        self.assertFalse(self.run_pipeline()['comparisonReused'])
        matrix = self.args.toolkit_root / 'skills/renderscale-tuning-nvidia/references/matrix.v1.json'
        matrix.parent.mkdir(parents=True)
        matrix.write_text('{}', encoding='utf-8')
        self.assertFalse(self.run_pipeline()['comparisonReused'])

    def test_evidence_change_during_generation_is_not_cached(self):
        def mutate(command):
            self.invoke(command)
            (self.args.candidate_root / 'raw/receipt.json').write_text('{"count":2}', encoding='utf-8')
        with patch.object(MODULE, 'invoke', side_effect=mutate):
            with self.assertRaisesRegex(ValueError, 'changed during comparison'):
                MODULE.complete_reporting(self.args, self.ledger)
        self.assertFalse((self.args.output_root / 'comparison-cache.json').exists())

    def test_prepared_update_preserves_history_and_rejects_stale_hash(self):
        old = self.ledger.read_bytes()
        self.args.ledger_candidate = Path(self.temp.name) / 'prepared.csv'
        with self.ledger.open(newline='', encoding='utf-8') as stream:
            table = list(csv.reader(stream))
        with self.args.ledger_candidate.open('w', newline='', encoding='utf-8') as stream:
            csv.writer(stream).writerows([row + ['new-run' if i == 0 else 'unavailable'] for i, row in enumerate(table)])
        self.args.expected_ledger_sha256 = MODULE.sha256(self.ledger)
        result = self.run_pipeline()
        self.assertFalse(result['ledgerUnchanged'])
        self.assertEqual((self.args.output_root / 'ledger-before.csv').read_bytes(), old)
        published = self.ledger.read_bytes()
        with self.assertRaisesRegex(ValueError, 'changed since'):
            self.run_pipeline()
        self.assertEqual(self.ledger.read_bytes(), published)

    def test_historical_edit_never_published(self):
        before = self.ledger.read_bytes()
        self.args.ledger_candidate = Path(self.temp.name) / 'prepared.csv'
        self.args.ledger_candidate.write_bytes(before.replace(b'retained', b'changed'))
        self.args.expected_ledger_sha256 = MODULE.sha256(self.ledger)
        with self.assertRaisesRegex(ValueError, 'historical cells'):
            self.run_pipeline()
        self.assertEqual(self.ledger.read_bytes(), before)

    def test_large_detail_update_preserves_history_and_rejects_changed_detail(self):
        detail = json.dumps({'trace': 'retained evidence ' * 12000, 'values': [False, 0, None, [], {}]})
        with self.ledger.open('a', encoding='utf-8', newline='') as stream:
            csv.writer(stream).writerow(['tuning_detail_summary_json', detail, detail, 'retained'])
        before = self.ledger.read_bytes()
        previous_limit = csv.field_size_limit(131072)
        self.addCleanup(csv.field_size_limit, previous_limit)
        table = MODULE.read_ledger(self.ledger)
        self.args.ledger_candidate = Path(self.temp.name) / 'prepared.csv'
        prepared = [row + ['new-run' if i == 0 else detail] for i, row in enumerate(table)]
        with self.args.ledger_candidate.open('w', encoding='utf-8', newline='') as stream:
            csv.writer(stream).writerows(prepared)
        self.args.expected_ledger_sha256 = MODULE.sha256(self.ledger)
        self.assertFalse(self.run_pipeline()['ledgerUnchanged'])
        self.assertEqual((self.args.output_root / 'ledger-before.csv').read_bytes(), before)
        self.assertEqual(MODULE.read_ledger(self.ledger), prepared)
        self.assertEqual(csv.field_size_limit(), 131072)
        published = self.ledger.read_bytes()
        prepared[-1][1] += 'changed'
        with self.args.ledger_candidate.open('w', encoding='utf-8', newline='') as stream:
            csv.writer(stream).writerows(prepared)
        self.args.expected_ledger_sha256 = MODULE.sha256(self.ledger)
        with self.assertRaisesRegex(ValueError, 'historical cells'):
            self.run_pipeline()
        self.assertEqual(self.ledger.read_bytes(), published)
        self.assertEqual(csv.field_size_limit(), 131072)

    def test_tool_failure_invalidates_previous_complete_receipt(self):
        self.run_pipeline()
        self.args.node.write_bytes(b'changed node identity')
        with patch.object(MODULE, 'invoke', side_effect=ValueError('generation failure')):
            with self.assertRaisesRegex(ValueError, 'generation failure'):
                MODULE.complete_reporting(self.args, self.ledger)
        validation = json.loads((self.args.output_root / 'ledger-validation.json').read_text())
        self.assertEqual(validation['status'], 'INCOMPLETE')
        self.assertFalse((self.args.output_root / '.reporting.lock').exists())

    def request_finalization(self):
        self.args.finalize_candidate = Path(self.temp.name) / 'request.json'
        self.args.finalize_candidate.write_text(json.dumps(
            {'variant': 'nvidia', 'runId': 'candidate-run', 'buildId': 'build', 'expectedRows': 1}), encoding='utf-8')

    def test_finalization_reuse_preserves_missing_retry_limitations(self):
        self.request_finalization()
        first = self.run_pipeline()
        self.assertFalse(first['finalization']['reused'])
        second = self.run_pipeline()
        self.assertTrue(second['finalization']['reused'])
        self.assertEqual(second['finalization']['reporting']['status'], 'INCOMPLETE')
        self.assertEqual(len(self.invocations), 2)
        (self.args.candidate_root / 'evidence-values.csv').write_text('corrupt', encoding='utf-8')
        self.assertFalse(self.run_pipeline()['finalization']['reused'])

    def test_unflushed_worker_never_finalized(self):
        self.request_finalization()
        (self.args.candidate_root / 'worker-status.json').write_text(json.dumps(
            {'runId': 'candidate-run', 'buildId': 'build', 'state': 'RUNNING',
             'cleanupVerified': True, 'evidencePending': 1}), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'cleanup or evidence flush'):
            self.run_pipeline()
        self.assertEqual(self.invocations, [])

    def test_late_worker_diagnostic_does_not_block_complete_evidence(self):
        self.request_finalization()
        worker_path = self.args.candidate_root / 'worker-status.json'
        worker = {'runId': 'candidate-run', 'buildId': 'build', 'state': 'COMPLETE',
                  'cleanupVerified': True, 'evidencePending': 0}
        worker_path.write_text(json.dumps(worker), encoding='utf-8')
        def finish(command):
            self.invoke(command)
            worker['transportCleanupError'] = 'retained helper shutdown diagnostic'
            worker_path.write_text(json.dumps(worker), encoding='utf-8')
        with patch.object(MODULE, 'invoke', side_effect=finish):
            result = MODULE.complete_reporting(self.args, self.ledger)
        self.assertTrue(result['ok'])
        self.assertEqual(result['finalization']['execution'], 'COMPLETE')


if __name__ == "__main__":
    unittest.main()
