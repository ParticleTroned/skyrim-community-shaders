"""Package pinned N2 model catalogs as confined, hash-addressed AIO payloads."""
from pathlib import Path, PureWindowsPath
import argparse
import json
import re

from clone_model import semantic
from preserved_elf import require, sha

CATALOGS = {
    'cloned': '2b11eb5028bbdbb3b539be8789f10052868f46b5c256bdc16c1003a3e7f8d76e',
    'shared': '094ecf56151133f64e9c57553d5aabf62fd4bea615d4e75b071301005b804597',
}
MAX_MODULE_BYTES = 32 * 1024 * 1024
MAX_PAYLOAD_BYTES = 128 * 1024 * 1024


def digest(value):
    require(isinstance(value, str) and re.fullmatch('[0-9a-f]{64}', value), 'noncanonical SHA256')
    return value


def source_path(value, manifest):
    require(isinstance(value, str) and value, 'module path is missing')
    path = Path(value)
    if path.is_absolute():
        return path.resolve(strict=True)
    require(re.fullmatch(r'[0-9a-f]{64}\.cubin', value) and not PureWindowsPath(value).drive,
            'relative module path must be a hash-addressed basename')
    resolved = (manifest.parent / value).resolve(strict=True)
    require(resolved.parent == manifest.parent.resolve(), 'relative module path escapes catalog directory')
    return resolved


def read_catalog(path, role):
    path = Path(path).resolve(strict=True)
    require(path.stat().st_size <= 1024 * 1024, 'catalog file exceeds bound')
    raw = path.read_bytes()
    manifest = json.loads(raw)
    require(isinstance(manifest, dict) and set(manifest) == {'schema', 'batchCount', 'modules'} and
            manifest['schema'] == 'nr-model-kernel-replacement-v1' and type(manifest['batchCount']) is int and
            manifest['batchCount'] == 2 and isinstance(manifest['modules'], list) and len(manifest['modules']) == 9,
            'N2 catalog schema or module count differs')
    prior, names, blobs = '', set(), {}
    for module in manifest['modules']:
        require(isinstance(module, dict) and set(module) == {'originalSha256', 'sha256', 'path', 'functions'},
                'module schema differs')
        original, candidate = digest(module['originalSha256']), digest(module['sha256'])
        require(original > prior, 'original modules are duplicate or unsorted')
        prior = original
        require(isinstance(module['functions'], list) and 1 <= len(module['functions']) <= 44, 'module function count differs')
        previous_name = ''
        for function in module['functions']:
            require(isinstance(function, dict) and set(function) == {'name', 'paramSize', 'gridZ'}, 'function schema differs')
            name = function['name']
            require(isinstance(name, str) and re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]{0,255}', name) and
                    name > previous_name and name not in names, 'function identity is duplicate, invalid or unsorted')
            require(type(function['paramSize']) is int and 0 < function['paramSize'] <= 2048 and function['paramSize'] % 8 == 0 and
                    type(function['gridZ']) is int and function['gridZ'] in (1, 2, 4), 'function packet or grid ABI differs')
            previous_name = name
            names.add(name)
    require(len(names) == 44 and semantic(manifest) == CATALOGS[role], 'catalog semantic identity or function count differs')
    total = 0
    for module in manifest['modules']:
        candidate = module['sha256']
        module_path = source_path(module['path'], path)
        size = module_path.stat().st_size
        total += size
        require(0 < size <= MAX_MODULE_BYTES and total <= MAX_PAYLOAD_BYTES, 'module file or catalog exceeds bound')
        blob = module_path.read_bytes()
        require(sha(blob) == candidate and len(blob) >= 64 and blob[:4] == b'\x7fELF', 'module identity or ELF format differs')
        require(candidate not in blobs or blobs[candidate] == blob, 'module hash collision')
        blobs[candidate] = blob
    require(sum(map(len, blobs.values())) <= MAX_PAYLOAD_BYTES, 'catalog payload exceeds bound')
    return manifest, blobs, dict(sourceManifestSha256=sha(raw), semanticSha256=CATALOGS[role])


def package(cloned, shared, output):
    output = Path(output).resolve()
    require(not output.exists(), 'output must be new; preserve existing payloads')
    sources = {role: read_catalog(path, role) for role, path in (('cloned', cloned), ('shared', shared))}
    blobs = {}
    for _, values, _ in sources.values():
        for key, blob in values.items():
            require(key not in blobs or blobs[key] == blob, 'cross-catalog module collision')
            blobs[key] = blob
    require(sum(map(len, blobs.values())) <= MAX_PAYLOAD_BYTES, 'combined payload exceeds bound')
    output.mkdir(parents=True, exist_ok=False)
    report = dict(schema='nr-kernel-catalog-package-v1', status='incomplete', gpuExecuted=False, catalogs={}, modules=[])
    try:
        for key, blob in sorted(blobs.items()):
            filename = key + '.cubin'
            destination = output / filename
            with destination.open('xb') as stream:
                stream.write(blob)
            require(sha(destination.read_bytes()) == key, 'packaged module write verification failed')
            report['modules'].append(dict(path=filename, sha256=key, bytes=len(blob)))
        for role, (manifest, _, receipt) in sources.items():
            value = dict(manifest, modules=[dict(module, path=module['sha256'] + '.cubin') for module in manifest['modules']])
            require(semantic(value) == CATALOGS[role], 'portable paths changed catalog semantics')
            filename = role + '-n2.json'
            encoded = (json.dumps(value, indent=2) + '\n').encode()
            with (output / filename).open('xb') as stream:
                stream.write(encoded)
            report['catalogs'][role] = dict(receipt, path=filename, manifestSha256=sha(encoded))
        report['status'] = 'complete'
    except BaseException as error:
        report.update(status='failed', error=f'{type(error).__name__}: {error}')
        raise
    finally:
        (output / 'package-receipt.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cloned', type=Path, required=True)
    parser.add_argument('--shared', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = package(args.cloned, args.shared, args.output)
    print(json.dumps(report['catalogs'], indent=2))


if __name__ == '__main__':
    main()
