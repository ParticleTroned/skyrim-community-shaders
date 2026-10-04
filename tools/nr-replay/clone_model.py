"""Reproduce the pinned, replay-only SM120 independent-context N1/N2 catalogs.

This CPU tool consumes attributed live captures; it neither loads GPU modules
nor discovers installed providers. Intermediate assembler ELF must not execute.
"""
from pathlib import Path, PureWindowsPath
import argparse
import json
import re
import struct
import subprocess

import preserved_elf as elf

INVENTORY_SHA = '69912ff52623d24f658582b03bd0805b1a7e0b611adc976cdafd62d6c20e9d2d'
CATALOGS = {
    1: 'a8d6d4ebddadfa2ddfef5fa960a7a0713403ceceb7324fde3045eb52871bf281',
    2: '2b11eb5028bbdbb3b539be8789f10052868f46b5c256bdc16c1003a3e7f8d76e',
}
TOOL_SHA = {
    'cubit': 'b7bc46096ee69d4b78e915488b5716b34bf184954b6ac685a26e8662b797c29d',
    'table': '7ff43398b779fe826f6f4f0aa6accb2e3bb366a9a1b1129c587bd58d1e889d59',
    'nvdisasm': '71c5c94eecde05c2fa7c9bfe2a96bb1ef2627bc631c4193143ffca9ca2ff9a6c',
    'cuobjdump': 'db89b939a19ba2910cb639116bd71d01f42173d8e6479fd988395effe2e40df5',
    'ptxas': '462442e14a78d62e9165926e9ae75d4d8dbc5d9db2d4130f36f08e9aeaa0e0d5',
}
NOP = struct.pack('<QQ', 0x7918, 0x000fc00000000000)
STALL_MASK = 15 << 41
PC_TARGET = re.compile(r'\b(?:BRA(?:\.U)?|BSSY(?:\.RECONVERGENT)?|WARPSYNC(?:\.COLLECTIVE)?)\b.*?(0x[0-9a-f]+)$')
require = elf.require


def records(text):
    pairs = [(int(pc, 16), instruction.strip()) for pc, instruction in
             re.findall(r'/\*([0-9a-f]+)\*/\s*(.*?);', text)]
    require(len(dict(pairs)) == len(pairs), 'duplicate instruction PCs')
    return dict(pairs)


def raw(chunk):
    lo, hi = struct.unpack('<QQ', chunk)
    return f'    __raw__0x{hi:016x}{lo:016x} ;'


def branch_target(pc, low, high):
    value = ((high & 0x3ffff) << 38) | ((low >> 34) << 8) | ((low >> 16) & 255)
    signed = value - (1 << 56) if value & (1 << 55) else value
    return pc + 16 + signed * 4


