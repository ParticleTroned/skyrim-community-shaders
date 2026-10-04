"""Build bounded SM120 shared-body model catalogs from pinned live captures.

One appended GPR and a verified uniform-register gap hold the packet index.
The original logical-Z transformation and native ELF emitter remain shared
with clone_model. Generated artifacts require loader, N1 and N2 GPU gates.
"""
from pathlib import Path
import argparse
import json
import re
import struct

import clone_model as clone
import indexed_model as indexed
import preserved_elf as elf

UNIFORM = 48
PREFIX_BYTES = 128
require = elf.require


def catalog(count, modules):
    require(count in (1, 2) and len(modules) == 9 and
            len({module['originalSha256'] for module in modules}) == 9,
            'replacement catalog module inventory differs')
    return dict(schema='nr-model-kernel-replacement-v1', batchCount=count,
                modules=sorted(modules, key=lambda value: value['originalSha256']))


def admit_uniform_gap(instructions):
    for instruction in instructions.values():
        for value in map(int, re.findall(r'\bUR(\d+)\b', instruction)):
            if value == 79:
                require(re.fullmatch(r'ELECT P[0-6], UR79, PT|MOV R\d+, UR79', instruction),
                        'special high uniform register has an unmodeled use')
                continue
            require(value <= 40, 'uniform register inventory differs')
            span = 16 if re.search(r'\bTEX\b|\bUBLKCP\b|\bdesc\[', instruction) else 4
            require(value + span <= UNIFORM, 'implicit uniform operand overlaps persistent index')


def prefix_words(optimized, unoptimized, geometry, gpr, stride, z):
    require(type(gpr) is int and 1 <= gpr <= 254 and stride > 0 and stride % 16 == 0 and
            stride <= 2048 and z in (1, 2, 4), 'index prefix allocation or geometry invalid')
    source = clone.mask_producer(optimized['S2R R0, SR_CTAID.Z'], 'S2R R0, SR_CTAID.Z')
    low, high = struct.unpack('<QQ', source)
    source = struct.pack('<QQ', (low & ~(255 << 16)) | (gpr << 16), high)
    low, high = struct.unpack('<QQ', geometry['SHF.R.U32.HI R0, RZ, 0x1, R0'])
    require(low == 0x00000001ff007819 and high == 0x001fca0000011600,
            'compiler item-selection encoding differs')
    shift = {1: 0, 2: 1, 4: 2}[z]
    low = (low & ~((255 << 16) | (0xffffffff << 32))) | (gpr << 16) | (shift << 32)
    high = (high & ~255) | gpr
    divide = struct.pack('<QQ', low, high)
    low, high = struct.unpack('<QQ', optimized['IMAD R0, R0, 0x60, RZ'])
    require((high >> 52) & 63 == 1 and (high >> 41) & 15 >= 5, 'compiler multiply dependency differs')
    low = (low & ~((0xffff << 16) | (0xffffffff << 32))) | (gpr << 16) | (gpr << 24) | (stride << 32)
    multiply = struct.pack('<QQ', low, high)
    low, high = struct.unpack('<QQ', unoptimized['R2UR UR4, R6'])
    require(low == 0x00000000060472ca and high == 0x000fc000000e0000, 'compiler uniform transfer differs')
    transfer = struct.pack('<QQ', (low & ~(0xffff << 16)) | (UNIFORM << 16) | (gpr << 24),
                           (high & ~clone.STALL_MASK) | (15 << 41))
    low, high = struct.unpack('<QQ', clone.NOP)
    delay = struct.pack('<QQ', low, high | (15 << 41))
    return source + divide + multiply + transfer + delay * 4


def index_body(code, instructions, prefix, parameter, gpr):
    require(len(prefix) == PREFIX_BYTES and set(instructions) == set(range(0, len(code), 16)),
            'logical model instruction coverage differs')
    admit_uniform_gap(instructions)
    output, loads = bytearray(code), []
    for pc, instruction in instructions.items():
        constant = re.search(r'\bc\[0x0\]\[(0x[0-9a-f]+)\]', instruction)
        if constant and int(constant[1], 16) >= 0x380:
            replacement, expected = indexed.address_load(code[pc:pc + 16], instruction,
                parameter_bytes=parameter, gpr=gpr, uniform=UNIFORM, allow128=True)
            output[pc:pc + 16] = replacement
            loads.append(dict(pc=pc, original=instruction, indexed=expected))
    require(loads, 'kernel has no packet loads')
    return prefix + output, loads


