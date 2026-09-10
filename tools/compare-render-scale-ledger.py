"""Generate the required offline comparison and audit canonical ledger timings."""

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time
from contextlib import contextmanager


TIMINGS = {
    "strict_ms": ("strictMs",),
    "presentation_ms": ("presentationMs",),
    "cleanup_ms": ("cleanupMs",),
    "cleanup_tail_ms": ("cleanupTailMs",),
    "strict_frames": ("strictFrames",),
    "dispatch_qpc_tick": ("qpc", "dispatchTick"),
    "strict_qpc_tick": ("qpc", "strictSatisfiedTick"),
    "qpc_frequency_hz": ("qpc", "tickFrequency"),
}
PATTERN = re.compile(
    r"^(?P<prefix>.*?)pass(?P<pass>\d+)_transition_(?P<ordinal>\d+)_.*_(?P<metric>"
    + "|".join(TIMINGS)
    + r")$"
)


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def audit_ledger(ledger, comparison):
    """Verify every retained numeric timing, without interpreting missing cells."""
    with ledger.open(encoding="utf-8-sig", newline="") as stream:
        table = list(csv.reader(stream))
    if not table or any(len(row) != len(table[0]) for row in table):
        raise ValueError("canonical ledger is empty or nonrectangular")
    if len({row[0] for row in table[1:]}) != len(table) - 1:
        raise ValueError("canonical ledger has duplicate metric rows")
    timing_rows = {}
    for cells in table[1:]:
        match = PATTERN.fullmatch(cells[0])
        if match:
            key = (int(match['pass']), int(match['ordinal']), match['metric'])
            timing_rows.setdefault(key, []).append((match['prefix'], cells))
    verified, unavailable = [], []
    for side in ("baseline", "candidate"):
        run = comparison[side]
        columns = [i for i, header in enumerate(table[0]) if run["runId"] in header]
        if len(columns) != 1:
            raise ValueError(f"{side}: exact run must identify one canonical ledger column")
        column = columns[0]
        multi_lane = len({row.get("lane") for row in run["rows"]}) > 1
        for row in run["rows"]:
            for metric, keys in TIMINGS.items():
                value = row["switchTimings"]
                for key in keys:
                    value = value.get(key) if isinstance(value, dict) else None
                if value is None:
                    unavailable.append({"side": side, "pass": row["pass"],
                                        "ordinal": row["ordinal"], "metric": metric,
                                        "reason": "not_exposed_in_retained_receipt"})
                    continue
                matches = []
                for prefix, cells in timing_rows.get((row['pass'], row['ordinal'], metric), []):
                    if multi_lane and not re.search(
                        r"(?:^|_)" + re.escape(row["lane"]) + r"(?:_|$)", prefix
                    ):
                        continue
                    matches.append(cells)
                if len(matches) != 1:
                    raise ValueError(f"{side} {row.get('lane')} pass {row['pass']} row {row['ordinal']} {metric}: missing or ambiguous ledger metric")
                try:
                    actual = float(matches[0][column])
                except ValueError as error:
                    raise ValueError(f"measured timing absent from ledger: {matches[0][0]}") from error
                if actual != value:
                    raise ValueError(f"ledger timing disagrees with receipt: {matches[0][0]}")
                verified.append({"side": side, "metric": matches[0][0], "value": value})
    return {"verifiedNumericCells": len(verified), "verified": verified,
            "unavailable": unavailable, "historicalRows": len(table) - 1}


def write_json(path, value):
    temporary = path.with_name(path.name + '.tmp-reporting')
    with temporary.open('w', encoding='utf-8', newline='\n') as stream:
        json.dump(value, stream, indent=2)
        stream.write('\n')
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


def fingerprint(paths):
    """Content checks deliberately do not trust modification times or sizes."""
    return {str(path.resolve()): sha256(path) for path in sorted(set(paths))}


def run_inputs(root, finalized=False):
    paths = [p for p in (root / 'raw').rglob('*') if p.is_file()]
    names = ('summary.json', 'receipt-index.json') if finalized else ('worker-status.json',)
    return paths + [root / name for name in names if (root / name).is_file()]


