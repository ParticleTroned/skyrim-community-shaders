#!/usr/bin/env python3
"""Verify an SE screenshot sequence's committed files and mono frame evidence.

Usage: python tools/screenshot_verify.py PATH/sequence.json [--require-neural]
       [--require-exposure] [--require-contiguous]

This read-only verifier accepts final csx.screenshot v1 sequence manifests. It
checks their relative contained paths, byte counts, SHA-256 digests, native image
sizes and single-view acquisition provenance. It never changes game settings or
starts a capture. A successful result proves the recorded files/evidence are
consistent; it does not measure image quality, GPU time, or physical display output.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath, PureWindowsPath
import re
import struct
import sys
from typing import Any

MAX_MANIFEST_BYTES = 64 * 1024 * 1024
MAX_ARTIFACT_BYTES = 1024 * 1024 * 1024


class VerificationError(ValueError):
    pass


def require(condition: bool, reason: str) -> None:
    if not condition:
        raise VerificationError(reason)


def integer(value: Any, minimum: int, maximum: int, field: str) -> int:
    require(type(value) is int and minimum <= value <= maximum, f"invalid {field}")
    return value


def artifact_path(directory: Path, recorded: Any) -> Path:
    require(isinstance(recorded, str) and bool(recorded) and '\x00' not in recorded and ':' not in recorded, 'invalid artifact path')
    windows = PureWindowsPath(recorded)
    portable = PurePosixPath(recorded.replace('\\', '/'))
    require(not windows.drive and not windows.root and not portable.is_absolute(), 'absolute artifact path')
    require(all(part not in ('', '.', '..') for part in recorded.replace('\\', '/').split('/')), 'noncanonical artifact path')
    root = directory.resolve(strict=True)
    candidate = root.joinpath(*portable.parts)
    require(candidate.resolve(strict=True).is_relative_to(root), 'artifact escaped sequence directory')
    current = root
    for part in portable.parts:
        current = current / part
        require(not current.is_symlink(), 'artifact path contains a symbolic link')
    require(candidate.is_file(), 'artifact is not a regular file')
    return candidate


def image_dimensions(prefix: bytes, image_format: str) -> tuple[int, int]:
    if image_format == 'png':
        require(len(prefix) >= 33 and prefix[:8] == b'\x89PNG\r\n\x1a\n' and prefix[12:16] == b'IHDR', 'invalid PNG header')
        require(struct.unpack('>I', prefix[8:12])[0] == 13, 'invalid PNG IHDR')
        return struct.unpack('>II', prefix[16:24])
    require(image_format == 'bmp' and len(prefix) >= 26 and prefix[:2] == b'BM', 'unsupported or invalid native image format')
    dib = struct.unpack('<I', prefix[14:18])[0]
    if dib == 12:
        return struct.unpack('<HH', prefix[18:22])
    require(dib >= 40, 'unsupported BMP header')
    width, height = struct.unpack('<ii', prefix[18:26])
    require(width > 0 and height != 0, 'invalid BMP dimensions')
    return width, abs(height)


def verify(path: Path, *, require_neural: bool = False, require_exposure: bool = False,
           require_contiguous: bool = False) -> dict:
    require(path.is_file() and path.stat().st_size <= MAX_MANIFEST_BYTES, 'missing or oversized manifest')
    document = json.loads(path.read_text(encoding='utf-8'))
    require(isinstance(document, dict), 'manifest must be an object')
    contract = document.get('contract', {})
    require(contract.get('name') == 'csx.screenshot' and contract.get('major') == 1, 'unsupported screenshot contract')
    require(document.get('state') == 'final', 'manifest is not final')
    require(document.get('terminalOutcome') in ('completed', 'completed_with_warnings'), 'sequence did not complete')
    children = document.get('children')
    require(isinstance(children, list) and 0 < len(children) <= 10000, 'invalid sequence children')
    counts = document.get('counts', {})
    requested = integer(counts.get('requested'), 1, 10000, 'requested count')
    require(len(children) == requested and counts.get('written') == requested and counts.get('acquired') == requested,
            'sequence does not contain every requested frame')
    require(all(counts.get(name) == 0 for name in ('failed', 'cancelled', 'dropped', 'inFlight')), 'sequence has unresolved or missing frames')
    ordered = sorted(children, key=lambda child: integer(child.get('ordinal'), 1, requested, 'child ordinal'))
    require([child['ordinal'] for child in ordered] == list(range(1, requested + 1)), 'duplicate or missing child ordinal')
    seen_paths: set[Path] = set()
    frames: list[int] = []
    bytes_verified = 0
    for child in ordered:
        require(child.get('state') in ('completed', 'completed_with_warnings'), 'child did not complete')
        acquisition = child.get('actual', {}).get('acquisition', {})
        require(acquisition.get('source') == 'desktop_mirror' and acquisition.get('generatedFrame') is False,
                'source is not an accepted mono game frame')
        frame = integer(acquisition.get('engineFrame'), 0, (1 << 32) - 1, 'engine frame')
        frames.append(frame)
        planes = acquisition.get('planes')
        require(isinstance(planes, list) and len(planes) == 1, 'acquisition must have exactly one source plane')
        source_width = integer(planes[0].get('sourceWidth'), 1, 16384, 'source width')
        source_height = integer(planes[0].get('sourceHeight'), 1, 16384, 'source height')
        require(planes[0].get('samples') == 1, 'multisample source was not resolved')
        if require_neural or require_exposure:
            neural = acquisition.get('nrEvidence', {})
            require(neural.get('available') is True and neural.get('viewCount') == 1 and
                    neural.get('sourceWorldFrame') == neural.get('evaluationFrame') == neural.get('presentationFrame') == frame,
                    'neural evidence is missing or belongs to another frame')
            integer(neural.get('modelWidth'), 128, 16384, 'model width')
            integer(neural.get('modelHeight'), 128, 16384, 'model height')
            if require_exposure:
                diagnostics = child.get('actual', {}).get('captureDiagnostics', {})
                require(diagnostics.get('finalized') is True, 'exposure diagnostics are not finalized')
                exposure = neural.get('neuralExposure', {})
                integer(exposure.get('sequence'), 1, (1 << 64) - 1, 'exposure sequence')
                evidence = [sample for sample in diagnostics.get('exposures', []) if sample.get('key') == exposure]
                require(len(evidence) == 1 and evidence[0].get('available') is True and
                        evidence[0].get('sample', {}).get('readbackComplete') is True, 'exact captured exposure is unavailable')
        artifacts = child.get('artifacts')
        require(isinstance(artifacts, list) and 0 < len(artifacts) <= 4, 'invalid output artifacts')
        for artifact in artifacts:
            require(artifact.get('committed') is True, 'artifact was not committed')
            destination = artifact_path(path.parent, artifact.get('path'))
            require(destination not in seen_paths, 'artifact reused by multiple captures')
            seen_paths.add(destination)
            expected_bytes = integer(artifact.get('bytes'), 1, MAX_ARTIFACT_BYTES, 'artifact bytes')
            expected_hash = artifact.get('sha256')
            require(isinstance(expected_hash, str) and re.fullmatch('[0-9a-fA-F]{64}', expected_hash) is not None, 'invalid artifact SHA-256')
            digest = hashlib.sha256()
            count = 0
            with destination.open('rb') as stream:
                prefix = stream.read(64)
                digest.update(prefix)
                count += len(prefix)
                while chunk := stream.read(1024 * 1024):
                    count += len(chunk)
                    require(count <= expected_bytes, 'artifact exceeds committed byte count')
                    digest.update(chunk)
            require(count == expected_bytes and digest.hexdigest() == expected_hash.lower(), 'artifact bytes or SHA-256 changed')
            actual = artifact.get('actual', {})
            require(actual.get('view') == 'source_native' and actual.get('generatedFrame') is False and actual.get('engineFrame') == frame,
                    'artifact frame provenance differs from acquisition')
            width = integer(actual.get('width'), 1, 16384, 'artifact width')
            height = integer(actual.get('height'), 1, 16384, 'artifact height')
            require(actual.get('colourContract') in ('sdr_srgb', 'hdr10_pq'), 'unknown artifact color contract')
            require(image_dimensions(prefix, actual.get('format')) == (width, height), 'encoded dimensions differ from receipt')
            if 'burstRegions' not in actual:
                crop = actual.get('crop', {})
                x = integer(crop.get('x'), 0, source_width - 1, 'crop x')
                y = integer(crop.get('y'), 0, source_height - 1, 'crop y')
                require(crop.get('width') == width and crop.get('height') == height and
                        x + width <= source_width and y + height <= source_height, 'crop exceeds acquisition')
            bytes_verified += count
    for previous, current in zip(frames, frames[1:]):
        advance = (current - previous) & 0xffffffff
        require(0 < advance < 0x80000000, 'source frames do not advance')
        if require_contiguous or 'continuity' in document:
            require(advance == 1, 'source frames are not contiguous')
    if 'continuity' in document:
        require(document['continuity'].get('complete') is True, 'burst continuity was not accepted')
    return {'verified': True, 'schema': 'csx.screenshot.mono-verification.v1', 'frames': len(frames),
            'artifacts': len(seen_paths), 'bytes': bytes_verified, 'firstEngineFrame': frames[0],
            'lastEngineFrame': frames[-1], 'neuralEvidenceRequired': require_neural or require_exposure,
            'exposureEvidenceRequired': require_exposure}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('--require-neural', action='store_true')
    parser.add_argument('--require-exposure', action='store_true')
    parser.add_argument('--require-contiguous', action='store_true')
    args = parser.parse_args()
    try:
        result = verify(args.manifest, require_neural=args.require_neural, require_exposure=args.require_exposure,
                        require_contiguous=args.require_contiguous)
    except (VerificationError, OSError, ValueError, TypeError, KeyError, AttributeError) as error:
        print(json.dumps({'verified': False, 'reason': str(error)}), file=sys.stderr)
        return 1
    print(json.dumps(result, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
