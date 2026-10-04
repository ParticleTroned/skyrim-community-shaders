"""Build a pinned first-stage shared-body N1/N2 experiment without GPU execution.

The original model body reads a CTA-selected parameter packet. Captured ELF
metadata and instruction dependencies are preserved and independently decoded.
These artifacts require separate N1 and N2 output-equivalence qualification.
"""
from pathlib import Path
import argparse
import json
import re
import struct

import clone_model as clone
import preserved_elf as elf

ENTRY = 'cc_tinlayout_fused_swin_1h_32_1_inpview_tilesync_fp8'
TEXT_SHA = '866a3c05c234eb151ff659e6633af32a987c60f110e248264c94ca48d61aab6f'
PARAMETER_BYTES = 96
PREFIX_BYTES = 128
GPR, UNIFORM = 1, 19
require = elf.require
ENCODED = re.compile(r'/\*([0-9a-f]+)\*/\s*(.*?)\s*;\s*/\*\s*(0x[0-9a-f]+)\s*\*/\s*/\*\s*(0x[0-9a-f]+)\s*\*/')


def instruction_words(text):
    result = {}
    for _, instruction, low, high in ENCODED.findall(text):
        value = struct.pack('<QQ', int(low, 16), int(high, 16))
        instruction = instruction.strip()
        require(instruction not in result or result[instruction] == value, 'compiler instruction encoding is ambiguous')
        result[instruction] = value
    require(result, 'compiler fixture has no encoded instructions')
    return result


def address_load(chunk, instruction, *, parameter_bytes=PARAMETER_BYTES, gpr=GPR, uniform=UNIFORM, allow128=False):
    match = re.fullmatch(r'(?:@!?U?P[0-6] )?(LDCU|LDC)(\.64|\.128)? (?:U)?R\d+, c\[0x0\]\[(0x[0-9a-f]+)\]', instruction)
    require(match is not None, 'packet load is outside the first-stage address ABI')
    require(allow128 or match[2] != '.128', '128-bit indexed load requires separate qualification')
    offset = int(match[3], 16)
    width = 16 if match[2] == '.128' else 8 if match[2] else 4
    require(0x380 <= offset and offset + width <= 0x380 + parameter_bytes, 'packet load exceeds immutable source packet')
    low, high = struct.unpack('<QQ', chunk)
    shift = 37 if match[1] == 'LDCU' else 38
    require((low >> 24) & 255 == 255 and (low >> shift) & 0x1fff == offset,
            'constant address is already indexed or differs from its disassembly')
    index = uniform if match[1] == 'LDCU' else gpr
    require(type(index) is int and 0 <= index <= 254, 'index register is outside native encoding')
    result = struct.pack('<QQ', (low & ~(255 << 24)) | (index << 24), high)
    expected = instruction.replace(f'[{hex(offset)}]', f'[{"UR" if match[1] == "LDCU" else "R"}{index}+{hex(offset)}]')
    return result, expected


def admit_registers(instructions):
    require(instructions.get(0) == 'LDC R1, c[0x0][0x37c]', 'original stack prologue differs')
    require([pc for pc, text in instructions.items() if re.search(r'\bR1\b', text)] == [0], 'R1 is live in the model body')
    for text in instructions.values():
        require(not re.search(r'\b(?:LDL|STL|CALL|RET|JMP|BRX)\b|SR_CTAID.Z|\bUR19\b', text),
                'stack, indirect control, logical Z or uniform index register is live')
        if re.search(r'\bR0\b', text):
            wide_multiply = re.fullmatch(r'IMAD.WIDE R([0-9]+), R0, 0x[0-9a-f]+, R([0-9]+)', text)
            scalar_multiplicand = wide_multiply and all(int(value) >= 2 for value in wide_multiply.groups())
            require(scalar_multiplicand or not re.search(r'\.64\b|\.128\b|\b(?:CS2R|HMMA|QMMA|IMMA|DMMA|LDSM|IMAD\.WIDE)\b|\[R0', text),
                    'implicit wide GPR operand aliases R1')
        require(not (re.search(r'\bUR(?:16|17|18)\b', text) and re.search(r'\.64\b|\.128\b', text)),
                'implicit wide uniform operand may alias UR19')


