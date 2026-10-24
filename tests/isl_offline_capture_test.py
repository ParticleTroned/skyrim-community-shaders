"""Exercise collection-only ownership, portable export, recovery and engine analysis."""

import importlib.util
import io
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from measurement_controller_transport_test import ROOT, isl, nr_status


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'tools/isl-diagnostics' / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


capture_file, engine = load('capture_file'), load('engine_analysis')


def recording():
    header = {'format': capture_file.FORMAT, 'raw': {'total': 1}, 'isl': {'total': 1},
              'metrics': ['EngineUpdate'], 'callers': {'15': {'module': 'SkyrimVR.exe', 'rva': 3}},
              'sideColumns': ['ordinal', 'metric', 'enterQpc', 'exitQpc', 'generation', 'frame', 'thread', 'caller', 'labelId', 'cpu100ns', 'cycles', 'cpuStart100ns']}
    return [header, [0, {'stream': 'application', 'ordinal': 0, 'data': {'frame': 0, 'completed': False}}],
            [1, 0, 0, 1, 11, 7, None, 5, 15, 0, 0, None, 100], {'complete': True, 'rawTotal': 1, 'islTotal': 1}]


def encoded(rows):
    return ''.join(json.dumps(row) + '\n' for row in rows).encode()


class Files(unittest.TestCase):
    def test_roundtrip_keeps_zero_null_and_resolves_metric_caller(self):
        raw, side, quality = capture_file.read_capture(io.BytesIO(encoded(recording())))
        self.assertTrue(quality['complete'])
        self.assertEqual(raw['records'][0]['data'], {'frame': 0, 'completed': False})
        row = side['records'][0]
        self.assertEqual(row['metric'], 'EngineUpdate')
        self.assertEqual(row['caller'], {'module': 'SkyrimVR.exe', 'rva': 3})
        self.assertEqual(row['cpu100ns'], 0)
        self.assertIsNone(row['cycles'])
        self.assertIsNone(row['frame'])
        self.assertIsNone(capture_file.pages(side)[0]['nextOffset'])

    def test_truncation_preserves_prior_rows_and_never_claims_complete(self):
        data = encoded(recording()[:-1]) + b'[1,0'
        raw, side, quality = capture_file.read_capture(io.BytesIO(data))
        self.assertFalse(quality['complete'])
        self.assertEqual(len(raw['records']), 1)
        self.assertEqual(len(side['records']), 1)
        self.assertTrue(any('footer missing' in x for x in quality['limitations']))

    def test_bad_metric_footer_count_and_trailing_data_are_visible(self):
        for mutate in (lambda rows: rows[2].__setitem__(2, -1),
                       lambda rows: rows[-1].__setitem__('rawTotal', 2),
                       lambda rows: rows.append([0, {}])):
            rows = recording()
            mutate(rows)
            self.assertFalse(capture_file.read_capture(io.BytesIO(encoded(rows)))[2]['complete'])


