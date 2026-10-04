"""CPU-only evidence, ownership and failure admission for kernel-chain probes."""

import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import kernel_chain_probe as probe
import test_packed_report as fixtures


class KernelChainProbeTests(unittest.TestCase):
    def samples(self, gpu=100, cpu=200, native=150, pixels=b'\x10\x20\x30\xff'):
        return [{'crops': {(0, 0): pixels, (1, 0): pixels}, 'nativeGpuMicroseconds': gpu - 10,
                 'batchGpuMicroseconds': gpu, 'submissionCpuMicroseconds': cpu,
                 'nativeCpuMicroseconds': native} for _ in range(2)]

    def test_geometry_is_fixed_and_private_for_all_modes(self):
        rects = [[0, 0, 128, 128], [256, 128, 128, 128]]
        cases = probe.cases(rects, 384, 256)
        self.assertEqual([case['kernelChainMode'] for case in cases], ['off', 'forward', 'group', 'off'])
        self.assertTrue(all(case['rects'] == rects and case['kind'] == 'separate' for case in cases))
        self.assertTrue(all('nativeHandlePolicy' not in case for case in cases))
        self.assertEqual(len(probe.cases([rects[0]], 384, 256)), 4)

    def test_unsafe_geometry_is_rejected(self):
        for rects in ([], [[0, 0, 64, 128]], [[300, 0, 128, 128]],
                      [[0, 0, 128, 128], [64, 64, 128, 128]], [[0, 0, 128, 128]] * 5):
            with self.subTest(rects=rects), self.assertRaises(ValueError):
                probe.cases(rects, 384, 256)

    def test_submission_cpu_includes_cost_outside_native_evaluation(self):
        result = probe.assess_cpu(self.samples(), self.samples(cpu=180, native=1), self.samples())
        self.assertTrue(result['submissionCpuMicroseconds']['timingStable'])
        self.assertEqual(result['submissionCpuMicroseconds']['records']['candidate']['mean'], 180)
        self.assertEqual(result['nativeCpuMicroseconds']['records']['candidate']['mean'], 1)

    def test_cpu_drift_separate_from_gpu_and_nonpositive_baseline_rejected(self):
        result = probe.assess_cpu(self.samples(), self.samples(), self.samples(cpu=240))
        self.assertFalse(result['submissionCpuMicroseconds']['timingStable'])
        self.assertTrue(result['nativeCpuMicroseconds']['timingStable'])
        result = probe.assess_cpu(self.samples(cpu=0), self.samples(), self.samples(cpu=0))
        self.assertFalse(result['submissionCpuMicroseconds']['timingStable'])
        self.assertIsNone(result['submissionCpuMicroseconds']['baselineDriftFraction'])

    def test_journal_binds_order_outputs_hashes_and_complete_status(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            plan = {'cases': probe.cases([[0, 0, 128, 128]], 128, 128), 'repeats': 2}
            probe.write(root / 'plan.json', plan)
            loaded = {(case['id'], repeat): {'resultSha256': f'{index:064x}'}
                      for index, (case, repeat) in enumerate(probe.ordered_jobs(plan))}
            journal = {'schema': 'csx-nr-kernel-chain-run-v1', 'status': 'complete',
                       'planSha256': probe.digest(root / 'plan.json'), 'jobs': [
                           {'case': case['id'], 'repeat': repeat, 'status': 'complete',
                            'output': str(root / case['resultDirectory'] / f'repeat-{repeat}'),
                            'resultsSha256': loaded[(case['id'], repeat)]['resultSha256']}
                           for case, repeat in probe.ordered_jobs(plan)]}
            probe.validate_journal(plan, root, journal, loaded)
            for mutation in (lambda j: j.update(status='running'), lambda j: j['jobs'].reverse(),
                             lambda j: j['jobs'].pop(), lambda j: j['jobs'][0].update(repeat=True),
                             lambda j: j['jobs'][0].update(resultsSha256='f'*64),
                             lambda j: j['jobs'][0].update(output=str(root/'wrong')),
                             lambda j: j.update(planSha256='f'*64)):
                bad = copy.deepcopy(journal); mutation(bad)
                with self.assertRaises(ValueError): probe.validate_journal(plan, root, bad, loaded)

    def test_native_failure_stops_and_preserves_journal(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            plan = {'cases': probe.cases([[0, 0, 128, 128]], 128, 128), 'repeats': 1,
                    'samples': 2, 'warmup': 1, 'secondsPerCase': 10, 'replayExecutable': {'path': 'replay.exe'},
                    'sourceManifest': {'path': 'manifest.json'}, 'runtime': {'path': 'provider.dll'}}
            probe.write(root/'plan.json', plan)
            with mock.patch.object(probe, 'admit', return_value=({}, '')), \
                    mock.patch.object(probe, 'require_idle_game') as idle, \
                    mock.patch.object(probe, 'invoke', side_effect=RuntimeError('native failed')) as invoke, \
                    mock.patch.object(probe, 'summarize', return_value={'status': 'failed'}):
                with self.assertRaisesRegex(RuntimeError, 'native failed'): probe.execute(plan, root)
                idle.assert_called_once(); invoke.assert_called_once()
                self.assertNotIn('--experimental-kernel-chain', invoke.call_args.args[0])
            journal = probe.read(root/'run.json')
            self.assertEqual(journal['status'], 'failed')
            self.assertEqual(len(journal['jobs']), 1)
            self.assertEqual(journal['jobs'][0]['status'], 'failed')

    def test_forward_output_mismatch_never_runs_group(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            plan = {'cases': probe.cases([[0, 0, 128, 128]], 128, 128), 'repeats': 1,
                    'samples': 2, 'warmup': 1, 'secondsPerCase': 10, 'replayExecutable': {'path': 'replay.exe'},
                    'sourceManifest': {'path': 'manifest.json'}, 'runtime': {'path': 'provider.dll'}}
            probe.write(root/'plan.json', plan)
            results = [{'outputHashes': {'0': 'a'}, 'resultSha256': 'a'*64},
                       {'outputHashes': {'0': 'b'}, 'resultSha256': 'b'*64}]
            with mock.patch.object(probe, 'admit', return_value=({}, '')), \
                    mock.patch.object(probe, 'require_idle_game'), mock.patch.object(probe, 'invoke') as invoke, \
                    mock.patch.object(probe, 'admitted_repeat', side_effect=results), \
                    mock.patch.object(probe, 'summarize', return_value={'status': 'failed'}):
                with self.assertRaisesRegex(ValueError, 'exact native RGBA mismatch'): probe.execute(plan, root)
                self.assertEqual(invoke.call_count, 2)
                self.assertEqual(invoke.call_args.args[0][-2:], ['--experimental-kernel-chain', 'forward'])
            self.assertEqual(probe.read(root/'run.json')['status'], 'failed')


class KernelChainHealthTests(unittest.TestCase):
    def setUp(self):
        self.fixture = fixtures.PackedReportTests()
        self.fixture.setUp()
        case, raw, directory, _ = self.fixture.handle_fixture('independent')
        self.directory, self.raw, self.case = directory, raw, case
        self.manifest = probe.read(Path(case['manifest']))
        self.plan = {'ownedRects': case['rects'], 'sourceExtent': [384, 256], 'samples': 2, 'warmup': 1}
        value = self.raw['cases'][0]
        value.update(inputStoragePolicy='captured', axis='packed_region_experiment')
        for sample in value['samples']:
            sample['evalCpuMicroseconds'] = 40
            for footprint in sample['providerFootprint']:
                footprint['alternateBytePattern'] = True

    def tearDown(self):
        self.fixture.tearDown()

    def test_all_samples_private_inputs_outputs_and_hashes_admitted(self):
        result = probe.execution_health(self.raw['cases'][0], self.directory, self.manifest, self.plan)
        self.assertEqual(set(result), {'0', '4', '1', '5'})

    def test_warmup_mutations_fail_closed(self):
        for mutation in (
                lambda c: c['samples'][0].update(reset=False),
                lambda c: c['samples'][0].update(success=False),
                lambda c: c['samples'][0]['providerFootprint'][0].update(modifiedOutsidePixels=1),
                lambda c: c['samples'][0]['providerFootprint'][0].update(nonfiniteInsidePixels=1),
                lambda c: c['samples'][0]['providerFootprint'][0].update(unchangedInsidePixels=1),
                lambda c: c['samples'][0]['immutableInputChecks'][1].update(sha256='f'*64),
                lambda c: c['samples'][0]['runtimeCalls'][0].update(nativeHandleSlot=1),
                lambda c: c['samples'][0]['runtimeCalls'][0].update(createSucceeded=False),
                lambda c: c['samples'][0]['outputFiles'][0].update(sha256='f'*64),
                lambda c: c['samples'][0].update(evalCpuMicroseconds=None),
                lambda c: c.update(creationExtent=[512, 256]),
                lambda c: c['resourceExtents'].update(depth=[512, 256]),
                lambda c: c.update(nativeHandleCount=2),
                lambda c: c.update(nativeHandleReuseBarriers=2),
                lambda c: c['samples'].pop()):
            value = copy.deepcopy(self.raw['cases'][0]); mutation(value)
            with self.assertRaises(ValueError):
                probe.execution_health(value, self.directory, self.manifest, self.plan)



class KernelChainReceiptTests(unittest.TestCase):
    def receipt(self, mode='group', cross_region=False):
        import hashlib
        packets = [{'descriptorId': identity, 'eye': identity if cross_region else 0, 'region': 0,
                    'function': 4096 + identity, 'grid': [4, 4, 1], 'block': [8, 8, 1],
                    'dynamicSharedMemoryBytes': 0, 'originalParamsPointer': 32768,
                    'paramSize': 2, 'paramsHex': '00ff', 'paramsSha256': hashlib.sha256(bytes([0, 255])).hexdigest()}
                   for identity in range(2)]
        events = []
        def event(kind, ids, reason, pending=0):
            events.append({'ordinal': len(events), 'kind': kind, 'reason': reason, 'commandList': 8192,
                           'descriptorIds': ids, 'descriptors': [copy.deepcopy(packets[i]) for i in ids],
                           'pendingDescriptorsBefore': pending, 'pendingDescriptorsAfter': pending + len(ids) if kind == 'observed' else 0,
                           'status': 0, 'apiCpuMicroseconds': 1.5 if kind == 'submitted' else 0})
        for identity in range(2):
            event('observed', [identity], 'provider_launch', identity if mode == 'group' else 0)
            if mode == 'forward': event('submitted', [identity], 'forward', 1)
        if mode == 'group': event('submitted', [0, 1], 'command_list_end', 2)
        event('boundary', [], 'command_list_end', 2 if mode == 'group' else 0)
        return {'mode': mode, 'iteration': 1, 'warmup': False, 'failed': False, 'reason': '',
                'observedCalls': 2, 'observedDescriptors': 2, 'submittedCalls': 1 if mode == 'group' else 2,
                'submittedDescriptors': 2, 'multiDescriptorCalls': int(mode == 'group'),
                'maxSubmittedDescriptors': 2 if mode == 'group' else 1,
                'crossRegionCalls': int(mode == 'group' and cross_region),
                'maxRegionsPerSubmission': 2 if mode == 'group' and cross_region else 1,
                'pendingDescriptors': 0, 'parameterBytes': 4,
                'flushReasons': {'command_list_end': 1} if mode == 'group' else {'forward': 2},
                'apiCpuMicroseconds': 1.5 if mode == 'group' else 3.0, 'hookCpuMicroseconds': 0.25,
                'events': events}

    def test_forward_and_group_preserve_descriptor_and_parameter_order(self):
        for mode in ('forward', 'group'):
            result = probe.chain_sample(self.receipt(mode), mode, 1, False, 2, 1)
            self.assertEqual(result['groupingAchieved'], mode == 'group')
            self.assertFalse(result['independentRegionBatchingAchieved'])
        result = probe.chain_sample(self.receipt(cross_region=True), 'group', 1, False, 2, 1)
        self.assertTrue(result['independentRegionBatchingAchieved'])

    def test_no_grouping_is_not_batching(self):
        receipt = self.receipt('forward')
        receipt['mode'] = 'group'
        result = probe.chain_sample(receipt, 'group', 1, False, 2, 1)
        self.assertFalse(result['groupingAchieved'])
        self.assertFalse(result['independentRegionBatchingAchieved'])

    def test_changed_packet_order_bytes_counts_and_boundaries_rejected(self):
        for mutation in (
                lambda r: r.update(failed=True), lambda r: r.update(pendingDescriptors=1),
                lambda r: r.update(observedCalls=3), lambda r: r.update(submittedDescriptors=3),
                lambda r: r.update(parameterBytes=3), lambda r: r.update(crossRegionCalls=1),
                lambda r: r.update(apiCpuMicroseconds=2), lambda r: r.update(flushReasons={}),
                lambda r: r['events'][2]['descriptorIds'].reverse(),
                lambda r: r['events'][2]['descriptors'][0].update(paramsHex='ff00'),
                lambda r: r['events'][0]['descriptors'][0].update(paramsSha256='0'*64),
                lambda r: r['events'][2].update(commandList=4096),
                lambda r: r['events'][2].update(status=-1),
                lambda r: r['events'][3].update(pendingDescriptorsAfter=1),
                lambda r: r['events'][3].update(pendingDescriptorsBefore=1),
                lambda r: r['events'].pop()):
            receipt = self.receipt(); mutation(receipt)
            with self.assertRaises(ValueError): probe.chain_sample(receipt, 'group', 1, False, 2, 1)

    def test_pending_work_cannot_cross_a_command_boundary(self):
        receipt = self.receipt()
        receipt['events'][1], receipt['events'][3] = receipt['events'][3], receipt['events'][1]
        for ordinal, event in enumerate(receipt['events']): event['ordinal'] = ordinal
        with self.assertRaises(ValueError): probe.chain_sample(receipt, 'group', 1, False, 2, 1)

    def test_restoration_and_provider_identity_are_required(self):
        receipt = self.receipt()
        experiment = {'requested': True, 'mode': 'group', 'providerDiskSha256': 'a'*64,
                      'performanceQualified': False, 'inMemoryState': 'original_provider_cache',
                      'groupingScope': 'original_order_between_intercepted_command_boundaries',
                      'transitions': [{'state': 'applied_own_replay_process_only', 'providerDiskSha256': 'a'*64,
                                       'mode': 'group', 'codeSha256': '0d0543585e6765a87886678efbdb6462f02be2a3b6c26c9454c0e9f304194dc5',
                                       'cacheRva': 0x1157d08, 'originalCachePointer': 0, 'realLaunchPointer': 4096},
                                      {'state': 'restored', 'gpuIdleProven': True, 'originalProtectionRestored': True,
                                       'restoredCodeSha256': '0d0543585e6765a87886678efbdb6462f02be2a3b6c26c9454c0e9f304194dc5',
                                       'restoredCachePointer': 0}]}
        raw = {'kernelChainExperiment': experiment, 'cases': [{'logicalEyeCount': 2, 'evaluatedRects': [{}, {}],
                                                              'samples': [{'iteration': 1, 'warmup': False, 'kernelChain': receipt}]}]}
        self.assertTrue(probe.chain_receipts(raw, 'group', 'a'*64)['groupingAchieved'])
        for mutation in (lambda r: r['kernelChainExperiment'].update(inMemoryState='hooked'),
                         lambda r: r['kernelChainExperiment']['transitions'].pop(),
                         lambda r: r['kernelChainExperiment']['transitions'][1].update(gpuIdleProven=False),
                         lambda r: r['kernelChainExperiment'].update(providerDiskSha256='b'*64)):
            altered = copy.deepcopy(raw); mutation(altered)
            with self.assertRaises(ValueError): probe.chain_receipts(altered, 'group', 'a'*64)


class KernelChainBatchTimingTests(unittest.TestCase):
    def setUp(self):
        self.fixture = fixtures.PackedReportTests(); self.fixture.setUp()
        self.case = copy.deepcopy(self.fixture.campaign['cases'][0])
        self.directory = self.fixture.root / self.case['resultDirectory'] / 'repeat-0'
        self.raw = probe.read(self.directory / 'results.json')
        self.raw['batchTimingOnly'] = True
        self.raw['cases'][0]['batchTimingOnly'] = True
        for sample in self.raw['cases'][0]['samples']:
            sample['evaluationGpuMicroseconds'] = [None] * self.raw['cases'][0]['evaluationsPerSample']
        probe.write(self.directory / 'results.json', self.raw)

    def tearDown(self):
        self.fixture.tearDown()

    def admitted(self, planned=True):
        from packed_report import _repeat, _manifest
        manifest, content = _manifest(Path(self.case['manifest']))
        case = copy.deepcopy(self.case)
        if planned: case['batchTimingOnly'] = True
        return _repeat(case, self.directory, manifest, Path(case['manifest']), content,
                       probe.digest(self.fixture.exe), [tuple(r) for r in self.case['rects']])

    def test_only_explicit_plan_admits_unavailable_per_evaluation_times(self):
        result = self.admitted()
        self.assertIsNone(result['steady'][0]['nativeGpuMicroseconds'])
        self.assertEqual(result['steady'][0]['perEvaluationGpuMicroseconds'], [None]*4)
        self.assertIsNone(result['checked']['perCallGpuMicroseconds'])
        self.assertEqual(result['checked']['perCallTimingUnavailableReason'], 'disabled_by_explicit_batch_timing_only')
        with self.assertRaises(ValueError): self.admitted(False)
        from replay_report import checked_case
        with self.assertRaises(ValueError): checked_case(self.raw['cases'][0])

    def test_batch_plan_requires_exact_flags_nulls_and_valid_batch_timing(self):
        original = copy.deepcopy(self.raw)
        for mutation in (lambda r: r.pop('batchTimingOnly'), lambda r: r['cases'][0].pop('batchTimingOnly'),
                         lambda r: r['cases'][0]['samples'][1].update(evaluationGpuMicroseconds=[0]*4),
                         lambda r: r['cases'][0]['samples'][1].update(evaluationGpuMicroseconds=[]),
                         lambda r: r['cases'][0]['samples'][1].update(gpuMicroseconds=None),
                         lambda r: r['cases'][0]['samples'][1].update(gpuMicroseconds=0)):
            altered = copy.deepcopy(original); mutation(altered)
            probe.write(self.directory/'results.json', altered)
            with self.assertRaises(ValueError): self.admitted()

    def test_bracket_does_not_fabricate_evaluation_gpu_times(self):
        values = [{'crops': {(0,0): b'rgba'}, 'nativeGpuMicroseconds': None, 'batchGpuMicroseconds': 100}]*2
        result = probe.assess_bracket(values, values, values, batch_timing_only=True)
        self.assertTrue(result['timingQualified'])
        self.assertIsNone(result['records']['candidate']['evaluationGpuMicroseconds'])
        with self.assertRaises(ValueError): probe.assess_bracket(values, values, values)

    def test_module_identity_capture_cannot_qualify_timing(self):
        for value in (True, None, 0, "false"):
            self.raw['captureKernelModules'] = value
            probe.write(self.directory / 'results.json', self.raw)
            with self.assertRaisesRegex(ValueError, "module capture instrumentation"):
                self.admitted()
        self.raw['captureKernelModules'] = False
        probe.write(self.directory / 'results.json', self.raw)
        self.admitted()
        self.raw['kernelChainExperiment'] = {'identityCapture': {'requested': True}}
        probe.write(self.directory / 'results.json', self.raw)
        with self.assertRaisesRegex(ValueError, "module capture instrumentation"):
            self.admitted()
        self.raw.pop('kernelChainExperiment')
        self.raw['cases'][0]['samples'][0]['kernelCommandDetails'] = []
        probe.write(self.directory / 'results.json', self.raw)
        with self.assertRaisesRegex(ValueError, "module capture instrumentation"):
            self.admitted()

    def test_pair_qualification_cannot_be_admitted_as_production_timing(self):
        for mode in ("original", "control", "layer-control", "batch", "model-batch", None, False, 0):
            self.raw['kernelPairMode'] = mode
            probe.write(self.directory / 'results.json', self.raw)
            with self.assertRaisesRegex(ValueError, "pair qualification"):
                self.admitted()
        self.raw['kernelPairMode'] = ""
        probe.write(self.directory / 'results.json', self.raw)
        self.admitted()

    def test_model_qualification_cannot_be_admitted_as_production_timing(self):
        for value in (True, None, 0, "false"):
            self.raw['modelReplacementRequested'] = value
            probe.write(self.directory / 'results.json', self.raw)
            with self.assertRaisesRegex(ValueError, "model replacement qualification"):
                self.admitted()
        self.raw['modelReplacementRequested'] = False
        probe.write(self.directory / 'results.json', self.raw)
        self.admitted()


if __name__ == '__main__':
    unittest.main()
