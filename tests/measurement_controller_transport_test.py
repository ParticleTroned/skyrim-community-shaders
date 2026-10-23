"""Exercise the external PowerShell boundary without a game or network endpoint."""

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
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/reflex-measurement'))
from runner import Campaign, EvidenceError  # noqa: E402

spec = importlib.util.spec_from_file_location('isl_transport_runner', ROOT / 'tools/isl-diagnostics/runner.py')
isl = importlib.util.module_from_spec(spec)
spec.loader.exec_module(isl)


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
    [switch]$RequireSuccess, [switch]$RequirePerformanceNeutral, $Tool, $ArgumentsJson
)
$description = 'keyboard name' + [char]0x2192 + 'DirectInput-scan-code; ' + [char]0x65e5
@{ok=$true;transportOk=$true;description=$description;arguments=$ArgumentsJson;
  encoding=[Console]::OutputEncoding.CodePage;parameters=$PSBoundParameters} |
    ConvertTo-Json -Depth 20 -Compress
''', encoding='utf-8')
        self.args = SimpleNamespace(output=str(self.root / 'output'), controller=str(self.controller),
                                    runtime='runtime.json', build_id='a' * 64, artifact='plugin.dll',
                                    artifact_sha256='b' * 64, plan='core')

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

    def test_finished_message_waits_for_restoration_and_reports_cleanup_failure(self):
        config = json.loads((ROOT / 'tools/isl-diagnostics/core.json').read_text())
        for restoration_failed in (False, True):
            self.args.output = str(self.root / f'finished-{restoration_failed}')
            campaign = isl.Campaign(self.args, config)
            condition = {'isl': {'loaded': True}, 'editor': {'clean': True}, 'startup': {'active': 'normal'}}
            catalog = {'data': {'tools': [{'name': isl.ISL_TOOL}, {'name': isl.base.TOOL}]}}
            def phase(item):
                campaign.completed.append({'name': item['name'], 'condition': item, 'valid': True,
                                           'waveform': {'status': 'UNAVAILABLE'}})
            def restore():
                if restoration_failed:
                    campaign.cleanup['restorationFailure'] = 'synthetic restoration failure'
            stdout, stderr = io.StringIO(), io.StringIO()
            with patch.object(campaign, 'controller', return_value=catalog), \
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
