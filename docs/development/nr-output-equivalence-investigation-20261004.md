# Native NR output equivalence investigation

## Decision and scope

The frozen C fixture confirms that changing the native valid rectangle can
change owned output even when the captured colour, depth and motion textures
remain unchanged. Independent tight rectangles do not reproduce full-eye
output. A shared enclosing rectangle reduces native cost and, with sufficient
tested context, approaches full-eye output more closely than those tight
rectangles. None of the reduced valid-domain cases passes exact RGB equality
with the full-eye reference.

This extends the [packed-region feasibility result](nr-packed-region-feasibility-20261004.md).
It does not identify a private model operation, establish a universal context
margin, or qualify a production adapter. No production renderer, shader,
default, installed DLL or AIO changed. The implementation is an offline
controller and evidence analysis using the existing native replay executable.

## Source and measurement identity

The source is immutable C frame **41177** from
`build/validation/nr-task9-11-stationary-20261003/native-input/manifest.json`.
Its original capture producer was source
`46fd070fabb565f4827510e475e589f6470f4547`, marked dirty, with Build ID
`f0acc3926842a84b507d0657b0fa4936cf45286d9627e8524ac807cca7a8f3b6`.
That capture identity is distinct from the executable performing this replay.

Each eye has 1008 × 1120 RGBA8 colour/output, R32 depth and RG16 motion.
The fixed owned rectangles are `(192,512,128,128)` and
`(672,512,128,128)`: manually selected patches containing the left NPC and
bard, respectively. These are not live mask attribution or complete
three-character coverage. Both eyes retain their captured inputs and motion
scales. Every native evaluation resets history.

The executable is
`build/nr-packed-replay-20261004/Release/csx_nr_replay.exe`, built from
`main-vr-nr` base `1e07a237fa3e58804bed5a3a1203af600c191305` plus the
standalone replay changes subsequently committed as
`cfb6c32055bd42de44969dbbad83f3d6ed7b23c3`.
The embedded per-file source inventory in every result remains the exact
compiled-source identity. In particular:

| Identity                     | SHA-256                                                            |
| ---------------------------- | ------------------------------------------------------------------ |
| Source capture manifest      | `3c02792f5d203d2afca501583327f54b4f663293730972c4e85836ceabca12d6` |
| Replay source-content digest | `2aa8cff07001b10eefb557c22babc53e0abea97fbd807d2f558e9ec1b5a5c74e` |
| Replay executable            | `9fbc0505d7841dc88c569e376ba699fb2221345da9f30c817ab306d37621a536` |
| Compiled replay `main.cpp`   | `1177bc14faff57f39dcc5c3860ddafb8a00674b7495c624b4de0f92e5ebf18c8` |
| Compiled runtime source      | `9a2fe412cb880d942bf3bef180b843e6e666ce9b9429356527271001d95d7bd3` |
| Compiled interop source      | `d63abf08e3ea7709202cb99ddffbaeaf0bd784937f85734cc229113af5ff23ee` |
| Native provider 310.8.0      | `8270b350cd82de5ce89806872cdd6b6a9249b80836b91bbeb3573470744cc206` |
| Context plan                 | `dc29e0270f1841f513e140fdfc2dd8cb034a567ed52200e57eff603c05b91358` |
| Final context summary        | `59242730c2a61b228c18909a88566d32a2570a22077fbe801b903c521bc79624` |

The physical provider came from
`build/pnr1004b/aio/Shaders/Upscaling/Streamline/nvngx_dlssnr.dll`.
The captured adapter is the NVIDIA GeForce RTX 5070 Ti Laptop GPU; replay
verifies the captured adapter, LUID and driver identity. The context plan
also pins each Python tool hash, including `context_probe.py` as
`7e372ab2f543c153a1bf26945ccd64ae17bf479df05730be00e2aee04a11f65a`.

Complete local evidence is retained under
`build/validation/nr-output-context-20261004`, including `plan.json`,
`run.json`, `summary.json`, all native results and owned-output payloads.
The source-content digest hashes the concatenated per-resource SHA-256
strings in replay load order; it is not a second manifest-file hash.

## Protocol and results

