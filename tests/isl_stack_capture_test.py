"""Exercise owned tracing, bounded comparisons, failure preservation and offline coverage."""

import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from measurement_controller_transport_test import ROOT, isl


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'tools/isl-diagnostics' / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


stack_evidence = load('stack_evidence')
with patch.dict('sys.modules', {'stack_evidence': stack_evidence}):
    stack_wait = load('stack_wait')
with patch.dict('sys.modules', {'runner': isl, 'stack_wait': stack_wait}):
    stack_runner = load('stack_runner')

READY = {'state': 'recording', 'instance': 'owned', 'startedQpc': 10, 'qpcFrequency': 1000}
CLOSED = READY | {'state': 'stopped', 'stopReason': 'requested', 'errors': [], 'stopRequestedQpc': 100}


class Tracing(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.trace = stack_wait.OwnedTrace(self.root, 'recorder', 'wpr')

    def write(self, name, value):
        self.trace.directory.mkdir(exist_ok=True)
        (self.trace.directory / name).write_text(json.dumps(value))

    def test_plan_has_reversals_and_bounded_engine_windows(self):
        config = json.loads((ROOT / 'tools/isl-diagnostics/core.json').read_text())
        phases = isl.plan(config, 'stacks')
        self.assertEqual([p['islEnabled'] for p in phases], [True, False, True]*2)
        self.assertEqual(sum(p['durationMs'] for p in phases), 60000)
        self.assertTrue(all(p['durationMs'] == 10000 and p['group'] == 'engine' and
                            p['gates']['observationLevel'] == 'engine' for p in phases))
        with self.assertRaisesRegex(ValueError, 'pacing'):
            isl.plan(config | {'pacing': 'off_unlimited'}, 'stacks')

    def test_ready_then_owned_stop_preserves_etl(self):
        def start(*args):
            self.write('trace-ready.json', READY)
        with patch.object(stack_wait, 'control', side_effect=start), contextlib.redirect_stdout(io.StringIO()):
            self.trace.start()
            self.write('trace-result.json', CLOSED)
            etl = self.trace.directory / 'cpu-stack-wait.etl'
            etl.write_bytes(b'preserved trace')
            result = self.trace.finish()
        self.assertTrue(result['filesSaved'])
        self.assertTrue((self.trace.directory / 'stop-trace').exists())
        self.assertEqual(etl.read_bytes(), b'preserved trace')

    def test_incomplete_ready_receipt_stops_before_engine_capture(self):
        for changes in ({'instance': None}, {'startedQpc': 0}, {'qpcFrequency': True}):
            with patch.object(stack_wait, 'control', side_effect=lambda *args: self.write('trace-ready.json', READY | changes)), \
                 contextlib.redirect_stdout(io.StringIO()), self.assertRaisesRegex(RuntimeError, 'readiness'):
                self.trace.start()

    def test_early_worker_stop_is_not_silently_accepted(self):
        self.write('trace-result.json', {'state': 'stopped', 'stopReason': 'deadline', 'errors': []})
        with self.assertRaisesRegex(RuntimeError, 'before the comparisons'):
            self.trace.ensure_active()

    def test_missing_result_is_pending_and_stop_requested(self):
        self.trace.requested = True
        self.trace.directory.mkdir()
        with contextlib.redirect_stdout(io.StringIO()):
            result = self.trace.finish(timeout=0)
        self.assertFalse(result['filesSaved'])
        self.assertEqual(result['state'], 'pending')
        self.assertTrue((self.trace.directory / 'stop-trace').exists())

    def test_wrong_instance_errors_or_empty_etl_cannot_pass(self):
        self.trace.requested = True
        self.trace.ready = READY
        for instance, errors, contents in [('other', [], b'ETL'), ('owned', ['failure'], b'ETL'), ('owned', [], b'')]:
            self.write('trace-result.json', CLOSED | {'errors': errors, 'instance': instance})
            (self.trace.directory / 'cpu-stack-wait.etl').write_bytes(contents)
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertFalse(self.trace.finish()['filesSaved'])

    def test_missing_or_changed_clock_does_not_claim_complete_trace(self):
        self.trace.requested, self.trace.ready = True, READY
        for changes in ({'stopRequestedQpc': None}, {'stopRequestedQpc': 9}, {'startedQpc': 11},
                        {'qpcFrequency': 1001}, {'errors': None}):
            self.write('trace-result.json', CLOSED | changes)
            (self.trace.directory / 'cpu-stack-wait.etl').write_bytes(b'ETL')
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertFalse(self.trace.finish()['filesSaved'])

    def campaign(self):
        args = SimpleNamespace(output=str(self.root / 'campaign'), controller='unused', runtime='unused',
                               disable_neural_rendering=True, compare_neural_rendering=False, plan='stacks',
                               recorder_directory='recorder', wpr_path='wpr')
        return stack_runner.StackCampaign(args, json.loads((ROOT / 'tools/isl-diagnostics/core.json').read_text()))

    def test_campaign_finalizes_trace_even_if_engine_campaign_raises(self):
        campaign = self.campaign()
        with patch.object(isl.Campaign, 'run', side_effect=RuntimeError('engine failure')), \
             patch.object(campaign.trace, 'finish', return_value={'filesSaved': True}) as finish, \
             contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaisesRegex(RuntimeError, 'engine failure'):
                campaign.run()
        finish.assert_called_once()
        self.assertFalse(json.loads((campaign.root / 'stack-campaign.json').read_text())['complete'])

    def test_engine_success_does_not_hide_trace_failure(self):
        campaign = self.campaign()
        with patch.object(isl.Campaign, 'run', return_value=0), \
             patch.object(campaign.trace, 'finish', return_value={'filesSaved': False, 'state': 'pending'}), \
             contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(campaign.run(), 1)
        self.assertNotIn('ALL FILES SAVED:', output.getvalue())

    def test_stop_request_failure_does_not_claim_stop_was_requested(self):
        campaign = self.campaign()
        campaign.trace.requested = True
        campaign.trace.directory.mkdir()
        original_touch = Path.touch
        def touch(path, *args, **kwargs):
            if path.name == 'stop-trace':
                raise PermissionError('cannot write stop flag')
            return original_touch(path, *args, **kwargs)
        with patch.object(isl.Campaign, 'run', return_value=0), patch.object(Path, 'touch', touch), \
             contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(campaign.run(), 1)
        result = json.loads((campaign.root / 'stack-campaign.json').read_text())['trace']
        self.assertFalse(result['stopRequested'])
        self.assertIn('not confirmed', result['recovery'])

    def test_interrupted_finalization_retains_actual_stop_state(self):
        campaign = self.campaign()
        campaign.trace.requested = True
        campaign.trace.directory.mkdir()
        with patch.object(isl.Campaign, 'run', return_value=0), \
             patch.object(stack_wait.time, 'sleep', side_effect=KeyboardInterrupt), \
             contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(campaign.run(), 1)
        result = json.loads((campaign.root / 'stack-campaign.json').read_text())['trace']
        self.assertTrue(result['stopRequested'])
        self.assertTrue((campaign.trace.directory / 'stop-trace').exists())

    def test_first_phase_starts_trace_and_every_phase_checks_health(self):
        campaign = self.campaign()
        events = []
        def start():
            campaign.trace.requested = True
            events.append('start')
        with patch.object(campaign.trace, 'start', side_effect=start), \
             patch.object(campaign.trace, 'ensure_active', side_effect=lambda: events.append('health')), \
             patch.object(isl.Campaign, 'phase', side_effect=lambda p: events.append(p)):
            campaign.phase('one')
            campaign.phase('two')
        self.assertEqual(events, ['start', 'health', 'one', 'health', 'health', 'two', 'health'])

    def test_preflight_failure_prevents_game_campaign(self):
        with patch('sys.argv', ['stack_runner', '--controller', 'x', '--runtime', 'r', '--recorder-directory', 's', '--output', 'o']), \
             patch.object(stack_runner, 'control', side_effect=RuntimeError('not elevated')), \
             patch.object(stack_runner, 'StackCampaign') as campaign, contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(stack_runner.main(), 1)
        campaign.assert_not_called()


class Offline(unittest.TestCase):
    def source(self, result):
        values = {'stack-campaign.json': {}, 'trace-result.json': result,
                  'trace-ready.json': READY, 'cpu-stack-wait.etl': b'trace'}
        return SimpleNamespace(names=values, json=lambda name, default=None: values.get(name, default),
                               open=lambda name: io.BytesIO(values[name]))

    def test_coverage_requires_clock_order_identity_and_closed_trace(self):
        result = {'instance': 'owned', 'startedQpc': 10, 'stopRequestedQpc': 100, 'qpcFrequency': 1000,
                  'state': 'stopped', 'stopReason': 'requested', 'errors': []}
        side = {'boundaries': {'startQpc': 20, 'stopQpc': 90, 'qpcFrequency': 1000}}
        self.assertTrue(stack_evidence.coverage(self.source(result), side)['covered'])
        for changes in ({'instance': 'foreign'}, {'startedQpc': 30}, {'stopRequestedQpc': 80},
                        {'qpcFrequency': 1}, {'errors': ['lost']}, {'state': 'stop_failed'}):
            self.assertFalse(stack_evidence.coverage(self.source(result | changes), side)['covered'])

    def test_trace_copy_is_offline_and_never_follows_embedded_paths(self):
        source = self.source({'etl': '../outside.etl'})
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            result = stack_evidence.preserve(source, output)
            self.assertEqual(result['status'], 'PRESERVED')
            self.assertEqual((output / 'cpu-stack-wait.etl').read_bytes(), b'trace')
            with self.assertRaises(ValueError):
                stack_evidence.preserve(source, output)


@unittest.skipUnless(os.name == 'nt' and os.environ.get('CSX_STACK_RECORDER_DIRECTORY'),
                     'set CSX_STACK_RECORDER_DIRECTORY to exercise the maintained worker with simulated WPR')
class PowerShellWorker(unittest.TestCase):
    """Use real launcher/worker processes; replace only elevation and native WPR."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='stack worker spaces ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.recorder = self.root / 'recorder'
        self.recorder.mkdir()
        original = Path(os.environ['CSX_STACK_RECORDER_DIRECTORY'])
        for name in ('GameFtStackWait.psm1', 'Invoke-StackWaitWorker.ps1', 'CpuStackWait.wprp'):
            shutil.copyfile(original / name, self.recorder / name)
        # Keep the actual process lifecycle and receipt writer; no real ETW session is started.
        with (self.recorder / 'GameFtStackWait.psm1').open('a', encoding='utf-8') as output:
            output.write('''
function Assert-StackWaitElevation {}
function Resolve-StackWaitWpr { param([string]$Path); @{ path = 'synthetic-wpr'; version = 'test' } }
function Invoke-StackWaitWpr {
    param([string]$Executable, [string[]]$Arguments, [string]$LogPath)
    $Arguments | ConvertTo-Json | Set-Content -LiteralPath $LogPath
    $mode = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'mode.txt') -Raw
    if ($mode.Trim() -eq $Arguments[0]) { throw "simulated failure $($Arguments[0])" }
    if ($Arguments[0] -eq '-stop') { [IO.File]::WriteAllText($Arguments[1], 'synthetic ETL, not usable evidence') }
}
''')
        self.trace = stack_wait.OwnedTrace(self.root, self.recorder, None)

    def test_real_launcher_and_worker_owned_start_stop_with_spaces(self):
        (self.recorder / 'mode.txt').write_text('success')
        with contextlib.redirect_stdout(io.StringIO()):
            preflight = stack_wait.control('preflight', self.recorder)
            self.assertEqual(preflight['path'], 'synthetic-wpr')
            try:
                self.trace.start()
                self.trace.ensure_active()
            finally:
                result = self.trace.finish(timeout=15)
        self.assertTrue(result['filesSaved'], result)
        for step in ('start', 'status', 'stop'):
            arguments = json.loads((self.trace.directory / f'wpr-{step}.log').read_text(encoding='utf-8-sig'))
            self.assertIn('-instancename', arguments)
            self.assertEqual(arguments[-1], self.trace.ready['instance'])
        self.assertIn('-skipPdbGen', arguments)

    def test_worker_start_failure_returns_receipt_and_never_stops_another_trace(self):
        (self.recorder / 'mode.txt').write_text('-start')
        with contextlib.redirect_stdout(io.StringIO()):
            try:
                with self.assertRaisesRegex(RuntimeError, 'before the comparisons'):
                    self.trace.start()
            finally:
                result = self.trace.finish(timeout=15)
        self.assertEqual(result['state'], 'start_failed')
        self.assertFalse(result['filesSaved'])
        self.assertFalse((self.trace.directory / 'wpr-stop.log').exists())

    def test_worker_stop_failure_retains_error_without_success(self):
        (self.recorder / 'mode.txt').write_text('-stop')
        with contextlib.redirect_stdout(io.StringIO()):
            try:
                self.trace.start()
            finally:
                result = self.trace.finish(timeout=15)
        self.assertEqual(result['state'], 'stop_failed')
        self.assertFalse(result['filesSaved'])
        self.assertTrue(result['trace']['errors'])

    def test_worker_saves_partial_trace_when_its_python_owner_exits(self):
        (self.recorder / 'mode.txt').write_text('success')
        script = '''
import sys
sys.path.insert(0, sys.argv[1])
from stack_wait import OwnedTrace
OwnedTrace(sys.argv[2], sys.argv[3], None).start()
'''
        try:
            result = subprocess.run([sys.executable, '-c', script, str(ROOT / 'tools/isl-diagnostics'),
                                     str(self.root), str(self.recorder)], capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            receipt = self.trace.directory / 'trace-result.json'
            deadline = time.monotonic() + 5
            while not receipt.exists() and time.monotonic() < deadline:
                time.sleep(.1)
            self.assertTrue(receipt.is_file())
            value = stack_wait.read_json(receipt)
            self.assertEqual(value['stopReason'], 'owner_exited')
            self.assertEqual(value['state'], 'stopped')
            self.assertGreater((self.trace.directory / 'cpu-stack-wait.etl').stat().st_size, 0)
        finally:
            self.trace.requested = self.trace.directory.exists()
            with contextlib.redirect_stdout(io.StringIO()):
                self.trace.finish(timeout=15)


if __name__ == '__main__':
    unittest.main()