def make_prefix(optimized, unoptimized):
    source = optimized['S2R R0, SR_CTAID.Z']
    source = clone.mask_producer(source, 'S2R R0, SR_CTAID.Z')
    low, high = struct.unpack('<QQ', source)
    source = struct.pack('<QQ', (low & ~(255 << 16)) | (GPR << 16), high)
    low, high = struct.unpack('<QQ', optimized['IMAD R0, R0, 0x60, RZ'])
    require((high >> 52) & 63 == 1 and (high >> 41) & 15 >= 5, 'index multiply lacks its producer wait or issue delay')
    multiply = struct.pack('<QQ', (low & ~(0xffff << 16)) | (GPR << 16) | (GPR << 24), high)
    low, high = struct.unpack('<QQ', unoptimized['R2UR UR4, R6'])
    require(low & 0xffff == 0x72ca and high == 0x000fc000000e0000, 'uniform transfer fixture differs')
    uniform = struct.pack('<QQ', (low & ~(0xffff << 16)) | (UNIFORM << 16) | (GPR << 24),
                          (high & ~clone.STALL_MASK) | (15 << 41))
    low, high = struct.unpack('<QQ', clone.NOP)
    delay = struct.pack('<QQ', low, high | (15 << 41))
    return source + multiply + uniform + delay * 5


def transform(original, instructions, prefix):
    require(elf.sha(original) == TEXT_SHA and len(original) == 45056 and len(prefix) == PREFIX_BYTES,
            'first-stage body or prefix identity differs')
    require(set(instructions) == set(range(0, len(original), 16)), 'source instruction coverage differs')
    admit_registers(instructions)
    body = bytearray(original)
    body[:16] = clone.NOP
    loads = []
    for pc, instruction in instructions.items():
        if pc == 0:
            continue
        constant = re.search(r'\bc\[0x0\]\[(0x[0-9a-f]+)\]', instruction)
        if constant and int(constant[1], 16) >= 0x380:
            replacement, decoded = address_load(original[pc:pc + 16], instruction)
            body[pc:pc + 16] = replacement
            loads.append(dict(pc=pc, original=instruction, indexed=decoded))
    require(len(loads) == 26, 'first-stage packet-load census differs')
    mapping = {pc: pc + PREFIX_BYTES for pc in range(0, len(original) + 1, 16)}
    return prefix + body, mapping, loads


def audit_code(original, instructions, candidate, decoded, loads):
    require(set(decoded) == set(range(0, len(candidate), 16)), 'candidate instruction coverage differs')
    expected_prefix = ['S2R R1, SR_CTAID.Z', 'IMAD R1, R1, 0x60, RZ', 'R2UR UR19, R1'] + ['NOP'] * 5
    require([decoded[pc] for pc in range(0, PREFIX_BYTES, 16)] == expected_prefix, 'indexed prefix decoding differs')
    source_high = struct.unpack_from('<Q', candidate, 8)[0]
    require((source_high >> 41) & 15 >= 2 and (source_high >> 46) & 7 == 0,
            'immediate index consumer lacks the corrected source dependency')
    replacements = {load['pc']: load['indexed'] for load in loads}
    for pc, instruction in instructions.items():
        expected = 'NOP' if pc == 0 else replacements.get(pc, instruction)
        target = clone.PC_TARGET.search(expected)
        if target:
            expected = expected[:target.start(1)] + hex(int(target[1], 16) + PREFIX_BYTES)
        require(decoded[pc + PREFIX_BYTES] == expected, f'candidate arithmetic/control differs at {pc:#x}')
        before, after = original[pc:pc + 16], candidate[pc + PREFIX_BYTES:pc + PREFIX_BYTES + 16]
        if pc in replacements:
            require(before[:3] == after[:3] and before[4:] == after[4:], 'indexed load changed more than its address register')
        else:
            require(after == (clone.NOP if pc == 0 else before), 'original model bytes changed')