The campaign completed **34 native processes and 204 steady samples**:
17 cases, two repeats, three warmups and six measured samples per process.
Case order reverses on the second repeat. All processes completed native
work and clean shutdown, and all requested steady samples were admitted.
No capture instrumentation was attached. Frozen input hashes, executable
identity, runtime identity, actual geometry, calls, output hashes and
nonzero native edits are checked before accepting measurements.

The cases include full-eye and tight references, a raw enclosing rectangle,
each raw individual rectangle, and individual/shared context expansions at
0, 64, 128 and 256 pixels. Context expansions align outward to the existing
64-pixel CSX policy and clip to source bounds. This alignment is not a
verified private CNN stride. The second raw patch starts at x=672, so its
aligned zero-halo rectangle becomes `(640,512,192,128)`; zero halo does not
mean identical geometry for that patch.

The following times are medians of **summed native GPU evaluation intervals**
over 12 steady samples per row. CPU preparation, upload, readback and final
composition are excluded. Every row owns the same 65,536 stereo pixels.
RGB errors compare those exact owned pixels with the same repeat's fresh
full-eye reference. Error units are 8-bit RGB code levels, not scene-linear
values or perceptual scores.

| Configuration              | Native calls, stereo | Evaluated pixels, stereo | Native median ms | Native observed max ms | Mean absolute RGB error | Maximum RGB error | Exact full-reference RGB |
| -------------------------- | -------------------: | -----------------------: | ---------------: | ---------------------: | ----------------------: | ----------------: | ------------------------ |
| Full eye                   |                    2 |                2,257,920 |           8.5815 |                  8.756 |                       0 |                 0 | Yes                      |
| Two tight regions per eye  |                    4 |                   65,536 |           8.6245 |                  8.933 |                  3.7620 |                25 | No                       |
| Shared raw enclosure       |                    2 |                  155,648 |           4.5900 |                  4.611 |                  1.9933 |                19 | No                       |
| Shared enclosure, halo 128 |                    2 |                  688,128 |           5.0270 |                  5.041 |                  1.7980 |                16 | No                       |
| Shared enclosure, halo 256 |                    2 |                1,290,240 |           6.2335 |                  6.249 |                  0.9479 |                11 | No                       |

Full-eye and tight reference crops are bitwise RGB-repeatable across both
repeats. Tight differs from full in 99.74% of owned pixels. The shared
256-pixel case reduces that to 89.11%, with a maximum per-crop p99 absolute
channel error of five code levels. Alpha remains unchanged in these
comparisons. These differences do not establish a perceptual acceptance
or failure verdict, but they fail the stated exact-equivalence gate.

### Fresh full-eye reference reproduces the captured output

Both entire fresh full-eye RGBA outputs have the same hashes as the original
captured full-eye outputs, not just matching small owned crops:

| Eye   | Captured and fresh full-eye output SHA-256                         |
| ----- | ------------------------------------------------------------------ |
| Left  | `cba8da139f8c51659454480016c6ab64522b10d526f5c699f224248eda1c411a` |
| Right | `f631d48c9fc9b3c75a486eaf95a8e31694ef436abc870e65bed7b4b94da88eaa` |

This supplies a reproduced full-eye reference for this input and provider.
It does not make the older capture a new live-game qualification.

### Independent contexts versus shared evaluation

Running either raw individual rectangle alone reproduces its tight batched
owned output exactly, including the second rectangle when it uses a fresh
region-zero feature slot. The observed difference from full-eye output
therefore does not require inter-region batching, a particular feature slot
or a shared-input ordering effect in this fixture.

The table below compares shared native means with sums of separately
measured individual-case means, averaged over the two repeats. Those sums
are explicitly **not a single measured batched pass**. They show the cost of
retaining independent native contexts, with four calls across the two eyes,
against one shared evaluation per eye.

| Context policy   | Sum of individual means ms | Shared enclosure mean ms |
| ---------------- | -------------------------: | -----------------------: |
| Raw rectangles   |                     8.7019 |                   4.5907 |
| Aligned halo 0   |                     8.6568 |                   4.5953 |
| Aligned halo 64  |                     8.6641 |                   4.7221 |
| Aligned halo 128 |                     8.9015 |                   5.0279 |
| Aligned halo 256 |                    10.3758 |                   6.2331 |