def reusable(cache_path, inputs, outputs):
    if not cache_path.is_file() or not all(path.is_file() for path in outputs):
        return False
    try:
        cache = json.loads(cache_path.read_text(encoding='utf-8'))
        return isinstance(cache, dict) and cache.get('inputs') == inputs and cache.get('outputs') == fingerprint(outputs)
    except (ValueError, OSError):
        return False


def invoke(command):
    result = subprocess.run(command, capture_output=True, text=True, encoding='utf-8')
    if result.returncode:
        raise ValueError(result.stderr.strip() or result.stdout.strip() or f'command failed: {command[1]}')


@contextmanager
def exclusive(path):
    try:
        descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL)
    except FileExistsError as error:
        raise ValueError(f'reporting already owned; inspect retained lock: {path}') from error
    try:
        os.write(descriptor, str(os.getpid()).encode('ascii'))
        yield
    finally:
        os.close(descriptor)
        path.unlink()


def finalize_candidate(args, tool_identity):
    """Finalize once; retain limitations when historical telemetry is unavailable."""
    request = json.loads(args.finalize_candidate.read_text(encoding='utf-8-sig'))
    required = {'variant', 'runId', 'buildId', 'expectedRows'}
    allowed = required | {'artifactPath', 'manifestPath', 'generatedUtc'}
    if not isinstance(request, dict) or not required <= request.keys() or request.keys() - allowed:
        raise ValueError('finalization request has missing or unsupported fields')
    worker_path = args.candidate_root / 'worker-status.json'
    if worker_path.is_file():
        worker = json.loads(worker_path.read_text(encoding='utf-8-sig'))
        if (worker.get('state') not in ('COMPLETE', 'INTERRUPTED') or
                worker.get('cleanupVerified') is not True or worker.get('evidencePending') != 0):
            raise ValueError('candidate capture cleanup or evidence flush is not verified')
        if worker.get('runId') != request['runId'] or worker.get('buildId') != request['buildId']:
            raise ValueError('candidate worker identity disagrees with finalization request')
    artifacts = [Path(request[key]) for key in ('artifactPath', 'manifestPath') if request.get(key)]
    inputs = {'tool': tool_identity, 'request': request,
              'evidence': fingerprint(run_inputs(args.candidate_root)),
              'deployment': fingerprint(artifacts)}
    outputs = [args.candidate_root / name for name in
               ('summary.json', 'transitions.csv', 'report.md', 'receipt-index.json', 'evidence-values.csv')]
    cache = args.output_root / 'finalization-cache.json'
    reused = reusable(cache, inputs, outputs)
    if not reused:
        original_evidence = inputs['evidence']
        names = {'runId': 'run-id', 'buildId': 'build-id', 'expectedRows': 'expected-rows',
                 'artifactPath': 'artifact-path', 'manifestPath': 'manifest-path', 'generatedUtc': 'generated-utc'}
        command = [str(args.node), str(args.toolkit_root / 'tools/renderscale-tuning-finalizer/finalizer.js'),
                   '--root', str(args.candidate_root)]
        for key, value in request.items():
            command.extend(['--' + names.get(key, key), str(value)])
        invoke(command)
        # Materialization adds projections; cache the complete resulting input set.
        inputs['evidence'] = fingerprint(run_inputs(args.candidate_root))
        refreshed = {str(worker_path.resolve())}
        if artifacts:
            refreshed.update(str((args.candidate_root / 'raw/startup' / name).resolve()) for name in
                             ('deployment-verification.json', 'deployment-manifest.json'))
        if any(inputs['evidence'].get(name) != digest for name, digest in original_evidence.items() if name not in refreshed):
            raise ValueError('existing input evidence changed during finalization')
        if inputs['deployment'] != fingerprint(artifacts):
            raise ValueError('deployment changed during finalization')
        write_json(cache, {'inputs': inputs, 'outputs': fingerprint(outputs)})
    summary = json.loads(outputs[0].read_text(encoding='utf-8'))
    return {'reused': reused, 'reporting': summary['reporting'],
            'execution': summary['assayExecution']['status']}


