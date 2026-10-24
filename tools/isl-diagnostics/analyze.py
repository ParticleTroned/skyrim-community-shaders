"""Export and analyze a collected folder or ZIP offline. Skyrim and DevBench are not needed."""

import argparse
import json
from pathlib import Path
import re
import subprocess
import time
import zipfile

import capture_file
import engine_analysis
import evidence


class Source:
    def __init__(self, path):
        self.path = Path(path)
        self.archive = zipfile.ZipFile(path) if self.path.is_file() else None
        entries = self.archive.namelist() if self.archive else [p.relative_to(path).as_posix() for p in self.path.rglob('*') if p.is_file()]
        self.names = {}
        for entry in entries:
            if entry.endswith('/'):
                continue
            name = entry.rsplit('/', 1)[-1]
            if name in self.names:
                raise ValueError(f"ambiguous duplicate filename: {name}")
            self.names[name] = entry

    def close(self):
        if self.archive:
            self.archive.close()

    def open(self, name):
        entry = self.names[name]
        return self.archive.open(entry) if self.archive else (self.path / entry).open('rb')

    def json(self, name, default=None):
        if name not in self.names:
            return default
        with self.open(name) as stream:
            return json.load(stream)

    def recordings(self):
        names = set()
        for name in self.names:
            if name.endswith(('.csxcap', '.csxcap.partial')):
                names.add(name.split('.csxcap')[0])
            match = re.fullmatch(r'(.+)-raw-page-\d{8}\.json', name)
            if match:
                names.add(match[1])
        return sorted(names)

    def recording(self, name):
        filename = next((name + suffix for suffix in ('.csxcap', '.csxcap.partial') if name + suffix in self.names), None)
        if filename:
            with self.open(filename) as stream:
                return capture_file.read_capture(stream)
        documents, errors = [], []
        for channel in ('raw', 'isl'):
            selected = sorted(n for n in self.names if re.fullmatch(re.escape(name) + '-' + channel + r'-page-\d{8}\.json', n))
            header, rows = None, []
            for page_name in selected:
                page = self.json(page_name)
                current = {k: v for k, v in page.items() if k not in ('records', 'offset', 'nextOffset')}
                if header is not None and channel == 'raw' and current.get('environment') is None:
                    current['environment'] = header.get('environment')
                if header is not None and current != header:
                    errors.append(f'{channel}: frozen metadata changed at {page_name}')
                header = header or current
                if page['offset'] != len(rows):
                    errors.append(f'{channel}: page gap/overlap at {page_name}')
                rows.extend(page['records'])
            if header is None:
                errors.append(f'{channel}: missing channel')
                documents.append(None)
            else:
                if len(rows) != header['total']:
                    errors.append(f'{channel}: incomplete ({len(rows)}/{header["total"]} records)')
                documents.append(header | {'records': rows, 'offset': 0, 'nextOffset': None})
        return *documents, {'complete': not errors, 'limitations': errors}


def save(path, value):
    with path.open('x', encoding='utf-8') as stream:
        json.dump(value, stream, separators=(',', ':'), allow_nan=False)
        stream.write('\n')


def attempt(operation):
    try:
        return operation()
    except (ValueError, TypeError, KeyError, IndexError, ZeroDivisionError) as error:
        return {'complete': False, 'valid': False, 'status': 'UNAVAILABLE', 'limitations': [str(error)]}


def guard(source, name, output, module):
    before, after = name + '-guard-before.json', name + '-guard-after.json'
    if before not in source.names or after not in source.names or not module.is_file():
        return {'valid': False, 'limitations': ['offline performance qualification inputs/module missing']}
    for file in (before, after):
        save(output / file, source.json(file))
    command = ['pwsh', '-NoProfile', '-File', str(Path(__file__).with_name('check-window.ps1')),
               '-ModulePath', str(module), '-BeforePath', str(output / before), '-AfterPath', str(output / after)]
    try:
        result = subprocess.run(command, capture_output=True, text=True, timeout=30, check=False)
        if result.returncode:
            return {'valid': False, 'limitations': [result.stderr]}
        return json.loads(result.stdout)
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        return {'valid': False, 'limitations': [str(error)]}