class Collection(unittest.TestCase):
    def setUp(self):
        parent = ROOT / 'build/measurement-transport-tests'
        parent.mkdir(parents=True, exist_ok=True)
        self.directory = tempfile.TemporaryDirectory(dir=parent)
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.native = self.root / 'native.csxcap'
        self.native.write_bytes(encoded(recording()))
        args = SimpleNamespace(output=str(self.root / 'output'), controller='unused', runtime='unused',
                               build_id=None, plan='core', disable_neural_rendering=False)
        self.campaign = isl.Campaign(args, {})
        self.receipt = {'saved': True, 'format': capture_file.FORMAT, 'captureId': 7, 'path': str(self.native),
                        'bytes': self.native.stat().st_size, 'rawTotal': 1, 'islTotal': 1}
        self.events = []

    def call(self, action, **kwargs):
        self.events.append(action)
        if action == 'save_capture':
            return self.receipt
        self.assertEqual((self.campaign.root / 'one.csxcap').read_bytes(), self.native.read_bytes())
        return {'released': True}

    def test_copy_precedes_ack_and_collection_does_not_parse_or_analyze(self):
        with patch.object(self.campaign, 'call', side_effect=self.call), \
             patch.object(self.campaign, 'isl', side_effect=self.call), \
             patch.object(isl.evidence, 'sidecar', side_effect=AssertionError('live analysis')), \
             patch.object(isl.evidence.raw, 'validate', side_effect=AssertionError('live analysis')):
            self.campaign.export_pair(7, 'one')
        self.assertEqual(self.events, ['save_capture', 'acknowledge_export', 'acknowledge_export'])
        self.assertIsNone(self.campaign.raw_unacked)
        self.assertIsNone(self.campaign.side_unacked)
        self.assertTrue(self.native.exists())

    def test_truncated_source_is_not_acknowledged(self):
        self.receipt['bytes'] += 1
        with patch.object(self.campaign, 'call', side_effect=self.call), \
             patch.object(self.campaign, 'isl') as side, self.assertRaisesRegex(ValueError, 'size changed'):
            self.campaign.export_pair(7, 'one')
        side.assert_not_called()
        self.assertEqual(self.events, ['save_capture'])

    def test_lost_raw_ack_retains_both_totals_and_completed_file(self):
        def call(action, **kwargs):
            if action == 'acknowledge_export':
                raise TimeoutError('reply lost')
            return self.call(action, **kwargs)
        with patch.object(self.campaign, 'call', side_effect=call), self.assertRaises(TimeoutError):
            self.campaign.export_pair(7, 'one')
        self.assertEqual((self.campaign.raw_unacked, self.campaign.side_unacked), (1, 1))
        self.assertEqual((self.campaign.root / 'one.csxcap').read_bytes(), self.native.read_bytes())

    def test_failed_copy_preserves_partial_and_refuses_acks(self):
        def fail(incoming, outgoing, **kwargs):
            outgoing.write(incoming.read(8))
            raise OSError('disk full')
        with patch.object(self.campaign, 'call', side_effect=self.call), \
             patch.object(isl.shutil, 'copyfileobj', side_effect=fail), self.assertRaises(OSError):
            self.campaign.export_pair(7, 'one')
        self.assertEqual(self.events, ['save_capture'])
        self.assertEqual((self.campaign.root / 'one.csxcap.partial').stat().st_size, 8)
        self.assertTrue(self.native.exists())

    def test_engine_plan_brackets_detailed_observers_with_quiet_on_off_on_windows(self):
        config = json.loads((ROOT / 'tools/isl-diagnostics/core.json').read_text())
        phases = isl.plan(config, 'engine')
        self.assertEqual(sum(p['durationMs'] for p in phases), 150000)
        self.assertEqual([p['islEnabled'] for p in phases], [True]*3 + [False]*3 + [True]*3)
        self.assertEqual([p['gates']['observationLevel'] for p in phases], ['counters', 'engine', 'engine']*3)

    def test_noon_reset_precedes_settling_and_weather_change_does_not_discard_recording(self):
        campaign = self.campaign
        campaign.config = json.loads((ROOT / 'tools/isl-diagnostics/core.json').read_text())
        before = dict(ready=True, gameHour=12.01, pacing={}, weather=1, cell=1, nrRequested=False,
                      effective=isl.NORMAL.copy(), shadowBatch={}, shadowCulling={'settings': {}}, isl={'enabled': True})
        after = before | {'weather': 2}
        campaign.original = before
        def call(action, **kwargs):
            self.events.append(action)
            return {'set_noon': {'gameHour': 12}, 'start': {'captureId': 7, 'admissionDelayMs': 1000, 'condition': {}},
                    'stop': {'frozen': True}, 'condition': {}}[action]
        with patch.object(campaign, 'call', side_effect=call), \
             patch.object(campaign, 'isl', return_value=after), patch.object(campaign, 'settle_isl', return_value=before), \
             patch.object(campaign, 'nr_condition', return_value=nr_status()), \
             patch.object(campaign, 'export_pair', return_value={'file': 'test.csxcap', 'bytes': 1}), \
             patch.object(isl.base, 'qpc_now', return_value=1), \
             patch.object(isl.time, 'monotonic', side_effect=[0, 100]):
            campaign.phase(isl.plan(campaign.config, 'engine')[0])
        self.assertEqual(self.events, ['set_noon', 'start', 'stop', 'condition'])
        self.assertTrue(campaign.completed[0]['collected'])
        inputs = json.loads((campaign.root / 'engine-01-R-analysis-input.json').read_text())
        self.assertNotEqual(inputs['before']['weather'], inputs['after']['weather'])
        self.assertEqual(inputs['sceneTimePolicy'], 'noon reset then settled')


class Engine(unittest.TestCase):
    def test_nested_and_other_thread_work_are_not_added_to_frame_cost(self):
        def row(ordinal, metric, begin, end, thread=1):
            return dict(ordinal=ordinal, metric=metric, enterQpc=begin, exitQpc=end, thread=thread, generation=1)
        side = {'boundaries': {'qpcFrequency': 1000, 'startQpc': 1, 'stopQpc': 101}, 'generation': 1,
                'threads': [], 'records': [row(0, 'EngineUpdate', 1, 101), row(1, 'RenderWorld', 41, 81),
                    row(2, 'FeaturePass', 51, 61), row(3, 'RetainedShadowRender', 11, 21),
                    row(4, 'PoseCycle', 21, 31), row(5, 'VRSubmit', 81, 91), row(6, 'Present', 91, 96),
                    row(7, 'FeaturePass', 1, 101, 2)]}
        result = engine.analyze(side)
        self.assertEqual(result['frames'][0]['wallMs'], 100)
        self.assertEqual(result['frames'][0]['uncoveredMs'], 25)
        self.assertEqual(result['frames'][0]['stagesMs']['beforeShadows'], 10)
        self.assertIsNone(result['frames'][0]['cpuMs'])

    def test_broad_regular_teeth_need_no_clean_reference_or_four_frame_width(self):
        rows = [dict(systemSeconds=i / 90, frame=i, epoch=1, presents=2 if i % 20 == 19 else 1,
                     waitGetPosesCalledMs=(i % 20) * .55) for i in range(400)]
        result = engine.cadence(rows)
        self.assertTrue(result['combDetected'])
        self.assertEqual(result['repeatCorroboratedWraps'], 19)
        self.assertEqual(result['extraPresentations'], 20)
        quiet = [dict(r, waitGetPosesCalledMs=8, presents=1) for r in rows]
        self.assertFalse(engine.cadence(quiet)['combDetected'])
        self.assertEqual(engine.cadence(quiet)['extraPresentations'], 0)


if __name__ == '__main__':
    unittest.main()
