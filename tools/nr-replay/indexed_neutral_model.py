"""Generate shared-body catalogs without changing original GPU register budgets."""
from pathlib import Path
import argparse
import json
import re
import struct

import clone_model as clone
import indexed_model as indexed
import indexed_full_model as full
import preserved_elf as elf

require = elf.require
UNIFORM = 48
PREFIX_BYTES = 128
PRELUDE_BYTES = 32
MOVE = struct.pack('<QQ', 0x0000003000067c02, 0x000fde0008000f00)


def uniform_prefix(words, stride, z):
    require(stride > 0 and stride % 16 == 0 and stride <= 2048 and z in (1, 2, 4), 'uniform index geometry differs')
    low, high = struct.unpack('<QQ', words['S2UR UR5, SR_CTAID.Z'])
    require(low == 0x579c3 and high == 0x000e240000002700, 'official uniform producer differs')
    producer = struct.pack('<QQ', (low & ~(255 << 16)) | (UNIFORM << 16), high)
    low, high = struct.unpack('<QQ', words['USHF.R.U32.HI UR4, URZ, 0x1, UR5'])
    require(low == 0x00000001ff047899 and high == 0x001fc80008011605, 'official uniform divide differs')
    low = (low & ~((255 << 16) | (0xffffffff << 32))) | (UNIFORM << 16) | ({1: 0, 2: 1, 4: 2}[z] << 32)
    divide = struct.pack('<QQ', low, (high & ~255) | UNIFORM)
    low, high = struct.unpack('<QQ', words['UIMAD UR4, UR4, 0x60, URZ'])
    require(low == 0x00000060040478a4 and high == 0x000fc8000f8e02ff, 'official uniform multiply differs')
    low = (low & ~((0xffff << 16) | (0xffffffff << 32))) | (UNIFORM << 16) | (UNIFORM << 24) | (stride << 32)
    multiply = struct.pack('<QQ', low, (high & ~clone.STALL_MASK) | (15 << 41))
    lo, hi = struct.unpack('<QQ', clone.NOP)
    delay = struct.pack('<QQ', lo, hi | (15 << 41))
    return producer + divide + multiply + delay * 5


def packet_loads(instructions, parameter):
    result = {}
    for pc, text in instructions.items():
        match = re.fullmatch(r'((?:@!?P[0-6] )?)LDC(?:\.64)? R(\d+), c\[0x0\]\[(0x[0-9a-f]+)\]', text)
        if match and 0x380 <= int(match[3], 16) < 0x380 + parameter:
            result[pc] = dict(predicate=match[1], destination=int(match[2]))
    return result


def pc_map(size, sites):
    require(size > 0 and size % 16 == 0 and len(sites) == len(set(sites)) and
            all(0 <= value < size and value % 16 == 0 for value in sites), 'load prelude sites invalid')
    return {pc: PREFIX_BYTES + pc + PRELUDE_BYTES * sum(site < pc for site in sites)
            for pc in range(0, size + 1, 16)}


def load_prelude(original, destination):
    require(type(destination) is int and 0 <= destination <= 254, 'load destination is outside native register encoding')
    low, high = struct.unpack('<QQ', clone.NOP)
    drain = struct.pack('<QQ', low, high | (63 << 52) | (15 << 41))
    low, high = struct.unpack('<QQ', MOVE)
    original_low = struct.unpack_from('<Q', original)[0]
    low = (low & ~((255 << 16) | 0xf000)) | (destination << 16) | (original_low & 0xf000)
    return drain + struct.pack('<QQ', low, high)