def audit_indexed(logical, instructions, code, decoded, loads, gpr, stride, z):
    expected_prefix = [f'S2R R{gpr}, SR_CTAID.Z',
        f'SHF.R.U32.HI R{gpr}, RZ, {hex({1: 0, 2: 1, 4: 2}[z])}, R{gpr}',
        f'IMAD{".SHL" if stride & (stride - 1) == 0 else ""} R{gpr}, R{gpr}, {hex(stride)}, RZ', f'R2UR UR{UNIFORM}, R{gpr}'] + ['NOP'] * 4
    require(set(decoded) == set(range(0, len(code), 16)) and
            [decoded[pc] for pc in range(0, PREFIX_BYTES, 16)] == expected_prefix,
            'shared-body prefix or instruction extent differs')
    replacements = {item['pc']: item['indexed'] for item in loads}
    for pc, original in instructions.items():
        expected = replacements.get(pc, original)
        target = clone.PC_TARGET.search(expected)
        if target:
            expected = expected[:target.start(1)] + hex(int(target[1], 16) + PREFIX_BYTES)
        require(decoded[pc + PREFIX_BYTES] == expected, f'shared-body instruction differs at {pc:#x}')
        before, after = logical[pc:pc + 16], code[pc + PREFIX_BYTES:pc + PREFIX_BYTES + 16]
        require(before[:3] == after[:3] and before[4:] == after[4:] if pc in replacements else before == after,
                'shared-body changed arithmetic or scheduling bytes')