Expanding separate contexts does not remove the invocation cost. Even the
individual 256-pixel cases fail equality with full-eye output. Shared
256-pixel evaluation is cheaper and closer to that reference than the
tight baseline, but it still fails exact equality. Increasing context is
not uniformly monotonic: the shared 64-pixel case has greater error than
the raw enclosure in this fixture.

## Spatial analysis of the earlier packed outputs

CPU analysis reuses the immutable
`build/validation/nr-packed-feasibility-20261004-v2` outputs. All ten cases
have one distinct output hash per slot across their 24 steady samples.
The saved audit is
`build/validation/nr-output-equivalence-20261004/spatial-audit.json`,
SHA-256 `7ff5f1f8f2c6263a2e9b568aa0fae0c6cd2eee2c43bccbb141f3497d48ed3bdb`.
Its adjacent `spatial_audit.py` preserves the CPU-only analysis.

-   All 44 candidate/eye/patch RGB searches prefer zero offset within a
    plus-or-minus-two-pixel integer translation search. No integer crop or
    scatter displacement is identified; subpixel or other mapping defects
    are not thereby excluded.
-   Differences persist inside the patches. Against tight output, the raw
    enclosure's mean absolute error is 3.383 levels over the full patch and
    3.500 over its central 32 × 32 pixels. The 128-pixel atlas gives 3.086 and
    3.767 respectively. An edge-only seam does not explain these results.
-   Channel changes differ by subject. The raw enclosure raises the left
    NPC's mean red value by 4.15/4.49 levels in the two eyes while lowering
    the bard's by 6.86/4.53. Independent channel gain/offset fits still leave
    approximately 1.4–3.7 levels of RGB RMSE for those enclosing comparisons.
    A uniform brightness correction is not an established repair.

The [analytical contact sheet](../../build/validation/nr-output-equivalence-20261004/native-context-contact.png)
shows left-eye tight, full, raw-enclosing, 128-context and 256-context native
owned crops. Each 128 × 128 crop is enlarged threefold with nearest-neighbor
sampling, without colour/exposure/sharpening changes. The sheet is labeled
as native output, **not composed game or HMD comparison images**. Its PNG
SHA-256 is `2064ba97beb0376fff783b71edf15b260771ac55953d338e27b6923933fefbe7`.
The adjacent `contact-crops/manifest.json` preserves source output and crop
hashes; its SHA-256 is
`72289c065b5aa624ee70aaf41cbef48ecc4e78ea539714b9ed29cfa7a0042130`.

## Interpretation and limits

### Translation and square-page controls

The translation campaign at
`build/validation/nr-output-translation-20261004` completed ten processes
and 40 steady samples. It cyclically rolls every input grid by 0, 1, 16, 32
or 64 pixels and moves the first owned rectangle equally. Both eyes retain
the same backing dimensions, owned bytes, settings and motion-vector scale.
All outputs match the unshifted owned RGB exactly, including the reversed
second repeat. This does not establish general network translation
invariance: cyclic wrapping changes distant boundary adjacency. It does
reject an absolute-coordinate or simple alignment explanation for this
particular patch and range of shifts.

Two square-page campaigns duplicate each source context twice, without
padding. The horizontal layout is `A B / A B`; the diagonal layout is
`A B / B A`. The selected top-row contexts and owned coordinates remain
identical. Only the bottom-row contexts exchange places. CPU verification
checks every role and eye: exact top context bytes, bottom-context swap,
owned bytes, guide dimensions and the resulting identical pixel populations.
Each layout is also tested with source order reversed.

The horizontal campaign completes 12 processes and 72 steady samples. The
diagonal campaign completes five admitted processes and 30 steady samples
before its sixth process is rejected for an ambiguous output sentinel.
It does not execute its second repeat. The following comparisons use six
steady samples from the first repeat; the last row is a separately admitted
alternate-sentinel diagnostic, not a repaired campaign pass.

