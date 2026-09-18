# Task 2: bounded native NR replay

This checkpoint starts from `deb4b320d478524a33699f6af2c5a62997a05a62`
on `main-vr-nr`. The fetched branch contains Task 1 and its validation;
Task 2 was not transferred from another machine. The unrelated dirty
checkout remains untouched. Task 3 ROI changes have not been repeated or
started in this checkpoint.

## Implementation and contracts

The optional DevBench `communityshaders.nr_replay` tool retains prepared
native colour, depth, motion and private output after successful NR, before
colour reconstruction and CSX composition. Full initialized rectangles,
source/crop/jitter provenance, auto-mask, real model edits and the existing
frame-evidence experiment are required. It preserves scaled guide grids
and feature mode. Requests are bounded by 32 frames, 30 seconds, two
pending GPU readbacks and 512 MiB of logical staging/CPU payload reservation
(twice row-packed bytes, excluding driver allocation padding). Failures,
missing consecutive frames and cancellation leave explicit incomplete
receipts. GPU staging is asynchronous; payload hashing/writes use a worker.
Completed bundles live beside the SKSE log under `NRReplay`.

The [standalone replay](../../tools/nr-replay/README.md) compiles the existing
production runtime and interop bodies with substituted platform includes.
Runtime admission and native submission remain shared. The developer tool
varies geometry, offset, aspect ratio, one/two-region calls, creation
capacity and reset/cold/continuous history. Capacity experiments preserve
source density and integer source/guide phase. Private output canaries
record the provider's actual footprint. Failed, bypassed, unwritten or
zero-edit evaluations do not qualify as fast results.

The reporter retains raw timings, counts, reset/creation state, resource
and evaluated rectangles, source identities, logical bytes and DXGI memory.
It separates cold, static-reset and continuous results and reports sample
counts, spread and descriptive 95% intervals. Comparisons reject changed
controls and require independent equally initialized temporal contexts.
Serially correlated samples are not independent population estimates.

Production A/B/C selection, native auto-mask, colour/Lighting preservation,
stereo publication, source-crop/jitter rules and defaults are unchanged.
No semantic mask is passed as a provider ControlMask. Occupancy experiments
use a labeled synthetic CPU composite proxy after native evaluation; they
do not measure the actual CSX GPU compositor or Preserve Source quality.
Four/eight regions remain deferred until the capacity task.

## Actual SDK and runtime inspection

Pinned Streamline SDK 2.14.1 exposes the core NR enum (1004), but this
checkout has no NR-specific header or loaded Streamline NR plugin. No
NR-specific feature requirement query was issued. Generic viewport or
duplicate-instance contracts are not treated as capacity guarantees.

The executable's real admission probe recognized the existing patched
runtime, version `310.8.0`, with SHA-256
`8270B350CD82DE5CE89806872CDD6B6A9249B80836B91BBEB3573470744CC206`.
The immutable local receipt is
`build/nr-replay-inspection-final-20260918/results.json`. This was an
inspection only: no model evaluation or GPU timing was performed.

## Validation and remaining measurements

The standalone Release build passed, as did its nine input/decoder checks.
Capture WARP checks exercise formats, scaled guides, ownership, drift,
timeouts, cancellation, budget admission, readback and receipt completion.
Request tests exercise the actual extracted DevBench handler and its
developer/configuration guards. Reporter tests cover rejected/confounded
samples, intervals, dimensions, footprints and temporal pairing. Final DLL,
controller-test and package receipts are retained with the local AIO.
No HLSL was changed; this checkpoint makes no shader equivalence claim.

The Release universal DLL and controller targets built successfully with
`DEVBENCH_BRIDGE=ON`, `AUTO_PLUGIN_DEPLOYMENT=OFF` and shader tests disabled.
`ctest --test-dir build/nrb -C Release -L ControllerTests --output-on-failure`
passed **155/155**, including native WARP capture, request guards and all
18 reporter checks. The retained output is
`build/replay-controller-tests.log`. Scoped pre-commit and diff checks
passed. These results do not establish live Skyrim behaviour or timings.

| Route | Measured samples | Native cost                                | Capacity/temporal verdict |
| ----- | ---------------- | ------------------------------------------ | ------------------------- |
| A     | 0                | Unavailable: native input capture required | Unmeasured                |
| B     | 0                | Unavailable: native input capture required | Unmeasured                |
| C     | 0                | Unavailable: native input capture required | Unmeasured                |

Existing PNG recordings cannot supply native depth/motion resources. The
next step is installing the new AIO and collecting one full source frame
per route with matched content/settings and a nonzero NR edit. Run static
replay first, then bounded consecutive captures for independently
initialized history pairs. Short GPU captures are additional evidence and
have not been collected. A high-resolution stereo capture may permit only
two frames under the payload budget; that checks history plumbing without
establishing temporal quality. Native submission timing is not total
in-game A/B/C cost. No bottleneck, supported instance count, performance
improvement or new production default is established yet.
