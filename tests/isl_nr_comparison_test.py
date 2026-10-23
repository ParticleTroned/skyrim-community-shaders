"""Exercise NR reversal ownership, incomplete evidence and combined reporting."""

import contextlib
import copy
import io
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from measurement_controller_transport_test import ROOT, isl, nr_status, nr_tool


def window(group, label, enabled, comb=False, mountains=0):
    return {'name': f'{group}-{label}', 'condition': {'group': group, 'label': label,
            'nrEnabled': enabled if group == 'nr' else group == 'interaction', 'islEnabled': enabled},
            'valid': True, 'statistics': {}, 'neuralRendering': {'valid': True},
            'waveform': {'status': 'MEASURED_PROXY', 'combDetected': comb,
                         'mountainCount': mountains, 'mountainRatePerSecond': mountains / 60,
                         'classification': 'COMB_PROXY' if comb else 'NO_EXCURSIONS'}}


class Reversal(unittest.TestCase):
    def setUp(self):
        parent = ROOT / 'build/measurement-transport-tests'
        parent.mkdir(parents=True, exist_ok=True)
        self.directory = tempfile.TemporaryDirectory(dir=parent)
        self.addCleanup(self.directory.cleanup)
        args = SimpleNamespace(output=str(Path(self.directory.name) / 'output'), controller='unused', runtime='unused',
                               build_id=None, plan='core', disable_neural_rendering=False, compare_neural_rendering=True)
        self.campaign = isl.Campaign(args, json.loads((ROOT / 'tools/isl-diagnostics/core.json').read_text()))
        self.status = nr_status(True)
        self.events = []
        self.campaign.original = {'isl': {'loaded': True, 'enabled': True}, 'editor': {'clean': True},
                                  'startup': {'active': 'normal'}, 'effective': isl.NORMAL.copy()}

    def nr(self, action, label, **args):
        self.events.append((action, args.get('enabled')))
        if action == 'nr_configure':
            self.assertEqual(args['expectedConfigurationFingerprint'], self.status['requestedConfigurationFingerprint'])
            self.status = nr_status(args['enabled'])
        self.campaign.save(label + '-status.json', self.status)
        return copy.deepcopy(self.status)

    def test_initial_capture_precedes_all_mutations_and_disabled_capture_is_bracketed(self):
        def phase(item):
            self.events.append(('capture', item['nrEnabled']))
            if len(self.events) == 1:
                self.assertTrue(self.status['settings']['enabled'])
        with patch.object(self.campaign, 'nr', side_effect=self.nr), \
             patch.object(self.campaign, 'phase', side_effect=phase), \
             patch.object(self.campaign, 'settle_nr'), contextlib.redirect_stdout(io.StringIO()):
            self.campaign.compare_nr(copy.deepcopy(self.status))
        self.assertEqual(self.events[0], ('capture', True))
        self.assertEqual([e[1] for e in self.events if e[0] == 'capture'], [True, False, True, False])
        self.assertEqual([e[1] for e in self.events if e[0] == 'nr_configure'], [False, True, False])
        self.assertFalse(self.status['settings']['enabled'])

    def test_interrupted_enable_is_resolved_by_reading_state_before_final_off(self):
        campaign = self.campaign
        campaign.nr_owned_configuration = copy.deepcopy(nr_status()['requestedConfiguration'])
        self.status = nr_status()
        def lose_reply(action, label, **args):
            result = self.nr(action, label, **args)
            if action == 'nr_configure':
                raise TimeoutError('reply lost after enable')
            return result
        with patch.object(campaign, 'nr', side_effect=lose_reply), self.assertRaises(TimeoutError):
            campaign.change_nr(True, 'enable')
        self.assertTrue(self.status['settings']['enabled'])
        self.assertIsNotNone(campaign.nr_pending_configuration)
        with patch.object(campaign, 'nr', side_effect=self.nr):
            campaign.finish_nr_comparison()
        self.assertFalse(self.status['settings']['enabled'])
        self.assertNotIn('nrFinalizationFailure', campaign.cleanup)
        self.assertEqual([e[1] for e in self.events if e[0] == 'nr_configure'], [True, False])

    def test_user_configuration_change_is_preserved_during_cleanup(self):
        campaign = self.campaign
        campaign.nr_comparison_touched = True
        campaign.nr_owned_configuration = copy.deepcopy(self.status['requestedConfiguration'])
        self.status['requestedConfiguration']['upscaling']['neuralRenderingIntensity'] = .9
        with patch.object(campaign, 'nr', side_effect=self.nr), contextlib.redirect_stderr(io.StringIO()):
            campaign.finish_nr_comparison()
        self.assertIn('nrFinalizationFailure', campaign.cleanup)
        self.assertFalse(any(e[0] == 'nr_configure' for e in self.events))

    def test_active_capture_prevents_nr_cleanup_mutation(self):
        campaign = self.campaign
        campaign.nr_comparison_touched = True
        campaign.cleanup['stopOrExportFailure'] = 'still draining'
        with patch.object(campaign, 'nr') as nr, contextlib.redirect_stderr(io.StringIO()):
            campaign.finish_nr_comparison()
        nr.assert_not_called()
        self.assertIn('nrFinalizationFailure', campaign.cleanup)

    def test_fov_taa_is_not_silently_changed_by_enable(self):
        self.status = nr_status()
        self.status['requestedConfiguration']['upscaling']['periphery_taa_enable'] = True
        self.campaign.nr_owned_configuration = copy.deepcopy(self.status['requestedConfiguration'])
        with patch.object(self.campaign, 'nr', side_effect=self.nr), self.assertRaisesRegex(isl.evidence.EvidenceError, 'FOV'):
            self.campaign.change_nr(True, 'enable')
        self.assertFalse(any(e[0] == 'nr_configure' for e in self.events))

    def test_first_real_phase_keeps_nr_and_isl_as_found_and_exports_on_work(self):
        campaign = self.campaign
        condition = {'ready': True, 'pacing': {}, 'weather': None, 'cell': 1, 'nrRequested': True,
                     'effective': isl.NORMAL.copy(), 'shadowBatch': {}, 'shadowCulling': {'settings': {}},
                     'isl': {'enabled': True}}
        campaign.original = condition
        campaign.nr_owned_configuration = copy.deepcopy(self.status['requestedConfiguration'])
        after = copy.deepcopy(self.status)
        for key in ('attempts', 'featureEvaluations', 'outputCommits'):
            after['counters'][key] += 100
        def call(action, **args):
            if action == 'start':
                self.assertEqual(args['durationMs'], 30000)
                return {'captureId': 1, 'admissionDelayMs': 1000, 'condition': {}}
            return {'frozen': True} if action == 'stop' else {}
        def export(*args):
            campaign.raw_pages = [{'boundaries': {'applicationAdmissionStartQpc': 10}}]
            campaign.side_pages = [{'condition': condition}]
            return {'complete': True}, {'complete': True, 'counters': {}}
        phase = {'name': 'NR-00-as-found', 'group': 'nr', 'label': 'as-found', 'nrEnabled': True,
                 'gates': isl.NORMAL.copy(), 'islEnabled': True, 'shadowCulling': None, 'pruneFrames': None}
        with patch.object(campaign, 'isl', return_value=condition) as control, \
             patch.object(campaign, 'settle_isl', return_value=condition), \
             patch.object(campaign, 'nr', side_effect=[self.status, after]), \
             patch.object(campaign, 'call', side_effect=call), \
             patch.object(campaign, 'export_pair', side_effect=export), \
             patch.object(campaign, 'performance_window', return_value={'valid': True}), \
             patch.object(isl.base, 'qpc_now', return_value=1), \
             patch.object(isl.time, 'monotonic', side_effect=[0, 100]), \
             patch.object(isl.evidence, 'streams', return_value=[]), \
             patch.object(isl.evidence, 'reference_rules', return_value={}), \
             patch.object(isl.evidence, 'scene_match', return_value={'valid': True, 'reference': {}}), \
             patch.object(isl.evidence, 'correlate_qpc', return_value={}), \
             patch.object(isl.evidence.raw, 'summarize', return_value={}), contextlib.redirect_stdout(io.StringIO()):
            campaign.phase(phase)
        self.assertEqual([c.args[0] for c in control.call_args_list], ['condition'])
        self.assertTrue(campaign.completed[0]['valid'])
        self.assertEqual(campaign.completed[0]['neuralRendering']['counterDeltas']['outputCommits'], 100)

    def test_complete_campaign_includes_all_states_and_leaves_nr_off(self):
        campaign = self.campaign
        catalog = {'data': {'tools': [{'name': isl.ISL_TOOL}, {'name': isl.base.TOOL}, nr_tool()]}}
        def phase(item):
            self.events.append(('capture', item['name']))
            result = window(item.get('group', 'core'), item['label'], item.get('nrEnabled', False))
            result.update(name=item['name'], condition=item, validation={'isl': {'timedMs': {}}}, qpcAssociations={})
            campaign.completed.append(result)
        with patch.object(campaign, 'controller', return_value=catalog), \
             patch.object(campaign, 'nr', side_effect=self.nr), \
             patch.object(campaign, 'call', return_value={'sessionId': 1}), \
             patch.object(campaign, 'isl', return_value=campaign.original), \
             patch.object(campaign, 'phase', side_effect=phase), \
             patch.object(campaign, 'settle_nr'), patch.object(campaign, 'restore'), \
             contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(campaign.run(), 0)
        self.assertEqual(len(campaign.completed), 34)
        self.assertIn('34/34', output.getvalue())
        self.assertEqual([e for e in self.events if e[0] == 'capture'][0][1], 'NR-00-as-found')
        interaction = [p for p in campaign.completed if p['condition'].get('group') == 'interaction']
        self.assertEqual([p['condition']['islEnabled'] for p in interaction], [False, True, False])
        self.assertFalse(self.status['settings']['enabled'])
        self.assertTrue((campaign.root / 'report.json').exists())


