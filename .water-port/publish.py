#!/usr/bin/env python3
"""Materialize verified water-port work branches; never open a pull request."""
import base64
import hashlib
import importlib.util
import json
import lzma
import os
from pathlib import Path
import subprocess
import sys

REPOSITORY = 'ParticleTroned/skyrim-community-shaders'
BASE = 'd64dc05b60d38774f43797b9d4e6c1f357b8b1f8'
DONOR = 'f140c878138fe2783a6a4ad40420a3a2a996ccd0'
INTEGRATION = 'build/os-dev-water-integration-20261101'
CONTROL = Path(__file__).resolve().parent
ROOT = Path(os.environ['GITHUB_WORKSPACE'])
SOURCE = ROOT / 'source'
DONOR_ROOT = ROOT / 'donor'
OUTPUT = ROOT / 'water-port-artifacts'
OUTPUT.mkdir(exist_ok=True)
if os.environ.get('GITHUB_REPOSITORY') != REPOSITORY:
    raise RuntimeError('Publishing is restricted to the CSX fork.')
if os.environ.get('GITHUB_REF') != 'refs/heads/build/os-water-preparation-20261101':
    raise RuntimeError('Unexpected preparation branch.')

encoded = ''.join((CONTROL / f'generator-{i}.txt').read_text().strip() for i in range(1, 6))
generator = lzma.decompress(base64.b64decode(encoded, validate=True))
if hashlib.sha256(generator).hexdigest() != 'c9064c9d3929e57512defb291e1ef7301f7ea91139ed2aabf2aaf8b2e20e2272':
    raise RuntimeError('Preparation source digest mismatch.')
generator_path = OUTPUT / 'prepare_water_ports.py'
generator_path.write_bytes(generator)
spec = importlib.util.spec_from_file_location('water_ports', generator_path)
ports = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ports)

# id, parent, applications, type, source-change IDs, commit subject
SPECS = [
 ('04-flowmap', None, ['04'], 'BUG / CORRECTION', [5, 7, 8], 'fix(water): validate flowmaps before activation'),
 ('02-refraction', None, ['02'], 'BUG / CORRECTION', [2, 3], 'fix(water): commit only valid refraction state'),
 ('06-cache', '04-flowmap', ['06'], 'BUG / CORRECTION', [9, 10], 'fix(water): publish complete caches safely'),
 ('03-child-worldspace', None, ['03'], 'CORRECTION', [13, 14], 'fix(water): refresh child-worldspace tile state'),
 ('05-lifetime', None, ['05'], 'CORRECTION', [11, 12], 'fix(water): guard tile ownership transitions'),
 ('07-flowmap-lock', None, ['07'], 'CORRECTION', [6], 'perf(water): narrow flowmap context locking'),
 ('08-parallax', None, ['08a', '08b'], 'CORRECTION / FEATURE', [15, 16], 'feat(water): bound and foveate parallax work'),
 ('09-history', None, ['09'], 'CORRECTION', [26], 'fix(water): guard history render targets'),
 ('10a-reflection-source', None, ['10a'], 'BUG', [18], 'fix(water): select the authored reflection source'),
 ('10b-ssr', '10a-reflection-source', ['10b'], 'CORRECTION', [19], 'fix(water): gate and linearize SSR blending'),
 ('10c-reflection-height', '10a-reflection-source', ['10c'], 'FEATURE', [20], 'feat(water): add height-faded reflection control'),
 ('11a-light-eligibility', None, ['11a'], 'BUG', [22], 'fix(water): respect light water eligibility'),
 ('11b-contact-candidates', '11a-light-eligibility', ['11b'], 'CORRECTION', [23], 'perf(water): budget contact-shadow candidates'),
 ('shallow-base', None, ['04', '02'], 'INTEGRATION', [], 'merge(water): prepare shallow-water prerequisites'),
 ('01-shallow', 'shallow-base', ['01'], 'FEATURE', [4], 'feat(water): restore bounded shallow surfaces'),
]
EXPECTED = {
 '04-flowmap': 'cabc18b2030b1b8535a0d1c04671a04d9c1058c1f8a2125fae0c54c9af39b082',
 '02-refraction': '7b84100cd74432f92b07dfcae50403deef1e44dcdedb469bce92c1c8541f73f0',
 '06-cache': 'c0ceced4bb1d9099fd7ace27080f73ebdbe147c2862d51ac443558149b73575f',
 '03-child-worldspace': '6ab3cd045996011a83ac434a8a1533fb2c37e95d04531ba62b935fef82757315',
 '05-lifetime': '48e70d26949359967c716388ab83947ed109cbd1ff2cca6e886c2a50d46449e2',
 '07-flowmap-lock': 'e956d0929486cfd349e4e97a3003d8314c5c3dac5bc8aab61eb003eeb9aa3a75',
 '08-parallax': 'bdad8bb929515d22a07f309a51984dcd065f031a96a75ece986885a67de8d17b',
 '09-history': '20e7a9ff268c116ee91a5a2d5147192b06688f0b6865e00f45bfa52bb7115980',
 '10a-reflection-source': '071f1092f08b6cf01ab3f471d6117bd0bf4262fca7a94e6f969b3c2232e25e39',
 '10b-ssr': '234610d3e10c30551da510b52231cbfe52c41dc0e7eb3b535969cd1b1b2d44b9',
 '10c-reflection-height': 'c46eac05399094f6eeaac0ca9c366586828952ce2e99a815d79ea9005cdb21e0',
 '11a-light-eligibility': 'fdf2756f0b36d05016b598f48eff9f2c688113423c689de04252da173c218b43',
 '11b-contact-candidates': '2d6c8c3f9230990d97f045952f2d528b805d89a9daccccdb1975aea68b1ddeb6',
 'shallow-base': 'abbf624b7e45f9307a01b2653ff60605a095786b9c12f73304794fd13ede1537',
 '01-shallow': '02b759e7fe0935627b073a114772e69bff5cf32f89235cab73678b9d1e1cc2a5',
 'integration': 'cc150bf649b673fe4a154625541a00d18b17ed5ce040070078481c53194a35cb',
}