class Generator(clone.Generator):
    """Reuse pinned capture admission and bounded tool receipts from clone generation."""
    def __init__(self, args):
        super().__init__(args)
        self.fixture = Path(__file__).with_name('indexed_prefix.ptx')
        for path in (self.fixture, Path(__file__).resolve()):
            self.pin(path, elf.sha(path.read_bytes()))

    def prefix(self):
        compiled = {}
        for level in (0, 3):
            path = self.output / f'prefix-O{level}.cubin'
            self.run([self.tools['ptxas'], '--gpu-name', 'sm_120', '--opt-level', str(level), self.fixture, '--output-file', path], f'prefix-O{level}-compile')
            compiled[level] = instruction_words(self.run([self.tools['nvdisasm'], '--no-dataflow', '--print-instruction-encoding', path], f'prefix-O{level}-decode'))
        return make_prefix(compiled[3], compiled[0])

    def generate(self):
        kernel = self.inventory['kernels'][3]
        require(kernel['name'] == ENTRY and kernel['gridShapes'] == [[20, 20, 1]] and kernel['blockShapes'] == [[32, 1, 1]] and
                kernel['parameterBytes'] == [PARAMETER_BYTES] and kernel['ctaZ'] == [], 'first-stage captured launch contract differs')
        source, source_sha = self.sources[kernel['module']]
        original = clone.section(source, source_sha, '.text.' + ENTRY)
        raw_source = self.write_bytes('source-body.not-for-execution.raw', original)
        instructions = clone.records(self.run([self.tools['nvdisasm'], '--binary', 'SM120', '--no-dataflow', raw_source], 'source-body-decode'))
        code, mapping, loads = transform(original, instructions, self.prefix())
        raw_candidate = self.write_bytes('indexed-body.not-for-execution.raw', code)
        decoded = clone.records(self.run([self.tools['nvdisasm'], '--binary', 'SM120', '--no-dataflow', '--print-instruction-encoding', raw_candidate], 'indexed-body-decode'))
        audit_code(original, instructions, code, decoded, loads)
        original_resources = clone.resources(self.run([self.tools['cuobjdump'], '--dump-resource-usage', source], 'original-resources'))
        for count in (1, 2):
            path = self.output / f'tilesync-indexed-n{count}.cubin'
            receipt = elf.emit_module(source, source_sha, path, {ENTRY: dict(code=code, pc_maps=[mapping], parameter_bytes=count * PARAMETER_BYTES)})
            self.run([self.tools['cuobjdump'], '--dump-elf', path], f'n{count}-elf')
            usage = clone.resources(self.run([self.tools['cuobjdump'], '--dump-resource-usage', path], f'n{count}-resources'))
            require(set(usage) == set(original_resources), 'native function resource inventory differs')
            for name, value in original_resources.items():
                expected = re.sub(r'CONSTANT\[0\]:\d+', f'CONSTANT[0]:{0x380 + count * PARAMETER_BYTES}', value) if name == ENTRY else value
                require(usage[name] == expected, 'shared-body resource metadata changed: ' + name)
            self.run([self.tools['nvdisasm'], '--no-dataflow', '--print-instruction-encoding', path], f'n{count}-decode')
            require(clone.section(path, receipt['sha256'], '.text.' + ENTRY) == code, 'emitted text differs from audited body')
            manifest = dict(schema='nr-n1-kernel-replacement-v1' if count == 1 else 'nr-pair-kernel-replacement-v1', batchCount=count,
                original=dict(moduleSha256=self.inventory['sourceModules'][kernel['module']]['sourceSha256'], entry=ENTRY, paramSize=PARAMETER_BYTES),
                candidate=dict(path=str(path), sha256=receipt['sha256'], entry=ENTRY))
            manifest_path = self.output / f'tilesync-indexed-n{count}-manifest.json'
            manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
            self.bundles.append(dict(batchCount=count, path=str(path), sha256=receipt['sha256'], manifest=str(manifest_path), manifestSha256=elf.sha(manifest_path.read_bytes())))
        self.entries.append(dict(entry=ENTRY, sourceTextSha256=elf.sha(original), candidateTextSha256=elf.sha(code),
            originalBytes=len(original), candidateBytes=len(code), indexGpr=GPR, indexUniformRegister=UNIFORM,
            originalGridZ=1, packetStride=PARAMETER_BYTES, pcMap=mapping, indexedLoads=loads))

    def execute(self):
        self.output.mkdir(parents=True, exist_ok=False)
        failure = None
        try:
            self.generate()
            for path, expected in list(self.inputs.items()):
                self.pin(Path(path), expected)
        except BaseException as error:
            failure = f'{type(error).__name__}: {error}'
            raise
        finally:
            report = dict(schema='nr-indexed-first-stage-generation-v1', status='failed' if failure else 'cpu_audited', failure=failure,
                gpuExecuted=False, outputEquivalencePerformed=False, performanceQualified=False, inputs=self.inputs,
                entries=self.entries, bundles=self.bundles, jobs=self.jobs,
                limits=['Only the pinned SM120 tilesync entry and original Z1 shape are admitted.',
                    'N1 output equivalence must pass before N2 dispatch; CPU decoding is not GPU qualification.'])
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
