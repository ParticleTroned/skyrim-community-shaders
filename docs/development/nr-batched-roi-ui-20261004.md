# Character ROI methods and shared-body batching

## User controls

Character NR now exposes three persisted ROI methods at Info logging in
pipelines A, B and C: automatic single ROI, independent multi-ROI and
batched multi-ROI. Automatic single uses the existing enclosing-region
planner. Independent retains separate native evaluations. Batched uses
the shared-body kernel catalog when the captured native graph and resource
contracts match; otherwise it reports the reason and uses independent
execution. Selecting a method does not change model context, output
ownership, preset, camera or AI.

The ordinary ROI eligibility and edge settings are also visible at Info.
Forced masks, visibility/composite bypasses and diagnostic images remain
in Developer Mode. `nr_configure.characterRoiMethod` exposes the same
three methods through DevBench. `communityshaders.nr_kernel` additionally
selects original, layer-control, cloned-body or shared-body execution for
comparisons and original-only graph inspection in A/B/C.

## Runtime ownership

The default automatic method creates no batching adapter, provider hooks
or private kernel resources. Batched selection attaches the adapter before
native feature creation, forwards three original warmup frames, and
checks the complete native graph, provider, hardware, parameter packets,
descriptor ownership and allocations before private submission. It does
not reject A/B solely because of their pipeline name. Different graphs or
incompatible region layouts receive a visible fallback reason.

Packet vectors and command proxies have bounded frame owners and retire
only after their exact command-context fence completes. A pre-submission
admission rejection can retry the original logical request only when no
native launch was attempted and the unsubmitted list and provider epoch
are safely retired. A native callback failure, partial submission, foreign
thread or uncertain fence ownership prevents that retry. Uncertain GPU
owners are retained and NR fails closed.

The shared implementation is compiled independently of DevBench. The
bridge adds inspection and comparison controls; ordinary selection and
the runtime adapter are not bridge-gated.

## GPU fault isolation and correction

The first complete indexed catalog appended a GPR and transferred its
packet index into a uniform register. Its full N1 test timed out at GPU
completion, as did a prefix test replacing only the first copy stage.
All recorded native launch calls returned success; device-removal reason
was zero. Those candidates are rejected by the active catalog pins.

An allocation-only N1 control retained the original instructions with
the same expanded register budgets. It passed five lanes and 100 exact
RGBA outputs. Register growth alone therefore did not reproduce the
fault; no individual instruction is claimed as the sole cause.

The replacement computes the packet index directly in UR48 using the
official uniform producer sequence. Each GPR packet load temporarily uses
its own destination register for the index, with matching predication and
an explicit dependency drain. Original register counts, caps, stack,
local memory, shared memory and arithmetic remain unchanged. Branches and
PC metadata are relocated through the existing preserved-ELF utilities.

## Exact-output validation

The corrected catalog passed the complete N1 gate: six lanes, 120 saved
RGBA outputs, all byte-identical to the unchanged provider. The matching
N2 gate passed five lanes and 100 byte-identical outputs. Each steady N2
frame retains 632 logical stages across four private regions and submits
316 physical kernel launches, with no original-kernel suffix. The gates
also verify immutable inputs, unowned output sentinels, complete packet
packing, all 44 private functions, all nine modules and hook retirement.

The preserved replay executable SHA-256 is
`364d51c2f8c72278d7235345f309140804939ef3e7ebfe7770f5f3d360eef257`.
The provider SHA-256 is
`8270b350cd82de5ce89806872cdd6b6a9249b80836b91bbeb3573470744cc206`.
The shared N1 semantic catalog is
`2b4b945d3ac65ebc5c7d9f2ab72405d892412d59d875864c51edb611f7bba6c8`;
shared N2 is
`094ecf56151133f64e9c57553d5aabf62fd4bea615d4e75b071301005b804597`.
The cloned N2 comparison remains
`2b11eb5028bbdbb3b539be8789f10052868f46b5c256bdc16c1003a3e7f8d76e`.

Evidence remains local beneath
`build/validation/nr-true-batch-20261004/indexed-neutral-model-01/`:
`audit.json`, `source-audit.json`, `n1-gpu-01/run.json` and
`n2-gpu-01/run.json`. The new producer passed all eight CTest tests. CPU
generator checks passed all 14 indexed tests and five packaging tests.

These are exact-output and lifecycle gates for the captured stateless C
scene on the admitted SM120 RTX 5070 Ti Laptop provider. Short gate timing
windows include large excursions and do not qualify a performance gain.
Live A/B/C visual, temporal, transition and performance checks remain
necessary for the new DLL. In-game benefit or universal support is not
claimed from these offline results.

## Requested testing archive

The testing AIO uses a VR-only DLL with DevBench enabled. Its installer
omits SE/AE and Horizon Fix choices and reuses the preserved 4,301-record
optimized VR shader cache. Packaging requires exact original shader and
feature bytes, matching cache ABI, unchanged cache bytes, full successful
N1/N2 catalog receipts, and archive extraction/hash verification. The
added native kernel payload does not replace D3D11 shader-cache records.
Archive receipts retain the physical DLL SHA-256, producer Build ID and
source identity. Packaging does not install the DLL or launch Skyrim.