def relocate_branch(pc, low, high, target):
    delta = target - pc - 16
    require(pc >= 0 and target >= 0 and pc % 16 == target % 16 == 0 and
            delta % 4 == 0 and -(1 << 55) <= delta // 4 < (1 << 55), 'branch target outside encoding')
    value = (delta // 4) & ((1 << 56) - 1)
    low = ((low & ~0xfffffffc00ff0000) | ((value & 255) << 16) |
           (((value >> 8) & 0x3fffffff) << 34))
    high = (high & ~0x3ffff) | (value >> 38)
    require(branch_target(pc, low, high) == target, 'branch relocation did not round trip')
    return low, high


def mask_producer(chunk, instruction):
    require(re.fullmatch(r'S2(?:U)?R (?:U)?R\d+, SR_CTAID.Z', instruction), 'unsupported logical-Z producer')
    low, high = struct.unpack('<QQ', chunk)
    opcode = 0x79c3 if instruction.startswith('S2UR') else 0x7919
    require(low & 0xffff == opcode and high & 0xffff == 0x2700 and
            (high >> 46) & 7 < 6, 'logical-Z producer encoding or scoreboard differs')
    stall = (high >> 41) & 15
    require(stall >= 1, 'logical-Z producer has no admitted issue delay')
    # A new immediate consumer needs the compiler's minimum producer delay.
    if stall == 1:
        high = (high & ~STALL_MASK) | (2 << 41)
    return struct.pack('<QQ', low, high)


def rebase_text(instruction, stride, parameter_bytes):
    return re.sub(r'c\[0x0\]\[(0x[0-9a-f]+)\]',
                  lambda m: f'c[0x0][{hex(int(m[1], 16) + stride)}]'
                  if 0x380 <= int(m[1], 16) < 0x380 + parameter_bytes else m[0], instruction)


def rebase_load(chunk, instruction, stride, parameter_bytes):
    match = re.search(r'\b(LDCU|LDC)(?:\.[A-Z0-9]+)* .*?c\[0x0\]\[(0x[0-9a-f]+)\]', instruction)
    require(match is not None and stride >= parameter_bytes and stride % 16 == 0, 'unsupported packet load')
    offset = int(match[2], 16)
    width = 16 if '.128 ' in instruction else 8 if '.64 ' in instruction else 4
    require(0x380 <= offset and offset + width <= 0x380 + parameter_bytes and
            offset + stride < 0x2000, 'packet load exceeds its source or target extent')
    low, high = struct.unpack('<QQ', chunk)
    shift = 37 if match[1] == 'LDCU' else 38
    require((low >> shift) & 0x1fff == offset, 'constant-load encoding differs from disassembly')
    return struct.pack('<QQ', low + (stride << shift), high)


def pc_maps(size, sites, count):
    require(count in (1, 2) and size > 0 and size % 128 == 0 and
            len(set(sites)) == len(sites) and all(0 <= p < size and p % 16 == 0 for p in sites),
            'invalid source extent, insertion PCs or batch count')
    extent = size + 128 * len(sites)
    bases = [0] if count == 1 else [128, 128 + extent]
    return [{pc: base + pc + 128 * sum(site < pc for site in sites)
             for pc in range(0, size + 1, 16)} for base in bases]


def section(path, digest, name):
    sections = elf.read_elf(path, digest)[2]
    matches = [s['data'] for s in sections if s['name'] == name]
    require(len(matches) == 1, 'required ELF section missing or duplicated: ' + name)
    return matches[0]


def semantic(manifest):
    value = dict(batchCount=manifest['batchCount'],
                 modules=[{k: v for k, v in m.items() if k != 'path'} for m in manifest['modules']])
    return elf.sha(json.dumps(value, sort_keys=True, separators=(',', ':')).encode())


def resources(text):
    return {name: ' '.join(body.split()) for name, body in
            re.findall(r'^ Function (.*?):\s*\r?\n(.*?)(?=^ Function |\Z)', text, re.M | re.S)}


class Generator:
    def __init__(self, args):
        self.output = args.output.resolve()
        self.tools = {name: getattr(args, name).resolve() for name in TOOL_SHA}
        self.jobs, self.inputs, self.entries, self.bundles = [], {}, [], []
        self.inventory_path = args.inventory.resolve()
        raw_inventory = self.pin(self.inventory_path, INVENTORY_SHA)
        self.inventory = json.loads(raw_inventory)
        require(self.inventory['schema'] == 'nr-live-full-chain-static-v1' and
                len(self.inventory['sourceModules']) == 9 and len(self.inventory['kernels']) == 44,
                'captured model inventory differs')
        self.recorded_root = PureWindowsPath(self.inventory['source']).parents[2]
        self.capture_root = args.capture_root.resolve()
        for name, path in self.tools.items():
            self.pin(path, TOOL_SHA[name])
        self.pin(self.resolve_capture(self.inventory['source']), self.inventory['sourceSha256'])
        self.sources = {}
        for module in self.inventory['sourceModules']:
            self.pin(self.resolve_capture(module['source']), module['sourceSha256'])
            path = self.resolve_capture(module['cubin'])
            self.pin(path, module['cubinSha256'])
            self.sources[module['identity']] = (path, module['cubinSha256'])
        require(set(self.sources) == set(range(9)), 'source module set differs')
        require([k['ordinal'] for k in self.inventory['kernels']] == list(range(44)), 'kernel order differs')
        self.fixture = Path(__file__).with_name('clone_selector.ptx')
        self.pin(self.fixture, elf.sha(self.fixture.read_bytes()))
        self.pin(Path(__file__).resolve(), elf.sha(Path(__file__).read_bytes()))
        self.pin(Path(elf.__file__).resolve(), elf.sha(Path(elf.__file__).read_bytes()))

    def pin(self, path, expected):
        data = path.read_bytes()
        require(elf.sha(data) == expected, 'input SHA256 differs: ' + str(path))
        self.inputs[str(path)] = expected
        return data

    def resolve_capture(self, recorded):
        try:
            relative = PureWindowsPath(recorded).relative_to(self.recorded_root)
        except ValueError as error:
            raise ValueError('capture path leaves the pinned evidence root') from error
        path = self.capture_root.joinpath(*relative.parts).resolve()
        require(path.is_relative_to(self.capture_root), 'resolved capture path leaves evidence root')
        return path

    def run(self, args, label):
        command = list(map(str, args))
        timed_out, launch_error = False, None
        try:
            result = subprocess.run(command, capture_output=True, timeout=120, check=False)
            stdout, stderr, code = result.stdout, result.stderr, result.returncode
        except subprocess.TimeoutExpired as error:
            stdout, stderr, code = error.stdout or b'', error.stderr or b'', None
            timed_out = True
        except OSError as error:
            stdout, stderr, code = b'', str(error).encode('utf-8'), None
            launch_error = f'{type(error).__name__}: {error}'
        (self.output / (label + '.stdout.txt')).write_bytes(stdout)
        (self.output / (label + '.stderr.txt')).write_bytes(stderr)
        self.jobs.append(dict(command=command, exitCode=code, timedOut=timed_out, launchError=launch_error,
                              stdoutSha256=elf.sha(stdout), stderrSha256=elf.sha(stderr)))
        require(code == 0, f'tool failed or timed out: {label}; see retained output')
        return stdout.decode('utf-8', errors='strict')

    def selectors(self):
        result = {}
        source = self.fixture.read_text()
        require(source.count('setp.lt.u32 %first, %item, 1;') == 1, 'selector source marker differs')
        for z in (1, 2, 4):
            ptx = self.output / f'selector-z{z}.ptx'
            ptx.write_text(source.replace('setp.lt.u32 %first, %item, 1;', f'setp.lt.u32 %first, %item, {z};'))
            cubin = ptx.with_suffix('.cubin')
            self.run([self.tools['ptxas'], '--gpu-name', 'sm_120', '--opt-level', '3', ptx, '--output-file', cubin], f'selector-z{z}-compile')
            text = self.run([self.tools['nvdisasm'], '--no-dataflow', '--print-instruction-encoding', cubin], f'selector-z{z}-disassembly')
            pattern = r'/\*([0-9a-f]+)\*/\s*(.*?)\s*;\s*/\*\s*(0x[0-9a-f]+)\s*\*/\s*/\*\s*(0x[0-9a-f]+)\s*\*/'
            code = {int(pc, 16): (t.strip(), int(lo, 16), int(hi, 16)) for pc, t, lo, hi in re.findall(pattern, text)}
            require(code[0][0] == 'LDC R1, c[0x0][0x37c]' and code[16][0] == 'S2UR UR4, SR_CTAID.Z' and
                    code[32][0] == f'UISETP.GE.U32.AND UP0, UPT, UR4, {hex(z)}, UPT' and
                    code[48][0].startswith('BRA.U !UP0,'), 'compiler selector instructions differ')
            require((code[16][2] >> 46) & 7 == 0 and (code[32][2] >> 52) & 63 == 1 and
                    (code[32][2] >> 41) & 15 >= 8 and (code[48][2] >> 41) & 15 >= 5,
                    'compiler selector dependencies differ')
            nop = next(value for value in code.values() if value[0] == 'NOP')
            result[z] = code, struct.pack('<QQ', nop[1], nop[2])
        return result

    def clone(self, kernel, selectors):
        name, ordinal = kernel['name'], kernel['ordinal']
        source, source_sha = self.sources[kernel['module']]
        original = section(source, source_sha, '.text.' + name)
        require(elf.sha(original) == kernel['textSha256'], 'captured text identity differs')
        old = records(self.run([self.tools['nvdisasm'], '--binary', 'SM120', '--no-dataflow',
                                self.write_bytes(f'k{ordinal:02d}-original.bin', original)], f'k{ordinal:02d}-original'))
        require(set(old) == set(range(0, len(original), 16)), 'source disassembly is incomplete')
        require(len(kernel['blockShapes']) == len(kernel['parameterBytes']) == 1, 'kernel shape or packet varies')
        z_values = {shape[2] for shape in kernel['gridShapes']}
        require(len(z_values) == 1, 'kernel grid Z varies')
        z = next(iter(z_values))
        parameter = kernel['parameterBytes'][0]
        stride = elf.align(parameter, 16)
        require(z in (1, 2, 4) and kernel['blockShapes'][0][2] == 1 and
                parameter > 0 and parameter % 8 == 0 and kernel['userParameterBase'] == 0x380,
                'kernel geometry or packet ABI differs')
        frozen = self.run([self.tools['cubit'], 'disassemble', '-t', self.tools['table'], '-k', name, '--frozen', source], f'k{ordinal:02d}-frozen').splitlines()
        first = next(i for i, line in enumerate(frozen) if ('[' in line or '__raw__' in line) and not line.lstrip().startswith('.'))
        body = frozen[first:-1]
        require(len(body) * 16 == len(original), 'frozen assembler body is incomplete')
        declaration = next(line for line in frozen if '.reg ' in line)
        sites = {item['pc']: item['instruction'] for item in kernel['ctaZ']}
        require(sites == {pc: text for pc, text in old.items() if 'SR_CTAID.Z' in text}, 'logical-Z site inventory differs')
        transforms, receipts = {}, []
        for count in (1, 2):
            maps = pc_maps(len(original), sites, count)
            items = [0] if count == 1 else [1, 0]
            candidate_parameter = parameter if count == 1 else 2 * stride
            lines = [f'.entry {name}', declaration] + [f'    .param u64 packet_word_{i}' for i in range(candidate_parameter // 8)]
            cooperative = next((line.split()[1:] for line in frozen if '.merc_cgsites' in line), [])
            if cooperative:
                lines.append('    .merc_cgsites ' + ' '.join(f'{hex(mapping[int(v.split(":")[0], 16)])}:{v.split(":")[1]}' for mapping in maps for v in cooperative))
            if count == 2:
                selector, nop = selectors[z]
                for pc in range(0, 64, 16):
                    _, low, high = selector[pc]
                    if pc == 48:
                        low, high = relocate_branch(pc, low, high, maps[1][0])
                    lines.append(raw(struct.pack('<QQ', low, high)))
                lines += [raw(nop)] * 4
            for item, mapping in zip(items, maps):
                for i, line in enumerate(body):
                    pc = i * 16
                    chunk = original[pc:pc + 16]
                    target = PC_TARGET.search(old[pc])
                    replacement = None
                    if target and re.search(r'\bBRA\b', old[pc]):
                        low, high = struct.unpack('<QQ', chunk)
                        require(branch_target(pc, low, high) == int(target[1], 16), 'original branch encoding differs')
                        replacement = struct.pack('<QQ', *relocate_branch(mapping[pc], low, high, mapping[int(target[1], 16)]))
                    elif item and rebase_text(old[pc], stride, parameter) != old[pc]:
                        replacement = rebase_load(chunk, old[pc], stride, parameter)
                    elif pc in sites:
                        replacement = mask_producer(chunk, sites[pc])
                    if replacement is not None:
                        label = re.match(r'^(L_[0-9a-f]+:)\s*', line)
                        line = (label[1] + ' ' if label else '    ') + raw(replacement).strip()
                    lines.append(re.sub(r'\bL_([0-9a-f]+)\b', lambda m: f'B{item}_{m[1]}', line))
                    if pc in sites:
                        destination = sites[pc].split()[1].rstrip(',')
                        high = struct.unpack_from('<Q', chunk, 8)[0]
                        barrier = (high >> 46) & 7
                        wait = ''.join(str(j) if j == barrier else '-' for j in range(6))
                        uniform = destination.startswith('U')
                        mask = f'{"ULOP3" if uniform else "LOP3"}.LUT {destination}, {destination}, {hex(z - 1)}, {"URZ" if uniform else "RZ"}, 0xc0, {"!UPT" if uniform else "!PT"}'
                        lines.append(f'    [B{wait}:R-:W-:-:S15] {mask} ;')
                        lines += [raw(NOP)] * 7
            lines.append('.endentry')
            label = f'k{ordinal:02d}-n{count}'
            sass = self.output / (label + '.sass')
            sass.write_text('\n'.join(lines) + '\n')
            intermediate = self.output / (label + '-UNQUALIFIED-METADATA.cubin')
            self.run([self.tools['cubit'], 'asm', '-t', self.tools['table'], sass, '--output', intermediate], label + '-assemble')
            code = section(intermediate, elf.sha(intermediate.read_bytes()), '.text.' + name)
            require(len(code) == count * (len(original) + 128 * len(sites)) + (128 if count == 2 else 0), 'candidate instruction size differs')
            code_path = self.write_bytes(label + '-code.bin', code)
            decoded = records(self.run([self.tools['nvdisasm'], '--binary', 'SM120', '--no-dataflow', code_path], label + '-disassembly'))
            self.audit_code(original, old, code, decoded, sites, items, maps, parameter, stride, z)
            transforms[count] = dict(code=code, pc_maps=maps, parameter_bytes=candidate_parameter)
            receipts.append(dict(batchCount=count, codePath=str(code_path), codeSha256=elf.sha(code),
                                 parameterBytes=candidate_parameter, pcMaps=maps))
        self.entries.append(dict(ordinal=ordinal, entry=name, sourceTextSha256=kernel['textSha256'], transforms=receipts))
        print(f'K{ordinal:02d}: original, N1, N2 instruction audits passed', flush=True)
        return transforms

    def write_bytes(self, name, data):
        path = self.output / name
        path.write_bytes(data)
        return path

    @staticmethod
    def audit_code(original, old, code, decoded, sites, items, maps, parameter, stride, z):
        require(set(decoded) == set(range(0, len(code), 16)), 'candidate disassembly is incomplete')
        for item, mapping in zip(items, maps):
            for pc, instruction in old.items():
                expected = rebase_text(instruction, stride, parameter) if item else instruction
                target = PC_TARGET.search(expected)
                if target:
                    expected = expected[:target.start(1)] + hex(mapping[int(target[1], 16)])
                require(decoded[mapping[pc]] == expected, f'transformed instruction differs at {pc:#x}')
                before, after = original[pc:pc + 16], code[mapping[pc]:mapping[pc] + 16]
                baseline = mask_producer(before, instruction) if pc in sites else before
                require(struct.unpack_from('<Q', baseline, 8)[0] >> 41 == struct.unpack_from('<Q', after, 8)[0] >> 41,
                        'unadmitted instruction scheduling change')
                if not target and expected == instruction:
                    require(baseline == after, 'unadmitted instruction encoding change')
                elif not target:
                    require(before[8:] == after[8:], 'parameter rebasing changed upper instruction word')
                if pc in sites:
                    at = mapping[pc] + 16
                    destination = instruction.split()[1].rstrip(',')
                    uniform = destination.startswith('U')
                    mask = f'{"ULOP3" if uniform else "LOP3"}.LUT {destination}, {destination}, {hex(z - 1)}, {"URZ" if uniform else "RZ"}, 0xc0, {"!UPT" if uniform else "!PT"}'
                    require(decoded[at] == mask, 'logical-Z mask differs')
                    high = struct.unpack_from('<Q', code, at + 8)[0]
                    barrier = (struct.unpack_from('<Q', before, 8)[0] >> 46) & 7
                    require((high >> 41) & 15 == 15 and (high >> 52) & 63 == 1 << barrier and
                            (high >> 46) & 7 == 7 and (high >> 49) & 7 == 7 and code[at + 16:at + 128] == NOP * 7,
                            'logical-Z dependency or padding differs')

    def package(self, transforms):
        for count in (1, 2):
            modules = []
            for module in self.inventory['sourceModules']:
                ordinal = module['identity']
                source, digest = self.sources[ordinal]
                kernels = [k for k in self.inventory['kernels'] if k['module'] == ordinal]
                selected = {k['name']: transforms[k['name']][count] for k in kernels}
                label = f'module-{ordinal:02d}-n{count}'
                path = self.output / (label + '.cubin')
                receipt = elf.emit_module(source, digest, path, selected)
                original_resources = resources(self.run([self.tools['cuobjdump'], '--dump-resource-usage', source], label + '-original-resources'))
                for tool, option, suffix in (('cuobjdump', '--dump-elf', 'elf'), ('cuobjdump', '--dump-resource-usage', 'resources'), ('nvdisasm', '--no-dataflow', 'sass')):
                    text = self.run([self.tools[tool], option, path], label + '-' + suffix)
                    if suffix == 'resources':
                        candidate_resources = resources(text)
                require(set(original_resources) == set(candidate_resources), 'resource function set differs')
                for name, original in original_resources.items():
                    expected = re.sub(r'CONSTANT\[0\]:\d+', f"CONSTANT[0]:{0x380 + selected[name]['parameter_bytes']}", original) if name in selected else original
                    require(candidate_resources[name] == expected, 'resource metadata differs: ' + name)
                functions = sorted([dict(name=k['name'], paramSize=k['parameterBytes'][0], gridZ=k['gridShapes'][0][2]) for k in kernels], key=lambda v: v['name'])
                modules.append(dict(originalSha256=module['sourceSha256'], path=str(path), sha256=receipt['sha256'], functions=functions))
            modules.sort(key=lambda v: v['originalSha256'])
            manifest = dict(schema='nr-model-kernel-replacement-v1', batchCount=count, modules=modules)
            digest = semantic(manifest)
            require(digest == CATALOGS[count], f'N{count} catalog does not reproduce qualified bytes: {digest}')
            path = self.output / f'full-model-n{count}-manifest.json'
            path.write_text(json.dumps(manifest, indent=2) + '\n')
            self.bundles.append(dict(batchCount=count, path=str(path), semanticSha256=digest))
            print(f'N{count} reproduced qualified catalog {digest}', flush=True)

    def execute(self):
        self.output.mkdir(parents=True, exist_ok=False)
        failure = None
        try:
            selectors = self.selectors()
            transforms = {k['name']: self.clone(k, selectors) for k in self.inventory['kernels']}
            self.package(transforms)
            for path, expected in list(self.inputs.items()):
                self.pin(Path(path), expected)
        except BaseException as error:
            failure = f'{type(error).__name__}: {error}'
            raise
        finally:
            report = dict(schema='nr-reproducible-model-clone-v1', status='failed' if failure else 'reproduced',
                          gpuExecuted=False, outputEquivalencePerformed=False, failure=failure,
                          inputs=self.inputs, entries=self.entries, bundles=self.bundles, jobs=self.jobs,
                          limits=['Only exact pinned SM120 live-capture inputs are admitted.',
                                  'UNQUALIFIED-METADATA intermediates must not execute.',
                                  'GPU qualification is separate from this CPU reproduction.'])
            (self.output / 'audit.json').write_text(json.dumps(report, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inventory', type=Path, required=True)
    parser.add_argument('--capture-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    for name in TOOL_SHA:
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    require(not args.output.exists(), 'output already exists; preserve prior evidence')
    Generator(args).execute()


if __name__ == '__main__':
    main()
