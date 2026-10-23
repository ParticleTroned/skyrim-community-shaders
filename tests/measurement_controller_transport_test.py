"""Exercise the external PowerShell boundary without a game or network endpoint."""

import contextlib
import copy
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/reflex-measurement'))
from runner import Campaign, EvidenceError  # noqa: E402

spec = importlib.util.spec_from_file_location('isl_transport_runner', ROOT / 'tools/isl-diagnostics/runner.py')
isl = importlib.util.module_from_spec(spec)
spec.loader.exec_module(isl)


def nr_tool():
    return {'name': isl.NR_TOOL, 'inputSchema': {'properties': {
        'action': {'enum': ['nr_status', 'nr_configure']}, 'enabled': {'type': 'boolean'},
        'expectedBuildId': {'type': 'string'}, 'expectedConfigurationFingerprint': {'type': 'string'}}}}


def nr_status(enabled=False):
    return {'settings': {'enabled': enabled},
            'requestedConfiguration': {'upscaling': {'neuralRenderingEnabled': enabled, 'neuralRenderingIntensity': 0.5},
                                       'color': {'enabled': False}},
            'requestedConfigurationFingerprint': ('a' if enabled else 'b') * 32,
            'renderScalePrerequisite': {'available': False}, 'runtime': {'installed': True},
            'renderer': {'memoryRetirement': {'backendRetained': False}},
            'counters': {'attempts': 12, 'featureEvaluations': 24, 'outputCommits': 24, 'resetAttempts': 1}}


def nr_response(status, action='nr_status', ok=True):
    return {'ok': True, 'transportOk': True, 'data': {'content': [{'type': 'text', 'text': json.dumps({
        'action': action, 'ok': ok, 'producer': {'buildId': 'a' * 64}, 'neuralRendering': status})}]}}