class Evidence(unittest.TestCase):
    def test_on_requires_actual_work_and_off_requires_zero_work(self):
        before = nr_status(True)
        after = copy.deepcopy(before)
        self.assertFalse(isl.evidence.nr_window(before, after, True)['valid'])
        for key in ('attempts', 'featureEvaluations', 'outputCommits'):
            after['counters'][key] += 180
        self.assertTrue(isl.evidence.nr_window(before, after, True)['valid'])
        after['counters']['resetAttempts'] += 1
        self.assertFalse(isl.evidence.nr_window(before, after, True)['valid'])
        before, after = nr_status(), nr_status()
        after['counters']['featureEvaluations'] += 1
        self.assertFalse(isl.evidence.nr_window(before, after)['valid'])

    def test_reversal_distinguishes_recurrence_from_one_time_reset(self):
        phases = [window('nr', label, enabled, comb) for label, enabled, comb in
                  [('as-found', True, True), ('off', False, False), ('on', True, True), ('off', False, False)]]
        self.assertEqual(isl.evidence.nr_comparison(phases)['status'], 'NR_ASSOCIATED_COMB_PROXY')
        phases[2]['waveform']['combDetected'] = False
        self.assertEqual(isl.evidence.nr_comparison(phases)['status'], 'INITIAL_COMB_NOT_REPRODUCED')
        phases[1]['waveform']['combDetected'] = True
        self.assertEqual(isl.evidence.nr_comparison(phases)['status'], 'COMB_PERSISTS_WITH_NR_OFF')
        phases[1]['valid'] = False
        self.assertEqual(isl.evidence.nr_comparison(phases)['status'], 'INCONCLUSIVE')

    def test_interaction_and_mountains_are_not_mixed_with_core_isl_results(self):
        phases = [window('interaction', 'R' if state else 'M', state, state) for state in [False, True, False]]
        phases += [window('mountains', 'R' if state else 'M', state, mountains=1) for state in [True, False, True, False, True]]
        report = isl.evidence.comparison(phases)
        self.assertEqual(report['comparisons'], [])
        self.assertEqual(report['islWithNREnabled']['status'], 'COMB_ONLY_WITH_BOTH_ENABLED')
        self.assertEqual(report['mountainComparison']['status'], 'MOUNTAINS_PERSIST_WITH_BOTH_OFF')

    def test_successful_nonreproduction_is_not_described_as_incomplete(self):
        phases = [window('core', label, False) for label in ['R', 'L', 'L', 'R', 'V', 'V', 'R']]
        self.assertEqual(isl.evidence.comparison(phases)['comparisons'][0]['outcome'], 'reference comb not reproduced')

    def test_long_windows_use_counters_and_timed_probes_fit_bounded_collector(self):
        phases = isl.late_start_phases()
        self.assertEqual([p['islEnabled'] for p in phases[:5]], [True, False, True, False, True])
        self.assertTrue(all(p['durationMs'] == 60000 and p['gates']['observationLevel'] == 'counters' for p in phases[:5]))
        self.assertTrue(all(p['durationMs'] == 10000 and p['gates']['observationLevel'] == 'timed' for p in phases[5:]))
        self.assertEqual([p['islEnabled'] for p in phases[5:]], [value for value in [True, False, True, False, True] for _ in range(3)])

    def test_slow_interval_keeps_actual_wait_durations_and_thread_without_adding_nested_costs(self):
        apps = [{'enterQpc': n * 10} for n in range(12)] + [{'enterQpc': 150}]
        pages = [{'boundaries': {'qpcFrequency': 1000},
                  'records': [{'stream': 'application', 'data': row} for row in apps]}]
        side = [{'records': [{'ordinal': 1, 'metric': 'SubmitMutexWait', 'thread': 7, 'frame': None,
                              'enterQpc': 112, 'exitQpc': 146},
                             {'ordinal': 2, 'metric': 'VRSubmit', 'thread': 7, 'frame': 99,
                              'enterQpc': 111, 'exitQpc': 148}]}]
        result = isl.evidence.correlate_qpc(pages, side)
        interval = result['intervals'][0]
        self.assertEqual(interval['intervalMs'], 40)
        self.assertEqual(interval['longestOverlappingScopes'][0]['metric'], 'VRSubmit')
        self.assertEqual(interval['longestOverlappingScopes'][1]['durationMs'], 34)
        self.assertIsNone(interval['longestOverlappingScopes'][1]['frame'])


if __name__ == '__main__':
    unittest.main()
