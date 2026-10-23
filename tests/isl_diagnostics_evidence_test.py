import copy
import importlib.util
import json
from pathlib import Path
import unittest
from types import SimpleNamespace
from reflex_measurement_evidence_test import fixture

ROOT = Path(__file__).resolve().parents[1]
def load(name):
    spec = importlib.util.spec_from_file_location('isl_' + name, ROOT / 'tools/isl-diagnostics' / (name + '.py'))
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    return module
E = load('evidence')
R = load('runner')

def rows(kind='comb', count=600):
    values = []
    for i in range(count):
        value = 10 if kind in ('comb','mixed') and i % 15 == 5 else 0
        if kind in ('mountains','mixed') and 250 <= i < 280:
            value = 10
        values.append({'frame':i+1,'epoch':0,'systemSeconds':1+i/90,'waitGetPosesCalledMs':value})
    return values

def pair():
    raw = fixture()
    raw[0]['recording'].update(sessionId=2,owner='client')
    header = {'schema':1,'producer':{'buildId':'test'},'sessionId':2,'clientId':'client','captureId':1,'generation':3,
              'boundaries':{'startQpc':1,'stopQpc':21,'freezeQpc':22,'qpcFrequency':1000},
              'condition':{'ready':True,'requestedGeneration':3,'effectiveGeneration':3},
              'threads':[],'prune':{},'sampling':{},'offset':0,'total':0,'nextOffset':None,'records':[],
              'quality':{key:0 for key in ('oldGenerationAtAdmission','unstableInvocationInspection','unfinishedThreadSlots','overflow','threadRegistrationLoss','contention','uniqueOverflow','transitionOrBoundaryOverlap')}}
    header['quality']['contaminated']=False
    return raw,[header]