class Generator(clone.Generator):
    def __init__(self, args):
        super().__init__(args)
        self.index_fixture = Path(__file__).with_name('indexed_prefix.ptx')
        self.geometry_fixture = Path(__file__).with_name('indexed_geometry.ptx')
        for path in (self.index_fixture, self.geometry_fixture, Path(indexed.__file__).resolve(), Path(__file__).resolve()):
            self.pin(path, elf.sha(path.read_bytes()))
        self.index_entries = []

    def compile_prefix(self):
        values = {}
        for label, level, fixture in (('index-O0', 0, self.index_fixture), ('index-O3', 3, self.index_fixture),
                                      ('geometry-O3', 3, self.geometry_fixture)):
            path = self.output / (label + '.cubin')
            self.run([self.tools['ptxas'], '--gpu-name', 'sm_120', '--opt-level', str(level), fixture, '--output-file', path], label + '-compile')
            values[label] = indexed.instruction_words(self.run([self.tools['nvdisasm'], '--no-dataflow', '--print-instruction-encoding', path], label + '-decode'))
        return values

    def registers(self, kernel):
        path, digest = self.sources[kernel['module']]
        sections = elf.read_elf(path, digest)[2]
        table = {section['name']: section for section in sections}
        symbol = table['.text.' + kernel['name']]['header'][7]
        counts = [struct.unpack('<II', payload)[1] for _, kind, _, payload in elf.attributes(table['.nv.info']['data'])
                  if kind == 0x2f and struct.unpack('<II', payload)[0] == symbol]
        caps = [value for fmt, kind, value, _ in elf.attributes(table['.nv.info.' + kernel['name']]['data']) if kind == 0x1b and fmt == 3]
        require(len(counts) == len(caps) == 1 and 1 <= counts[0] <= caps[0] <= 255 and counts[0] <= 254,
                'original native register metadata differs')
        return counts[0], caps[0]

    def make_entry(self, kernel, selectors, prefix):
        native = super().clone(kernel, selectors)[1]
        logical, mapping = native['code'], native['pc_maps'][0]
        ordinal, parameter, z = kernel['ordinal'], kernel['parameterBytes'][0], kernel['gridShapes'][0][2]
        stride = elf.align(parameter, 16)
        gpr, old_cap = self.registers(kernel)
        instructions = clone.records((self.output / f'k{ordinal:02d}-n1-disassembly.stdout.txt').read_text())
        require(all(int(value) < gpr for text in instructions.values() for value in re.findall(r'\bR(\d+)\b', text)),
                'original instructions exceed authoritative register count')
        prologue = prefix_words(prefix['index-O3'], prefix['index-O0'], prefix['geometry-O3'], gpr, stride, z)
        code, loads = index_body(logical, instructions, prologue, parameter, gpr)
        raw = self.write_bytes(f'k{ordinal:02d}-indexed.not-for-execution.raw', code)
        decoded = clone.records(self.run([self.tools['nvdisasm'], '--binary', 'SM120', '--no-dataflow', raw], f'k{ordinal:02d}-indexed-decode'))
        audit_indexed(logical, instructions, code, decoded, loads, gpr, stride, z)
        require(len(loads) == sum(any(value['bank'] == 0 and value['offset'] >= 0x380 for value in item['staticAddresses'])
                                  for item in kernel['constantReads']), 'indexed packet load inventory differs')
        shifted = {pc: target + PREFIX_BYTES for pc, target in mapping.items()}
        transformed = dict(code=code, pc_maps=[shifted], register_count=gpr + 1,
                           register_cap=max(old_cap, gpr + 1))
        self.index_entries.append(dict(ordinal=ordinal, entry=kernel['name'], sourceTextSha256=kernel['textSha256'],
            nativeTextSha256=elf.sha(code), nativeBytes=len(code), originalRegisterCount=gpr, registerCount=gpr + 1,
            originalRegisterCap=old_cap, registerCap=transformed['register_cap'], indexUniformRegister=UNIFORM,
            originalZ=z, packetStride=stride, indexedLoads=loads, pcMap=shifted))
        return {count: dict(transformed, parameter_bytes=parameter if count == 1 else 2 * stride) for count in (1, 2)}

    def package(self, transforms):
        for count in (1, 2):
            modules = []
            for module in self.inventory['sourceModules']:
                identity = module['identity']
                source, digest = self.sources[identity]
                kernels = [kernel for kernel in self.inventory['kernels'] if kernel['module'] == identity]
                selected = {kernel['name']: transforms[kernel['name']][count] for kernel in kernels}
                path = self.output / f'module-{identity}-shared-n{count}.cubin'
                receipt = elf.emit_module(source, digest, path, selected)
                label = f'module-{identity}-shared-n{count}'
                self.run([self.tools['cuobjdump'], '--dump-elf', path], label + '-elf')
                original_resources = clone.resources(self.run([self.tools['cuobjdump'], '--dump-resource-usage', source], label + '-original-resources'))
                resources = clone.resources(self.run([self.tools['cuobjdump'], '--dump-resource-usage', path], label + '-resources'))
                require(set(resources) == set(original_resources), 'resource function inventory differs')
                for name, original in original_resources.items():
                    expected = original
                    if name in selected:
                        value = selected[name]
                        expected = re.sub(r'REG:\d+', f'REG:{value["register_count"]}', expected)
                        expected = re.sub(r'CONSTANT\[0\]:\d+', f'CONSTANT[0]:{0x380 + value["parameter_bytes"]}', expected)
                    require(resources[name] == expected, 'resource metadata changed outside indexed ABI: ' + name)
                self.run([self.tools['nvdisasm'], '--no-dataflow', path], label + '-decode')
                for name, value in selected.items():
                    require(clone.section(path, receipt['sha256'], '.text.' + name) == value['code'], 'emitted indexed text differs')
                functions = sorted([dict(name=kernel['name'], paramSize=kernel['parameterBytes'][0], gridZ=kernel['gridShapes'][0][2])
                                    for kernel in kernels], key=lambda value: value['name'])
                modules.append(dict(originalSha256=module['sourceSha256'], path=str(path), sha256=receipt['sha256'], functions=functions))
            manifest = catalog(count, modules)
            path = self.output / f'full-model-shared-n{count}-manifest.json'
            path.write_text(json.dumps(manifest, indent=2) + '\n')
            self.bundles.append(dict(batchCount=count, manifest=str(path), manifestSha256=elf.sha(path.read_bytes()), semanticSha256=clone.semantic(manifest)))

    def execute(self):
        self.output.mkdir(parents=True, exist_ok=False)
        failure = None
        try:
            prefix, selectors = self.compile_prefix(), self.selectors()
            transforms = {kernel['name']: self.make_entry(kernel, selectors, prefix) for kernel in self.inventory['kernels']}
            require(len(transforms) == len(self.index_entries) == 44, 'model transformation is incomplete')
            self.package(transforms)
            for path, expected in list(self.inputs.items()):
                self.pin(Path(path), expected)
        except BaseException as error:
            failure = f'{type(error).__name__}: {error}'
            raise
        finally:
            report = dict(schema='nr-indexed-full-model-generation-v1', status='failed' if failure else 'cpu_audited', failure=failure,
                gpuExecuted=False, outputEquivalencePerformed=False, performanceQualified=False, inputs=self.inputs,
                entries=self.index_entries, logicalZGeneration=self.entries, bundles=self.bundles, jobs=self.jobs,
                limits=['Appended GPRs can alter occupancy; private GPU module loading and N1/N2 exact outputs remain mandatory.',
                        'Indexed LDCU.128 requires its separate native execution gate before full-model dispatch.'])
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
