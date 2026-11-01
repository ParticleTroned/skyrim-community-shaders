#!/usr/bin/env python3
"""Fast-forward only the allowlisted water branches and their integration."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

from repair_sources import Edit, FIXES

REPO='ParticleTroned/skyrim-community-shaders'
PREP='refs/heads/build/os-water-preparation-20261101'
INTEGRATION='build/os-dev-water-integration-20261101'
OLD_INTEGRATION='d0840e116ebc4071834177497152cf865af63985'
BASE='d64dc05b60d38774f43797b9d4e6c1f357b8b1f8'
ROOT=Path(os.environ['GITHUB_WORKSPACE'])
SOURCE=ROOT/'source'
CONTROL=Path(__file__).resolve().parent
OUT=ROOT/'water-ci-repairs'
OUT.mkdir(exist_ok=True)
if os.environ.get('GITHUB_REPOSITORY')!=REPO or os.environ.get('GITHUB_REF')!=PREP:
    raise RuntimeError('Repair publication is restricted to the authorized fork/preparation branch.')

def git(*args, input=None):
    return subprocess.check_output(['git','-C',str(SOURCE),*args],input=input,text=True).strip()

def rawgit(*args):
    return subprocess.check_output(['git','-C',str(SOURCE),*args])

def hashes(paths):
    return {p:hashlib.sha256((SOURCE/p).read_bytes()).hexdigest() for p in sorted(paths) if p}

if git('rev-parse','HEAD')!=OLD_INTEGRATION:
    raise RuntimeError('Unexpected integration starting point.')
if git('remote','get-url','origin').rstrip('/') not in {f'https://github.com/{REPO}',f'https://github.com/{REPO}.git'}:
    raise RuntimeError('Refusing a non-fork origin.')
git('config','user.name','ParticleTroned')
git('config','user.email','248299730+ParticleTroned@users.noreply.github.com')
git('config','core.autocrlf','false')
manifest=json.loads((SOURCE/'docs/development/water-integration-manifest.json').read_text())
rows={r['id']:r for r in manifest['branches']}
expected=json.loads((CONTROL/'repair-expectations.json').read_text())
oldheads={key:row['commit'] for key,row in rows.items()}
heads=dict(oldheads)

# Identifiers, source repairs, and updated required parents (a recorded merge).
plans=[
 ('04-flowmap',['04-flowmap'],[]),
 ('02-refraction',['02-refraction'],[]),
 ('06-cache',['04-flowmap'],['04-flowmap']),
 ('03-child-worldspace',['03-child-worldspace'],[]),
 ('08-parallax',['08-parallax'],[]),
 ('09-history',['09-history'],[]),
 ('10a-reflection-source',['10a-reflection-source'],[]),
 ('10b-ssr',['10a-reflection-source'],['10a-reflection-source']),
 ('10c-reflection-height',['10a-reflection-source'],['10a-reflection-source']),
 ('11b-contact-candidates',['11b-contact-candidates'],[]),
 ('shallow-base',['04-flowmap','02-refraction'],['04-flowmap','02-refraction']),
 ('01-shallow',['04-flowmap','02-refraction','01-shallow'],['shallow-base']),
]
subjects={
 '04-flowmap':'fix(water): bridge flowmap D3D types',
 '02-refraction':'fix(water): preserve finite refraction guards',
 '03-child-worldspace':'fix(water): pass raw nodes to culling helpers',
 '08-parallax':'fix(water): retain optimized finite checks',
 '09-history':'fix(water): compare target bounds consistently',
 '10a-reflection-source':'fix(water): preserve finite reflection guards',
 '11b-contact-candidates':'fix(water): type candidate sentinels explicitly',
 '01-shallow':'fix(water): retain finite shallow-depth guards',
}
branches=[rows[key]['branch'] for key,_,_ in plans]+[INTEGRATION]
# Verify all work refs, including unchanged ones referenced by the manifest.
allrefs={f"refs/heads/{r['branch']}":r['commit'] for r in rows.values()}
allrefs[f'refs/heads/{INTEGRATION}']=OLD_INTEGRATION
observed=dict(line.split()[::-1] for line in git('ls-remote','--heads','origin',*allrefs).splitlines())
if observed!=allrefs:
    raise RuntimeError('A source branch moved since review; refusing to overwrite it.')
# These shallow fetches acquire exact tips and their trees without pulling all fork history.
for key,_,_ in plans:
    branch=rows[key]['branch']
    git('fetch','--no-tags','--depth=1','origin',f'refs/heads/{branch}:refs/remotes/origin/{branch}')
git('fetch','--no-tags','--depth=1','origin',BASE)

def stage_and_verify(key, old):
    git('add','-A')
    git('diff','--cached','--check',old)
    paths=git('diff','--cached','--name-only','-z',old).split('\0')
    actual=hashes(paths)
    if actual!=expected[key]:
        (OUT/(key+'-unexpected.json')).write_text(json.dumps(actual,indent=2))
        raise RuntimeError(f'{key}: repair differs from the locally reviewed file hashes')
    return actual

def commit(subject,parents,body):
    args=['commit-tree',git('write-tree')]
    for parent in parents:args.extend(['-p',parent])
    return git(*args,input=subject+'\n\n'+body+'\n')

report={'repository':REPO,'originalIntegration':OLD_INTEGRATION,'failedRun':41299604824,'updates':[]}
for key,fixes,deps in plans:
    row=rows[key];old=oldheads[key]
    git('checkout','--detach',old)
    editor=Edit(SOURCE)
    for fix in fixes:FIXES[fix](editor)
    actual=stage_and_verify(key,old)
    parents=[old]+[heads[dep] for dep in deps]
    body=(f'Follow-up to {old}.\n'
          'Repair the water integration failures from run 41299604824.\n'
          'Retain strict compiler warnings, shader tests and the 64-step cap.\n'
          'Keep existing branch history; publish only in the CSX fork.\n')
    if deps:body+='Merge repaired prerequisites: '+', '.join(deps)+'.\n'
    sha=commit(subjects.get(key,'merge(water): carry validated source repairs'),parents,body)
    git('update-ref',f"refs/heads/{row['branch']}",sha,'0'*40)
    heads[key]=sha
    row['originalCommit']=old
    row['commit']=sha
    row['repairParents']=parents
    row['requiredParents']=[heads.get(dep,oldheads.get(dep)) for dep in deps] if deps else row['parents']
    row['repairFilesSha256']=actual
    # Preserve original scope file inventory while updating its content hashes.
    for path in row['changedFilesSha256']:
        row['changedFilesSha256'][path]=hashlib.sha256((SOURCE/path).read_bytes()).hexdigest()
    row['changedFilesSha256'].update(actual)
    report['updates'].append({'id':key,'branch':row['branch'],'before':old,'after':sha,'parents':parents,'files':actual})
    (OUT/(key+'.patch')).write_bytes(rawgit('diff','--binary','--no-ext-diff',old,sha))
    print(row['branch'],sha,flush=True)
    git('reset','--hard',sha)

# Apply exactly the same changes to the already-resolved combined source tree.
git('checkout','--detach',OLD_INTEGRATION)
editor=Edit(SOURCE)
for key in ['04-flowmap','03-child-worldspace','09-history','02-refraction','08-parallax','10a-reflection-source','01-shallow','11b-contact-candidates']:
    FIXES[key](editor)
stage_and_verify('integration',OLD_INTEGRATION)
manifest['repairOfIntegration']=OLD_INTEGRATION
manifest['status']='CI repairs applied; rerun Windows, SE/VR shader and unit validation. In-game validation is not established.'
manifest['repairRun']=41299604824
source_paths=set(manifest['integrationChangedFilesSha256'])|set(expected['integration'])
manifest['integrationChangedFilesSha256']=hashes(source_paths)
manifest['ciRepairUpdates']=report['updates']
(SOURCE/'docs/development/water-integration-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
doc=SOURCE/'docs/development/water-port-branches.md'
text=doc.read_text()
for key,old in oldheads.items():
    if heads[key]!=old:text=text.replace(f'`{old[:10]}`',f'`{heads[key][:10]}`')
text+='''
## CI repair follow-up

The failed integration run 41299604824 is followed by source fixes in the originating work branches and fast-forward merges into their dependents. No branch history was rewritten.

Repairs cover SDK/REX D3D type bridging (4), explicit node pointers (3), signed target bounds (9), FXC-safe finite checks and initialized refraction fallback (2, 8, 10a, 1), and unsigned candidate sentinels (11b). Shared finite helpers have direct shader tests. The 64-step cap, reverse-Z handling, strict warning gates and runtime-DLL exclusion remain unchanged.

Windows compilation and SE/VR shader validation must pass on the new integration commit before installation. In-game correctness and performance still require the existing test matrix.
'''
doc.write_text(text)
git('add',str(doc.relative_to(SOURCE)),'docs/development/water-integration-manifest.json')
git('diff','--cached','--check',OLD_INTEGRATION)
parents=[OLD_INTEGRATION]+[heads[key] for key,_,_ in plans]
sha=commit('fix(water): integrate branch CI repairs',parents,
    f'Follow-up to {OLD_INTEGRATION}.\n'
    'Merge the repaired work branches without rewriting published history.\n'
    'Update package provenance and retain all validation and scope limits.\n'
    'No upstream writes, pull requests, releases or main-VR changes.')
git('update-ref',f'refs/heads/{INTEGRATION}',sha,'0'*40)
report['integrationCommit']=sha
(OUT/'repair-publication.json').write_text(json.dumps(report,indent=2)+'\n')
(OUT/'integration-repair.patch').write_bytes(rawgit('diff','--binary',OLD_INTEGRATION,sha))
# No force options: a concurrent branch change makes the entire push fail.
refspecs=[f'refs/heads/{branch}:refs/heads/{branch}' for branch in branches]
git('push','--atomic','origin',*refspecs)
observed=dict(line.split()[::-1] for line in git('ls-remote','--heads','origin',*[f'refs/heads/{b}' for b in branches]).splitlines())
for key,_,_ in plans:
    if observed.get('refs/heads/'+rows[key]['branch'])!=heads[key]:raise RuntimeError('Remote work-branch verification failed')
if observed.get('refs/heads/'+INTEGRATION)!=sha:raise RuntimeError('Remote integration verification failed')
print('Verified all repaired refs in the authorized fork.',flush=True)
with (OUT/'repair-publication.json').open() as f:print(f.read())
