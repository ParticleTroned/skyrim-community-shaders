# Independent-context NR batching assessment — 2026-10-04

## Result

The offline prototype now batches independent model contexts while
preserving exact output. All 44 kernel entries use separate region
packets in true N=2 launches: 316 physical launches replace 632 logical
stages. The latest same-submission ABBA/BAAB comparison passed all 528
saved full outputs/crops and 15,168 actual N=2 calls, with clean hook and
native-object retirement.

In the predeclared analysis window, mean GPU cost fell from 12.914875 to
8.544938 ms against original order (**33.836468%**) and from 9.899313 to
9.277500 ms against layer-ordered originals (**6.281371%**). All eight
analyzed paired blocks favored N=2 in each comparison, with sixteen
passes per mode. The first two blocks were designated schedule warmup
before execution; every timing remains retained and no outlier was
removed. Including those two blocks, the layer comparison reverses:
10.953800 to 11.636250 ms, **6.230258% slower**. Timing spikes remain
visible in the analyzed window too.

This is bounded offline evidence for the fixed captured stateless-C
context, two equal regions per eye and the pinned GPU/provider. It does
not establish proportional pixel scaling, general performance, changing
game inputs, temporal behavior or production readiness. The 320 graph
floor is unchanged. No production path, setting, cost profile, game DLL
or AIO was promoted; a bounded DevBench-only runtime adapter and live
qualification are still required. Earlier failed candidates and timing
brackets remain recorded below.

### Earlier findings

No supported provider interface for evaluating several independent model
contexts in one inference call was identified. CSX already submits its
independent ROI calls in one D3D12 command list with one interop completion.
Submission batching and model batching are different operations.

A new offline experiment successfully reused one native handle per eye
without changing the tested independent outputs. Four evaluations retained
four private outputs while resident native handles fell from four to two.
Every retained RGBA crop matched the independent-handle reference exactly
across both fresh-process brackets. Inputs, dimensions, original coordinates,
valid rectangles, tuning and per-call reset were unchanged.

This does **not** establish the requested substantial performance gain.
Both timing brackets failed the predeclared 5% baseline-drift gate, and
recurring 50–100 ms samples remain unexplained. No production path, setting,
cost profile or DLL was changed or promoted by this investigation.

The live-memory follow-up found an active lower-level kernel-chain API,
currently used with one descriptor per call. Inspected pre/first-compute
kernel variants do not provide independent-image batch addressing. The
separate padding-floor probe completed safely but changed native output,
so it was not adopted. Detailed findings and remaining qualification limits
are recorded below. Skyrim and MO2 were subsequently closed cleanly.

The subsequent standalone kernel-chain implementation preserved exact output
and reduced driver submissions from 632 to 76 for four regions. It retained
all 632 kernel descriptors and grouped only within each region. Three
fresh-process short-window comparisons measured approximately 8.5% higher
GPU cost. Longer windows were unstable even without interception; those
results remain excluded from performance qualification. No production path
was promoted.

## Identity and retained evidence

Local evidence root: `build/validation/nr-independent-context-20261004/`.
`assessment.json` binds the live findings, complete native report, full-area
references and individual result hashes. Raw evidence remains local.

-   Live producer Build ID:
    `96180b99b07706cfac34693d523771ed1df97124d2a712521aa990c3237d1942`.
-   Compiled source: `1a455129b732a1b3e6d3fea9cdc461bdb0ad9698`, dirty digest
    `a1daa53ec31973a50fdf88692c08722bdff6fdcc5e814b0d73e735bec23e1a34`.
-   Enabled AIO: `CSX_AIO-main-vr-nr-SharedContext-DevBench-VR-NoShaderCache-20261004-96180b99b077`.
-   Physical DLL: 31,667,200 bytes, SHA-256
    `95210e747f6ebcd7b2d125c454ba0b6223c564844a7e2dcfef66b3a553b71e8d`;
    adjacent manifest, archive identity and runtime producer agree.
-   Provider 310.8.0, SHA-256
    `8270b350cd82de5ce89806872cdd6b6a9249b80836b91bbeb3573470744cc206`.
-   Final standalone replay SHA-256:
    `fb1ac2f2053f2f3ab527deb68728cce6778f875561a76e729a4ebdec68dee5ab`.
-   GPU: RTX 5070 Ti Laptop, driver 610.88.
-   Fresh frozen capture: `capture-1791098574068482-47524-1`, one complete
    stereo C frame, 1008×1120 per eye, RGBA8 colour, R32 depth and RG16 motion.
    All nine copied files were hash-verified; originals were retained.

The earlier diagnostic replay binary preceded mechanical formatting and
has SHA-256 `b5a7db56bbec064758d1950faddfde27d395b1a82e21565078c13eaec9c078f9`.
Its results are retained separately and are not substituted for the final
producer's references.

## Live verification

Skyrim PID 47524 started at `2026-10-04T06:57:43.933828Z`. The loaded
Bannered Mare view contained three detected actors. AI was disabled for
stationary measurements, subsequently confirmed by the user. No camera
command changed the view. Start/end position differed only by 0.0000305
world units vertically; horizontal position and rotation were identical.

All four timing windows used Debug mode, screenshot/native capture off,
300 resolved frames and the guarded bounded profiler API. GPU values below
are the inclusive NR D3D11 scope, including native/interop work; they are
not added to overlapping D3D12 native timers.

| Window                               | GPU mean ms | CPU mean ms | Interpretation                                                                     |
| ------------------------------------ | ----------: | ----------: | ---------------------------------------------------------------------------------- |
| Normal independent planner           |   10.597465 |    3.211810 | Tight splits and pending-bound fallback changed during the window; diagnostic only |
| One call per eye, reference 1        |    9.071671 |    1.558057 | Original contexts                                                                  |
| One call per eye, enclosing/halo 256 |    9.096796 |    1.581454 | Correct no-op, original contexts retained                                          |
| One call per eye, reference 2        |    9.127099 |    1.556247 | Original contexts                                                                  |

The enclosing no-op differs from the bracket mean by −0.002589 ms
(−0.0285%): neutral within the bracket. It reports
`no_coalescing_opportunity`. The new completed-timing evidence reports
`physicalPlanSource=frozen_execution` and correct physical evaluation counts
for both this two-call case and the forced four-to-two coalescing control.
Nearby planning telemetry is not used to reconstruct the sampled context.

The final live health observation recorded 45,086 evaluations, four resource
rebuilds total, and zero NR failures, stereo failures, quarantines or device
removals. Warm timing windows did not rebuild resources. This validates the
installed corrections; it does not qualify merged-context image equivalence.

The readiness limitation remains: current-source GPU bounds alternate
between ready and pending. The first nonblocking stereo decision is frozen
before mask preparation; pending safely uses larger projected geometry.
Reusing old bounds or waiting for the GPU was not introduced.

## Native handle experiment

`tools/nr-replay/native_handle_probe.py` uses the existing admitted runtime,
exclusive campaign lock, closed-game checks and immutable input validation.
Only explicit stateless C, equal-grid, 2–4 disjoint regions with extents at
least 128 are admitted. Outputs remain private; a conservative global UAV
barrier orders reuse of handle-private work. Batch GPU timing includes these
barriers. There is no claim of a documented provider batch/concurrency API.

Both eyes used unchanged full backing resources and these independent
rectangles: `(384,512,320,448)` and `(880,640,128,384)`. The second rectangle
was expanded for this probe's minimum extent; this is a new fixed reference,
not an assertion that it reproduces the live 112-pixel context.

Total evaluated area was 385,024 pixels versus 2,257,920 for full stereo:
**82.95% fewer pixels**, with **four calls versus two**. Each of six fresh
processes ran 16 declared warmups and 16 measured samples, ordered
independent/reuse/independent and then reversed. All 192 submissions and
768 evaluations completed; immutable colour/depth/motion, complete writes,
outside-write checks, clean retirement and retained output hashes passed.

All 96 measured stereo output sets were bitwise equal across reference and
reuse processes. This is exact frozen-input C evidence only; it is neither
temporal-mode qualification nor equality to a larger model context.

The following are **unqualified raw means**, including every spike:

| Bracket | Independent before ms | Reused handles ms | Independent after ms | Baseline drift |
| ------- | --------------------: | ----------------: | -------------------: | -------------: |
| Forward |             39.265125 |         63.069813 |            32.731563 |         18.15% |
| Reverse |             53.115438 |         61.321875 |            62.729375 |         16.60% |

Final-binary full-area reference means were 40.953313 and 32.832313 ms;
these are likewise unstable. Fast individual samples around 8.6–8.8 ms
are retained diagnostics, not a replacement statistic or a speed claim.
Increasing warmup from 3 to 16 did not remove later 50–100 ms spikes.

No Nsight installation was found. The installed WPR/GPUView trace failed
before replay with `0xc5585011` (system profiling policy unavailable).
No ETL was collected, no recording remained active, and no system policy,
clock, driver or security setting was changed. An idle P8 clock observation
does not establish workload throttling or explain the spikes.

## API finding and next useful work