def git(*args, input=None):
    return subprocess.check_output(['git', '-C', str(SOURCE), *args], input=input, text=True).strip()

def name(identifier):
    return f'work/os-water-{identifier}-20261101'

def prepare(parent, groups):
    git('checkout', '--detach', parent)
    tree = ports.Tree(SOURCE, DONOR_ROOT)
    for group in groups:
        getattr(ports, 'group' + group)(tree)
    subprocess.run([sys.executable, str(SOURCE / 'tools/extract-i18n.py'), '--write'], check=True)
    git('add', '-A')
    git('diff', '--cached', '--check', parent)

def verify(identifier, parent):
    paths = git('diff', '--cached', '--name-only', '-z', parent).split('\0')
    files = {path: hashlib.sha256((SOURCE / path).read_bytes()).hexdigest() for path in paths if path}
    actual = hashlib.sha256(json.dumps(files, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
    if actual != EXPECTED[identifier]:
        (OUTPUT / f'{identifier}-unexpected.json').write_text(json.dumps(files, indent=2))
        raise RuntimeError(f'{identifier}: generated tree does not match locally verified source ({actual})')
    return files

def commit(subject, parents, body):
    tree = git('write-tree')
    args = ['commit-tree', tree]
    for parent in parents:
        args.extend(['-p', parent])
    return git(*args, input=subject + '\n\n' + body + '\n')

if git('rev-parse', 'HEAD') != BASE:
    raise RuntimeError('Unexpected upstream baseline.')
actual_donor = subprocess.check_output(['git', '-C', str(DONOR_ROOT), 'rev-parse', 'HEAD'], text=True).strip()
if actual_donor != DONOR:
    raise RuntimeError('Unexpected donor baseline.')
if git('remote', 'get-url', 'origin').rstrip('/') not in {
    f'https://github.com/{REPOSITORY}', f'https://github.com/{REPOSITORY}.git'}:
    raise RuntimeError('Origin is not the authorized fork.')
git('config', 'user.name', 'ParticleTroned')
git('config', 'user.email', '248299730+ParticleTroned@users.noreply.github.com')
git('config', 'core.autocrlf', 'false')
refs = [name(row[0]) for row in SPECS] + [INTEGRATION]
if git('ls-remote', '--heads', 'origin', *['refs/heads/' + ref for ref in refs]):
    raise RuntimeError('A destination branch exists; refusing to overwrite it.')

heads = {}
manifest = {'repository': REPOSITORY, 'upstreamDevCommit': BASE, 'donorCommit': DONOR,
            'branches': [], 'status': 'Prepared source; Windows CI and in-game validation still required.'}
for identifier, parent_id, groups, kind, source_ids, title in SPECS:
    parent = heads[parent_id] if parent_id else BASE
    prepare(parent, groups)
    files = verify(identifier, parent)
    parents = [heads['04-flowmap'], heads['02-refraction']] if identifier == 'shallow-base' else [parent]
    body = (f'Type: {kind}. Source-change IDs: {source_ids}.\n'
            f'Port reference: {DONOR}.\n'
            'Prepared against the pinned Open Shaders baseline for fork-only testing.\n'
            'No upstream PR is opened. Runtime reproduction is not claimed.\n'
            'Preserve reverse-Z, the 64-step water cap and clustered shadow ownership.')
    sha = commit(title, parents, body)
    branch = name(identifier)
    git('update-ref', 'refs/heads/' + branch, sha, '0' * 40)
    heads[identifier] = sha
    manifest['branches'].append({'id': identifier, 'branch': branch, 'commit': sha,
                                 'parents': parents, 'type': kind, 'sourceChanges': source_ids,
                                 'changedFilesSha256': files})
    patch = git('diff', '--binary', parent, sha)
    (OUTPUT / f'{identifier}.patch').write_text(patch + '\n')
    print(branch, sha, flush=True)
    # commit-tree does not move detached HEAD; make the next checkout clean.
    git('reset', '--hard', sha)

prepare(BASE, ['04', '06', '02', '01', '03', '05', '07', '08a', '08b', '09', '10a', '10b', '10c', '11a', '11b'])
manifest['integrationChangedFilesSha256'] = verify('integration', BASE)
doc = SOURCE / 'docs/development/water-port-branches.md'
doc.parent.mkdir(parents=True, exist_ok=True)
lines = ['# Fork-only water port work branches', '', f'Baseline: `{BASE}`. Donor: `{DONOR}`.', '',
         'No upstream PRs. These are preparation branches, not merge-approved changes.', '',
         '| Work branch | Commit | Type | Required parent |', '|---|---|---|---|']
for row in manifest['branches']:
    parent_names = [next((b['id'] for b in manifest['branches'] if b['commit'] == p), 'pinned baseline') for p in row['parents']]
    lines.append(f"| `{row['branch']}` | `{row['commit'][:10]}` | {row['type']} | {', '.join(parent_names)} |")
lines.extend(['', '## Scope and test controls', '',
 'Group 1: set Unified Water / Shallow Surface Strength above 0 to test; default 0 preserves the existing appearance.',
 'Group 8: enable Foveated Water Parallax with an active VR foveation profile. Disabled/unavailable masks retain full detail. No path may exceed 64 steps.',
 'Group 10c: Water Reflection Strength -1 preserves current output. Nonnegative values enable height-faded strength.',
 'Group 11b: Water Contact Candidates 0 retains the existing loop. Values 1-8 enable the experimental shared per-cluster candidate pass; shadow-map contributions remain untouched.',
 '', '## Dependencies', '',
 '04 -> 06. Both 04 and 02 -> shallow-base -> 01. 10a -> 10b and 10c. 11a -> 11b. Other work branches start independently from the pinned baseline.',
 'The integration commit has the work-branch tips as parents and contains their verified combined source tree; it is not an upstream PR.',
 '', '## Deliberately excluded or narrowed', '',
 'Source 1 (an extra scene-depth copy) remains deferred until a skipped-deferred failure is demonstrated. The depth debug toggle is unchanged.',
 'Group 3 is a transition-test candidate, not a reproduced upstream bug. Plane refresh is restricted to generated horizontal tiles.',
 'Group 5 adds appended-object guards and duplicate occurrences of the current node; it does not add broad global cleanup scans.',
 'Group 6 keeps startup synchronous. Runtime cache publication runs on the game thread and failed/incomplete results retain the previous runtime snapshot.',
 'Group 7 keeps immediate-context execution and saving/readback protected. Only source loading/conversion and deferred recording move outside that scope. Profile generation-time contention.',
 'Group 11b adapts candidate budgeting to existing upstream grid padding rather than importing CSX global buffers. Its extra compute cost must be measured; no performance gain is claimed. Separate VR contact-shadow detail gating is not imported.',
 'No 256-step ceiling, blanket Shadow/PortalStrict exclusions, NR runtime DLL, version bump or release tag is added.',
 '', '## Validation required before upstream submission', '',
 'Run Windows plugin compilation, full/release-parity SE/VR shader compilation and shader/C++ unit tests. Then test in-game: corrupt/missing flowmap; failed cache generation and restart retry; child-worldspace and interior/map transitions; shoreline/deep-water output; both VR eyes and render scales; planar/cubemap/SSR permutations; eligible and ineligible engine lights plus particles; candidate budgets 0 and 1-8.',
 'Check the D3D debug layer and paired GPU timings. Defaults being unchanged does not replace these tests.', ''])
doc.write_text('\n'.join(lines), encoding='utf-8')
manifest_path = SOURCE / 'docs/development/water-integration-manifest.json'
manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
git('add', 'docs/development/water-port-branches.md', 'docs/development/water-integration-manifest.json')
parents = [BASE] + [heads[row[0]] for row in SPECS if row[0] != 'shallow-base']
integration_sha = commit('build(water): integrate fork-only water ports', parents,
    'Combine the independently reviewable branches for SE/AE/VR testing.\n'
    'This commit records explicit integration resolution and provenance.\n'
    'No upstream PR, default-branch change or external NR DLL redistribution.')
git('update-ref', 'refs/heads/' + INTEGRATION, integration_sha, '0' * 40)
manifest['integrationCommit'] = integration_sha
(OUTPUT / 'publication-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
(OUTPUT / 'water-port-branches.md').write_text(doc.read_text())
refspecs = [f'refs/heads/{ref}:refs/heads/{ref}' for ref in refs]
git('push', '--atomic', 'origin', *refspecs)
remote = git('ls-remote', '--heads', 'origin', *['refs/heads/' + ref for ref in refs])
observed = {line.split()[1]: line.split()[0] for line in remote.splitlines()}
for ref in refs:
    expected = integration_sha if ref == INTEGRATION else heads[next(row[0] for row in SPECS if name(row[0]) == ref)]
    if observed.get('refs/heads/' + ref) != expected:
        raise RuntimeError('Remote verification failed: ' + ref)
git('bundle', 'create', str(OUTPUT / 'water-work-branches.bundle'), '^' + BASE,
    *['refs/heads/' + ref for ref in refs])
print('Published and verified', len(refs), 'fork branches. Integration:', integration_sha)