class NeuralSetupTests(unittest.TestCase):
    def setUp(self):
        parent = ROOT / 'build/measurement-transport-tests'
        parent.mkdir(parents=True, exist_ok=True)
        self.directory = tempfile.TemporaryDirectory(dir=parent)
        self.addCleanup(self.directory.cleanup)
        self.args = SimpleNamespace(output=str(Path(self.directory.name) / 'output'), controller='unused.ps1',
                                    runtime='unused.json', build_id='a' * 64, artifact='unused.dll',
                                    artifact_sha256='b' * 64, plan='core', disable_neural_rendering=False)
        self.campaign = isl.Campaign(self.args, json.loads((ROOT / 'tools/isl-diagnostics/core.json').read_text()))
        self.settle_patch = patch.object(isl.Campaign, 'settle_nr', autospec=True)
        self.settle_patch.start()
        self.addCleanup(self.settle_patch.stop)

    def test_retired_backend_and_reset_are_allowed_only_before_quiet_settling(self):
        self.settle_patch.stop()
        before = nr_status()
        before['renderer']['memoryRetirement']['backendRetained'] = True
        after = nr_status()
        after['counters']['resetAttempts'] += 1
        clock = [0.0]
        with patch.object(self.campaign, 'nr', return_value=after), \
             patch.object(isl.time, 'monotonic', side_effect=lambda: clock[0]), \
             patch.object(isl.time, 'sleep', side_effect=lambda seconds: clock.__setitem__(0, clock[0] + seconds)):
            self.campaign.settle_nr(before)
        settled = json.loads((self.campaign.root / 'nr-settled.json').read_text())
        self.assertGreaterEqual(settled['quietSeconds'], self.campaign.config['settleSeconds'])
        self.assertEqual(settled['status']['counters']['resetAttempts'], 2)
        self.assertFalse(isl.evidence.nr_window(before, after)['valid'])

    def test_nr_cleanup_wait_is_bounded_and_preserves_configuration(self):
        self.settle_patch.stop()
        status = nr_status()
        status['renderer']['memoryRetirement']['backendRetained'] = True
        clock = [0.0]
        with patch.object(self.campaign, 'nr', return_value=status), \
             patch.object(isl.time, 'monotonic', side_effect=lambda: clock[0]), \
             patch.object(isl.time, 'sleep', side_effect=lambda seconds: clock.__setitem__(0, clock[0] + seconds)), \
             contextlib.redirect_stdout(io.StringIO()), \
             self.assertRaisesRegex(isl.evidence.EvidenceError, 'within 140s'):
            self.campaign.settle_nr(status)
        self.assertEqual(clock[0], 140)
        changed = nr_status(True)
        with patch.object(self.campaign, 'nr', return_value=changed), \
             patch.object(isl.time, 'sleep'), \
             self.assertRaisesRegex(isl.evidence.EvidenceError, 'settings changed'):
            self.campaign.settle_nr(status)

    def test_invalid_window_is_retained_without_aborting_remaining_data_collection(self):
        result = {'name': '01-R', 'valid': False, 'settingsAndSceneStable': True}
        with contextlib.redirect_stderr(io.StringIO()) as stderr:
            self.campaign.record_phase(result)
        self.assertEqual(self.campaign.completed, [result])
        self.assertEqual(self.campaign.failures[0]['type'], 'InvalidWindow')
        self.assertIn('continuing', stderr.getvalue())
        self.assertFalse(json.loads((self.campaign.root / '01-R-summary.json').read_text())['valid'])

    def test_changed_settings_stop_the_campaign_after_preserving_evidence(self):
        result = {'name': '01-R', 'valid': False, 'settingsAndSceneStable': False}
        with self.assertRaisesRegex(isl.evidence.EvidenceError, 'preserve user changes'):
            self.campaign.record_phase(result)
        self.assertEqual(self.campaign.completed, [result])

    def test_nr_cleanup_during_export_does_not_contaminate_finished_window(self):
        campaign = self.campaign
        condition = {'ready': True, 'pacing': {}, 'weather': None, 'cell': 1, 'nrRequested': False,
                     'effective': 1, 'shadowBatch': {}, 'shadowCulling': {'settings': {}}}
        campaign.original = condition
        campaign.rules = {'unavailable': 'test has no waveform'}
        status = nr_status()
        def call(action, **kwargs):
            if action == 'start':
                return {'captureId': 1, 'admissionDelayMs': 1000, 'condition': {}}
            return {'frozen': True} if action == 'stop' else {}
        def export(*args):
            status['counters']['resetAttempts'] += 1
            campaign.raw_pages = [{'boundaries': {'applicationAdmissionStartQpc': 10}}]
            campaign.side_pages = [{}]
            return {'complete': True}, {'complete': True}
        with patch.object(campaign, 'isl', return_value=condition), \
             patch.object(campaign, 'settle_isl', return_value=condition), \
             patch.object(campaign, 'nr_condition', side_effect=lambda *a: copy.deepcopy(status)), \
             patch.object(campaign, 'call', side_effect=call), \
             patch.object(campaign, 'export_pair', side_effect=export), \
             patch.object(campaign, 'performance_window', return_value={'valid': True}), \
             patch.object(isl.base, 'qpc_now', return_value=1), \
             patch.object(isl.time, 'monotonic', side_effect=[0, 100]), \
             patch.object(isl.evidence, 'streams', return_value=[]), \
             patch.object(isl.evidence, 'scene_match', return_value={'valid': True, 'reference': {}}), \
             patch.object(isl.evidence, 'experiment_applied', return_value={'valid': True}), \
             patch.object(isl.evidence, 'correlate_qpc', return_value={}), \
             patch.object(isl.evidence.raw, 'summarize', return_value={}):
            campaign.phase(isl.plan(campaign.config)[0])
        self.assertEqual(status['counters']['resetAttempts'], 2)
        self.assertTrue(campaign.completed[0]['neuralRendering']['valid'])
        self.assertTrue(campaign.completed[0]['valid'])

    def test_already_disabled_does_not_send_mutation(self):
        with patch.object(self.campaign, 'controller', return_value=nr_response(nr_status())) as controller:
            self.campaign.prepare_nr()
        controller.assert_called_once()
        self.assertEqual(controller.call_args.args[2]['action'], 'nr_status')
        self.assertFalse(self.campaign.nr_setup['changedBySetup'])
        self.assertNotIn('expectedBuildId', controller.call_args.args[2])
        self.assertEqual(self.args.build_id, 'a' * 64)

    def test_live_discovery_ignores_external_build_and_artifact_expectations(self):
        self.args.output = str(Path(self.directory.name) / 'live-discovery')
        self.args.build_id = 'stale-external-build'
        self.args.artifact = 'does-not-exist.dll'
        campaign = isl.Campaign(self.args, self.campaign.config)
        with patch.object(campaign, 'controller', return_value=nr_response(nr_status())) as controller:
            campaign.prepare_nr()
            campaign.nr('nr_status', 'later')
        self.assertNotIn('expectedBuildId', controller.call_args_list[0].args[2])
        self.assertEqual(controller.call_args_list[1].args[2]['expectedBuildId'], 'a' * 64)
        self.assertEqual(campaign.identity_parameters(), {'SkipRuntimeIdentityVerification': True})

    def test_discovery_missing_required_protocol_id_stops_before_mutation(self):
        response = nr_response(nr_status(True))
        content = json.loads(response['data']['content'][0]['text'])
        del content['producer']
        response['data']['content'][0]['text'] = json.dumps(content)
        self.args.disable_neural_rendering = True
        with patch.object(self.campaign, 'controller', return_value=response) as controller:
            with self.assertRaisesRegex(isl.evidence.EvidenceError, 'did not expose its build ID'):
                self.campaign.prepare_nr()
        controller.assert_called_once()

    def test_ui_unavailable_with_stored_on_requires_explicit_setup(self):
        with patch.object(self.campaign, 'controller', return_value=nr_response(nr_status(True))) as controller:
            with self.assertRaisesRegex(isl.evidence.EvidenceError, 'does not prove NR is rendering'):
                self.campaign.prepare_nr()
        controller.assert_called_once()
        self.assertTrue((self.campaign.root / 'nr-original-status.json').exists())
        self.assertEqual(self.campaign.nr_setup['state'], 'requires_explicit_disable')

    def test_explicit_disable_is_guarded_verified_and_preserves_tuning(self):
        self.args.disable_neural_rendering = True
        responses = [nr_response(nr_status(True)), nr_response(nr_status(), 'nr_configure'), nr_response(nr_status())]
        with patch.object(self.campaign, 'controller', side_effect=responses) as controller:
            self.campaign.prepare_nr()
        request = controller.call_args_list[1].args[2]
        self.assertEqual(request, {'action': 'nr_configure', 'expectedBuildId': self.args.build_id,
                                   'enabled': False, 'expectedConfigurationFingerprint': 'a' * 32})
        self.assertEqual([c.args[2]['action'] for c in controller.call_args_list], ['nr_status', 'nr_configure', 'nr_status'])
        self.assertEqual(self.campaign.nr_setup['state'], 'off_verified')
        self.assertTrue(self.campaign.nr_setup['changedBySetup'])
        self.assertTrue((self.campaign.root / 'nr-disable-intent.json').exists())

    def test_failed_or_lost_disable_is_never_retried_or_treated_as_verified(self):
        self.args.disable_neural_rendering = True
        for response in (nr_response(nr_status(True), 'nr_configure', False), TimeoutError('lost reply')):
            with self.subTest(response=response):
                self.args.output = str(Path(self.directory.name) / str(id(response)))
                campaign = isl.Campaign(self.args, self.campaign.config)
                with patch.object(campaign, 'controller', side_effect=[nr_response(nr_status(True)), response]) as controller:
                    with self.assertRaises((isl.base.EvidenceError, TimeoutError)):
                        campaign.prepare_nr()
                self.assertEqual(controller.call_count, 2)
                self.assertEqual(campaign.nr_setup['state'], 'disable_dispatched_outcome_unconfirmed')

    def test_unexpected_tuning_change_is_rejected(self):
        self.args.disable_neural_rendering = True
        changed = nr_status()
        changed['requestedConfiguration']['upscaling']['neuralRenderingIntensity'] = 0.9
        with patch.object(self.campaign, 'controller', side_effect=[nr_response(nr_status(True)), nr_response(changed, 'nr_configure')]):
            with self.assertRaisesRegex(isl.evidence.EvidenceError, 'additional configuration'):
                self.campaign.prepare_nr()

    def test_incomplete_or_contradictory_status_fails_closed(self):
        for change in ('missing_settings', 'numeric_enabled', 'inconsistent', 'fingerprint'):
            state = nr_status()
            if change == 'missing_settings':
                del state['settings']
            elif change == 'numeric_enabled':
                state['settings']['enabled'] = 0
            elif change == 'inconsistent':
                state['requestedConfiguration']['upscaling']['neuralRenderingEnabled'] = True
            else:
                state['requestedConfigurationFingerprint'] = ''
            with self.subTest(change=change), self.assertRaises(isl.evidence.EvidenceError):
                isl.evidence.nr_preference(state)

    def test_named_payload_requires_matching_action_and_success(self):
        for response in (nr_response(nr_status(), 'nr_configure'), nr_response(nr_status(), ok=False)):
            with self.assertRaises(isl.base.EvidenceError):
                isl.base.payload(response, 'neuralRendering', 'nr_status')
        response = nr_response(nr_status())
        self.assertEqual(isl.base.payload(response, 'neuralRendering', 'nr_status'), nr_status())

    def test_isl_and_nr_state_must_both_be_explicitly_off(self):
        for flag in (True, 0, None):
            with patch.object(self.campaign, 'nr', return_value=nr_status()), \
                 self.assertRaisesRegex(isl.evidence.EvidenceError, 'disagrees'):
                self.campaign.nr_condition('phase-before', {'nrRequested': flag})

    def test_execution_counter_changes_and_configuration_changes_invalidate_window(self):
        before = nr_status()
        self.assertTrue(isl.evidence.nr_window(before, copy.deepcopy(before))['valid'])
        for key in before['counters']:
            for delta in (-1, 1):
                after = copy.deepcopy(before)
                after['counters'][key] += delta
                self.assertFalse(isl.evidence.nr_window(before, after)['valid'])
        after = copy.deepcopy(before)
        after['requestedConfigurationFingerprint'] = 'c' * 32
        self.assertFalse(isl.evidence.nr_window(before, after)['valid'])
        for value in (None, True, -1):
            after['counters']['attempts'] = value
            with self.assertRaises(isl.evidence.EvidenceError):
                isl.evidence.nr_window(before, after)

    def test_preflight_failure_is_reported_before_session_or_capture(self):
        catalog = {'data': {'tools': [{'name': isl.ISL_TOOL}, {'name': isl.base.TOOL}, nr_tool()]}}
        with patch.object(self.campaign, 'controller', side_effect=[catalog, nr_response(nr_status(True))]), \
             patch.object(self.campaign, 'call') as call, patch.object(self.campaign, 'phase') as phase, \
             patch.object(self.campaign, 'restore') as restore, contextlib.redirect_stdout(io.StringIO()), \
             contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(self.campaign.run(), 1)
        call.assert_not_called()
        phase.assert_not_called()
        restore.assert_not_called()
        report = json.loads((self.campaign.root / 'campaign.json').read_text())
        self.assertEqual(report['neuralRenderingSetup']['state'], 'requires_explicit_disable')
        self.assertEqual(report['completed'], [])