The actual provider exports singular NGX evaluation entry points. The pinned
SDK supplies no NR-specific independent-image batch descriptor. Streamline's
input-structure array is not such a descriptor; its
[core API](https://github.com/NVIDIAGameWorks/Streamline/blob/main/include/sl_core_api.h)
and [programming guide](https://github.com/NVIDIAGameWorks/Streamline/blob/main/docs/ProgrammingGuide.md)
do not establish independent-context NR batching. NGX/Streamline CPU calls
are not thread safe. Private undocumented parameters cannot be ruled out,
but none was established or guessed into the live renderer.

The remaining substantial opportunity requires amortizing native work:

1. A provider-supported independent-image batch or provider-internal
   optimization that preserves each element's context, guides, output and
   temporal state. Putting rectangles in one atlas or enlarging their domain
   has already changed outputs in the retained context experiments.
2. A working native GPU trace to distinguish internal fixed work from
   scheduling gaps before altering synchronization or queue ownership.
3. Separately diagnose whether current-source bounds become naturally ready
   at a later safe planning point. This can avoid large fallback rectangles,
   but cannot remove the per-invocation cost or justify stale geometry.

Handle reuse is now an exact-output candidate for this captured C fixture,
with no demonstrated speed benefit. It remains offline. Existing capacity
and source-sharing experiments also did not establish substantial native
gains; repeating them is not the leading next action.

## Validation and cleanup

-   Standalone Release build passed; final executable input-admission tests:
    **15/15**.
-   Focused Python tests: **39/39** (handle probe 6, packed report 15,
    context probe 8, packed runner 10).
-   Adversarial review fixed journal binding, original resource/context-domain
    validation and warmup write/sentinel validation. Negative tests cover each;
    a second read-only review found no remaining issue within that scope.
-   Scoped pre-commit passed for all changed standalone probe files.
-   Native replay campaign and exact RGBA checks passed; timing qualification
    **failed**, and no production performance profile was adopted.
-   `prepare_tuning` success-marker correction passed the live guarded API;
    feedback `AUTO-20261004-060455702-26C6E3BC` was amended with evidence.
-   A separate DevBench `setTimeScale` convergence failure left an effective
    multiplier despite a 504 response. Standard `sgtm 1` restored verified
    requested/effective 1 before measurements. Feedback:
    `AUTO-20261004-071655605-355164CA`. No DevBench source fix is claimed.
-   Calendar time was frozen separately and restored to the prior-session
    default 20; its initial calendar global was not directly recorded. No
    save or persistent CSX settings write occurred. AI remained disabled as
    requested until exit; experimental NR settings were restored.
-   Skyrim exited through `qqq`. Exact MO2 recovery close completed, session
    and access leases were released, and surviving SteamVR processes had no
    `usvfs` modules. No new AIO is needed for these offline tool changes.

## Fresh live-provider investigation after restart

This follow-up uses the new running process, not the preceding replay or an
older binary snapshot. PID **74352** started at
`2026-10-04T08:18:00.4611865Z`. Its CSX Build ID and physical DLL hash match
the producer above. NR was initially disabled. At the user's direction it
was enabled in reduced-resolution C with character multi-ROI; Render Scale
was already active. Foveation, camera and AI were not changed.

Evidence root: `build/validation/nr-private-batch-20261004/`.
`live-module-74352/provenance.json` binds the fresh mapped image, PID,
start FILETIME, loaded path, memory protections, region hashes and physical
provider identity before and after capture. The live provider is classified
`patched-recognized`; this is not a claim of NVIDIA publisher trust.

-   Loaded NR base: `0x7ff813f40000`; mapped size: 165,945,344 bytes.
-   Capture interval: `2026-10-04T08:26:46.827816Z` through
    `2026-10-04T08:26:47.789294Z`.
-   Fresh mapped-image SHA-256:
    `17dc5425b1dd6b081c50539c2cc06db7481ef869e01ff9d666e198b0f4a8caa4`.
-   Ghidra reports `01-fresh-network.txt` through `05-fresh-launch.txt`
    verify the imported image and region hashes before targeted analysis.
    Analysis used temporary read-only imports of this live capture.
-   Memory access was read/query only, with no suspension, patching or
    injection. Captures are non-atomic observations, not execution traces.

### What the current live objects establish

The public creation path fixes the internal `N` dimension to one. Fresh
Ghidra evidence traces this through the manager and `CCNetwork` constructor;
the current heap contains four distinct managers/networks, all with `N=1`,
separate history records and 71 resident block pointers each. A resident
block count is not a measured GPU dispatch count.

The current model also has a graph-shape floor. The following table uses
**width by height**; the internal fields at `+0x278` through `+0x294` are
stored in height/width order.

| Resident instances | Active width × height | Padded width × height | Padded/active area |
| ------------------ | --------------------: | --------------------: | -----------------: |
| Two small contexts |             192 × 256 |             320 × 320 |            2.0833× |
| Two large contexts |             768 × 800 |             768 × 832 |            1.0400× |

Fresh builder RVA `0x3C580` clamps the applicable graph to at least 320 in
each axis and aligns it using the network's downsampling factors, observed
as 64 for this graph. It rebuilds blocks at padded extents. The active-size
setter at `0x3E3A0` updates the pre/post limits; it does not rebuild the
central blocks at the smaller active dimensions. This explains a structural
reason for nonlinear scaling. It does not measure the padded work's share
of GPU time or establish a new performance gain.

The first separate heap reads crossed graph rebuilds and are retained as
inconclusive for layer identity. Bounded dependent reads in
`live-heap-12-graph` and `live-heap-13-launch-backend` subsequently verified
the selected networks, vectors and dimensions at both observation boundaries.
These checks still cannot provide atomicity or rule out an ABA change.

The selected live pre/post layers resolve to RVAs `0x60CF0` and `0x758A0`.
They construct one image resource/metadata packet, derive their launch
dimensions from spatial extents and pass a literal fifth backend argument
of one. Their fourth layer argument is unused; the first fused compute
layer at `0x637B0` likewise does not use that argument. The block caller
`0x2AC60` supplies it from the first field of each layer's shape record.
The backend at `0x3D9D0` forwards to another kernel wrapper; the final GPU
kernel signature and independent-item strides have not been established.

The internal `N` field is therefore a real lead, but not a usable batch API
or proof that changing it produces correct independent outputs. No `N>1`
execution, kernel batch isolation, visual equivalence or speedup was tested.
The next useful experiment is to establish batch-aware tensor addressing
and per-item pre/post, guide, origin and history handling for matching-shape
contexts, then compare against independent calls. Removing the 320 floor
or combining contexts spatially is not established as output-preserving.

At this earlier checkpoint, bound status recorded **174,839 successful
evaluations**, zero NR failures, zero device removals and zero quarantines.
NR and Skyrim remained running. No production implementation or installed
DLL was changed, and no explicit settings-save command was issued; only
the requested runtime NR
controls and local investigation artifacts were used. Earlier unqualified replay
timings are not used to reject the independent-batching opportunity.

### The different four-region counts

The earlier observations did not establish a four-image provider batch:

-   The four CSX logical slots were main left/right and submit left/right.
    They were not interchangeable character regions.
-   The provider accepts Color, Depth, MVec and Output subrectangles for one
    evaluation. These are resource coordinates, not four independent images.
-   Two independent regions per eye use four native evaluations across
    stereo. Task 8 later exercised four regions per eye with eight native
    evaluations and private histories. These capacities do not establish
    a multi-image evaluation entry point.

These distinctions are preserved in the September 8 multi-ROI conclusion,
`dlss5-character-neural-rendering.md`, `RegionCapacity.h` and
`nr-task8-9-qualification-20261003.md`. The current live heap's four
independent `N=1` graphs agree with separate-context execution.

### What an independent batch implementation would require

The selected path carries `N` through graph metadata, but the observed
pre/post and first compute implementations do not use that incoming field.
This is consistent with a general tensor interface selecting a specialized
single-image implementation; it does not establish the provider author's
intent or rule out an unobserved alternative implementation.

A valid batched path must preserve each item's original context, source and
guide origins, active bounds, output ownership and history. Every compute
stage must address the correct item, including tensor/scratch strides,
reductions, attention and boundary handling. Allocations and dispatches must
cover all items with checked sizes. Adding an outer batch value while any
stage still operates on one item cannot satisfy that contract.

The smallest useful prototype is two equal padded shapes in stateless C,
compared with two independent `N=1` evaluations of the same captured inputs.
An existing compatible kernel family would be preferable if live tracing
establishes it. Otherwise the affected kernels and their metadata path need
implementation changes; a caller-side parameter or atlas does not provide
that isolation. Exact output checks precede timings, followed by larger
counts, differing shapes and finally temporal histories if C qualifies.

### Active kernel-chain API found in the live process

Further live tracing established a lower-level submission opportunity.
The selected preprocessing layer resolves through a CPU backend/kernel
pair to `CubinBackendNGX`, whose module container was read from the live
process. Fresh interface flags at `+0x3AA` and `+0x3AB` were both one,
selecting `NvAPI_D3D12_CreateCuModule`, `CreateCuFunction` and
**`LaunchCuKernelChain`**. The provider currently passes one kernel descriptor
to that chain API. The local declaration in `extern/nvapi/nvapi.h` accepts
an array with separate function handles, grid/block dimensions, dynamic
shared memory, parameter pointers and parameter sizes.

This is a concrete multi-kernel submission interface, distinct from the
unestablished multi-image NR evaluation API and fused tensor batching. The
header marks it experimental/internal. It does not document sufficient
ordering, visibility or parameter-lifetime guarantees to assume that
collecting current calls into one array is equivalent.

In particular, the current backend normally inserts a global UAV barrier
before dispatch. It also builds parameter packets on the CPU stack. A
prototype must first forward unchanged while recording command boundaries
and retaining owned parameter copies. It must preserve resources, feature
lifetimes, ordering, copies and transitions, and cannot defer work across
existing barriers without establishing an equivalent visibility guarantee.
Independent contexts at the same layer may offer a safe grouping boundary
after a dependency and alias audit. Descriptor grouping may reduce driver
submission cost without reducing GPU kernel count or model padding; no
performance gain is established yet.

Evidence: `live-heap-15-kernel-pair`, `live-heap-19-kernel-interface`,
`live-heap-21-kernel-container`, `live-heap-22-kernel-flags`, and the fresh
Ghidra reports 05–07 and corrected 09 under `live-module-padding-74352`.
Report 08 targeted incorrectly converted RVAs and is retained but excluded.
The detailed argument and barrier audit is
`live-module-padding-74352/independent-kernel-chain-review.md`.

The complete small-context graph observation in `live-heap-17-all-shapes`
contains 71 blocks, 152 layer objects, 42 distinct names and 18 layer vtables;
all stored input/output shapes have `N=1`. Its smallest observed input
spatial shape is **8×8**, not an inferred 5×5. Shape records contain one
initial input plus one output per layer, established by fresh initializer
RVA `0x2C060`. All 591 dependency boundary checks matched. These are resident
graph observations, not GPU dispatch counts or an atomic snapshot.

### Padding-floor experiment: executed, output equivalence rejected

The standalone replay now has an explicit, default-off
`--experimental-provider-floor 256` probe. It verifies the exact provider
disk identity, full loaded code hash, instruction window and memory
protection before changing the single shared clamp immediate at RVA
`0x3C83E`. It changes only its own loaded module and restores the original
code after GPU-idle proof. No game DLL, provider file or production path
was changed.

The first candidate in `floor256-campaign-01` stopped before patching:
the runtime returned an uppercase SHA-256 while the pin was lowercase.
Exact hexadecimal canonicalization fixed this probe admission defect.
The original executable and failed receipt remain preserved. Final probe
executable SHA-256 is
`9c42d009b44fd1930380ac0addfaa6a19c7759facc5882779a3526f7d2a0437d`.
CPU validation passed 211 contract checks and 17 input/parser tests;
22 existing probe tests also passed before that localized correction.

`floor256-campaign-02` used the immutable C capture with manifest SHA-256
`89b6ea194420197db499cfc6551fbe5ec064b4e8e0f5e2bbcb1cf751041af9b4`.
Each eye retained its complete 1008×1120 input storage, independent handle,
and output rectangle `(448,576,192,256)`. The modeled graph changes from
320×320 to **320×256**, not 256×256: the extra alignment branch remains.
This is a 20% reduction in modeled graph area, not a measured GPU saving.

Fresh processes ran baseline, candidate, baseline, with three warmups and
two measured samples each. All 15 submissions and 30 native evaluations
completed. Every retained warmup and measured sample passed unchanged
colour/depth/motion, finite/complete owned writes, outside-write checks and
within-process output repeatability. The candidate's original provider code
hash and page protection were restored after GPU-idle proof. The installed
provider's disk hash remained unchanged.

The unmodified reference outputs matched exactly across both processes.
The candidate did not:

| Eye   | Changed RGB pixels / owned pixels | Maximum absolute RGB difference | Mean absolute RGB difference |
| ----- | --------------------------------: | ------------------------------: | ---------------------------: |
| Left  |                   49,149 / 49,152 |                        0.207843 |                     0.032595 |
| Right |                   49,146 / 49,152 |                        0.160784 |                     0.024531 |

Alpha was unchanged. Values are normalized native RGBA8, before CSX colour
reconstruction; no composed-image or perceptual-quality pass is claimed.
The two-sample GPU batch means were 4.3175 / 4.2470 / 4.3290 ms for the
before/candidate/after processes. They are retained diagnostic observations,
not a performance qualification. The planned 16-sample timing phase was
not run because exact output equivalence failed. Lowering this floor alone
does not meet the unchanged-output requirement.

Before shutdown the live game recorded 474,270 successful NR evaluations,
zero failures, removals or quarantines, with NR enabled. Skyrim exited via
`qqq`; the response recorded the queued command, while its connection
cleanup failed after process exit. Process closure was independently
verified. Exact MO2 recovery-close and lease release succeeded; surviving
SteamVR processes had no `usvfs` modules. The runtime/profile, installed
packages and shader caches were preserved.

### Live-captured GPU kernels: batch index and image strides

The fresh live container was decoded for the pre and first-compute families
across all four embedded architectures (SM75/86/89/120), eight successful
decodes. Its SHA-256 is
`53d6eabda1f533a7106e3385deaad3664ba458e15a1e580f48bc54603f6a736c`.
Ghidra host analysis used freshly captured mapped code; the GPU instruction
analysis used NVIDIA's cuobjdump/nvdisasm on the live-captured container.
No installed provider DLL supplied the binary-analysis source.

ELF metadata establishes parameter-bank base `0x380`, with packet sizes
`0x60` for compute and `0x108` for pre. These match the host wrappers. The
inspected compute variants use X/Y tile coordinates, one input base and
one output base. Traced loads/stores add spatial and lane offsets to those
bases, without selecting another image or adding an item stride. None of
the four compute variants reads CTAID.Z or TID.Z. In pre, the Z read only
participates in an X/Y/Z block-zero cleanup gate; it does not select an
image. This is address/parameter evidence, not an inference from the absence
of a symbol named batch.

Consequently, changing N or launch Z alone cannot create independent outputs
for these inspected variants. The exact opaque runtime kernel handle was
not resolved to an entry address, so this establishes the inspected family
behavior rather than an instruction-pointer trace of the selected variant.
GPU postprocessing and every other layer were not decoded. An alternative
compatible family remains possible, but none has been established.

The complete instruction offsets, ELF fields, source hashes and limitations
are retained in
`cuda-tools/live-kernel-74352/batch-index-stride-review.md` under the private
batch evidence root. Host packets are mapped in
`live-module-padding-74352/independent-host-packets.md`.

### Concrete implementation sequence

The first prototype should instrument the active kernel-chain call in the
standalone replay and forward it unchanged. It must record function and
command-list identity, owned copies of parameter packets, resource lifetimes,
copies, barriers and transitions. That control must reproduce the independent
reference exactly before any grouping is attempted.

Next, use two equal-shape, stateless C contexts with private activations,
scratch and output buffers. Group only launches whose cross-context resources
and side effects are proven independent. Preserve each original parameter
packet and context, and preserve visibility between dependent layers. If
the provider's interleaved barriers or other operations prevent a safe group,
the recorder must retain the original sequence rather than silently removing
those operations. A compatible scheduler or backend would then be needed.
Compare exact outputs first, then CPU submission cost and GPU execution cost
separately. This tests driver-call overhead; it does not itself reduce the
number of GPU kernels or the padding required by each context.

True tensor batching is a larger implementation. A batch-aware kernel needs
an item index (for example dispatch Z), bounds checking, and an item descriptor
containing each input/output/intermediate base or verified stride, dimensions,
origins, guides and reset/history state. All resource-touching stages must
use that selection, including pre/post, attention windows and reductions;
weights may be shared while writable state remains private. The host must
allocate checked per-item storage and pass the matching metadata and dispatch
dimensions. Without an existing compatible family or source, this requires
replacement kernels/backend work that reproduces the provider's layouts,
FP8 behavior and arithmetic, not merely adding a caller parameter. Qualify
replacement N=1 against the original before comparing N=2 with two independent
calls. At this stage neither a replacement nor a multi-descriptor prototype
had been executed; the subsequent experiment is recorded below.

## Kernel-chain implementation and execution

The standalone replay now accepts `--experimental-kernel-chain forward|group`.
It intercepts only the pinned provider's resolved function-pointer cache in
its own process. Both modes retain complete owned copies of the original
kernel descriptors and parameter bytes. Forwarding preserves the original
API calls; grouping combines consecutive calls in original order, flushing
before every provider command-list mutation. Barriers, copies, state changes
and resources are not removed or reordered. Feature creation is an explicit
flush boundary. Warmups forward immediately; grouping starts in timed samples.

A complete base command-list proxy unwraps the real command list for the
driver call, rejects unknown interfaces and suppresses commands after failure.
The hook validates provider/code identity, thread ownership and bounded
capture sizes. Cleanup verifies the original cache pointer, page protection
and code hash after GPU retirement. Uncertain cleanup retains module and
parameter ownership until process exit. A provider-retained proxy disconnects
its callbacks and remains alive rather than exposing a dangling interface.
All additions are standalone tooling; the installed DLL, shader cache and
production renderer remain unchanged by this work.

`kernel_chain_probe.py` runs baseline, forwarding, grouping and baseline in
fresh processes. Its `--batch-timing-only` lane omits per-region timestamp
commands in every case so instrumentation does not itself force a region
boundary. Per-region GPU timings are explicitly unavailable. The whole GPU
batch and CPU submission loop are measured separately; CPU submission
includes deferred flushes but excludes receipt encoding, GPU waits and output
readback. Interception overhead remains included, so these CPU numbers are
not a prediction for a native provider implementation.

### Preserved execution

Evidence root: `build/validation/nr-kernel-chain-20261004/`.
Measured executable SHA-256:
`58bbaf8bd3997ebf0fd7a42603e2d38d02bcfdab38cc78ce2987bef657f5e238`.
Every result preserves the complete compiled source identity. The immutable
C capture and provider match the preceding padding experiment; the floor
remains 320. Full 1008×1120 inputs, original coordinates, independent native
handles, reset policy and complete output ownership were held fixed.

| Campaign               | Regions                                             | Fresh processes | Warmup / timed samples per process | Result                                                    |
| ---------------------- | --------------------------------------------------- | --------------: | ---------------------------------- | --------------------------------------------------------- |
| `smoke-02`             | One 192×256 region per eye at (448,576)             |               4 | 3 / 2                              | Exact output; grouping within each region                 |
| `four-region-01`       | Two 192×256 regions per eye at (256,576), (640,576) |               8 | 3 / 12                             | Exact output; baseline drift rejects long-window timings  |
| `four-region-short-01` | Same four regions                                   |              12 | 3 / 2                              | Exact output; three stable short-window baseline brackets |

All 200 submissions and 760 native evaluations completed. Each retained
warmup and timed output matched its uninstrumented reference exactly. Input
hashes, complete finite owned writes, outside-write sentinels, independent
handle routing, descriptor order/bytes and hook restoration passed. No group
crossed an independent region boundary. `smoke-01` stopped at the sandbox's
process-inventory restriction before any GPU execution and is preserved;
the successful runs used the same required process-exclusion check with
appropriate tool access.

### What grouping achieved

The four-region samples contained 632 original kernel descriptors. Forward
mode submitted them in 632 driver calls. Group mode submitted the same
descriptors in **76 calls**, including 44 multi-descriptor calls, with a
maximum of 40 descriptors in one chain. This is an 88% reduction in driver
calls, not GPU kernel count or model evaluation count. Four model evaluations
and all 632 kernel descriptors remained. Each sample retained 80 provider
`ResourceBarrier` commands and eight `SetDescriptorHeaps` commands.

Aggregating the three short repeats without removing samples gives:

| Metric                                    | Uninstrumented baseline | Grouping prototype |
| ----------------------------------------- | ----------------------: | -----------------: |
| Driver submissions per four-region sample |                     632 |                 76 |
| Kernel descriptors per sample             |                     632 |                632 |
| Whole-submission GPU mean                 |             8.615750 ms |        9.350667 ms |
| CPU submission mean                       |             1.619158 ms |        2.257350 ms |

Forwarding-instrument CPU mean was 2.559767 ms. Grouping was 11.81% lower
than that control, while remaining 39.42% higher than the uninstrumented
baseline. These CPU measurements include the collector's instrumentation.
The independent audit verified 760 exact retained output files, all input
hashes and 12 hooked-process restorations; its full reconstruction is in
`independent-audit.json` and `independent-audit.md` under the evidence root.

The three short-window comparisons measured GPU increases of **8.44%, 8.57%
and 8.58%** against their before/after baselines. Baseline GPU drift was
0.20%, 0.01% and 0.05%; CPU baseline drift was 0.90%, 2.24% and 2.38%.
Instrumented total CPU submission was also higher than the uninstrumented
baseline, although grouping reduced CPU time relative to the forwarding
instrument. This preserves the distinction between fewer driver calls and
lower complete submission cost.

The longer four-region campaign had baseline drift of 7.61% and 19.08%,
above the declared 5% gate, with roughly 50–100 ms GPU samples in unchanged
baselines as well as instrumented cases. The first and third short-window
forwarding controls also contained approximately 50 ms spikes. All samples
remain in the evidence; they are not attributed to batching without proof.
The short-window results establish a bounded regression for this contiguous
grouping prototype, not sustained cost or rejection of a different
independent-context execution path.

### Qualification and remaining architectural boundary

The implemented collector and chain submissions work and preserve the tested
native RGBA output. They do not make multi-ROI inference cheaper. Existing
provider command boundaries prevent this order-preserving collector from
combining different regions, even with per-region timing queries disabled.
Removing those boundaries without a dependency audit would be a different
and unqualified implementation.

True cross-region grouping requires recording resource allocation spans,
mapping CUDA texture/surface handles to their resources, and establishing
which intermediate and scratch allocations are private versus shared. A
scheduler can then combine proven-independent stages while retaining all
required visibility between dependent stages. Tensor batching that also
reduces the GPU kernel count still requires the batch-aware kernels described
above. Neither capability is claimed by this prototype.

Validation passed: three native CTest groups (including 313 hook checks),
19 input/admission tests, 17 campaign tests, and the existing packed-report
(15), native-handle (6), context-probe (8) and replay-report (26) suites.
Scoped formatting and whitespace checks passed. Build logs preserve existing
dependency warnings. Skyrim remained closed throughout the offline tests;
no AIO or production DLL was built or installed for this experiment.

After measurement, one CMake provenance correction made the NvAPI header
hash follow `CSX_NR_DEPENDENCY_ROOT`, matching its actual include location.
It changed no execution code. The measured executable and producer identity
remain preserved at `producer-58bbaf8bd399`; the final build uses the separate
`build/nr-chain-replay-final-20261004` directory so the measured executable
paths and immutable campaign plans remain valid.

## True-kernel batching implementation qualification

The next experiment rebuilds actual GPU kernels with independent per-item
parameter packets. It remains standalone and default-off; no production NR
path or installed DLL changes are made by this experiment. Reducing driver
submissions in the earlier chain experiment did not reduce kernel count.

### Complete live function binding

The forwarding probe now optionally captures the provider's live
`CreateCuModule` and `CreateCuFunction` calls, preserving their original
arguments and results. Bounded module copies, hashes, exact entry names and
returned handles bind every launch to its GPU program. Typed command capture
also retains barrier resource/state fields and descriptor-heap identities.
Capture instrumentation is explicitly excluded from performance admission.
All hooks and candidate resources are confined to the owned replay process.

Evidence: `build/validation/nr-true-batch-20261004/live-identity-02` and
`identity-inventory.json`. Producer executable SHA-256:
`5bc5a16b7ebb9bb46e6d63cdaab6e69dcdc90a2f3380e5e8d4a705759556cd83`.
The executable and generated build identity are preserved separately under
`producer-5bc5a16b7ebb`. The immutable captured input and provider remain the
same as the preceding kernel-chain experiment.

The capture verified nine modules totalling 17,108,936 bytes, 96 created
functions and **44 actually launched entry names**. All 3,160 descriptors
across five submissions and four regions mapped successfully. The complete
158-stage schedule matches across regions. Of those stages, 101 use grid Z=1,
24 use Z=2 and 33 use Z=4; batching must preserve that existing Z meaning.
Every corresponding region uses different parameter packets. The two steady
samples retain identical packets within each region.

Each region has 16 global UAV barriers and four resource-specific UAV
barriers. The non-null barrier resource identities differ across regions;
this does not prove independence of every scratch allocation. Both descriptor
heaps are shared across the four regions. The full typed trace is retained.

All 40 baseline/capture output files, including warmups, were reread and
compared byte-for-byte RGBA. They are identical. All immutable-input and
outside-write checks passed. The provider code hash, all three cache slots
and original page protections were restored after GPU idle, and both
processes closed successfully. This is instrumentation qualification, not
a batching or performance result. An earlier local audit attempt retained
under `live-identity-01` stopped after the baseline because its auditor
incorrectly expected twelve input checks; the shared input textures require
six checks, covering colour, depth and motion for each eye. No native failure
occurred in that attempt.

### Rebuilt kernel admission

Only live-captured provider code containers are used. A pinned build of the
MIT-licensed [cubit assembler](https://github.com/kacper-daftcode/cubit/tree/1f7a5aa6cb0096223f4054930b58c9c0208251e5)
round-tripped the captured module byte-for-byte after correcting its textual
aggregate-parameter declaration. NVIDIA's disassembler independently checked
the preserved instruction bodies and rebased parameter loads. Tool source,
licence, acquisition hashes, build logs and compiler fixtures remain local
under `nr-private-batch-20261004/clone-entry-feasibility`.

The live mapping establishes that the selected first compute stage is
`cc_tinlayout_fused_swin_1h_32_1_inpview_tilesync_fp8`. The ordinary
`inpview_fp8` variant examined earlier is not launched in this workload;
its loader checks do not qualify execution of the active model.

The active variant's N=1 candidate preserves all 45,056 native instruction
bytes. Its N=2 candidate uses a compiler-derived selector and two complete
bodies; 26 parameter loads in the second body select the second 96-byte
packet. Original local branches and tile-completion operations are retained.
The stage has no original CTAID.Z reads and is admitted only for original
grid Z=1. Its completion-buffer pointer at packet offset 0x38 must remain
private for each item. The other 43 programs have not thereby been adapted.

The rebuilt objects intentionally expose their metadata differences from the
provider objects, including parameter declarations and omitted MERC
companions. CPU disassembly alone cannot establish execution correctness.
The load-only verifier created and destroyed the original module and both
candidate types successfully on the captured RTX 5070 Ti Laptop adapter and
driver, with clean device health. It created no command queue and dispatched
no work. Active-variant receipts are under
`nr-true-batch-20261004/loader-tilesync-01`.

Active N=1 candidate SHA-256:
`0dce6033814b77b9798ef20f9981ba22055553a8287667328ef41b6ee76b9315`.
Active N=2 candidate SHA-256:
`a7193ca94af0b5ef33da0e2cf0ee503c6e63babc813273a43a042822e3ed72b9`.
Loader executable SHA-256:
`85d3bec69ad5274bbdcfccb49541169496c5a12709a0a4b96018400fcd451118`.
Loading proves neither output equivalence nor GPU cost. Full-model true
batching remains unqualified until replacement execution, independent item
state and the complete stage schedule are verified.

### Executed N=1 equivalence gate

The standalone replay can now load a pinned private replacement module and
substitute only the selected kernel's launch handle. Provider-owned module
and function creation results remain unchanged. The manifest admits exactly
the active tilesync entry, original module hash, 96-byte packet and N=1
candidate hash above. Other candidates or a missing target fail closed.
Parameters, dimensions, shared memory and all other function handles remain
unchanged. Private function/module ownership extends through proven GPU idle
and cache restoration; uncertain cleanup retains ownership until process exit.

Evidence: `nr-true-batch-20261004/n1-equivalence-01`. The executable and
generated source identity are archived in that campaign's `producer` folder.
Executable SHA-256:
`22a3e47babd5fc18e3488ae84cc7397f028cd3d689b2c4e2da5b601ccbff96cb`.
The campaign ran an uninstrumented baseline, an identity-capture forwarding
control and the N=1 candidate on the same frozen input. Each process ran
three warmups and two steady samples, with four original independent regions
per sample. The candidate actually replaced all twenty selected launches.
All sixty saved outputs matched byte-for-byte RGBA, including warmups.
Input integrity and output footprint checks passed. All three cache slots,
provider code and page protections were restored; private module/function
creation and destruction succeeded, and all processes closed cleanly.

This qualifies final RGBA output for this kernel, input and adapter. It does
not prove every intermediate tensor byte, true N=2 execution or a performance
gain. Capture instrumentation remains excluded from performance admission.
Steady resource evidence contains sixteen unique barrier resources, all
textures. First-warmup feature initialization additionally records eight
buffers: one weights buffer of 0x8ce0600 bytes and one scratch buffer of
0x6898800 bytes per ROI. Each first-stage input, output and completion address
maps inside that ROI's scratch buffer; its weights address maps inside the
other buffer. These are live resource/virtual-range bindings. Committed
allocation ownership and descriptor lifetime still require their own proof
before reordering two regions.

Validation passed: four native CTest groups, including 543 provider-hook
checks and 89 typed-command checks; 21 input/admission tests; 18 kernel-chain
campaign tests; and 15 packed-report tests. Scoped formatting passed. Existing
dependency warnings remain visible in the replay build output.

### Independent-region schedule controls

The pair experiment records the complete four-region command streams before
emitting any deferred work. It pins the observed 158-stage and 180-command
signatures per region, exact heap identities, per-region packet dependencies,
and retained tensor/completion-buffer extents. Its original-order lane first
checks deferred recording. The reordered control then prepares each pair's
prefixes, executes their two original first stages, inserts a global UAV
barrier, and resumes their original suffixes. Only the batch lane replaces
those two first-stage launches with one 192-byte, grid-Z2 launch.

Fresh Ghidra analysis of the same live mapped provider capture establishes
that buffer creation uses `CreateCommittedResource` when its NGX allocation
callback is absent. A read-only observer inserted into the standalone
generated runtime checks the exact parameters immediately before each
unchanged `CreateFeature` call. Production `Runtime.cpp` remains unchanged.
Both named and encoded allocation/release keys returned the SDK's
`UnsupportedParameter` result, `0xBAD00010`, with null values for all four
features. This closes the default-allocation construction condition without
inferring physical independence from virtual addresses alone.

Descriptor heaps require a separate lifetime gate. Fresh code analysis maps
every descriptor-cache miss to one of five public-header API signatures,
including the common `CaptureUAVInfo` path that covers a legacy converter
bypass. All five are observed through pinned provider data-cache slots.
Warmups forward original arguments/results unchanged. Any steady observation
rejects the entire unsubmitted command list. Cache-hit-only execution keeps
the retained descriptor entries stable; a heap pointer alone is insufficient.
The eight launch/identity/descriptor slots share one restore/ownership path.

Evidence: `nr-true-batch-20261004/pair-control-01`, with producer SHA-256
`6fb74251adfde421bcadae08e3bb84dfd1b59f5e245ea7b0cb521776ea4864d4`.
The archived producer includes generated-source identity. The campaign ran
baseline, captured forwarding, recorded original order, and reordered
original kernels, each with three warmups and two steady samples. All eighty
saved RGBA outputs matched the same baseline. Both recorded lanes observed
zero steady descriptor-cache misses; their first warmup observed 10 merged
conversions, 20 independent conversions and 20 UAV-info calls. All eight
slots restored after GPU idle. This campaign did not execute an N=2 kernel.

Validation passed: six native CTest groups, including 2,082 provider-hook and
pair-scheduler checks and 56 descriptor-counter checks; 22 input/admission
tests; 19 kernel-chain/report tests; 15 packed-report tests; and scoped
formatting. These control results remain quality/ownership evidence, not
production timing qualification.

### Executed N=2 first-stage gate

Evidence: `nr-true-batch-20261004/pair-batch-01`, with the same archived
`6fb74251adfde421bcadae08e3bb84dfd1b59f5e245ea7b0cb521776ea4864d4`
producer. Five lanes ran baseline, captured forwarding, recorded original
order, reordered originals and the N=2 candidate. All 100 saved output files
(19,660,800 bytes) matched exact RGBA, including every warmup. The independent
audit also verified 150 immutable-input checks and 965,760 owned parameter
bytes.

Four physical N=2 launches executed across the two steady samples. Each
192-byte packet was exactly the two original 96-byte packets concatenated,
with grid 20 x 20 x 2 and block 32 x 1 x 1. Each steady sample used 630
physical launches for 632 logical stages, with two explicit UAV joins.
Zero steady descriptor-cache misses occurred. All eight hooks, code and
page protections restored after GPU idle; private module/function creation
and destruction succeeded, and each process closed cleanly.

This demonstrates genuine batching of the selected first compute stage for
independent regions on the frozen input. The other 157 stages still run
separately. It does not qualify full-model batching, every intermediate
tensor byte, image quality on other inputs, or any performance improvement.

### Shared-body indexing and full-stage scheduling

The indexed candidate keeps one original kernel body and adds a 128-byte
prefix. Twenty-six parameter loads select the item's 96-byte packet through
R1/UR19; the unused R1 stack prologue becomes a NOP. All other original
instructions and scheduling words remain unchanged. Two EXIT and 48
cooperative-group metadata PCs relocate by exactly 128 bytes. Its N=1 and
N=2 texts are identical, with separately declared 96/192-byte parameter ABIs.

`indexed-n1-01` passed: 20 substitutions and all 60 saved RGBA outputs were
exact, with proven idle and private-object retirement. The archived producer
SHA-256 is
`3b1a694439658b5a2313cd23456ea73b92cbfcb3624795491a5045412e30b01a`.
The N=1 candidate hash is
`13ffb20e31779e38ca35e24403fe2609e52c13bdc1ff76028a95506186daa9a9`.

`indexed-n2-01` failed on its first steady submission. The four control
lanes' 80 outputs and the candidate's 12 unchanged warmup outputs matched.
Both N=2 API calls returned success with the correct original packets, but
`WaitForIdle D3D12 fence` returned `0x800705B4`. Device-removal reason was
zero. No completed steady output or timing exists. Restoration lacked GPU
idle proof, so private objects remained owned until process exit. This is
an unqualified GPU-completion failure, not confirmed device removal. The
failed candidate hash is
`20de0a9113261692fbae9727cb24ca8a760fe5cd9d6937bfe00708127937d6af`.
It does not invalidate the earlier successful two-body N=2 candidate.

The separate `index-address-gpu-01` diagnostic ran five finite two-item
kernels: official compiler output, its native control, indexed uniform
32-bit loads, indexed uniform 64-bit loads, and both uniform load widths.
All returned the exact known values at offsets zero and 96, including the
uniform index value, with proven GPU idle and cleanup. This demonstrates
nonzero indexed-load addressing on this adapter. It does not establish that
the model's added prefix has correct scheduling or register lifetime.

`layer-control-01` then ran the whole 158-stage model in corresponding
stage order across each pair of regions, retaining each original kernel,
packet, barrier and heap command. All 100 saved RGBA outputs matched across
five lanes. Each steady layer-control sample retained 632 physical/logical
launches and added 316 global UAV joins. Its producer SHA-256 is
`14d5b965b89966b88b416362194aae4bf8b6f609cfd3905afecb58580938b1ed`.
Six CTest groups, 5,784 provider/scheduler checks, 22 input tests, 19
kernel-chain tests and 15 report tests passed. This qualifies the scheduling
control on frozen inputs; it does not batch the full model or measure its
performance.

### Indexed-load follow-up and retired candidates

`nr-true-batch-20261004/index-address-gpu-02` repeated all five tiny
addressing variants with a nonzero output sentinel. Every expected value
matched, bytes outside the diagnostic writes remained unchanged, and GPU
idle and native-object cleanup succeeded. Its producer SHA-256 is
`7cb078980ff69fcb0006cb70bc5f7ce7ea848506a7f8ed4ac4e365af7cfdb656`.
This is five variant processes, each using grid 1 x 1 x 2 and block
1 x 1 x 1. It does not qualify the model's 32-thread tilesync block or the
added prefix's register and scheduling behavior.

`indexed-stall15-n1-01` tested the prefix-scheduling hypothesis by changing
only six stall fields: R2UR and the following five NOPs use stall 15. The
model body and metadata remain unchanged. Baseline and captured-forward
controls completed with matching outputs, but the candidate failed on its
first warmup. Four replacement API calls succeeded before `WaitForIdle
D3D12 fence` returned `0x800705B4`; device-removal reason remained zero.
There is no completed candidate output or GPU timing. Missing idle proof
retained private owners until process exit. Producer SHA-256:
`04138c90d0b64e24d4cda3f365bda6b2ebccfcdd5cb2b0322264ac9e837efb13`.

The failed stall-15 N=1 hash
`f6e83e14db1e8a033b050302e7661646f79d774962469f46688c58ba9525d9cb`
and its unexecuted N=2 counterpart
`b2721d77b0ea1fee6982e2e6ca596f1826c3ceb65aab56f9128a4e11ff534a9d`
are retired from admission. The earlier failed indexed N=2 hash is also
rejected. Their receipts remain preserved; no later successful control
turns these failures into passes.

### Complete-model N=1 replacement

The private replacement set now admits the exact nine-module, 44-entry
catalogue, retaining original provider handles and substituting private
functions only at launch. It owns candidate bytes, names and device
references through proven GPU idle and hook restoration. Missing coverage,
handle aliases, changed device identity or uncertain native ownership fail
closed. Manifest hashes and the path-independent semantic catalogue hash
pin the complete candidate set.

`nr-true-batch-20261004/model-n1-01` is preserved as a failed campaign. Its
native `model-original` process completed, but the checker incorrectly
required the original-forwarded warmup marker for a replacement warmup.
The recorded reason is `warmup scheduled differently`. The runner was
being corrected as the campaign started; that attempt was not reused as a
pass. This was a checker/lifecycle coordination failure, not a GPU fault.

The frozen corrected runner completed `model-n1-02`: baseline, captured
forwarding, recorded originals, layer-ordered originals, model replacement
in original order, and model replacement in layer order. Each lane ran
three warmups and two steady samples with four independent regions. All
120 saved RGBA files, totalling 23,592,960 bytes, matched exactly. Both
replacement lanes exercised all 44 entries in every sample: 632 private
N=1 launches per sample, 3,160 per lane and 6,320 overall. Input integrity,
outside-write and descriptor-stability checks passed.

This native-only candidate retains original instruction bodies and metadata
while omitting MERC companions. Its manifest file SHA-256 is
`e51f0ea40344608e0bed73e924247217561dff400132f02f50722169c5de08c5`;
semantic catalogue SHA-256 is
`c914eda8a1c46d87d91c4413df8b0be1bf3706ec64076a7921184aabd8c4a3ee`.
The archived producer is the `04138c90...37efb13` executable identified
above. Each replacement lane destroyed all 44 private functions and nine
private modules successfully after GPU idle; all eight cache slots, code
and page protections restored. These results qualify fixed-input final
RGBA equivalence, not intermediate tensors, other scenes or performance.

A later ownership review found that `ReleaseModules` released only the
three original cache-target DLL references, omitting the five added
descriptor-hook references. It now releases every retained cache-target
reference. Earlier receipts still establish the stated hook restoration,
private GPU-object destruction and image equivalence, but they do not
establish complete DLL reference-count cleanup. The extra references were
confined to the replay process and recovered at process exit. This
qualification applies to the earlier eight-hook campaigns above, including
`model-n1-02`; it does not reinterpret their image results as failures.

### Complete-model clone gates

`nr-true-batch-20261004/full-chain-clone-04/manifest.json` preserves the
instruction audit for all 44 entries: 6,981 mapped branch targets and 117
logical-Z masks across the N=1 and N=2 candidates. Existing grid Z values
1, 2 and 4 retain their within-item meaning. N=2 uses two complete native
bodies and separate parameter packets; each packet starts at a 16-byte
aligned stride, with zero padding between unchanged original packets.
This avoids relying on the failed shared-body indexed-load strategy.

`full-model-clone-01/audit.json` records eighteen final modules and all
54 successful NVIDIA ELF, resource and disassembly checks. The preserved
ELF emitter retains section/symbol identity, shared memory, frame/stack and
resource metadata, and relocates instruction PCs and DWARF FDE locations
and rows. Eleven synthetic emitter tests passed. The separate
`maintained-emitter-reproduction-01/audit.json` confirms the maintained
emitter reproduces all eighteen frozen module hashes exactly. These are
CPU artifact checks; they do not establish GPU execution by themselves.

The N=1 Z-mask control manifest file SHA-256 is
`0f91227ca36d4ccbe72a858ea34a69cbe5f60402fb7518f8c443ad8ba4e70295`;
semantic catalogue SHA-256 is
`ef9d5626c70e76ef7a517b94e9ed0761fab62efac1bf512551b5d2a3b16c4f2e`.
`model-masked-n1-01` then passed the same six-lane GPU gate: all 120 RGBA
files matched, with all 44 entries exercised in every replacement sample,
6,320 private N=1 launches overall, proven idle and private-object
retirement. All eight hooks, code and page protections restored. Its
archived producer SHA-256 is
`1ef814d2a3007f0d5c286c86793e6751fc389f5eae4ba68417357c840873604e`.
This producer includes the DLL-reference release correction.

The corresponding full N=2 manifest file SHA-256 is
`682c82ba7549ac285e444deaceb1bd9e682be30051daafeb078bfebdaa61e09f`;
semantic catalogue SHA-256 is
`b7fbb04508df5f2a32555bf19346559aa15a62b9607c009158e167777a14ecc3`.
`model-n2-01`, using that same producer and qualified N=1 input, failed on
the first steady submission. Its four controls completed, and the three
original-kernel warmups matched the baseline. The candidate submitted 316
N=2 launches for 632 logical stages with 316 global UAV joins and zero
steady descriptor-cache misses. All API calls were accepted, but
`WaitForIdle D3D12 fence` timed out with `0x800705B4` and device-removal
reason zero. No completed steady candidate output or GPU timing exists.
Restoration remained unproven and private owners were retained until
process exit. Full-model N=2 remains unqualified; the next diagnostic must
isolate the first affected stage before another complete-model submission.

### Prefix isolation: output boundary at stage 24

The bounded prefix control batches only the first N stages, then continues
with the original kernels in layer order. Each steady sample retains all
632 logical stages and 316 global UAV joins, with 2N actual N=2 calls and
632 - 4N original calls. The scheduler validates the full per-region command
projection before submission. Explicit per-entry submission counts include
zero for unselected functions; preparing all 44 private functions does not
claim that all 44 were executed. Seven CTest groups and 24 input tests
passed. The independent runner's CPU checks cover every prefix length,
unchanged suffix order and rejection of an unexpected private launch.

The archived prefix producer SHA-256 is
`746af00aabea68e641672fcb7e6c20829150969243bf9d53ac08e4e6be3db90c`.
`nr-true-batch-20261004/model-masked-n1-prefix-01` first requalified the
unchanged masked N=1 catalogue on this producer: six lanes and all 120
saved RGBA files passed. The following campaigns used the preceding N=2
catalogue `b7fbb045...14ecc3`, with fresh baseline, captured forwarding,
recorded original, layer-control and candidate processes for each prefix.
Every process ran three warmups and two steady samples.

| Evidence directory   | Batched stages | N=2 calls across two steady samples | Original calls across two steady samples | Saved RGBA result               |
| -------------------- | -------------: | ----------------------------------: | ---------------------------------------: | ------------------------------- |
| `model-prefix-1-01`  |              1 |                                   4 |                                    1,256 | All 100 exact                   |
| `model-prefix-3-01`  |              3 |                                  12 |                                    1,240 | All 100 exact                   |
| `model-prefix-7-01`  |              7 |                                  28 |                                    1,208 | All 100 exact                   |
| `model-prefix-16-01` |             16 |                                  64 |                                    1,136 | All 100 exact                   |
| `model-prefix-18-01` |             18 |                                  72 |                                    1,120 | All 100 exact                   |
| `model-prefix-19-01` |             19 |                                  76 |                                    1,112 | All 100 exact                   |
| `model-prefix-24-01` |             24 |                                  96 |                                    1,072 | All 100 exact                   |
| `model-prefix-25-01` |             25 |                                 100 |                                    1,064 | Candidate second regions differ |
| `model-prefix-26-01` |             26 |                                 104 |                                    1,056 | Candidate second regions differ |

Both failing image campaigns completed GPU work successfully, restored all
eight hooks and retired all nine private modules and 44 private functions.
Their original warmup outputs and both first-region outputs remained exact.
Both second regions differed in all 49,152 pixels during each steady
sample. The left-eye second region differed in 142,970 RGBA bytes with a
maximum byte difference of 80; the right-eye second region differed in
143,117 bytes with maximum difference 74. Each differing output repeated
exactly between the two steady samples. The runner preserved both campaigns
as failed with `static output differs between samples`, because original
warmups and modified steady outputs differ. This is reproducible output
inequivalence, separate from the full-model GPU timeout above.

The adjacent passing prefix 24 and failing prefix 25 isolate the first
observed output boundary to adding zero-based stage 24, inventory entry
K14, `cc_tinlayout_fused_swin_8h_256_8_ds_wait_fp8`. Its original launch
uses grid 3 x 3 x 1, block 32 x 8 x 1 and an 88-byte packet. The captured
body contains CTAID.Z-dependent conditional cleanup. That path and the
new item-selector prefix are diagnostic leads; this comparison does not
establish which instruction or dependency causes the wrong second-region
output. It also does not establish the cause of the separate full-model
timeout.

`full-model-scheduled-selector-02` supplies the next CPU-reviewed candidate.
Only the 128-byte selector prefix of each of the 44 entries changes; all
other bytes of the nine ELF modules match the preceding N=2 artifacts.
`audit.json` and `independent-selector-audit.json` retain the official
disassembly and independent control-flow/register review, including the
conditional UR4 initializers and their unchanged guards. These checks
establish artifact invariants, not GPU equivalence. The manifest SHA-256 is
`643f8eefa7b864e8d254f5ade499b8fc3b914501005947ba679ad85ae31217f0`;
semantic catalogue SHA-256 is
`2b94fefdd3f1c705e2b6696c806995ba66187137f921d11cdb984de6d92ecb1a`.
Current source admission selects this catalogue in place of the previous
N=2 pin; the previous producer and failed results remain immutable.

The separately frozen `run_model_scheduled_prefix.py` archives and hashes
every imported local Python source. The scheduled-selector producer
SHA-256 is
`1f32f64fe6b2ca400f68d540bc46e7e2be77bfdb631a1212f869799ab2da378a`.
`model-masked-n1-scheduled-01` requalified its unchanged masked N=1
catalogue: six lanes and all 120 RGBA files passed.

`model-scheduled-prefix-25-01` then completed GPU work but failed the image
gate with `static output differs between samples`. Its four control lanes
passed. All twenty candidate output files are byte-identical to the older
failing `model-prefix-25-01` files: warmups and first regions are exact,
while both steady second regions have precisely the same differences
quantified above. All eight hooks restored after proven idle, and all
nine private modules and 44 private functions retired successfully. Thus
the controlled selector-prefix change did not remove this output failure
on the frozen input; it does not establish the underlying cause.

The scheduled candidate's two steady GPU receipts are 5,712 and 5,691
microseconds; the older prefix-25 receipts are 5,704 and 5,710 microseconds.
Neither image-invalid result qualifies performance, and these instrumented
diagnostics do not establish a production gain. Further investigation
focuses on K14's original CTAID.Z read and its logical-Z correction, with
a narrow alternative and a small addressing diagnostic before another
complete-model submission. Full-model execution remains gated on fixing
the prefix-25 output boundary.

### K14 coordinate-source gate

`nr-true-batch-20261004/full-model-k14-tid-01` changes only K14's relevant
S2R source from CTAID.Z to TID.Z. The observed K14 launch has original
grid Z=1 and block Z=1, so the intended within-item value is zero. The
original S2R scheduling bits and following AND-zero instruction remain
unchanged. The N=1 module changes one byte; its two-body N=2 counterpart
changes two bytes. All other native instructions and ELF metadata remain
identical to their respective masked-N=1 and scheduled-selector parents.
This substitution is specific to this observed geometry, not a general
replacement for kernels that require original grid Z=2 or Z=4.

The N=1 manifest file SHA-256 is
`2cb01d1680620210cbe5a9884507ec336092e9f7a04e26476a6b6a9cfb71696c`;
semantic catalogue SHA-256 is
`da33dffc7fd03eecfbd45020d1c907d2d0f0eb1b8535ea48ad5e862efd276237`.
The N=2 manifest file SHA-256 is
`581f6a00ddaa2a809541350354187ce0c389485dc0b05091fe696fa496e936c8`;
semantic catalogue SHA-256 is
`afa5200f04f4d5bbbe5c22296f44671ee3089414eabb38334d8b4e62b0902e57`.
All four campaigns below use archived producer SHA-256
`85828a6af02e071c053eaca0d49474660a0235e884ea10521a25391cd6c68a5b`.

`model-k14-tid-n1-01` passed all six lanes and 120 saved RGBA outputs.
`model-k14-tid-prefix-25-01` then passed all five lanes and 100 outputs,
including 100 actual N=2 calls across its two steady samples.
`model-k14-tid-prefix-26-01` likewise passed all 100 outputs with 104
actual N=2 calls. Both prefix candidates restored all eight hooks after
GPU idle and retired all nine private modules and 44 private functions.
The coordinate-source change therefore repairs both measured output
boundaries for these frozen inputs. It does not yet explain why the
original CTAID.Z read followed by AND-zero produced the wrong result.
A standalone probe of that exact instruction sequence is a separate
diagnostic, not proof of the complete model's register or dependency state.

The subsequent `model-k14-tid-full-01` attempted all 158 stages and failed
at the first steady GPU wait. Its four controls completed, and its three
original warmups matched the baseline. The candidate's 316 N=2 calls for
632 logical stages were accepted, with 316 UAV joins and zero steady
descriptor-cache misses, but the fence wait returned `0x800705B4` with
device-removal reason zero. No completed steady output or GPU timing
exists; private owners remained retained until process exit because idle
and restoration were unproven. Fixing the earlier image boundary therefore
does not close the separate full-model completion failure or qualify
full-model batching or performance.

### Isolated K14 mask sequence

`nr-true-batch-20261004/k14-mask-gpu-01` isolates the original S2R and
inserted AND-zero sequence using three fresh processes. Its producer
SHA-256 is
`ec522941c8c5ac58a359d5dfcbd390709e46f380dbb0a2fc1d59daebf0afb9a5`;
fixture receipt SHA-256 is
`5a493e9031d4328c7a450b4c82ad108d73ac5cc9b8b72955925649254b4cd89c`.
Each launch uses block 32 x 8 x 1 and grid 1 x 1 x 2: 512 threads write
separate 32-byte records into a sentinel-initialized 16 KiB buffer. Six
words record the tested Z value, exit predicate, physical Z, linear index,
TID.X and TID.Y; the final eight bytes of every record must remain intact.

The official compiler control and the native control without the inserted
mask both passed all 512 records. The masked candidate embeds the exact
176-byte K14 sequence, including its original control words. It completed
GPU execution and preserved every sentinel, but failed the value check:
all 256 threads in physical Z=0 were correct, while all 256 in physical
Z=1 reported R6=1 and predicate=1 instead of the expected zero values.
Their physical Z, linear index and X/Y coordinates were correct. All
three processes proved GPU idle, reported no device removal and destroyed
their function/module before successful NVAPI unload. The masked receipt
reports `probe K14 mask values differ`; the campaign records a native
diagnostic exit failure. This is an arithmetic/dependency result, not a
completion or cleanup failure.

The masked candidate SHA-256 is
`6d4ef65e6154968b3b5cf56e38147b86963ce879b059881eb9454dc4299afe68`;
its exact source-sequence SHA-256 is
`c7862c54d818274ef275dbf09497b3c9feb8222d0617db0868981c4af24820e7`.
The AND-zero opcode payload matches the native AND encoding. These
observations do not establish a LUT encoder defect. Instruction dependency
scheduling, including a possible late S2R write, remains under
investigation. The diagnostic isolates this sequence and geometry; it does
not explain every full-model dependency or the later timeout.

`full-model-all-z1-tid-01` additionally preserves an unexecuted CPU-only
alternative: 25 original grid-Z1 reads across 13 entries change their
S2R/S2UR source to TID.Z, with block Z=1 required. It changes 25 bytes in
the N=1 bundle and 50 in N=2, retaining masks, control words, metadata and
all nonunit-grid-Z reads. The semantic catalogue hashes are
`2384dc3e03078335810c4dd7a9b13b5d830158427bf1804a7c847a0d23686b3e`
for N=1 and
`c7a6376186550ecff33f40cb979e753669347e49fc883d17b9d5f7fa7eeb96cc`
for N=2. Official CPU checks passed, but neither loader nor GPU execution
is qualified. This alternative remains parked while the mask/dependency
issue is isolated; the successful K14-only result does not qualify these
additional entries.

### K14 source-read dependency isolation

`nr-true-batch-20261004/k14-mask-variants-gpu-02` completed all fourteen
diagnostic processes: the three original controls and eleven bounded
variants. It reused the same `ec522941...afb9a5` host. The variant manifest
SHA-256 is
`83673389025e1a762a3cf07d1475e12b1c82e232cf8f48cff634a2e731a51362`;
runner SHA-256 is
`a1373d9b4261ca0a9cfc89a748ae08ffe370e84d7ed81511e0da82b0d61e31ed`.
Every process completed GPU work, retained all sentinels and coordinates,
reported healthy device state, and retired its native objects. The
campaign is `complete_diagnostic` with `allVariantsExact=false`, preserving
six value failures rather than converting completion into equivalence.

| Diagnostic change                                                            | Result across 512 threads                                         |
| ---------------------------------------------------------------------------- | ----------------------------------------------------------------- |
| Official original-Z control; native control without mask                     | Expected original Z and predicate in every thread                 |
| Original K14 S2R plus AND-zero sequence                                      | All 256 physical-Z1 threads retain value/predicate 1              |
| Official zero-output control                                                 | Expected zero value/predicate in every thread                     |
| Preload R6=1, then the same AND-zero                                         | Expected zero value/predicate in every thread                     |
| Increase source S2R stall from 1 to 2                                        | Expected zero value/predicate in every thread                     |
| Insert a dependency-waiting NOP before AND-zero                              | Expected zero value/predicate in every thread                     |
| Read Z, read Y, then AND-zero                                                | Expected zero value/predicate in every thread                     |
| Change only the mask's post-issue stall to 5 or 9                            | Both retain value/predicate 1 in all 256 physical-Z1 threads      |
| Preload R6=1, then subtract 1                                                | Expected zero value/predicate in every thread                     |
| Replace immediate mask with MOV-zero, register AND-zero or constant-zero LUT | All three retain value/predicate 1 in all 256 physical-Z1 threads |

These controlled results support a source-read dependency/scheduling defect
in the isolated inserted-mask sequence. The same AND encoding correctly
reduces a preloaded one to zero; changing the following zero-producing
operation alone does not repair the original S2R sequence. Increasing the
source stall or waiting before the mask does repair it. A late S2R write
overwriting the mask result is consistent with these observations, but the
diagnostic does not directly observe instruction issue/completion or prove
the hardware mechanism. It also does not qualify every S2R/S2UR variant or
the complete model. The next model gate retains the real CTAID.Z reads and
masks while changing only the affected source-read stall, with fresh N=1
equivalence required before N=2 execution.

### Complete-model N=2 gate passed with source stall 2

`nr-true-batch-20261004/full-model-mask-stall2-01` preserves the real
CTAID.Z reads and all logical-Z masks. Of 39 original source-read sites,
28 across 16 entries had stall 1 before an inserted mask; only that stall
field becomes 2. The N=1 bundle changes 28 bytes and N=2 changes 56 bytes.
Source registers, dependency barriers/waits, selectors, packet layout,
other instructions and ELF metadata remain unchanged. All eighteen
modules passed the official CPU checks. This applies the narrow source
timing correction established by the diagnostic, while retaining original
within-item grid-Z semantics.

N=1 manifest file SHA-256:
`3483b86cbf9a32705b9522df6166c6d855af1b66c338640664b056e5f87f7f0f`;
semantic catalogue SHA-256:
`a8d6d4ebddadfa2ddfef5fa960a7a0713403ceceb7324fde3045eb52871bf281`.
N=2 manifest file SHA-256:
`192f9b9cdb8a21b068ff467dbda632f9bb8f665b79638bb9efcb3f8aa2502cbd`;
semantic catalogue SHA-256:
`2b11eb5028bbdbb3b539be8789f10052868f46b5c256bdc16c1003a3e7f8d76e`.
Both GPU campaigns use archived producer SHA-256
`5fe1287c8be348f5600e17adaef40f28545e38b83cb233b5aeee2e5db174c0b4`.
Seven CTest groups and 24 input/admission tests passed before execution.

`model-mask-stall2-n1-01` passed all six lanes and 120 saved RGBA outputs.
Its matching N=2 gate, `model-mask-stall2-full-01`, then passed baseline,
captured forwarding, recorded original order, layer-ordered originals and
complete-model batching. All 100 saved RGBA files, including the original
warmups, matched exactly. Both steady candidate samples completed GPU work
with all 44 entries actually batched: 316 physical N=2 launches for 632
logical stages, 316 UAV joins and no original steady suffix. There were
632 actual N=2 calls across the two steady samples. The provider still
records four independent region evaluations; the complete native kernel
schedule pairs their independent packets at submission.

The read-only `model-mask-stall2-full-01/independent-audit.json` rechecked
19,660,800 saved RGBA bytes, 150 immutable-input checks and all 104,960
bytes of physical N=2 packets. Each packet contains the corresponding
original A/B packets with the required zero alignment padding. The audit
also verifies actual private function handles, candidate module hashes,
doubled grid-Z extents, exact logical IDs and positive per-entry execution
counts. This rules out an accidental original-kernel fallback in the
qualified steady samples. All eight cache slots, provider code and page
protections restored after proven GPU idle; all 44 private functions and
nine private modules retired successfully.

This is a complete-model true-batching pass for the captured stateless-C
input, two equal 192 x 256 regions per eye and full 1008 x 1120 input grids.
It does not establish intermediate-tensor equivalence, other inputs or
shapes, temporal stability, production behavior or a performance gain.
The longer comparison below preserves the output pass but does not
qualify performance. Earlier image failures and GPU timeouts retain their
original failed classifications.

### Longer complete-model timing: exact output, unstable bracket

`nr-true-batch-20261004/model-mask-stall2-timing-01` completed six fresh
processes in the fixed order shown below. It uses the same archived
`5fe1287c...174c0b4` producer and `2b11eb50...7f8d76e` N=2 catalogue as
the preceding successful gate. The frozen timing runner SHA-256 is
`d70e908fe0806cc24d24c68b4be42009246aaa1e8dd5f232a5e522ad1381d71f`;
completed `run.json` SHA-256 is
`d4b08208f73a503b086c92de1ce7cacde03f7e26f1be6de8cef6e4459ee311be`.
The prior N=1 and complete-model N=2 gates were revalidated before this
run. Input, provider, candidate, producer and imported Python sources were
pinned and archived.

Every lane retains 51 samples: three unchanged warmups and 48 nominal
samples. The analysis plan, fixed before execution, excludes the first
sixteen nominal samples as schedule warmup and uses iterations 19–50
(32 samples). It removes no outliers and replaces no missing samples.
All 306 samples, including both warmup categories, remain in the raw
reports and independent audit. The following values are milliseconds for
the same complete four-region work per sample.

| Lane                             |  GPU mean | GPU median | Instrumented CPU mean | Instrumented CPU median |
| -------------------------------- | --------: | ---------: | --------------------: | ----------------------: |
| Baseline before, no interception | 48.490750 |  50.777500 |              4.385263 |                1.631500 |
| Captured forwarding              | 49.043281 |  50.988500 |              5.881400 |                3.052350 |
| Recorded original order          | 44.929375 |  50.855000 |              6.317516 |                3.570650 |
| Layer-ordered original kernels   | 35.703531 |  37.040000 |              6.483866 |                3.710900 |
| Complete-model N=2               | 35.466906 |  36.540500 |              5.916616 |                3.344000 |
| Baseline after, no interception  | 56.634781 |  48.632000 |              4.409944 |                1.634050 |

GPU timing covers the aggregate native batch, with per-region queries
deliberately unavailable. CPU timing covers the native evaluation loop
and final command-schedule replay, including interception and admission
guards. It excludes receipt encoding, GPU waiting and readback. This
instrumented CPU value is not a production CPU measurement.

The baseline mean changed by **16.795020%**, above the predeclared 10%
limit, so `offlineGpuTimingComparable=false`. The complete-model GPU mean
was 21.060762% below recorded original order, but only 0.662750% below the
layer-ordered original control. Its diagnostic differences against the
baseline before/after were 26.858409%/37.376104%. None is a qualified
performance gain. Instrumented CPU mean was 6.345849% below original
order and 8.748639% below layer order, while exceeding the baseline
bracket by 34.920444%/34.165331%; the same qualification limit applies.

The distribution remains broad without removing the large values:

| Lane                           |   GPU p10 / p90 (ms) | GPU minimum / maximum (ms) | CPU maximum (ms) |
| ------------------------------ | -------------------: | -------------------------: | ---------------: |
| Baseline before                | 8.610300 / 99.610500 |      8.589000 / 100.162000 |        89.570900 |
| Captured forwarding            | 8.618100 / 99.362000 |       8.604000 / 99.705000 |        92.821800 |
| Recorded original order        | 8.613300 / 93.347600 |       8.604000 / 99.693000 |        90.205900 |
| Layer-ordered original kernels | 5.822100 / 73.481300 |       5.811000 / 73.693000 |        91.528200 |
| Complete-model N=2             | 5.326200 / 70.163000 |       5.321000 / 70.312000 |        84.659600 |
| Baseline after                 | 8.626200 / 99.628500 |       8.599000 / 99.869000 |        90.199400 |

The independent CPU-only audit reconstructed all 1,224 saved RGBA files
(240,648,192 bytes) against the initial baseline, with exact equality in
every output and no outside writes. It checked 1,836 immutable guide
receipts and all 15,168 physical N=2 submissions across the 48 nominal
candidate samples, including 10,112 in the analysis window. Their
2,519,040 parameter bytes match the original A/B packets with required
zero padding, correct private functions, doubled grid Z and pinned module
identities. Every steady sample executed all 44 entries, with 632 logical
stages, 316 physical launches, 316 joins and no original suffix. The first
three candidate samples retained original execution as planned.

All candidate samples succeeded. Eight provider cache slots, code and
page protection restored after proven idle; all 44 private functions and
nine private modules retired successfully. The audit independently
reconstructed every saved timing and all reported statistics, rather than
accepting the campaign's aggregate flags. Its complete numeric record is
[`independent-timing-audit.json`](../../build/validation/nr-true-batch-20261004/model-mask-stall2-timing-01/independent-timing-audit.json);
the campaign plan and raw report hashes are in
[`run.json`](../../build/validation/nr-true-batch-20261004/model-mask-stall2-timing-01/run.json).

The result is a longer exact-output success with an unqualified timing
comparison. It does not demonstrate proportional pixel scaling, a
production gain, or the cause of the timing excursions. A timing protocol
that keeps bounded repetitions submitted together can test whether gaps
between CPU readback/validation cycles contribute; that hypothesis is not
established by this campaign.

### Four schedules per submission: repetition gate passed

`nr-true-batch-20261004/model-schedule-r4-01` uses replay producer SHA-256
`e9468748e531b408d85163a8a7db4ad9ba58726294ecb512d3daf0bc66e1bf50`
and the same qualified `2b11eb50...7f8d76e` N=2 catalogue. The new producer
first passed matching N=1 qualification in `model-repeat-host-n1-01`
(120 RGBA files), then ordinary single-schedule full N=2 qualification in
`model-repeat-host-full-01` (100 RGBA files). Seven CTest groups and 25
input/admission tests passed before native execution.

The repeated-schedule runner SHA-256 is
`e2ce070152a749d3d8e59946b6d079a7cc30fbc32d12be14e28524b9b83f08b8`.
Its three CPU policy tests include 31 negative cases for incomplete
repetitions, event ranges, output metadata, lifecycle and full-resource
tampering, including a changed pixel with a matching replacement hash.
Independent review and the same-producer prerequisite recheck passed
before dispatch. The completed R4 `run.json` SHA-256 is
`ef07f181b9324d558c78c6d0a81a29fdc96c99a06b48c33b6b0e5314f9be5c63`.

Each of the original-order, layer-order and N=2 lanes contains three
unchanged warmups and two steady samples. A steady sample captures its
four independent region evaluations once and replays the complete
632-stage logical schedule four times in the same command list, without
a CPU wait or readback between passes. Before each pass, four outputs are
reset to alternating byte sentinels. After its GPU timer ends, all four
full outputs are copied to distinct retained readback footprints. UAV
states are restored around copies; between-pass global UAV joins remain
outside each pass timer. Only proven completion permits CPU reads and
resource retirement. The optional helper's staging allocation is bounded
to 256 MiB; ordinary single-schedule execution creates none of it.

All recorded per-pass GPU times are retained here in microseconds. Each
value measures one complete four-region schedule, excluding the sentinel
reset, output copies and between-pass join.

| Lane                    | First steady block, four passes (us) | Second steady block, four passes (us) | All-eight mean / median (us) |
| ----------------------- | ------------------------------------ | ------------------------------------- | ---------------------------: |
| Original order          | 51278, 51310, 24381, 8616            | 8648, 8645, 8645, 8625                |         21268.500 / 8646.500 |
| Layer-ordered originals | 38368, 38364, 38384, 11832           | 5838, 5819, 5842, 5821                |         18783.500 / 8837.000 |
| Complete-model N=2      | 5343, 5334, 5335, 5335               | 5348, 5337, 5333, 5342                |          5338.375 / 5336.000 |

Aggregate timers and CPU submission values cover different work and are
not substituted for those per-pass times:

| Lane                    | Aggregate GPU for each block, including copies (us) | Instrumented CPU submission for each block (us) |
| ----------------------- | --------------------------------------------------- | ----------------------------------------------- |
| Original order          | 150997, 40049                                       | 15559.9, 8320.1                                 |
| Layer-ordered originals | 147457, 28822                                       | 14893.9, 9194.6                                 |
| Complete-model N=2      | 26810, 26822                                        | 6293.6, 6830.4                                  |

CPU submission includes one provider recording and four schedule replays
with copy commands. It excludes receipt encoding, waiting and CPU
readback. It does not represent four independent production evaluations.

The read-only audit revalidated the ordinary qualification and every R4
lane against raw reports and saved bytes. All 96 full snapshots and 60
normal crops passed: 445,317,120 bytes in total, including every unowned
pixel reconstructed from the required alternating sentinel. It checked
90 immutable-guide receipts, all 2,528 actual N=2 launch packets and
419,840 physical packet bytes. Every N=2 pass executes all 44 entries
through 316 physical launches for 632 logical stages. The two original
control lanes execute 10,112 steady original launches in total. There
are three between-pass joins per steady sample; layer and N=2 schedules
also retain their 316 within-pass joins. All eight provider hooks restore
in every lane, and 44 private functions plus nine modules retire cleanly.
The complete audit is
[`model-schedule-r4-01/independent-audit.json`](../../build/validation/nr-true-batch-20261004/model-schedule-r4-01/independent-audit.json).

This is a successful bounded repetition/output gate, with encouraging
batched timing consistency in this short run. Controls still exhibit
first-block excursions, the lanes ran in separate processes, and only
eight passes per lane were measured. No timing samples are discarded and
no speedup is qualified by that gate. The following comparison alternates
control and batched schedules within the same four-pass submission, so
each block supplies both measurements with the same captured packets.

### Matched ABBA/BAAB comparison: bounded offline reduction

`nr-true-batch-20261004/model-matched-comparison-01` completed both short
mixed gates before running either measurement lane. Producer SHA-256 is
`4b3ddd5875a7525bef79183c368589bf757d3840ddd70542c896e3f9b17bf386`;
runner SHA-256 is
`fe9da7922959579475ce2a55b30a71732a415b6a4e377a334d3b4279623a7f37`.
The candidate remains the qualified `2b11eb50...7f8d76e` catalogue.
Same-producer N=1 and ordinary full N=2 qualification passed first in
`model-matched-host-n1-01` (120 files) and `model-matched-host-full-01`
(100 files). Seven CTest groups, 26 parser tests and six runner CPU tests
with 43 negative cases passed. Scoped checks passed; a test-only pointer
cast warning was corrected and its target rerun without changing the
measured replay executable.

A is the selected original-order or layer-order control; B is complete
N=2. Actual even sample iterations execute ABBA and odd iterations BAAB.
After three unchanged native warmups, each measured lane runs ten
four-pass blocks. The first two blocks, iterations 3–4, were predeclared
schedule warmup. Analysis uses all 32 passes in iterations 5–12: sixteen
control and sixteen N=2. There is no outlier removal, replacement or
post-hoc window selection. Each block shares its original captured
packets, guides, resource ownership and command list. Each pass retains
all four full output resources after resetting their alternating
sentinels; GPU timestamps exclude those resets/copies and between-pass
joins.

| Window and control             | Passes per mode | Control GPU mean / median (us) | N=2 GPU mean / median (us) | N=2 mean reduction |
| ------------------------------ | --------------: | -----------------------------: | -------------------------: | -----------------: |
| Analysis, original order       |              16 |           12914.875 / 8641.500 |       8544.9375 / 5339.500 |         33.836468% |
| Analysis, layer order          |              16 |           9899.3125 / 5836.500 |        9277.500 / 5338.000 |          6.281371% |
| All ten blocks, original order |              20 |           16302.550 / 8644.500 |       11044.200 / 5341.000 |         32.254770% |
| All ten blocks, layer order    |              20 |           10953.800 / 5836.500 |       11636.250 / 5339.000 |     **−6.230258%** |

All eight analyzed paired-block means favor N=2 for each control. The
layer-order comparison isolates the extra benefit of true batching over
the already-interleaved original kernels. The overall production and
in-game performance flags remain false. The all-block layer regression
is retained: its predeclared warmup iteration 4 favors the control,
with N=2 50.084623% slower in that block. The following tables contain
every measurement pass, including that block and the iteration-11 spikes.
All numeric timings are microseconds; reduction is computed from the
two A and two B times within each row.

Original-order control:

| Iteration | Use             | Order | Four GPU pass timings (us) | A / B paired means (us) | N=2 reduction |
| --------- | --------------- | ----- | -------------------------- | ----------------------: | ------------: |
| 3         | Schedule warmup | BAAB  | 5343, 8628, 8661, 5337     |           8644.5 / 5340 |    38.226618% |
| 4         | Schedule warmup | ABBA  | 51093, 36663, 36822, 51031 |         51062 / 36742.5 |    28.043359% |
| 5         | Analysis        | BAAB  | 5342, 8623, 8631, 5340     |             8627 / 5341 |    38.089718% |
| 6         | Analysis        | ABBA  | 8651, 5339, 5337, 8607     |             8629 / 5338 |    38.138834% |
| 7         | Analysis        | BAAB  | 5344, 8634, 8622, 5347     |           8628 / 5345.5 |    38.044738% |
| 8         | Analysis        | ABBA  | 8639, 5337, 5338, 8631     |           8635 / 5337.5 |    38.187609% |
| 9         | Analysis        | BAAB  | 5347, 8636, 8646, 5335     |             8641 / 5341 |    38.190024% |
| 10        | Analysis        | ABBA  | 8661, 5334, 5337, 8645     |           8653 / 5335.5 |    38.339304% |
| 11        | Analysis        | BAAB  | 31001, 42820, 42893, 30964 |       42856.5 / 30982.5 |    27.706416% |
| 12        | Analysis        | ABBA  | 8644, 5342, 5335, 8655     |         8649.5 / 5338.5 |    38.279669% |

Layer-ordered original control:

| Iteration | Use             | Order | Four GPU pass timings (us) | A / B paired means (us) |   N=2 reduction |
| --------- | --------------- | ----- | -------------------------- | ----------------------: | --------------: |
| 3         | Schedule warmup | BAAB  | 5349, 5826, 5820, 5333     |             5823 / 5341 |       8.277520% |
| 4         | Schedule warmup | ABBA  | 38388, 36804, 36799, 10653 |       24520.5 / 36801.5 | **−50.084623%** |
| 5         | Analysis        | BAAB  | 5337, 5826, 5836, 5337     |             5831 / 5337 |       8.471960% |
| 6         | Analysis        | ABBA  | 5861, 5347, 5336, 5822     |         5841.5 / 5341.5 |       8.559445% |
| 7         | Analysis        | BAAB  | 5351, 5830, 5829, 5338     |         5829.5 / 5344.5 |       8.319753% |
| 8         | Analysis        | ABBA  | 5847, 5340, 5331, 5845     |           5846 / 5335.5 |       8.732467% |
| 9         | Analysis        | BAAB  | 5342, 5823, 5834, 5336     |           5828.5 / 5339 |       8.398387% |
| 10        | Analysis        | ABBA  | 5850, 5346, 5336, 5837     |           5843.5 / 5341 |       8.599298% |
| 11        | Analysis        | BAAB  | 36949, 38422, 38250, 36744 |         38336 / 36846.5 |       3.885382% |
| 12        | Analysis        | ABBA  | 5842, 5332, 5338, 5835     |           5838.5 / 5335 |       8.623790% |

Both orders occur four times in each analyzed window. ABBA/BAAB mean
reductions were 38.236443%/31.624305% against original order and
8.628768%/5.298701% against layer order. These are descriptive subsets;
neither replaces the full predeclared result or explains the excursions.
Whole-submission instrumented CPU means over the analyzed blocks were
6737.950 us and 7009.0875 us respectively, including one original
recording, four mixed schedules and copy commands. No per-mode CPU
saving is attributed to that shared scope.

The two short gates also retain all timings: original-control BAAB
`5341, 8622, 8654, 5332` and ABBA `51004, 36823, 36724, 39851`;
layer-control BAAB `36671, 38402, 29516, 5339` and ABBA
`5829, 5344, 5333, 5833` us. The three unchanged native warmup GPU
times were `12561, 8661, 8659` us for the original gate,
`12549, 8671, 8615` for the layer gate, `12571, 8644, 8650` for
original measurement and `12536, 8654, 8649` for layer measurement.
Those original warmups contain no N=2 work and do not enter the paired
statistics. Their CPU creation/recording costs, every block's aggregate
GPU time including copies and all CPU timings remain in the audit.

The independent read-only audit rehashed and reconstructed all 384 full
resources and 144 crops: 1,762,394,112 bytes, with exact owned output and
every unowned sentinel pixel intact. It checked 216 immutable-input
receipts and all 15,168 N=2 packets (2,519,040 bytes), plus 30,336 steady
original-control calls. Per-event physical snapshots prove which original
or private function actually received each complete packet and grid;
mutable final descriptor flags are not used as that proof. All 44
entries execute in both N=2 passes per block. All eight hooks restore
and 44 private functions/nine modules retire in every lane. Independent
numeric reconstruction matches every reported mean, median and paired
block result. Completed `run.json` SHA-256 is
`74661b7315a0d61f43883aabc44f318997b20d972ddd6b323dbf593a60a6f6a5`.
Full records are
[`independent-audit.json`](../../build/validation/nr-true-batch-20261004/model-matched-comparison-01/independent-audit.json)
and [`independent-timings.md`](../../build/validation/nr-true-batch-20261004/model-matched-comparison-01/independent-timings.md).

### Original runtime integration boundary

The proposal below records the boundary before runtime implementation.
The implemented Info-level controls, production adapter and corrected
shared-body catalog are documented in
[the current integration record](nr-batched-roi-ui-20261004.md).

The successful backend remains an offline prototype, not an installed
game DLL or AIO. Initial integration should be a default-off adapter
compiled only with `DEVBENCH_BRIDGE_ENABLED`, at the existing four-ROI
native command-list boundary. It must preserve preparation, independent
model contexts and output/stereo composition. Admit only the proven
provider/catalog and SM120 geometry: stateless equal-grid C, two
192 x 256 regions per eye, 1008 x 1120 backing, the complete pinned graph,
private scratch and stable descriptors. Other cases retain original
execution.

The adapter needs identity hooks before feature creation, ordinary
warmup/rebuild forwarding and bounded per-frame packet/resource owners
recycled only after the exact GPU fence. The replay observer's accumulating
128-sample ownership model cannot be copied into an indefinite game loop.
Private native objects retire only after idle and hook restoration.
Deferred per-original-call timestamp commands must be relocated or marked
unavailable, with actual batched GPU timing reported separately. A mismatch
after deferral must abort the unsubmitted list safely, rather than emit a
partial batched stream.

First live qualification must cover changing pixels, both eyes and final
composition, then enable/disable, resets, 2-to-1/0 ROI changes, slot churn,
extent and mode fallback. Native GPU work must be measured separately from
host recording, preparation/copies, interop synchronization and composition.
Unequal shapes, temporal histories, different graphs/drivers/architectures
and SE/AE remain outside this initial VR DevBench experiment. The exact
read-only integration assessment is retained in
[`in-game-integration-readiness.md`](../../build/validation/nr-true-batch-20261004/in-game-integration-readiness.md).