class Evidence(unittest.TestCase):
    def test_cancelled_before_admission_exports_without_qualifying(self):
        raw,pages=pair()
        raw[0]['boundaries']['applicationAdmissionStartQpc']=None
        pages[0]['boundaries']['startQpc']=0
        result=E.sidecar(pages,raw,'test')
        self.assertTrue(result['schemaValid'] and result['paginationComplete'])
        self.assertFalse(result['complete'])
        self.assertIsNone(result['oldGenerationInvocationsDuringCapture'])
        self.assertIn('unavailable or invalid capture clock/boundaries',result['limitations'])
        pages[0]['boundaries']['startQpc']=None
        self.assertFalse(E.sidecar(pages,raw,'test')['complete'])

    def test_unexercised_subtraction_is_inconclusive(self):
        phase = {'gates': R.NORMAL | {'luminanceMode': 'native'}}
        self.assertFalse(E.experiment_applied(phase, {'counters': {}}, {}, {})['valid'])
        counts = {'LuminanceEligible': {'count': 1}, 'LuminanceNative': {'count': 2}}
        self.assertTrue(E.experiment_applied(phase, {'counters': counts}, {}, {})['valid'])
        phase['gates']['observationLevel'] = 'off'
        self.assertFalse(E.experiment_applied(phase, {'counters': {}}, {}, {})['valid'])
        self.assertTrue(E.experiment_applied({'gates': R.NORMAL | {'observationLevel': 'off'}}, {'counters': {}}, {}, {})['valid'])
        phase = {'gates': R.NORMAL, 'pruneFrames': 120}
        header = {'prune': {'consumed': False, 'delayed': False, 'activationQpc': 4}, 'boundaries': {'startQpc': 4}}
        self.assertFalse(E.experiment_applied(phase, {'counters': {}}, header, {})['valid'])
        header['prune'].update(consumed=True, delayed=True)
        self.assertTrue(E.experiment_applied(phase, {'counters': {}}, header, {})['valid'])

    def test_refresh_and_cap_cadences_preserve_separate_waveforms(self):
        for frequency in (30, 45, 72, 90, 120):
            series = rows()
            for i, row in enumerate(series): row['systemSeconds'] = 1 + i / frequency
            rules = E.reference_rules(series)
            self.assertTrue(E.waveform(series, rules)['combDetected'])
        text = E.readable_report([])
        self.assertIn('UNAVAILABLE', text)
        self.assertIn('No runtime fix', text)

    def test_scene_motion_and_missing_pose_reject_comparison(self):
        condition = {'scene': {'cellFormId': 1, 'weatherFormId': 2, 'upscaling': {'method': 1},
                              'gamePaused': False, 'position': [0, 0, 0]}, 'gameHour': 12}
        pose = {'hmdPose': {'available': True, 'matrix34': [1, 0, 0, 0, 0, 1, 0, 1.7, 0, 0, 1, 0]}}
        result = E.scene_match(condition, condition, [pose])
        self.assertTrue(result['valid'])
        moved = copy.deepcopy(pose); moved['hmdPose']['matrix34'][3] = .3
        self.assertFalse(E.scene_match(condition, condition, [moved], result['reference'])['valid'])
        changed = copy.deepcopy(condition); changed['scene']['upscaling']['method'] = 2
        self.assertFalse(E.scene_match(changed, changed, [pose], result['reference'])['valid'])
        self.assertFalse(E.scene_match(condition, condition, [])['valid'])
        self.assertTrue(E.scene_match(condition | {'gameHour': 13}, condition | {'gameHour': 13.1}, [pose], result['reference'])['valid'])
        self.assertTrue(E.scene_match(condition, condition | {'gameHour': 13}, [pose], result['reference'])['valid'])
        self.assertFalse(E.scene_match(condition, condition | {'gameHour': None}, [pose], result['reference'])['valid'])

    def test_lost_ack_recovery_does_not_reexport_released_sidecar(self):
        campaign = object.__new__(R.Campaign)
        campaign.args = SimpleNamespace(build_id='test', runtime='runtime', artifact='dll', artifact_sha256='hash')
        campaign.client, campaign.session, campaign.last_capture_id = 'owner', 2, 3
        campaign.raw_unacked, campaign.side_unacked = None, 0
        campaign.cleanup = {}
        actions = []
        def call(action, **kwargs):
            actions.append(('raw', action))
            if action == 'recover_and_stop': return {'sessionId': 2, 'captureId': 3, 'released': False, 'frozen': True}
            return {'restoration': {'requestedRestored': True, 'unchangedReleased': True, 'conflict': False}}
        def isl(action, **kwargs):
            actions.append(('isl', action))
            if action == 'acknowledge_export': return {'released': True}
            return {'liveRestored': True, 'conflict': False}
        campaign.call, campaign.isl = call, isl
        campaign.save = lambda *args: None
        campaign.export_pair = lambda *args: self.fail('acknowledged buffer was reexported')
        campaign.restore()
        self.assertIsNone(campaign.side_unacked)
        self.assertNotIn('stopOrExportFailure', campaign.cleanup)
        self.assertNotIn('restorationFailure', campaign.cleanup)
        self.assertIn(('raw', 'restore'), actions)

    def test_stop_pending_preserves_owner_but_attempts_restoration(self):
        campaign = object.__new__(R.Campaign)
        campaign.args = SimpleNamespace(build_id='test', runtime='runtime', artifact='dll', artifact_sha256='hash')
        campaign.client, campaign.session, campaign.last_capture_id = 'owner', 2, 3
        campaign.raw_unacked, campaign.side_unacked, campaign.cleanup = None, None, {}
        def call(action, **kwargs):
            if action == 'recover_and_stop': return {'sessionId': 2, 'captureId': 3, 'released': False, 'frozen': False}
            raise RuntimeError('acquisition still draining')
        campaign.call = call
        campaign.save = lambda *args: None
        campaign.restore()
        self.assertIn('stopOrExportFailure', campaign.cleanup)
        self.assertIn('restorationFailure', campaign.cleanup)
        self.assertEqual(campaign.session, 2)

    def test_comb_mountains_and_mixed_are_separate(self):
        rules=E.reference_rules(rows())
        for source, expected in [('comb','COMB_PROXY'),('mountains','MOUNTAIN_PROXY'),('mixed','MIXED_PROXY'),('none','NO_EXCURSIONS')]:
            result=E.waveform(rows(source),rules)
            self.assertEqual(result['classification'],expected)
        self.assertEqual(rules,E.reference_rules(rows()))
        self.assertEqual(rules['input'],'waitGetPosesCalledMs')

    def test_gaps_and_missing_offsets_are_unavailable(self):
        rules=E.reference_rules(rows())
        for changed in (rows(),rows()):
            changed[25]['frame']+=2
            self.assertEqual(E.waveform(changed,rules)['status'],'UNAVAILABLE')
        changed=rows();changed[25]['waitGetPosesCalledMs']=None
        self.assertEqual(E.waveform(changed,rules)['status'],'UNAVAILABLE')

    def test_no_reference_is_never_a_fix(self):
        phases=[{'name':str(i),'condition':{'label':label},'valid':True,'waveform':{'status':'MEASURED_PROXY','combDetected':False}}
                for i,label in enumerate(['R','L','L','R','V','V','R'])]
        result=E.comparison(phases)
        self.assertFalse(result['referenceCombReproduced'])
        self.assertTrue(all(x['status']=='INCONCLUSIVE' and not x['runtimeFixProven'] for x in result['comparisons']))
        for p in phases:
            if p['condition']['label']=='R':p['waveform']['combDetected']=True
        self.assertTrue(all(x['status']=='REPEATED_PROXY_REDUCTION' for x in E.comparison(phases)['comparisons']))
        phases[1]['waveform']={'status':'UNAVAILABLE','classification':'UNAVAILABLE'}
        self.assertEqual(E.comparison(phases)['comparisons'][0]['status'],'INCONCLUSIVE')
        for p in phases:
            p['waveform']={'status':'MEASURED_PROXY','combDetected':True,
                           'toothRatePerSecond': 2 if p['condition']['label']=='R' else 1}
        result=E.comparison(phases)
        self.assertTrue(all(x['status']=='REPEATED_PROXY_REDUCTION' and x['outcome']=='reduced' for x in result['comparisons']))
        phases[0]['valid']=False
        self.assertFalse(E.comparison(phases)['referenceCombReproduced'])

    def test_sidecar_identity_and_loss(self):
        raw,pages=pair()
        self.assertTrue(E.sidecar(pages,raw,'test')['complete'])
        for key,value in [('captureId',2),('sessionId',4),('clientId','outside'),('generation',5)]:
            changed=copy.deepcopy(pages);changed[0][key]=value
            if key=='generation':
                self.assertFalse(E.sidecar(changed,raw,'test')['complete'])
            else:
                with self.assertRaises(E.EvidenceError):E.sidecar(changed,raw,'test')
        for field in ('overflow','threadRegistrationLoss','contention','unfinishedThreadSlots','contaminated'):
            changed=copy.deepcopy(pages);changed[0]['quality'][field]=True if field=='contaminated' else 1
            self.assertFalse(E.sidecar(changed,raw,'test')['complete'])

    def test_sidecar_pages_and_boundaries(self):
        raw,pages=pair();page=pages[0]
        page['quality']['transitionOrBoundaryOverlap']=1
        page['total']=2;page['nextOffset']=1
        row={'ordinal':0,'metric':'EngineUpdate','generation':3,'thread':7,'frame':None,'caller':None,'enterQpc':1,'exitQpc':3}
        page['records']=[row]
        next_page=copy.deepcopy(page);next_page.update(offset=1,nextOffset=None,records=[row|{'ordinal':1,'enterQpc':20,'exitQpc':22}]);pages.append(next_page)
        result=E.sidecar(pages,raw,'test')
        self.assertEqual(result['steadyRows'],1)
        self.assertEqual(result['excludedBoundaryOrTransitionRows'],1)
        changed=copy.deepcopy(pages);changed[1]['offset']=0
        with self.assertRaises(E.EvidenceError):E.sidecar(changed,raw,'test')
        with self.assertRaises(E.EvidenceError):E.sidecar(pages[:1],raw,'test')

    def test_old_policy_entering_after_admission_cannot_qualify(self):
        raw,pages=pair()
        # Unsampled/counters-only invocations still increment the collector's overlap count.
        pages[0]['quality']['transitionOrBoundaryOverlap']=1
        result=E.sidecar(pages,raw,'test')
        self.assertFalse(result['complete'])
        self.assertEqual(result['oldGenerationInvocationsDuringCapture'],1)

    def test_isolated_native_availability_keeps_reflex_rules(self):
        pages=fixture();condition=pages[0]['condition']
        condition.update(probe=False,acceptedCondition=False,requested={'enabled':False,'boost':False,'capped':False,'fps':60,'markers':False})
        self.assertFalse(E.raw.validate(pages,'A','test')['complete'])
        condition.update(purpose='isl',nativeChannel='NOT_REQUESTED')
        result=E.raw.validate(pages,'A','test')
        self.assertTrue(result['complete'])
        self.assertTrue(result['nativeLimitations'])
        condition['requested']['enabled']=True;condition['nativeChannel']='UNAVAILABLE'
        self.assertTrue(E.raw.validate(pages,'A','test')['complete'])
        pages[0]['preferencesChanged']=True
        self.assertFalse(E.raw.validate(pages,'A','test')['complete'])

    def test_fixed_default_and_conditional_plans(self):
        config=json.loads((ROOT/'tools/isl-diagnostics/core.json').read_text(encoding='utf-8-sig'))
        plan=R.plan(config)
        self.assertEqual([p['label'] for p in plan],['R','L','L','R','V','V','R'])
        self.assertEqual(config['pacing'],'preserve');self.assertEqual(config['sceneTime'],'preserve')
        self.assertTrue(all(p['gates']['particleDetectionMode']=='normal' for p in plan))
        conversion=R.plan(config,'conversion')
        self.assertEqual(conversion[0]['gates']['luminanceMode'],'native')
        self.assertEqual(conversion[0]['gates']['rendererMode'],'vanilla_output')
        self.assertEqual(conversion[1]['gates']['rendererMode'],'vanilla_direct')
        config['sequence']=['R','V']
        with self.assertRaises(R.evidence.EvidenceError):R.plan(config)

if __name__=='__main__':unittest.main()
