# Shared-body captured-model generation

These CPU tools generate standalone SM120 replay experiments from the same
pinned live-capture inventory used by `clone_model.py`. They do not inspect
an installed provider, load CUDA modules, run GPU work or modify the game.
Generated binaries and captured inputs remain local; none is versioned.

`indexed_model.py` transforms only the actual first tilesync entry. It reuses
the proven-unused R1 and UR19 registers without changing resource allocation.
It changes 26 user-packet loads, preserves the runtime descriptor constant,
and emits identical N1/N2 instruction bodies with different packet extents.
The source S2R has a delay of at least two cycles before the dependent index
calculation. An explicit producer wait and conservative uniform-transfer
delay precede the original body.

`indexed_full_model.py` transforms all 44 captured entries. Every entry has
one persistent GPR appended beyond its authoritative original register
count. Explicit `register_cap` metadata admits expansion where necessary;
the emitter preserves the function-symbol identity, local frame, stack and
shared resources. The added register can change occupancy, so output and
performance qualification are separate requirements.

The full model uses UR48 only after checking the pinned uniform-register
inventory, including conservative spans for texture, descriptor and block
copy operands. The packet index is
`floor(physicalCTA.z / originalGrid.z) * alignUp(originalPacketBytes, 16)`.
Original grid Z values 1, 2 and 4 retain their logical Z through the same
qualified masks used by clone generation. Each context retains all original
packet bytes, pointers and model arithmetic. N2 host packing must zero the
alignment padding and concatenate two complete independent packets.

Both commands accept the same arguments as `clone_model.py`:

```powershell
$indexArgs = @(
    '--inventory', '<capture-root>/full-chain-static-01/inventory.json',
    '--capture-root', '<capture-root>',
    '--output', '<new-output-directory>',
    '--cubit', '<pinned-cubit.exe>',
    '--table', '<pinned-sm120.json>',
    '--nvdisasm', '<pinned-nvdisasm.exe>',
    '--cuobjdump', '<pinned-cuobjdump.exe>',
    '--ptxas', '<pinned-ptxas.exe>'
)
python tools/nr-replay/indexed_model.py @indexArgs
# Select another new output directory before invoking the full-model tool.
python tools/nr-replay/indexed_full_model.py @indexArgs
python -m unittest discover -s tools/nr-replay -p 'test_indexed*.py' -v
python tools/nr-replay/test_preserved_elf.py
```

Every input/tool identity, complete PC map, changed packet load, original
and candidate resource count, bounded subprocess result and final module
hash is retained in `audit.json`. Existing output directories are rejected.
Interruption leaves a failed receipt. CPU success requires exact original
arithmetic and scheduling bytes outside the admitted prefix, logical-Z
transformation and packet-index fields, followed by official disassembly,
ELF and resource validation.

Before executing full-model candidates, qualify the indexed 128-bit uniform
load with a bounded pointer-independent native diagnostic. Then load the
complete module set and qualify N1 exact final images before N2. CPU checks
do not establish output equivalence, GPU retirement, supported shapes or a
performance gain. The pinned captured geometry and stateless private
contexts require separate qualification before extending the runtime
adapter to other presets, shapes or temporal histories.

## Resource-neutral full-model generation

The resource-neutral alternative is `indexed_neutral_model.py`, using the
same command arguments and a new output directory. Its official uniform
prefix computes the packet index in UR48. Each GPR constant load temporarily
uses its own destination as the address index, with a matching-predicate
scalar move and a bounded dependency drain. The load then replaces that
temporary value. Uniform constant loads use UR48 directly. Original GPR
budgets, register caps, local frames and shared memory remain unchanged;
all inserted instructions and relocated control-flow targets are audited.

The earlier full-model generator that appends one GPR completed CPU checks
but timed out in the full-model N1 GPU gate. Its allocation-only control
passed, so register growth alone does not explain that failure. The
resource-neutral alternative requires its own N1 and N2 image and
retirement gates; CPU decoding and unchanged resource counts are not a
substitute for them.

The retained CPU-audited neutral catalogs have semantic hashes
`2b4b945d3ac65ebc5c7d9f2ab72405d892412d59d875864c51edb611f7bba6c8`
(N1) and
`094ecf56151133f64e9c57553d5aabf62fd4bea615d4e75b071301005b804597`
(N2). These identify generated code, not a GPU qualification result.

## Portable AIO payload

After qualification, package the pinned cloned and shared N2 catalogs into
a new payload directory:

```powershell
python tools/nr-replay/package_kernel_catalog.py `
    --cloned '<clone-output>/full-model-n2-manifest.json' `
    --shared '<shared-output>/full-model-shared-n2-manifest.json' `
    --output '<new-payload-directory>'
python tools/nr-replay/test_package_kernel_catalog.py
```

The utility requires the two known semantic catalogs, exactly nine modules
and 44 functions per catalog, complete original packet/geometry identities,
and every candidate module SHA-256. It produces `cloned-n2.json`,
`shared-n2.json`, hash-addressed `.cubin` files and a package receipt.
Module paths become simple relative basenames without changing semantic
identity. Source files and existing output directories are preserved.
The payload directory can be passed to `CSX_NR_KERNEL_PAYLOAD_DIR` for
installation beneath `Shaders/Upscaling/NeuralRendering/KernelBatch`.
Packaging verifies identities; it does not qualify a GPU execution path.