| H versus D case                        | Changed owned RGB pixels | Mean absolute RGB error, code levels | Maximum RGB error, code levels |
| -------------------------------------- | -----------------------: | -----------------------------------: | -----------------------------: |
| Halo 0                                 |                   91.00% |                               1.2474 |                             13 |
| Halo 0, reversed                       |                   91.88% |                               1.5190 |                             26 |
| Halo 128                               |                   90.21% |                               0.9305 |                              9 |
| Halo 128, reversed, alternate sentinel |                   90.17% |                               0.9806 |                              9 |

At halo 128, changed content starts at page y=384, whereas the selected
owned rectangle occupies y=128 through 255. All 128 bottom halo rows remain
unchanged. Different output therefore persists with the same selected
coordinates, local context and global pixel population. Square pages and
128-pixel margins do not restore independent-region output. The observations
support spatial-context sensitivity beyond that preserved margin; they do
not reveal the provider's mechanism. Every admitted case is byte-repeatable
within its run.

Horizontal stereo native medians are 4.3255/4.3240 ms at halo zero and
6.1760/6.1730 ms at halo 128 for forward/reversed order. These remain
non-equivalent to separate inference. Diagonal timing is descriptive only
because that campaign lacks the planned second repeat.

The rejected diagonal halo-128 reversed run reports successful native
evaluations and clean shutdown, but one RGBA pixel matches the deterministic
fill sentinel in each iteration. A separate identical-input replay using
the existing `--alternate-output-sentinel` flag admits all six steady samples.
All 18 full retained RGBA outputs, including warmups and both eyes, have
identical hashes between the original and alternate runs. This resolves
the observed ambiguity as a finite marker collision for that output. The
original samples remain rejected and the original campaign remains failed.
No gate is weakened and no device failure is inferred from this rejection.
The controller now marks the active job failed as well as the campaign on
rejection; the original failed journal is preserved without rewriting its
stale per-job running marker.

| Additional evidence          | SHA-256                                                            |
| ---------------------------- | ------------------------------------------------------------------ |
| Translation campaign         | `1bbb7e0755423aa900efd953bb27941f5502c2d69a57c6d962f3ad0ab82c5eb7` |
| Translation summary          | `4f842ca64e44386e1f9ae7f1733e58d67bdd589e5c2925bf5d88814245fc23b8` |
| Horizontal campaign          | `2dc56d0141320e0f25c9e2b10dd5625c28f328c89cecdbcc6966f4a4d192fce1` |
| Horizontal summary           | `e6a6f08ab0c919ee51cd748f500754c9f1b822bf892aee273027759e307f36e3` |
| Diagonal campaign            | `e9ecfc73221c44a21e1fbddf0ac990388e1d068788606350ce08edeb13b9b736` |
| Diagonal partial summary     | `061cd73bf6809e6e2315ccece236a38613dacd317d94e5b2307fb713a9c2b3ec` |
| Alternate-sentinel diagnosis | `964bbb4bc93c4f7cfe4929710d3ea80d41b124dc4177e0943ea6c42a32ba8e24` |
| Cross-canvas analysis        | `a7e54c25e04d5187fb85030cb120c98f288c5dabded52b30be4c875903ee99f3` |

Canvas evidence lives under
`build/validation/nr-output-canvas-{horizontal,diagonal}-20261004`.
The diagonal tree retains the alternate diagnostic separately under
`alternate-sentinel/atlas-h128-reversed`. The CPU cross-canvas comparison
and its reproduction script are retained beside the spatial audit.

### What can preserve visuals and reduce cost

The current native layout maps colour, depth and motion valid rectangles
from the output rectangle. Replacing tight requests with an enclosure
therefore changes the declared valid input domain as well as requested
output extent, although backing texture contents remain identical. The
measurements establish sensitivity to that contract. They do not distinguish
private normalization, attention, resampling, receptive fields, tiling or
other internal provider operations.

Exact equivalence to independent tight regions and exact equivalence to
full-eye NR are different targets: those references themselves differ.
The reproduced full-eye result prevents treating tight output as an
unexamined quality oracle. The available result remains a cost/quality
tradeoff, not a behaviour-preserving production optimization.