def analyze_window(source, name, output, module, references=None):
    raw, side, file_quality = source.recording(name)
    result = {'name': name, 'fileQuality': file_quality}
    inputs = source.json(name + '-analysis-input.json')
    legacy = source.json(name + '-summary.json', {})
    for kind, document in (('raw', raw), ('isl', side)):
        if document is not None:
            save(output / (name + '-' + kind + '.json'), document)
    if raw is None:
        return result | {'valid': False, 'limitations': ['raw channel missing']}
    raw_pages = capture_file.pages(raw)
    build = raw.get('environment', {}).get('producer', {}).get('buildId')
    raw_quality = attempt(lambda: evidence.raw.validate(raw_pages, raw['recording']['mode'], build))
    compositor = evidence.streams(raw_pages, 'compositor')
    result.update(rawQuality=raw_quality, statistics=attempt(lambda: evidence.raw.summarize(raw_pages)),
                  cadence=attempt(lambda: engine_analysis.cadence(compositor)))
    if side is not None:
        side_pages = capture_file.pages(side)
        result.update(islQuality=attempt(lambda: evidence.sidecar(side_pages, raw_pages, build)),
                      engine=attempt(lambda: engine_analysis.analyze(side)),
                      qpcAssociations=attempt(lambda: evidence.correlate_qpc(raw_pages, side_pages)))
    if inputs:
        references = references if references is not None else {}
        group = inputs['condition'].get('group', 'core')
        scene = attempt(lambda: evidence.scene_match(inputs['started']['condition'], inputs['rawAfter'], compositor, references.get(group)))
        if group == 'engine':
            hour = inputs['before'].get('gameHour')
            if not evidence.finite(hour) or not 12 <= hour <= 12.05:
                scene['valid'] = False
                scene.setdefault('limitations', []).append('engine comparison did not settle within noon range; timing retained')
        if 'reference' in scene:
            references.setdefault(group, scene['reference'])
        result.update(condition=inputs['condition'], before=inputs['before'], after=inputs['after'],
                      neuralRendering=attempt(lambda: evidence.nr_window(inputs['nrBefore'], inputs['nrAfter'], inputs['condition'].get('nrEnabled', False))),
                      sceneMatch=scene,
                      performanceWindow=guard(source, name, output, module), sceneTimePolicy=inputs['sceneTimePolicy'])
        ready = inputs['clientReadyQpc']
        admission = raw['boundaries']['applicationAdmissionStartQpc']
        result['controllerOutsideAdmission'] = evidence.finite(admission) and admission > ready
        before, after = inputs['before'], inputs['after']
        result['settingsStable'] = all(before.get(k) == after.get(k) for k in ('pacing', 'nrRequested', 'effective', 'shadowBatch', 'isl')) and before.get('shadowCulling', {}).get('settings') == after.get('shadowCulling', {}).get('settings')
        result['requestedGatesObserved'] = side is not None and all(side['condition']['effective'].get(k) == v for k, v in inputs['condition']['gates'].items())
        result['experimentApplied'] = attempt(lambda: evidence.experiment_applied(inputs['condition'], result['islQuality'], side, inputs['original'])) if side else {'valid': False}
    else:
        for key in ('condition', 'before', 'after', 'neuralRendering', 'sceneMatch', 'performanceWindow', 'controllerOutsideAdmission', 'experimentApplied'):
            if key in legacy:
                result[key] = legacy[key]
        result['settingsStable'] = legacy.get('settingsAndSceneStable', False)
        result['requestedGatesObserved'] = side is not None and all(side['condition'].get('effective', {}).get(k) == v for k, v in result.get('condition', {}).get('gates', {}).items())
        result['contextSource'] = 'retained legacy receipts; waveform and raw/side evidence recomputed'
    result['channelsComplete'] = raw_quality.get('complete', False) and result.get('islQuality', {}).get('complete', False) and file_quality['complete']
    result['matchedComparisonEligible'] = result['channelsComplete'] and result.get('settingsStable', False) and result.get('requestedGatesObserved', False) and result.get('controllerOutsideAdmission', False) and all(
        result.get(k, {}).get('valid', False) for k in ('sceneMatch', 'performanceWindow', 'neuralRendering', 'experimentApplied'))
    # Record limitations separately: unmatched weather does not erase measured timing data.
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path, help='recording folder or ZIP; original files are never changed')
    parser.add_argument('--output', type=Path, required=True, help='NEW directory for exports and analysis')
    parser.add_argument('--controller-module', type=Path,
                        default=Path(__file__).resolve().parents[2] / 'transport/DevBenchControl.psm1',
                        help='optional local pure performance-window validator; no connection is made')
    parser.add_argument('--phase', action='append', help='only analyze the named phase (repeatable)')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    source, started, reports = Source(args.input), time.monotonic(), []
    references = {}
    try:
        names = args.phase or source.recordings()
        campaign = source.json('campaign.json', {})
        order = [p['name'] for p in campaign.get('completed', [])]
        names.sort(key=lambda n: (order.index(n) if n in order else len(order), n))
        if not names:
            raise ValueError('no native recordings or legacy raw pages found')
        for name in names:
            if not re.fullmatch(r'[A-Za-z0-9_-]+', name):
                raise ValueError('unsafe phase filename')
            print(f'Offline export and analysis: {name}', flush=True)
            try:
                result = analyze_window(source, name, args.output, args.controller_module, references)
            except (ValueError, KeyError, TypeError, OSError) as error:
                result = {'name': name, 'status': 'UNAVAILABLE', 'limitations': [str(error)]}
            save(args.output / (name + '-analysis.json'), result)
            reports.append({k: v for k, v in result.items() if k not in ('engine', 'qpcAssociations')})
        elapsed = time.monotonic() - started
        save(args.output / 'analysis.json', {'phases': reports, 'elapsedSeconds': elapsed,
             'meaning': 'Independent measured channels and matched-comparison eligibility are separate. No automatic root-cause or exact SteamVR Late Start claim.'})
        with (args.output / 'report.txt').open('x', encoding='utf-8') as stream:
            stream.write('OFFLINE ANALYSIS — original recordings preserved\n')
            stream.write('Phase | channels complete | matched comparison | extra presentations | repeat-correlated wraps | cadence proxy\n')
            for result in reports:
                c = result.get('cadence', {})
                stream.write(f"{result['name']} | {result.get('channelsComplete', False)} | {result.get('matchedComparisonEligible', False)} | "
                             f"{c.get('extraPresentations', 'unknown')} | {c.get('repeatCorroboratedWraps', 'unknown')} | {c.get('classification', 'unavailable')}\n")
            stream.write(f'\nElapsed: {elapsed:.2f}s. Per-phase files contain full exports, CPU stages, contexts, associations and limitations.\n')
            stream.write('Cadence wraps are not an exact reconstruction of combs/mountains. Do not infer AI causation from actor counts alone.\n')
        print(f'OFFLINE ANALYSIS FINISHED: {len(reports)} recordings in {elapsed:.2f}s; {args.output}', flush=True)
        return int(any(r.get('status') == 'UNAVAILABLE' for r in reports))
    finally:
        source.close()


if __name__ == '__main__':
    raise SystemExit(main())