@unittest.skipUnless(shutil.which('pwsh'), 'PowerShell 7 is required')
class TransportTests(unittest.TestCase):
    def setUp(self):
        parent = ROOT / 'build/measurement-transport-tests'
        parent.mkdir(parents=True, exist_ok=True)
        self.directory = tempfile.TemporaryDirectory(dir=parent)
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.controller = self.root / "controller $value & 'quoted' é.ps1"
        self.controller.write_text('''param(
    $Command, $RuntimePath, $ExpectedBuildId, $ArtifactPath, $ExpectedArtifactSha256,
    $EvidenceDirectory, $EvidenceLabel, $MaxTransientRetries, [switch]$NoExit,
    [switch]$RequireSuccess, [switch]$RequirePerformanceNeutral, [switch]$SkipRuntimeIdentityVerification,
    $Tool, $ArgumentsJson
)
$description = 'keyboard name' + [char]0x2192 + 'DirectInput-scan-code; ' + [char]0x65e5
@{ok=$true;transportOk=$true;description=$description;arguments=$ArgumentsJson;
  encoding=[Console]::OutputEncoding.CodePage;parameters=$PSBoundParameters} |
    ConvertTo-Json -Depth 20 -Compress
''', encoding='utf-8')
        self.args = SimpleNamespace(output=str(self.root / 'output'), controller=str(self.controller),
                                    runtime='runtime.json', build_id='a' * 64, artifact='plugin.dll',
                                    artifact_sha256='b' * 64, plan='core', disable_neural_rendering=False)

    def test_utf8_structured_arguments_and_discovery_flags(self):
        campaign = Campaign(self.args, {})
        result = campaign.controller('list', 'catalog')
        self.assertEqual(result['description'], 'keyboard name→DirectInput-scan-code; 日')
        self.assertEqual(result['encoding'], 65001)
        parameters = result['parameters']
        self.assertNotIn('RequireSuccess', parameters)
        self.assertNotIn('RequirePerformanceNeutral', parameters)
        self.assertEqual(parameters['ExpectedBuildId'], self.args.build_id)
        self.assertEqual(parameters['ExpectedArtifactSha256'], self.args.artifact_sha256)
        request = {'action': 'condition', 'literal': "\"' & $(throw 'must not execute') 日"}
        result = campaign.controller('call', 'condition', request)
        self.assertEqual(json.loads(result['arguments']), request)
        self.assertTrue(result['parameters']['RequireSuccess']['IsPresent'])
        self.assertTrue(result['parameters']['RequirePerformanceNeutral']['IsPresent'])

    def test_isl_transport_omits_artifact_audits_but_keeps_action_guards(self):
        campaign = isl.Campaign(self.args, {})
        result = campaign.controller('call', 'status', {'action': 'nr_status'}, tool=isl.NR_TOOL)
        parameters = result['parameters']
        self.assertTrue(parameters['SkipRuntimeIdentityVerification']['IsPresent'])
        self.assertTrue(parameters['RequireSuccess']['IsPresent'])
        self.assertTrue(parameters['RequirePerformanceNeutral']['IsPresent'])
        for name in ('ArtifactPath', 'ExpectedArtifactSha256', 'ExpectedBuildId'):
            self.assertNotIn(name, parameters)

    def test_legacy_console_reproduces_reported_control_character(self):
        script = "[Console]::OutputEncoding=[Text.Encoding]::GetEncoding(437); @{description=('name'+[char]0x2192+'scan-code')} | ConvertTo-Json -Compress"
        result = subprocess.run(['pwsh', '-NoProfile', '-Command', script], capture_output=True, check=True)
        self.assertIn(b'\x1a', result.stdout)
        with self.assertRaises(json.JSONDecodeError):
            json.loads(result.stdout)

    @unittest.skipUnless(sys.platform == 'win32', 'Windows download-block metadata')
    def test_remote_signed_block_and_one_folder_unblock(self):
        zone = Path(str(self.controller) + ':Zone.Identifier')
        zone.write_text('[ZoneTransfer]\nZoneId=3\n', encoding='utf-8')
        module = self.root / 'DevBenchControl.psm1'
        module.write_text('# offline test module\n')
        module_zone = Path(str(module) + ':Zone.Identifier')
        module_zone.write_text('[ZoneTransfer]\nZoneId=3\n', encoding='utf-8')
        launch = ['pwsh', '-NoProfile', '-ExecutionPolicy', 'RemoteSigned', '-File']
        blocked = subprocess.run(launch + [str(self.controller)], capture_output=True)
        self.assertNotEqual(blocked.returncode, 0)
        preflight = subprocess.run(launch + [str(ROOT / 'tools/isl-diagnostics/run.ps1'),
                                    '-Controller', str(self.controller), '-Runtime', 'unused',
                                    '-PluginDirectory', str(self.root / 'unused')], capture_output=True)
        self.assertNotEqual(preflight.returncode, 0)
        self.assertIn(b'download-blocked', preflight.stderr)
        unblock = 'Get-ChildItem -LiteralPath $env:CSX_TEST_TOOLKIT -Recurse -File | Where-Object Extension -in .ps1,.psm1,.py | Unblock-File'
        subprocess.run(['pwsh', '-NoProfile', '-Command', unblock],
                       env=os.environ | {'CSX_TEST_TOOLKIT': str(self.root)}, capture_output=True, check=True)
        self.assertFalse(zone.exists())
        self.assertFalse(module_zone.exists())
        unblocked = subprocess.run(launch + [str(self.controller)], capture_output=True)
        self.assertEqual(unblocked.returncode, 0, unblocked.stderr)

    def test_json_failure_is_not_hidden_by_empty_stderr_or_zero_exit(self):
        self.controller.write_text("'{\"ok\":false,\"transportOk\":true,\"errors\":[\"catalog rejected\"]}'", encoding='utf-8')
        campaign = Campaign(self.args, {})
        with self.assertRaisesRegex(EvidenceError, 'catalog rejected'):
            campaign.controller('list', 'catalog')
        self.assertEqual((campaign.root / '0001-catalog.stderr.txt').read_bytes(), b'')
        self.assertFalse(json.loads((campaign.root / '0001-catalog.json').read_text())['ok'])

    def test_invalid_text_is_preserved_and_never_repaired_into_success(self):
        for bad in (b'{"ok":true,"x":"bad\x1a"}', b'{"ok":true,"x":"\xff"}'):
            self.args.output = str(self.root / ('bad-' + str(bad[-3])))
            campaign = Campaign(self.args, {})
            result = subprocess.CompletedProcess([], 0, bad, b'')
            with patch('runner.subprocess.run', return_value=result), self.assertRaises((ValueError, UnicodeError)):
                campaign.controller('list', 'catalog')
            self.assertEqual((campaign.root / '0001-catalog.stdout.txt').read_bytes(), bad)

    def test_existing_output_is_preserved(self):
        output = Path(self.args.output)
        output.mkdir()
        (output / 'previous.txt').write_text('preserve')
        with self.assertRaisesRegex(EvidenceError, 'Choose a NEW folder'):
            Campaign(self.args, {})
        self.assertEqual((output / 'previous.txt').read_text(), 'preserve')

    def test_launcher_forwards_explicit_nr_disable_without_connecting(self):
        (self.root / 'DevBenchControl.psm1').write_text('# offline fixture')
        runtime = self.root / 'runtime.json'
        runtime.write_text('{}')
        script = self.root / 'launch.ps1'
        script.write_text('''param($Launcher, $Controller, $Runtime, $PluginDirectory)
function global:python { $args | ConvertTo-Json -Compress; $global:LASTEXITCODE = 0 }
& $Launcher -Controller $Controller -Runtime $Runtime -PluginDirectory $PluginDirectory -DisableNeuralRendering
''')
        (self.root / 'CSX.BuildManifest.json').write_text('invalid stale manifest')
        for plugin in ('', str(self.root / 'nonexistent-provider'), str(self.root)):
            with self.subTest(plugin=plugin):
                result = subprocess.run(['pwsh', '-NoProfile', '-File', str(script),
                                         '-Launcher', str(ROOT / 'tools/isl-diagnostics/run.ps1'),
                                         '-Controller', str(self.controller), '-Runtime', str(runtime),
                                         '-PluginDirectory', plugin], capture_output=True, check=True)
                arguments = json.loads(result.stdout.decode('utf-8-sig').splitlines()[-1])
                self.assertIn('--disable-neural-rendering', arguments)
                for name in ('--artifact', '--artifact-sha256', '--build-id'):
                    self.assertNotIn(name, arguments)

    def test_failed_catalog_has_visible_failure_and_no_game_actions(self):
        self.controller.write_text("'{\"ok\":false,\"transportOk\":true,\"errors\":[\"catalog rejected\"]}'", encoding='utf-8')
        config = json.loads((ROOT / 'tools/isl-diagnostics/core.json').read_text())
        campaign = isl.Campaign(self.args, config)
        stdout, stderr = io.StringIO(), io.StringIO()
        with patch.object(campaign, 'call') as call, contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            self.assertEqual(campaign.run(), 1)
        call.assert_not_called()
        self.assertIn('TEST FAILED / INCOMPLETE: 0/7', stderr.getvalue())
        self.assertIn('catalog rejected', stderr.getvalue())
        self.assertIn('Send the ENTIRE output folder', stdout.getvalue())
        self.assertIn('Capture execution: FAILED / INCOMPLETE', (campaign.root / 'report.txt').read_text())
        self.assertEqual(json.loads((campaign.root / 'campaign.json').read_text())['completed'], [])

    def test_launcher_comparison_flag_and_incompatible_options(self):
        (self.root / 'DevBenchControl.psm1').write_text('# offline fixture')
        runtime = self.root / 'runtime.json'
        runtime.write_text('{}')
        script = self.root / 'compare.ps1'
        script.write_text('''param($Launcher, $Controller, $Runtime)
function global:python { $args | ConvertTo-Json -Compress; $global:LASTEXITCODE = 0 }
& $Launcher -Controller $Controller -Runtime $Runtime -CompareNeuralRendering
''')
        launcher = ROOT / 'tools/isl-diagnostics/run.ps1'
        result = subprocess.run(['pwsh', '-NoProfile', '-File', str(script), '-Launcher', str(launcher),
                                 '-Controller', str(self.controller), '-Runtime', str(runtime)], capture_output=True, check=True)
        arguments = json.loads(result.stdout.decode('utf-8-sig').splitlines()[-1])
        self.assertIn('--compare-neural-rendering', arguments)
        self.assertNotIn('--disable-neural-rendering', arguments)
        for extra in (['-DisableNeuralRendering'], ['-OffUnlimited'], ['-Plan', 'shadow'], ['-Observation', 'timed']):
            result = subprocess.run(['pwsh', '-NoProfile', '-File', str(launcher), '-Controller', 'missing',
                                     '-Runtime', 'missing', '-CompareNeuralRendering'] + extra, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(b'Use -CompareNeuralRendering', result.stderr)

    def test_finished_message_waits_for_restoration_and_reports_cleanup_failure(self):
        config = json.loads((ROOT / 'tools/isl-diagnostics/core.json').read_text())
        for restoration_failed in (False, True):
            self.args.output = str(self.root / f'finished-{restoration_failed}')
            campaign = isl.Campaign(self.args, config)
            condition = {'isl': {'loaded': True}, 'editor': {'clean': True}, 'startup': {'active': 'normal'}}
            catalog = {'data': {'tools': [{'name': isl.ISL_TOOL}, {'name': isl.base.TOOL}, nr_tool()]}}
            def phase(item):
                campaign.completed.append({'name': item['name'], 'condition': item, 'valid': True,
                                           'waveform': {'status': 'UNAVAILABLE'}})
            def restore():
                if restoration_failed:
                    campaign.cleanup['restorationFailure'] = 'synthetic restoration failure'
            stdout, stderr = io.StringIO(), io.StringIO()
            with patch.object(campaign, 'controller', return_value=catalog), \
                 patch.object(campaign, 'prepare_nr'), \
                 patch.object(campaign, 'call', return_value={'sessionId': 1}), \
                 patch.object(campaign, 'isl', return_value=condition), \
                 patch.object(campaign, 'phase', side_effect=phase), \
                 patch.object(campaign, 'restore', side_effect=restore) as restore_call, \
                 contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
                self.assertEqual(campaign.run(), int(restoration_failed))
            restore_call.assert_called_once()
            if restoration_failed:
                self.assertNotIn('TEST FINISHED', stdout.getvalue())
                self.assertIn('TEST FAILED / INCOMPLETE', stderr.getvalue())
            else:
                self.assertIn('TEST FINISHED: 7/7', stdout.getvalue())
                self.assertIn('diagnosis may still be inconclusive', stdout.getvalue())


if __name__ == '__main__':
    unittest.main()
