"""Temporary, hash-checked workbench; never part of the final candidate tree."""
import base64
import hashlib
import json
import lzma
import os
import pathlib
import subprocess
import sys
import urllib.request

ROOT = pathlib.Path.cwd()
BASE_TREE = 'ea4e93ac4970ff0fa2be6256c08ac7d6f3319d3e'
PATCH_SHA256 = '765b88ba79ab9cf40f5de07fff699b06618bd4945c0755fc3336e98aa36afde1'

def run(*args):
    subprocess.run(args, check=True)

def replace(path, before, after):
    p = ROOT / path
    text = p.read_text(encoding='utf-8')
    if text.count(before) != 1:
        raise RuntimeError('unexpected fixup anchor in ' + path)
    p.write_text(text.replace(before, after), encoding='utf-8', newline='\n')
    run('git', 'add', '--', path)

def apply():
    encoded = ''.join((ROOT / f'tools/nr_colour_patch.{i}.b64').read_text().strip() for i in range(5))
    patch = lzma.decompress(base64.b64decode(encoded, validate=True))
    if hashlib.sha256(patch).hexdigest() != PATCH_SHA256:
        raise RuntimeError('patch hash mismatch')
    path = pathlib.Path(os.environ.get('RUNNER_TEMP', '.')) / 'nr-colour.patch'
    path.write_bytes(patch)
    run('git', 'apply', '--check', str(path))
    run('git', 'apply', '--index', str(path))
    replace('CMakeLists.txt', '        add_dependencies(controller_tests neural_rendering_color_warp_test)\n    endif()\nmessage(', '        add_dependencies(controller_tests neural_rendering_color_warp_test)\n    endif()\nendif()\nmessage(')
    replace('tests/neural_rendering_color_settings_test.cpp', 'for (const json invalid : {', 'for (const json& invalid : {')
    common = 'features/Upscaling/Shaders/Upscaling/NeuralRendering/ColorCommon.hlsli'
    replace(common, 'pow((v + 0.055) / 1.055, 2.4)', 'pow(max((v + 0.055) / 1.055, 0.0), 2.4)')
    replace(common, 'pow(v, 1.0 / 2.4)', 'pow(max(v, 0.0), 1.0 / 2.4)')
    run('git', 'diff', '--cached', '--check')
    run('git', 'diff', '--cached', '--stat')

def headers():
    target = ROOT / 'build/nr-colour/include/nlohmann'
    target.mkdir(parents=True, exist_ok=True)
    with urllib.request.urlopen('https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp', timeout=60) as response:
        (target / 'json.hpp').write_bytes(response.read())

def linux():
    headers()
    out = ROOT / 'build/nr-colour'
    for test in ['neural_rendering_color_contract_test', 'neural_rendering_devbench_contract_test', 'neural_rendering_submit_pair_contract_test', 'streamline_runtime_source_contract_test']:
        run('cmake', '-DPROJECT_ROOT=' + str(ROOT), '-P', 'tests/' + test + '.cmake')
    tests = ['neural_rendering_color_policy_test', 'neural_rendering_pipeline_policy_test', 'character_region_policy_test', 'frame_telemetry_ring_test', 'dlss_viewport_crop_test', 'foveated_center_alignment_test', 'foveated_region_plan_test']
    for test in tests:
        run('clang++', '-std=c++23', '-O2', '-DNDEBUG', '-Wall', '-Wextra', '-Werror', '-Isrc', 'tests/' + test + '.cpp', '-o', str(out / test))
        run(str(out / test))
    run('clang++', '-std=c++23', '-O2', '-DNDEBUG', '-Wall', '-Wextra', '-Werror', '-Isrc', '-Ibuild/nr-colour/include', 'tests/neural_rendering_color_settings_test.cpp', 'src/Features/Upscaling/NeuralRendering/ColorSettings.cpp', '-o', str(out / 'settings'))
    run(str(out / 'settings'))
    run('clang++', '-std=c++23', '-O1', '-g', '-fsanitize=address,undefined', '-Isrc', 'tests/neural_rendering_color_policy_test.cpp', '-o', str(out / 'policy-sanitized'))
    run(str(out / 'policy-sanitized'))

def windows():
    headers()
    common = ['cl.exe', '/nologo', '/std:c++latest', '/EHsc', '/W4', '/WX', '/O2', '/DNDEBUG', '/DNOMINMAX', '/DWIN32_LEAN_AND_MEAN', '/Zc:__cplusplus', '/Isrc', '/Ibuild/nr-colour/include']
    run(*common, 'tests/neural_rendering_color_policy_test.cpp', '/Febuild/nr-colour/policy.exe')
    run(str(ROOT / 'build/nr-colour/policy.exe'))
    run(*common, 'tests/neural_rendering_color_settings_test.cpp', 'src/Features/Upscaling/NeuralRendering/ColorSettings.cpp', '/Febuild/nr-colour/settings.exe')
    run(str(ROOT / 'build/nr-colour/settings.exe'))
    run(*common, 'tests/neural_rendering_color_warp_test.cpp', 'src/Features/Upscaling/NeuralRendering/ColorPipeline.cpp', '/Febuild/nr-colour/warp.exe', '/link', 'd3d11.lib', 'd3dcompiler.lib', 'dxgi.lib')
    run(str(ROOT / 'build/nr-colour/warp.exe'), str(ROOT))

def publish():
    files = subprocess.check_output(['git', 'diff', '--cached', '--name-only'], text=True).splitlines()
    if not files or any(p.startswith(('.github/', 'tools/', 'extern/')) for p in files):
        raise RuntimeError('unexpected final-tree path')
    api = 'https://api.github.com/repos/ParticleTroned/skyrim-community-shaders/git/'
    token = os.environ['GITHUB_TOKEN']
    def post(endpoint, obj):
        req = urllib.request.Request(api + endpoint, json.dumps(obj).encode(), method='POST', headers={'Authorization': 'Bearer ' + token, 'Accept': 'application/vnd.github+json', 'Content-Type': 'application/json', 'X-GitHub-Api-Version': '2022-11-28'})
        with urllib.request.urlopen(req, timeout=60) as response: return json.load(response)
    entries = []
    for path in files:
        data = (ROOT / path).read_bytes()
        blob = post('blobs', {'content': base64.b64encode(data).decode(), 'encoding': 'base64'})
        entries.append({'path': path, 'mode': '100644', 'type': 'blob', 'sha': blob['sha']})
    tree = post('trees', {'base_tree': BASE_TREE, 'tree': entries})['sha']
    print('CANDIDATE_TREE_SHA=' + tree)
    (ROOT / 'candidate-tree.json').write_text(json.dumps({'tree': tree, 'baseTree': BASE_TREE, 'patchSha256': PATCH_SHA256, 'fixups': 4, 'files': entries}, indent=2))

if __name__ == '__main__':
    {'apply': apply, 'linux': linux, 'windows': windows, 'publish': publish}[sys.argv[1]]()
