"""Fork-only, graph-preserving water rebase with a separate publication approval.

Transplant every original snapshot through a three-way merge against the pinned
old/new upstream bases, then recreate each commit with its mapped parents. This
preserves manually resolved integration merges and all originating repair commits.
No branch is moved unless a reviewed approval matches all computed output heads.
"""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

REPOSITORY = 'ParticleTroned/skyrim-community-shaders'
UPSTREAM = 'https://github.com/alandtse/open-shaders.git'
OLD_BASE = 'd64dc05b60d38774f43797b9d4e6c1f357b8b1f8'
NEW_BASE = '34b4b82881d2507ebba26a958f471a7e5f8a7742'
OLD_INTEGRATION = '362160e22b2b7589590ed9489e34bd31100639cd'
INTEGRATION = 'build/os-dev-water-integration-20261101'
DATE = '2026-11-02T19:04:48Z'
BACKUP = 'backup/os-water-pre-dev-34b4b82881-20261102/'
TRANSLATION = 'package/SKSE/Plugins/CommunityShaders/Translations/en.json'
EXPECTED = {
    '04-flowmap': '1dcf4adc6eea60ad2f09a24f16663cb5e1f5429d',
    '02-refraction': 'e3e44eaaa9163b225d113c2704f2b5ce3adad61f',
    '06-cache': '99de91ced50626219fb71f11d23392adb85ae70a',
    '03-child-worldspace': '88b92dd7d9eb5248b0acb83bc0c60a5adae9e09c',
    '05-lifetime': 'dfc440cd215335ab2ae42fbff9812ec93a7fa965',
    '07-flowmap-lock': '3202b55301127f0d96d49aa1a0943f525c7ab684',
    '08-parallax': '87621e7f8a25033a2f33330b71b87152f4e29453',
    '09-history': '76b2b5c42080b4cbf24e21038e3a01f3456f6093',
    '10a-reflection-source': '9e08766a025b4e621cce78fd80eef6080a2b40e9',
    '10b-ssr': '76ce4e459da2d1aa410178befe51aa7defd6b76b',
    '10c-reflection-height': '47b79a46cc0aa4c743510a3e7d044ead639cac53',
    '11a-light-eligibility': '4005635b03664f650062328d851c7502ea0189e0',
    '11b-contact-candidates': '0a1f389f04756f89bfff4cc34f7081096b35162e',
    'shallow-base': '02b839baa6006dfa3012060694a03fc42f63dfde',
    '01-shallow': 'cd7370c1b80e8190ffb92a7edfb93375ff92d09f',
    'base': OLD_BASE,
}
ROOT = Path(os.environ['GITHUB_WORKSPACE'])
SOURCE = ROOT / 'source'
OUT = ROOT / 'water-rebase-output'
OUT.mkdir(exist_ok=True)