def validate_extension(original, candidate):
    """A prepared ledger may append rows/columns, never rewrite historical cells."""
    def read(path):
        with path.open(encoding='utf-8-sig', newline='') as stream:
            table = list(csv.reader(stream))
        if not table or any(len(row) != len(table[0]) for row in table):
            raise ValueError('ledger is empty or nonrectangular')
        if len({row[0] for row in table[1:]}) != len(table) - 1 or len(set(table[0])) != len(table[0]):
            raise ValueError('ledger has duplicate metric rows or columns')
        return table
    before, after = read(original), read(candidate)
    if len(after) < len(before) or len(after[0]) < len(before[0]):
        raise ValueError('prepared ledger removes historical cells')
    if any(new[:len(old)] != old for old, new in zip(before, after)):
        raise ValueError('prepared ledger changes historical cells')
    return {'historicalCellsPreserved': True, 'rowsBefore': len(before) - 1,
            'rowsAfter': len(after) - 1, 'columnsBefore': len(before[0]), 'columnsAfter': len(after[0])}


def complete_reporting(args, ledger):
    """Generate once, audit before publishing a prepared ledger, and retain timings."""
    started = time.perf_counter()
    args.output_root.mkdir(parents=True, exist_ok=True)
    validation_path = args.output_root / 'ledger-validation.json'
    performance_path = args.output_root / 'reporting-performance.json'
    stages = {}
    with exclusive(args.output_root / '.reporting.lock'):
        write_json(validation_path, {'status': 'INCOMPLETE', 'reason': 'validation_in_progress'})
        try:
            tick = time.perf_counter()
            tool = args.toolkit_root / 'tools/renderscale-tuning-finalizer/comparison.js'
            if not tool.is_file():
                raise ValueError('toolkit has no comparison reporter; use maintained automation source')
            # Hash imported JavaScript as well as the entry point before reusing results.
            code = (list((args.toolkit_root / 'tools').rglob('*.js')) +
                    list((args.toolkit_root / 'skills').glob('renderscale-tuning-*/references/*.json')) +
                    [Path(__file__), Path(args.node)])
            identity = fingerprint(code)
            stages['toolIdentityMs'] = (time.perf_counter() - tick) * 1000
            finalized = None
            if args.finalize_candidate:
                tick = time.perf_counter()
                finalized = finalize_candidate(args, identity)
                stages['finalizationMs'] = (time.perf_counter() - tick) * 1000
            tick = time.perf_counter()
            extras = [p for p in (args.provenance_path, args.policy_path) if p]
            inputs = {'tool': identity, 'roots': [str(args.baseline_root.resolve()), str(args.candidate_root.resolve())],
                      'options': {name: str(value.resolve()) if value else None for name, value in
                                  (('provenance', args.provenance_path), ('policy', args.policy_path))},
                      'evidence': fingerprint(run_inputs(args.baseline_root, True) + run_inputs(args.candidate_root, True) + extras)}
            outputs = [args.output_root / ('comparison.' + extension) for extension in ('json', 'md', 'csv')]
            cache = args.output_root / 'comparison-cache.json'
            reused = reusable(cache, inputs, outputs)
            if not reused:
                command = [str(args.node), str(tool), '--baseline-root', str(args.baseline_root),
                           '--candidate-root', str(args.candidate_root), '--output-root', str(args.output_root)]
                for name in ('provenance', 'policy'):
                    value = getattr(args, name + '_path')
                    if value:
                        command.extend(['--' + name + '-path', str(value)])
                invoke(command)
                if inputs['evidence'] != fingerprint(run_inputs(args.baseline_root, True) + run_inputs(args.candidate_root, True) + extras):
                    raise ValueError('input evidence changed during comparison')
                write_json(cache, {'inputs': inputs, 'outputs': fingerprint(outputs)})
            stages['comparisonMs'] = (time.perf_counter() - tick) * 1000
            comparison = json.loads(outputs[0].read_text(encoding='utf-8'))
            tick = time.perf_counter()
            with exclusive(ledger.with_name(ledger.name + '.reporting.lock')):
                original_hash = sha256(ledger)
                candidate = args.ledger_candidate or ledger
                candidate_hash = sha256(candidate)
                extension = None
                if args.ledger_candidate:
                    if args.expected_ledger_sha256 != original_hash:
                        raise ValueError('canonical ledger changed since the update was prepared')
                    extension = validate_extension(ledger, candidate)
                audit = audit_ledger(candidate, comparison)
                if sha256(ledger) != original_hash or sha256(candidate) != candidate_hash:
                    raise ValueError('ledger changed during validation')
                if args.ledger_candidate and candidate_hash != original_hash:
                    shutil.copyfile(ledger, args.output_root / 'ledger-before.csv')
                    temporary = ledger.with_name(ledger.name + '.tmp-reporting')
                    shutil.copyfile(candidate, temporary)
                    with temporary.open('r+b') as stream:
                        os.fsync(stream.fileno())
                    if sha256(temporary) != candidate_hash or sha256(ledger) != original_hash:
                        raise ValueError('ledger changed before publication')
                    os.replace(temporary, ledger)
                audit.update(canonicalLedger=str(ledger), ledgerSha256=sha256(ledger),
                             ledgerUnchanged=candidate_hash == original_hash,
                             ledgerUpdate=extension, comparisonScriptSha256=sha256(tool),
                             assessment=comparison['changeAssessment'], prInclusion='USER_DECIDES', status='COMPLETE')
                write_json(validation_path, audit)
            stages['ledgerAuditAndUpdateMs'] = (time.perf_counter() - tick) * 1000
            result = {'ok': True, 'status': 'COMPLETE', 'comparisonReused': reused, 'finalization': finalized,
                      'verifiedNumericCells': audit['verifiedNumericCells'], 'ledgerUnchanged': audit['ledgerUnchanged'],
                      'assessment': comparison['changeAssessment'], 'comparison': str(args.output_root.resolve()),
                      'stagesMs': stages, 'elapsedMs': (time.perf_counter() - started) * 1000}
            write_json(performance_path, result)
            return result
        except (ValueError, OSError, KeyError) as error:
            write_json(validation_path, {'status': 'INCOMPLETE', 'error': str(error)})
            write_json(performance_path, {'ok': False, 'status': 'INCOMPLETE', 'error': str(error),
                                        'stagesMs': stages, 'elapsedMs': (time.perf_counter() - started) * 1000})
            raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("toolkit-root", "baseline-root", "candidate-root", "output-root"):
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--node", default=shutil.which("node"))
    parser.add_argument("--provenance-path", type=Path)
    parser.add_argument("--policy-path", type=Path)
    parser.add_argument('--finalize-candidate', type=Path, metavar='REQUEST_JSON',
                        help='Finalize the candidate using this explicit request; reuse only hash-verified outputs.')
    parser.add_argument('--ledger-candidate', type=Path, help='Prepared append-only update to the existing ledger.')
    parser.add_argument('--expected-ledger-sha256', help='Required with --ledger-candidate; reject concurrent changes.')
    parser.add_argument('--quiet', action='store_true', help='Suppress routine stdout; errors and receipts remain available.')
    args = parser.parse_args()
    if not args.node:
        parser.error("Node is unavailable; supply --node with the executable path")
    resolved_node = shutil.which(str(args.node))
    if not resolved_node:
        parser.error('Node executable does not exist')
    args.node = Path(resolved_node)
    if bool(args.ledger_candidate) != bool(args.expected_ledger_sha256):
        parser.error('--ledger-candidate and --expected-ledger-sha256 must be supplied together')
    output = args.output_root.resolve()
    if any(output == root.resolve() or output.is_relative_to(root.resolve()) or root.resolve().is_relative_to(output)
           for root in (args.baseline_root, args.candidate_root)):
        parser.error('comparison output must be separate from both run directories')
    repo = Path(__file__).resolve().parent.parent
    ledger = repo / "docs/development/vr-render-scale-comparison-ledger.csv"
    try:
        result = complete_reporting(args, ledger)
    except (ValueError, OSError, KeyError) as error:
        parser.exit(1, f'reporting failed: {error}\n')
    if not args.quiet:
        print(json.dumps(result))


if __name__ == "__main__":
    main()
