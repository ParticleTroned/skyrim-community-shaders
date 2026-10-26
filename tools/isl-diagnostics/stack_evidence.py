"""Validate trace receipts and preserve clock coverage offline, without a live game."""

import shutil


def recording_ready(receipt):
    return (isinstance(receipt, dict) and receipt.get('state') == 'recording' and
            isinstance(receipt.get('instance'), str) and bool(receipt['instance']) and
            all(type(receipt.get(key)) is int and receipt[key] > 0
                for key in ('startedQpc', 'qpcFrequency')))


def closed_trace(ready, result):
    """A closed receipt must belong to the started trace and retain its clock."""
    return (recording_ready(ready) and isinstance(result, dict) and
            result.get('state') == 'stopped' and result.get('stopReason') == 'requested' and
            result.get('errors') == [] and
            all(result.get(key) == ready[key] for key in ('instance', 'startedQpc', 'qpcFrequency')) and
            type(result.get('stopRequestedQpc')) is int and result['stopRequestedQpc'] >= ready['startedQpc'])


def coverage(source, side):
    if 'stack-campaign.json' not in source.names and 'trace-ready.json' not in source.names:
        return {'status': 'NOT_REQUESTED'}
    result = source.json('trace-result.json', {})
    ready = source.json('trace-ready.json', {})
    boundaries = side.get('boundaries', {}) if side else {}
    frequency = boundaries.get('qpcFrequency')
    start, stop = boundaries.get('startQpc'), boundaries.get('stopQpc')
    trace_start, trace_stop = result.get('startedQpc'), result.get('stopRequestedQpc')
    clocks = all(type(v) is int and v > 0 for v in (frequency, start, stop, trace_start, trace_stop))
    covered = (clocks and frequency == result.get('qpcFrequency') and
               trace_start <= start <= stop <= trace_stop)
    closed = closed_trace(ready, result) and 'cpu-stack-wait.etl' in source.names
    return {'status': 'COVERED' if covered and closed else 'INCOMPLETE', 'covered': bool(covered and closed),
            'windowQpc': {'start': start, 'stop': stop, 'frequency': frequency},
            'traceQpc': {'start': trace_start, 'stop': trace_stop, 'frequency': result.get('qpcFrequency')},
            'meaning': 'Receipt clock coverage only. Verify ETL events, lost events and symbols offline before stack attribution.'}


def preserve(source, output):
    if 'cpu-stack-wait.etl' not in source.names:
        requested = 'stack-campaign.json' in source.names or 'trace-ready.json' in source.names
        return {'status': 'MISSING' if requested else 'NOT_REQUESTED'}
    partial = output / 'cpu-stack-wait.etl.partial'
    with source.open('cpu-stack-wait.etl') as incoming, partial.open('xb') as outgoing:
        shutil.copyfileobj(incoming, outgoing, length=1024 * 1024)
    size = partial.stat().st_size
    if not size:
        return {'status': 'EMPTY', 'file': partial.name}
    target = output / 'cpu-stack-wait.etl'
    if target.exists():
        raise ValueError('trace output already exists; original and partial copy preserved')
    partial.rename(target)
    return {'status': 'PRESERVED', 'file': target.name, 'bytes': size,
            'analysis': 'Use Inspect-StackTrace.ps1 here, then CPU sampled and precise views in WPA with matching symbols.'}