def git(*args: str, data: str | None = None, env: dict | None = None) -> str:
    result = subprocess.run(['git', '-C', str(SOURCE), '-c', 'core.hooksPath=/dev/null',
        '-c', 'commit.gpgsign=false', *args], input=data, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
    if result.returncode:
        print(result.stdout, file=sys.stderr)
        print(result.stderr, file=sys.stderr)
        raise RuntimeError(f'git {args[0]} failed ({result.returncode})')
    return result.stdout.rstrip('\n')


def tree(ref: str) -> dict[str, tuple[str, str]]:
    result = {}
    for entry in git('ls-tree', '-r', '-z', ref).split('\0'):
        if entry:
            header, path = entry.split('\t', 1)
            mode, kind, oid = header.split()
            result[path] = (mode, oid)
    return result


def contents(ref: str, path: str) -> str:
    return git('show', f'{ref}:{path}')


def remote_heads(remote: str, *patterns: str) -> dict[str, str]:
    return {ref: sha for sha, ref in (line.split() for line in
        git('ls-remote', '--heads', remote, *patterns).splitlines())}


def commit(tree_sha: str, parents: list[str], message: str, author: tuple[str, str, str]) -> str:
    env = os.environ.copy()
    env.update(GIT_AUTHOR_NAME=author[0], GIT_AUTHOR_EMAIL=author[1], GIT_AUTHOR_DATE=author[2],
        GIT_COMMITTER_NAME='ParticleTroned',
        GIT_COMMITTER_EMAIL='248299730+ParticleTroned@users.noreply.github.com',
        GIT_COMMITTER_DATE=DATE)
    args = ['commit-tree', tree_sha]
    for parent in parents:
        args += ['-p', parent]
    return git(*args, data=message, env=env)


def flatten(value: object, prefix: tuple = ()) -> dict:
    if isinstance(value, dict):
        result = {}
        for key, child in value.items():
            result.update(flatten(child, prefix + (key,)))
        return result
    return {prefix: value}


if os.environ.get('GITHUB_REPOSITORY') != REPOSITORY:
    raise RuntimeError('This workflow is restricted to the user fork.')
if git('rev-parse', 'HEAD') != OLD_INTEGRATION:
    raise RuntimeError('Unexpected checkout.')
if git('remote', 'get-url', 'origin').removesuffix('.git') != 'https://github.com/' + REPOSITORY:
    raise RuntimeError('Unexpected publication remote.')
old_heads = {'work/os-water-' + name + '-20261101': sha for name, sha in EXPECTED.items()}
old_heads[INTEGRATION] = OLD_INTEGRATION
actual = remote_heads('origin', 'refs/heads/work/os-water-*', 'refs/heads/' + INTEGRATION)
if actual != {'refs/heads/' + name: sha for name, sha in old_heads.items()}:
    raise RuntimeError('Water branches changed or unexpected branches appeared; review before retrying.')
if remote_heads(UPSTREAM, 'refs/heads/dev') != {'refs/heads/dev': NEW_BASE}:
    raise RuntimeError('Upstream dev moved; review the new baseline before retrying.')
protected_before = remote_heads('origin', 'refs/heads/main-VR', 'refs/heads/main-SE')
git('fetch', '--no-tags', '--no-recurse-submodules', '--depth=100', 'origin',
    '+refs/heads/work/os-water-*:refs/remotes/origin/work/os-water-*',
    '+refs/heads/' + INTEGRATION + ':refs/remotes/origin/' + INTEGRATION)
git('fetch', '--no-tags', '--no-recurse-submodules', '--depth=100', UPSTREAM,
    NEW_BASE + ':refs/remotes/upstream/water-dev')
git('merge-base', '--is-ancestor', OLD_BASE, NEW_BASE)
base_tree, upstream_tree = tree(OLD_BASE), tree(NEW_BASE)
upstream_paths = {path for path in base_tree.keys() | upstream_tree.keys()
                  if base_tree.get(path) != upstream_tree.get(path)}
original_manifest = json.loads(contents(OLD_INTEGRATION, 'docs/development/water-integration-manifest.json'))
if {row['branch']: row['commit'] for row in original_manifest['branches']} != {
    name: sha for name, sha in old_heads.items() if name not in (INTEGRATION, 'work/os-water-base-20261101')}:
    raise RuntimeError('Source manifest does not match all branch heads.')
original_commits = git('rev-list', '--topo-order', '--reverse', *sorted(set(old_heads.values())),
                       '--not', OLD_BASE).splitlines()
if not 1 <= len(original_commits) <= 150:
    raise RuntimeError('Unexpected water commit range size.')
mapping = {OLD_BASE: NEW_BASE}
audit = []
missing = object()
base_json = flatten(json.loads(contents(OLD_BASE, TRANSLATION)))
upstream_json = flatten(json.loads(contents(NEW_BASE, TRANSLATION)))
for old in original_commits:
    old_tree = tree(old)
    water_paths = {path for path in base_tree.keys() | old_tree.keys()
                   if base_tree.get(path) != old_tree.get(path)}
    overlap = water_paths & upstream_paths
    if overlap - {TRANSLATION}:
        raise RuntimeError(f'Unreviewed upstream overlap at {old}: {sorted(overlap)}')
    merged_tree = git('merge-tree', '--write-tree', '--merge-base=' + OLD_BASE,
                      NEW_BASE, old).splitlines()[0]
    result_tree = tree(merged_tree)
    for path in base_tree.keys() | upstream_tree.keys() | old_tree.keys() | result_tree.keys():
        if path in overlap:
            continue
        expected = upstream_tree.get(path) if path in upstream_paths else old_tree.get(path)
        if result_tree.get(path) != expected:
            raise RuntimeError(f'Unintended source change in {old}: {path}')
    if TRANSLATION in overlap:
        old_json = flatten(json.loads(contents(old, TRANSLATION)))
        result_json = flatten(json.loads(contents(merged_tree, TRANSLATION)))
        for key in base_json.keys() | upstream_json.keys() | old_json.keys() | result_json.keys():
            before, upstream, water = (dictionary.get(key, missing) for dictionary in
                                      (base_json, upstream_json, old_json))
            if upstream != before and water != before and upstream != water:
                raise RuntimeError(f'Conflicting translation edits: {key}')
            expected = upstream if upstream != before else water
            if result_json.get(key, missing) != expected:
                raise RuntimeError(f'Translation lost or changed: {key}')
    old_parents = git('show', '-s', '--format=%P', old).split()
    parents = []
    for parent in old_parents:
        if parent in mapping:
            parents.append(mapping[parent])
        else:
            git('merge-base', '--is-ancestor', parent, NEW_BASE)
            parents.append(parent)
    author_parts = git('show', '-s', '--format=%an%x00%ae%x00%aI', old).split('\0')
    message = git('cat-file', 'commit', old).split('\n\n', 1)[1] + '\n'
    mapping[old] = commit(merged_tree, parents, message, tuple(author_parts))
    audit.append({'before': old, 'after': mapping[old], 'tree': merged_tree,
                  'parents': parents, 'overlap': sorted(overlap)})
    print(f'Replayed {old[:10]} -> {mapping[old][:10]}', flush=True)
new_heads = {name: mapping[old] for name, old in old_heads.items()}
git('switch', '--detach', new_heads[INTEGRATION])
manifest = original_manifest
manifest['preRebaseUpstreamDevCommit'] = OLD_BASE
manifest['upstreamDevCommit'] = NEW_BASE
manifest['preRebaseIntegrationCommit'] = OLD_INTEGRATION
manifest['rebasedIntegrationSourceCommit'] = new_heads[INTEGRATION]
manifest['rebaseDate'] = DATE
manifest['rebaseBackupPrefix'] = BACKUP
manifest['integrationCommit'] = new_heads[INTEGRATION]
for row in manifest['branches']:
    old = row['commit']
    row['preRebaseCommit'] = old
    row['commit'] = mapping[old]
    for key in ('parents', 'requiredParents'):
        if key in row:
            row[key] = [mapping.get(parent, parent) for parent in row[key]]
    row['actualParents'] = git('show', '-s', '--format=%P', row['commit']).split()
    paths = git('diff', '--name-only', '--diff-filter=ACMRT', NEW_BASE, row['commit']).splitlines()
    row['changedFilesSha256'] = {path: hashlib.sha256(subprocess.check_output(
        ['git', '-C', str(SOURCE), 'show', f'{row["commit"]}:{path}'])).hexdigest()
        for path in paths}
    for parent in row.get('requiredParents', row.get('parents', [])):
        git('merge-base', '--is-ancestor', parent, row['commit'])
manifest_path = SOURCE / 'docs/development/water-integration-manifest.json'
manifest_path.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
guide_path = SOURCE / 'docs/development/water-port-branches.md'
guide = guide_path.read_text(encoding='utf-8')
guide = guide.replace('Baseline: `' + OLD_BASE + '`', 'Baseline: `' + NEW_BASE + '`', 1)
for row in manifest['branches']:
    pattern = r'(\| `' + re.escape(row['branch']) + r'` \| `)[0-9a-f]+(` \|)'
    guide, count = re.subn(pattern, lambda m: m[1] + row['commit'][:10] + m[2], guide)
    if count != 1:
        raise RuntimeError('Missing/ambiguous branch table row: ' + row['branch'])
guide += f'''\n## Dev rebase — 2026-11-02\n\nAll fourteen code branches, both dependency bases and the integration are rebased onto `{NEW_BASE}`. The commit graph and prior CI repair commits are preserved. Production water changes are unchanged; upstream translation additions are retained. No 256-step ceiling, blanket Shadow/PortalStrict exclusions or NR runtime DLL is introduced.\n\nPrevious heads are retained under `{BACKUP}` when publication completes. Earlier CI runs and packages refer to the old baseline; use the new integration run as the build/install gate. In-game validation is not established by this rebase.\n'''
guide_path.write_text(guide, encoding='utf-8')
summary = '# Water dev rebase — 2026-11-02\n\n'
summary += f'Old upstream: `{OLD_BASE}`. New upstream: `{NEW_BASE}`.\n\n'
summary += 'All original water and repair commits have been replayed with their dependency/merge topology intact. Every non-upstream-overlapping source file is byte-identical to its old branch snapshot; the only overlap is the translation catalog, whose key/value changes from both sides are preserved.\n\n'
summary += '| Branch | Before | Rebased source |\n|---|---|---|\n'
for name in old_heads:
    summary += f'| `{name}` | `{old_heads[name][:10]}` | `{new_heads[name][:10]}` |\n'
summary += f'\nBackups: `{BACKUP}`. No main-VR/main-SE update or upstream PR is part of publication. Previous successful AIO packages are not packages of this rebased source. New Windows CI and in-game verification remain required.\n'
(SOURCE / 'docs/development/water-rebase-20261102.md').write_text(summary, encoding='utf-8')
doc_paths = ['docs/development/water-integration-manifest.json',
             'docs/development/water-port-branches.md', 'docs/development/water-rebase-20261102.md']
git('add', *doc_paths)
git('diff', '--cached', '--check')
if set(git('diff', '--cached', '--name-only').splitlines()) != set(doc_paths):
    raise RuntimeError('Unexpected documentation commit paths.')
new_heads[INTEGRATION] = commit(git('write-tree'), [new_heads[INTEGRATION]],
    'docs(water): record rebase onto Open Shaders dev 34b4b82881\n',
    ('ParticleTroned', '248299730+ParticleTroned@users.noreply.github.com', DATE))
git('switch', '--detach', new_heads[INTEGRATION])
for name, head in new_heads.items():
    git('merge-base', '--is-ancestor', NEW_BASE, head)
    git('diff', '--check', NEW_BASE, head)
    git('update-ref', 'refs/heads/rebased/' + name, head)
backup_heads = {BACKUP + (name.removeprefix('work/os-water-').removesuffix('-20261101')
                         if name != INTEGRATION else 'integration'): head
                for name, head in old_heads.items()}
report = {'repository': REPOSITORY, 'oldBase': OLD_BASE, 'newBase': NEW_BASE,
          'oldHeads': old_heads, 'newHeads': new_heads, 'backupHeads': backup_heads,
          'upstreamChangedPaths': sorted(upstream_paths), 'commitAudit': audit,
          'productionWaterSourcePreserved': True, 'translationMergeVerified': True,
          'protectedHeadsBefore': protected_before, 'testsPassed': [], 'published': False}
(OUT / 'rebase-report.json').write_text(json.dumps(report, indent=2) + '\n')
for test in ('test_build_shader_cache.py', 'test_verify_shader_cache.py',
             'test_build_fomod_package.py', 'test_scene_settings_catalog_generator.py',
             'test_cmake_shader_staging.py'):
    result = subprocess.run([sys.executable, '-m', 'unittest', 'discover', '-s', 'tests', '-p', test],
        cwd=SOURCE, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (OUT / (test + '.log')).write_text(result.stdout, encoding='utf-8')
    print(result.stdout, flush=True)
    if result.returncode:
        raise RuntimeError('Pre-publication regression suite failed: ' + test)
    report['testsPassed'].append(test)
    (OUT / 'rebase-report.json').write_text(json.dumps(report, indent=2) + '\n')
git('archive', '--format=tar.gz', '--output=' + str(OUT / 'rebased-integration-source.tar.gz'),
    new_heads[INTEGRATION])
for name, head in new_heads.items():
    patch = git('diff', '--binary', NEW_BASE, head) + '\n'
    (OUT / (name.replace('/', '_') + '.patch')).write_text(patch, encoding='utf-8')
(OUT / 'water-rebase-summary.md').write_text(summary, encoding='utf-8')
approval_path = ROOT / 'control/.water-port/water-rebase-approval.json'
if approval_path.exists():
    approval = json.loads(approval_path.read_text())
    if approval != {'newBase': NEW_BASE, 'oldHeads': old_heads, 'newHeads': new_heads}:
        raise RuntimeError('Publication approval does not exactly match reviewed output.')
    if remote_heads(UPSTREAM, 'refs/heads/dev') != {'refs/heads/dev': NEW_BASE}:
        raise RuntimeError('Upstream moved before publication; review again.')
    if remote_heads('origin', *('refs/heads/' + name for name in old_heads)) != {
            'refs/heads/' + name: head for name, head in old_heads.items()}:
        raise RuntimeError('A water branch changed during preparation; nothing is published.')
    if remote_heads('origin', 'refs/heads/' + BACKUP + '*'):
        raise RuntimeError('Backup refs already exist; refusing to overwrite.')
    leases = ['--force-with-lease=refs/heads/' + name + ':' + head for name, head in old_heads.items()]
    leases += ['--force-with-lease=refs/heads/' + name + ':' for name in backup_heads]
    refspecs = [head + ':refs/heads/' + name for name, head in (new_heads | backup_heads).items()]
    git('push', '--atomic', *leases, 'origin', *refspecs)
    verified = remote_heads('origin', *('refs/heads/' + name for name in new_heads | backup_heads))
    if verified != {'refs/heads/' + name: head for name, head in (new_heads | backup_heads).items()}:
        raise RuntimeError('Remote publication verification failed.')
    report['published'] = True
    report['protectedHeadsAfter'] = remote_heads('origin', 'refs/heads/main-VR', 'refs/heads/main-SE')
    print('Published and verified all 17 branch heads and 17 backup refs.', flush=True)
else:
    print('PREVIEW ONLY. No work/integration/backup refs have been updated.', flush=True)
(OUT / 'rebase-report.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps({'newHeads': new_heads, 'published': report['published']}, indent=2))