def audit_body(logical, old, candidate, decoded, mapping, sites, loads, prefix, stride, z):
    require(candidate[:PREFIX_BYTES] == prefix and set(decoded) == set(range(0, len(candidate), 16)), 'neutral body extent differs')
    expected_prefix = [f'S2UR UR48, SR_CTAID.Z', f'USHF.R.U32.HI UR48, URZ, {hex({1: 0, 2: 1, 4: 2}[z])}, UR48',
        f'UIMAD UR48, UR48, {hex(stride)}, URZ'] + ['NOP'] * 5
    require([decoded[pc] for pc in range(0, PREFIX_BYTES, 16)] == expected_prefix, 'uniform prefix decoding differs')
    for pc, instruction in old.items():
        at = mapping[pc] + (PRELUDE_BYTES if pc in sites else 0)
        expected = loads.get(pc, instruction)
        target = clone.PC_TARGET.search(expected)
        if target:
            expected = expected[:target.start(1)] + hex(mapping[int(target[1], 16)])
        require(decoded[at] == expected, f'neutral instruction differs at {pc:#x}')
        before, after = logical[pc:pc + 16], candidate[at:at + 16]
        require(struct.unpack_from('<Q', before, 8)[0] >> 41 == struct.unpack_from('<Q', after, 8)[0] >> 41,
                'original scheduling changed')
        if pc in loads:
            require(before[:3] == after[:3] and before[4:] == after[4:], 'packet load changed beyond index register')
        elif not target:
            require(before == after, 'original arithmetic changed')
        if pc in sites:
            destination = sites[pc]['destination']
            require(candidate[mapping[pc]:at] == load_prelude(before, destination), 'load prelude bytes differ')
            require(decoded[mapping[pc]] == 'NOP' and decoded[mapping[pc] + 16] ==
                    sites[pc]['predicate'] + f'MOV R{destination}, UR48', 'predicated load prelude differs')
    require(candidate[mapping[len(logical)]:] == clone.NOP * ((len(candidate) - mapping[len(logical)]) // 16),
            'code-alignment tail contains executable changes')


class Generator(full.Generator):
    def __init__(self, args):
        super().__init__(args)
        self.uniform_fixture = Path(__file__).with_name('indexed_uniform.ptx')
        for path in (self.uniform_fixture, Path(full.__file__).resolve(), Path(__file__).resolve()):
            self.pin(path, elf.sha(path.read_bytes()))

    def compile_prefix(self):
        output = self.output / 'uniform-prefix.cubin'
        self.run([self.tools['ptxas'], '--gpu-name', 'sm_120', '--opt-level', '3', self.uniform_fixture, '--output-file', output], 'uniform-prefix-compile')
        return indexed.instruction_words(self.run([self.tools['nvdisasm'], '--no-dataflow', '--print-instruction-encoding', output], 'uniform-prefix-decode'))

    def make_entry(self, kernel, selectors, words):
        native = clone.Generator.clone(self, kernel, selectors)[1]
        logical = native['code']
        ordinal, parameter, z = kernel['ordinal'], kernel['parameterBytes'][0], kernel['gridShapes'][0][2]
        stride = elf.align(parameter, 16)
        old = clone.records((self.output / f'k{ordinal:02d}-n1-disassembly.stdout.txt').read_text())
        full.admit_uniform_gap(old)
        sites = packet_loads(old, parameter)
        mapping = pc_map(len(logical), sites)
        prefix = uniform_prefix(words, stride, z)
        intermediate = self.output / f'k{ordinal:02d}-n1-UNQUALIFIED-METADATA.cubin'
        frozen = self.run([self.tools['cubit'], 'disassemble', '-t', self.tools['table'], '-k', kernel['name'], '--frozen', intermediate],
                          f'k{ordinal:02d}-neutral-frozen').splitlines()
        first = next(i for i, line in enumerate(frozen) if ('[' in line or '__raw__' in line) and not line.lstrip().startswith('.'))
        body = frozen[first:-1]
        require(len(body) * 16 == len(logical), 'logical model frozen instruction extent differs')
        lines = [f'.entry {kernel["name"]}', next(line for line in frozen if '.reg ' in line)]
        lines += [f'    .param u64 packet_word_{i}' for i in range(parameter // 8)]
        lines += [clone.raw(prefix[pc:pc + 16]) for pc in range(0, PREFIX_BYTES, 16)]
        loads = {}
        for i, line in enumerate(body):
            pc, chunk = i * 16, logical[i * 16:i * 16 + 16]
            label = re.match(r'^(L_[0-9a-f]+:)\s*', line)
            if pc in sites:
                prelude = load_prelude(chunk, sites[pc]['destination'])
                lines.append((label[1] + ' ' if label else '    ') + clone.raw(prelude[:16]).strip())
                lines.append(clone.raw(prelude[16:]))
                if label:
                    line = line[label.end():]
                label = None
            constant = re.search(r'\bc\[0x0\]\[(0x[0-9a-f]+)\]', old[pc])
            replacement = None
            if constant and int(constant[1], 16) >= 0x380:
                replacement, loads[pc] = indexed.address_load(chunk, old[pc], parameter_bytes=parameter,
                    gpr=sites[pc]['destination'] if pc in sites else 0, uniform=UNIFORM, allow128=True)
            else:
                target = clone.PC_TARGET.search(old[pc])
                if target and re.search(r'\bBRA\b', old[pc]):
                    low, high = struct.unpack('<QQ', chunk)
                    replacement = struct.pack('<QQ', *clone.relocate_branch(mapping[pc], low, high, mapping[int(target[1], 16)]))
            if replacement is not None:
                line = (label[1] + ' ' if label else '    ') + clone.raw(replacement).strip()
            lines.append(line)
        size = elf.align(mapping[len(logical)], 128)
        lines += [clone.raw(clone.NOP)] * ((size - mapping[len(logical)]) // 16) + ['.endentry']
        label = f'k{ordinal:02d}-neutral'
        sass = self.output / (label + '.sass')
        sass.write_text('\n'.join(lines) + '\n')
        temporary = self.output / (label + '-UNQUALIFIED-METADATA.cubin')
        self.run([self.tools['cubit'], 'asm', '-t', self.tools['table'], sass, '--output', temporary], label + '-assemble')
        code = clone.section(temporary, elf.sha(temporary.read_bytes()), '.text.' + kernel['name'])
        raw = self.write_bytes(f'k{ordinal:02d}-indexed.not-for-execution.raw', code)
        decoded = clone.records(self.run([self.tools['nvdisasm'], '--binary', 'SM120', '--no-dataflow', raw], label + '-decode'))
        require(len(code) == size, 'neutral native code size differs')
        audit_body(logical, old, code, decoded, mapping, sites, loads, prefix, stride, z)
        gpr, cap = self.registers(kernel)
        composed = {pc: mapping[target] for pc, target in native['pc_maps'][0].items()}
        transform = dict(code=code, pc_maps=[composed], register_count=gpr, register_cap=cap)
        self.index_entries.append(dict(ordinal=ordinal, entry=kernel['name'], sourceTextSha256=kernel['textSha256'],
            nativeTextSha256=elf.sha(code), nativeBytes=len(code), originalRegisterCount=gpr, registerCount=gpr,
            originalRegisterCap=cap, registerCap=cap, indexUniformRegister=UNIFORM, originalZ=z, packetStride=stride,
            packetLoads=len(loads), loadPreludes=len(sites), pcMap=composed))
        return {count: dict(transform, parameter_bytes=parameter if count == 1 else 2 * stride) for count in (1, 2)}

    def execute(self):
        self.output.mkdir(parents=True, exist_ok=False)
        failure = None
        try:
            words, selectors = self.compile_prefix(), self.selectors()
            transforms = {kernel['name']: self.make_entry(kernel, selectors, words) for kernel in self.inventory['kernels']}
            require(len(transforms) == len(self.index_entries) == 44, 'neutral model transformation is incomplete')
            self.package(transforms)
            for path, expected in list(self.inputs.items()):
                self.pin(Path(path), expected)
        except BaseException as error:
            failure = f'{type(error).__name__}: {error}'
            raise
        finally:
            report = dict(schema='nr-indexed-neutral-model-generation-v1', status='failed' if failure else 'cpu_audited', failure=failure,
                gpuExecuted=False, outputEquivalencePerformed=False, performanceQualified=False, inputs=self.inputs,
                entries=self.index_entries, logicalZGeneration=self.entries, bundles=self.bundles, jobs=self.jobs,
                limits=['Original GPR budgets, local frames and shared resources are unchanged.',
                        'Resource neutrality and CPU decoding do not replace N1/N2 exact-output GPU gates.'])
            (self.output / 'audit.json').write_text(json.dumps(report, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('inventory', 'capture-root', 'output', *clone.TOOL_SHA):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    require(not args.output.exists(), 'output already exists; preserve prior evidence')
    Generator(args).execute()


if __name__ == '__main__':
    main()
