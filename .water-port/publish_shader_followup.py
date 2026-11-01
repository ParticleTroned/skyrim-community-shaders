#!/usr/bin/env python3
"""Publish the narrowly scoped follow-up to water shader CI, fork only."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

REPO = 'ParticleTroned/skyrim-community-shaders'
INTEGRATION = 'build/os-dev-water-integration-20261101'
OLD = 'a72099c42e5c404e5942f5b69ae9b52880896441'
ROOT = Path(os.environ['GITHUB_WORKSPACE'])
SOURCE = ROOT / 'source'
OUT = ROOT / 'water-shader-followup'
OUT.mkdir(exist_ok=True)
if os.environ.get('GITHUB_REPOSITORY') != REPO or os.environ.get('GITHUB_REF') != 'refs/heads/build/os-water-preparation-20261101':
    raise RuntimeError('Refusing publication outside the authorized fork/preparation branch.')

TESTS = {
 'package/Shaders/Tests/TestWaterFinite.hlsl': {
  '0x80000000u': '(1u << 31)',
  '0xff7fffffu': '((1u << 31) | 0x7f7fffffu)',
  '0xff800000u': '((1u << 31) | 0x7f800000u)',
  '0xffc00001u': '((1u << 31) | 0x7fc00001u)',
 },
 'package/Shaders/Tests/TestWaterParallaxMath.hlsl': {
  '0xff800000u': '((1u << 31) | 0x7f800000u)',
  '0xffc00001u': '((1u << 31) | 0x7fc00001u)',
 },
 'package/Shaders/Tests/TestWaterRefraction.hlsl': {
  '0xff800000u': '((1u << 31) | 0x7f800000u)',
 },
 'package/Shaders/Tests/TestWaterContactCandidates.hlsl': {'0xffffffffu': '~0u'},
 'package/Shaders/Common/WaterContactCandidates.hlsli': {'0xffffffffu': '~0u'},
}
CAND = 'features/Light Limit Fix/Shaders/LightLimitFix/WaterContactCandidatesCS.hlsl'
EXPECTED = {
 'package/Shaders/Tests/TestWaterFinite.hlsl': '8ff50b50f29aaa049b756c07529e76d5601bff4df962d130a377f6b69d15f243',
 'package/Shaders/Tests/TestWaterParallaxMath.hlsl': '93671fb6f95a176debd30984d0891700f54704be6b13cf6414e9baf88bb39462',
 'package/Shaders/Tests/TestWaterRefraction.hlsl': '31279503ea5ac9c36bef16b0a691f5a7b6248e09fb4ff452b39aa41b5cf739f4',
 'package/Shaders/Tests/TestWaterContactCandidates.hlsl': '47eae8117ef01ce5ae690940eefe5e4c446f030149b418f4f52e92aa4685ec85',
 'package/Shaders/Common/WaterContactCandidates.hlsli': '46fc7c00afb55e4b5bd56e52da4c5ff45f36e99113c73eb46080fae50ed5eae6',
 CAND: 'dc6d85af1594857323de05745af67620dec385dd8eb6ac1d09c4d7090b703320',
}

def git(*args, input=None):
    return subprocess.check_output(['git', '-C', str(SOURCE), *args], input=input, text=True).strip()

def sha256(path):
    return hashlib.sha256((SOURCE / path).read_bytes()).hexdigest()

def repair():
    changed = []
    for name, replacements in TESTS.items():
        path = SOURCE / name
        if not path.exists():
            continue
        text = path.read_text()
        for before, after in replacements.items():
            if before not in text:
                raise RuntimeError(f'{name}: missing expected literal {before}')
            text = text.replace(before, after)
        path.write_text(text, encoding='utf-8', newline='\n')
        changed.append(name)
    path = SOURCE / CAND
    if path.exists():
        text = path.read_text()
        spans = [
            ('uint2 packed = WaterContactCandidates::Empty();', '\n\tgrid.pad0[0]', 'packSlot'),
            ('\t\t[unroll] for (uint slot', '\n\t}\n\tuint2 packed', 'rankSlot'),
            ('\t[unroll] for (uint slot', '\n\tuint count', 'initSlot'),
        ]
        for begin, end, var in spans:
            i = text.index(begin)
            j = text.index(end, i)
            text = text[:i] + re.sub(r'\bslot\b', var, text[i:j]) + text[j:]
        path.write_text(text, encoding='utf-8', newline='\n')
        changed.append(CAND)
    if not changed:
        raise RuntimeError('No expected source edits found.')
    for path in changed:
        if sha256(path) != EXPECTED[path]:
            raise RuntimeError(f'{path}: repaired output differs from the reviewed hash')
    git('add', *changed)
    git('diff', '--cached', '--check')
    staged = {path for path in git('diff', '--cached', '--name-only', '-z').split('\0') if path}
    if staged != set(changed):
        raise RuntimeError(f'Unexpected staged paths: {staged ^ set(changed)}')
    return {path: sha256(path) for path in changed}

def commit(subject, parents, body):
    args = ['commit-tree', git('write-tree')]
    for parent in parents:
        args += ['-p', parent]
    return git(*args, input=subject + '\n\n' + body + '\n')

if git('rev-parse', 'HEAD') != OLD:
    raise RuntimeError('Unexpected integration starting commit.')
if git('remote', 'get-url', 'origin').rstrip('/') not in {f'https://github.com/{REPO}', f'https://github.com/{REPO}.git'}:
    raise RuntimeError('Origin must be the authorized fork.')
git('config', 'user.name', 'ParticleTroned')
git('config', 'user.email', '248299730+ParticleTroned@users.noreply.github.com')
git('config', 'core.autocrlf', 'false')
manifest_path = SOURCE / 'docs/development/water-integration-manifest.json'
manifest = json.loads(manifest_path.read_text())
rows = {row['id']: row for row in manifest['branches']}
oldheads = {key: row['commit'] for key, row in rows.items()}
heads = dict(oldheads)
plans = [
    ('02-refraction', []),
    ('08-parallax', []),
    ('10a-reflection-source', []),
    ('10b-ssr', ['10a-reflection-source']),
    ('10c-reflection-height', ['10a-reflection-source']),
    ('11b-contact-candidates', []),
    ('shallow-base', ['02-refraction']),
    ('01-shallow', ['shallow-base']),
]
allrefs = {f"refs/heads/{row['branch']}": row['commit'] for row in rows.values()}
allrefs[f'refs/heads/{INTEGRATION}'] = OLD
observed = dict(line.split()[::-1] for line in git('ls-remote', '--heads', 'origin', *allrefs).splitlines())
if observed != allrefs:
    raise RuntimeError('A reviewed branch moved; refusing to overwrite changes.')
for key, _ in plans:
    branch = rows[key]['branch']
    git('fetch', '--no-tags', '--depth=1', 'origin', f'refs/heads/{branch}:refs/remotes/origin/{branch}')
report = {'repository': REPO, 'failedRun': 41302393633, 'previousIntegration': OLD, 'updates': []}
for key, deps in plans:
    row = rows[key]
    old = oldheads[key]
    git('checkout', '--detach', old)
    changed = repair()
    parents = [old] + [heads[dep] for dep in deps]
    sha = commit('fix(water): make shader literals portable', parents,
        f'Follow-up to {old}.\n'
        'Resolve the remaining DXC legacy-literal diagnostics from run 41302393633.\n'
        'Use unsigned sign-bit expressions and all-ones sentinels; rename candidate\n'
        'loop indices for FXC. Preserve warning gates, test coverage and the 64-step cap.')
    git('update-ref', f"refs/heads/{row['branch']}", sha, '0' * 40)
    heads[key] = sha
    row['commit'] = sha
    row.setdefault('followupHistory', []).append({'before': old, 'after': sha, 'parents': parents, 'files': changed})
    row['changedFilesSha256'].update(changed)
    report['updates'].append({'id': key, 'branch': row['branch'], 'before': old, 'after': sha, 'parents': parents, 'files': changed})
    (OUT / (key + '.patch')).write_bytes(subprocess.check_output(['git', '-C', str(SOURCE), 'diff', '--binary', old, sha]))
    git('reset', '--hard', sha)

git('checkout', '--detach', OLD)
changed = repair()
manifest['status'] = 'Shader compiler compatibility follow-up applied; final validation pending.'
manifest['integrationChangedFilesSha256'].update(changed)
manifest.setdefault('ciFollowups', []).append(report.copy())
manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
doc = SOURCE / 'docs/development/water-port-branches.md'
text = doc.read_text()
for key, old in oldheads.items():
    if heads[key] != old:
        text = text.replace(f'`{old[:10]}`', f'`{heads[key][:10]}`')
text += '\nShader follow-up: all SE/VR water permutations compiled cleanly in both depth conventions in run 41302393633. Its remaining candidate-loop warning and DXC high-bit literal test failures are addressed without changing compiler flags or warning limits. Final validation must still pass on this follow-up.\n'
doc.write_text(text)
git('add', 'docs/development/water-integration-manifest.json', 'docs/development/water-port-branches.md')
git('diff', '--cached', '--check')
sha = commit('fix(water): integrate shader CI follow-ups', [OLD] + [heads[key] for key, _ in plans],
    f'Follow-up to {OLD}.\n'
    'Merge the branch-local compatibility fixes and refresh source provenance.\n'
    'Keep all validation gates, package restrictions and existing branch history.')
git('update-ref', f'refs/heads/{INTEGRATION}', sha, '0' * 40)
report['integrationCommit'] = sha
(OUT / 'publication.json').write_text(json.dumps(report, indent=2) + '\n')
(OUT / 'integration.patch').write_bytes(subprocess.check_output(['git', '-C', str(SOURCE), 'diff', '--binary', OLD, sha]))
branches = [rows[key]['branch'] for key, _ in plans] + [INTEGRATION]
git('push', '--atomic', 'origin', *[f'refs/heads/{branch}:refs/heads/{branch}' for branch in branches])
observed = dict(line.split()[::-1] for line in git('ls-remote', '--heads', 'origin', *[f'refs/heads/{branch}' for branch in branches]).splitlines())
for key, _ in plans:
    if observed.get('refs/heads/' + rows[key]['branch']) != heads[key]:
        raise RuntimeError('Remote work-branch verification failed.')
if observed.get('refs/heads/' + INTEGRATION) != sha:
    raise RuntimeError('Remote integration verification failed.')
print(json.dumps(report, indent=2))