This campaign covers one frozen C source with stateless evaluations, two
manually selected patches per eye, one provider and one GPU/driver setup.
It does not qualify temporal A/B histories, moving inputs, arbitrary
characters, native read footprints, stereo perception, composition or
whole-frame performance. No live-game test or blinded perceptual evaluation
was performed. Observed maxima are retained samples, not tail guarantees.

The caller already shares a D3D12 command list and its return GPU wait
across region evaluations. Compatible resources and native feature handles
are retained. `Runtime.cpp` places per-call GPU timestamps immediately
around native `evaluateFeature`, after feature creation and CPU parameter
setup. The reported native intervals exclude replay uploads and readbacks;
the broader batch timer is a different metric. These invariants are visible
in `Renderer.cpp`'s `BeginD3D12`/`EndD3D12` scope, `D3D12Interop.cpp`'s
submission path and `Runtime.cpp`'s evaluation path. Additional caller-side
submission batching cannot be credited with removing an already measured
native floor. Private kernels, copies and serialization remain opaque.

The available engineering routes are:

1. Preserve the current independent-region output exactly. This needs an
   established native contract for independent patch conditioning within
   a batch, or provider changes that share suitable internal work without
   changing each region's context. The inspected interface exposes scalar
   rectangles and no verified independent-patch batch or reusable internal
   feature interface. GPU profiling or provider clarification is needed
   before selecting such an implementation.
2. Preserve the full-eye appearance as the visual target. One shared context
   in original coordinates is a stronger candidate than packing unrelated
   regions. Here the 256-pixel shared context saves 27.72% of native median
   time against tight calls, while differing from full-eye output by an
   average 0.9479 code levels and a maximum 11. This is a candidate for
   blinded native-HMD, temporal and stereo qualification, not a promise of
   identical visuals. Full-eye evaluation itself reproduces captured output
   exactly, but its 8.5815 ms cost gives no material reduction versus two
   tight regions per eye in this fixture.
3. Reduce surrounding preparation, copies and composition while preserving
   the native requests. That can preserve output but addresses a separate
   cost. Shared source storage already exists as a DevBench experiment;
   [earlier exact-output comparisons](nr-task7-8-qualification-20261003.md)
   did not establish a native-cost benefit. Stationary characters also do
   not guarantee identical frame inputs, so cached output reuse is not a
   general exact-preserving solution.

The original approximately 50% packed native saving remains unqualified.
No measured alternative both retains that saving and reproduces the
independent-region output. Keep the production path unchanged; do not
promote an atlas, guessed correction or cost profile from these data.

## Tool validation

The packed suites run 36 tests: 35 pass and one Windows symlink-privilege
check skips. Fifteen context/translation probe tests and three canvas
builder tests pass. Tests exercise bounds, immutable identities, ownership
mapping, changed files, partial native failures, actual sample/warmup counts,
byte-preserving canvas/translation inputs and repeat ordering. Shared
derived-manifest construction avoids stale capture metadata; bounded input
reads allocate for actual file size and reject concurrent size changes.
All additions remain offline tools, with no production resource, pass or
setting added. No native executable rebuild was required.

## Reproduction

With Skyrim and other replay processes closed, the following creates a new
timestamped evidence directory and runs the bounded protocol. It neither
installs a DLL nor launches or closes Skyrim. The executable and provider
paths refer to the preserved artifacts identified above.

```powershell
$contextEvidence = 'build/validation/nr-output-context-repeat-' + (Get-Date -Format 'yyyyMMdd-HHmmss')
python tools/nr-replay/context_probe.py `
  --manifest build/validation/nr-task9-11-stationary-20261003/native-input/manifest.json `
  --replay build/nr-packed-replay-20261004/Release/csx_nr_replay.exe `
  --runtime build/pnr1004b/aio/Shaders/Upscaling/Streamline/nvngx_dlssnr.dll `
  --output $contextEvidence `
  --roi 192,512,128,128 --roi 672,512,128,128 `
  --halos 0,64,128,256 --repeats 2 --warmup 3 --samples 6 --seconds 120
```

The controller preserves the plan, reversed-repeat ordering, invocation
journal, partial failures and strict same-repeat output comparisons. It
rejects changed tool/input/executable identities and existing output trees;
it does not silently overwrite an earlier campaign.
