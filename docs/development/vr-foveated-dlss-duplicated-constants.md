# VR foveated DLSS duplicated-constants evidence

## Scope and producer identity

This record preserves the 2026-08-28 evidence for intermittent
`eErrorDuplicatedConstants` results on the submit-stage foveated DLSS path. It
is a failure-analysis record, not a completed render-scale comparison. The COC
scenario did not reach its requested Dragonsreach destination, so it produced
no valid render-scale latch-timing sample.

The exact producer was:

| Field              | Value                                                              |
| ------------------ | ------------------------------------------------------------------ |
| Build ID           | `c881bade50b23963ac2394005d5ad5614cab54f2dac83222d3a7cbb5318c5828` |
| Source commit      | `9e1ac9755e38478eb30c38bcd38a440a92730a7a`                         |
| Source description | `v3.19.0-pr39-138-g9e1ac9755`                                      |
| Configuration      | `Release`, clean source                                            |
| DevBench           | `1.15.4`                                                           |

The CommunityShaders log was archived before analysis at:

`D:\Coding\GitHub\CS logs\c881bade50b2__20260828T085605870Z__d67c6364__CommunityShaders.log`

The source and archive were both 4,072,605 bytes with SHA-256
`b4ecbaf1921d72f2afa8d066f106a0882243cf28bde5b6a0f369450ada0bb02f`.

## What the log establishes

Foveation was executing. This was not a requested setting that failed to
activate. Each failure was emitted for the `submit-stage foveated center`
dispatch with all of the following active-state evidence:

-   `foveatedConfigured=true`
-   `peripheryTAAConfigured=true`
-   runtime plan `foveated=true` and `peripheryTAA=true`
-   fallback record `preparedFoveatedEncode=true`

The active resolution plan was a 4936x2740 stereo display, 4192x2328 stereo
render input, and 4936x2740 final output. The failed center dispatch used an
843x939 input extent and 992x1104 output extent with scale
0.401945x0.402920. Its resolved Streamline viewport was 4608 (`0x1200`), eye
0, role `SubmitStageFoveatedCenter`.

Viewport 4608 is consistent with the current role allocator:

```
0x1000 + (SubmitStageFoveatedCenter=2 * 0x100) + (slot=0 * 2) + eye=0
= 0x1200
```

The role therefore had isolated Streamline viewport state. The evidence does
not support a collision with the full-eye or earlier foveated-center roles.

## Failure observations

The archived log contains three occurrences:

| Archive lines | Local time   | CS frame | Eye | Result                      | Retry frame | Immediate presentation behavior                           |
| ------------- | ------------ | -------: | --: | --------------------------- | ----------: | --------------------------------------------------------- |
| 12937-12948   | 09:47:00.646 |    58997 |   0 | `eErrorDuplicatedConstants` |       59027 | Eye 0 `VendorFailureStretch`; eye 1 `PresentationStretch` |
| 21903-21913   | 09:48:45.856 |    71587 |   0 | `eErrorDuplicatedConstants` |       71617 | Eye 0 `VendorFailureStretch`; eye 1 `PresentationStretch` |
| 37977-37981   | 09:55:52.870 |   122676 |   0 | `eErrorDuplicatedConstants` |      122706 | Fallback armed; archive ends before recovery evidence     |

The first two occurrences were 12,590 frames and 105.210 seconds apart. The
third was another 51,089 frames and 427.014 seconds later. All three occurred
on render thread 13304, used eye 0 and viewport 4608, and armed the same
30-frame retry backoff. This is sparse but repeatable behavior, not one
duplicated log message.

For the first two failures, the fallback record described per-eye input
2096x2328, per-eye output 2468x2740, combined input and bounds, no menu pause,
no loading state, no transition bypass, and no already-active backoff. It set
`forceFullEyeFallback=true`. The presentation controller reported both eyes as
vendor-evaluated on the next frame. That proves presentation recovery; it does
not prove that the foveated-center dispatch itself retried on the next frame,
because its retry backoff remained active.

The dispatch profile was DLSS quality 1, preset 1, HDR disabled. Frames 58997
and 122676 used jitter `(0.125000, 0.277778)`; frame 71587 used
`(0.312500, 0.203704)`. All used pinhole offset
`(-0.001621, +0.001460)`. Requested viewport 0 resolved to the isolated role
viewport 4608. The inspected calls retained these resource identities:

| Input             | D3D11 resource identity |
| ----------------- | ----------------------- |
| Color input       | `0x1E9A846FDB8`         |
| Color output      | `0x1E9A846F0B8`         |
| Depth             | `0x1E9A84702F8`         |
| Motion vectors    | `0x1E9A8470838`         |
| Reactive mask     | `0x1E9A8470AF8`         |
| Transparency mask | `0x1E9A8471578`         |

The bounded presentation record counted two traced vendor-failure eye
observations, four presentation-stretch eye observations, one completed
one-frame stretch episode, and a maximum presentation stretch of one frame
(20.5136 ms). Its episode accounting must be reported as emitted: it did not
provide two completed episodes even though the log contains two separated
traced failures. There were no bounds mismatches in the record.

The renderer's fidelity-mismatch counter advanced to 1, 2, and 3 beside the
three vendor failures. These are consequences of the rejected vendor frames,
not evidence of a separate activation failure. No associated hard renderer
failure, OOM, device loss, bounds mismatch, allocation failure, context
failure, generation mismatch, or dimension mismatch was observed. Because the
COC scenario was invalid, memory-growth and resource-retirement results are
not usable and remain `n/a`; absence of a completed result must not be reported
as zero.

## DevBench DLSS trace correlation

The bounded trace captured the first two failures and reported:

| Counter                       |     Value |
| ----------------------------- | --------: |
| `slSetConstants` calls        |   147,946 |
| `slEvaluateFeature` calls     |   147,944 |
| Duplicated-constants failures |         2 |
| Evaluation failures           |         0 |
| Exact constants-cache reuses  |         0 |
| Total records                 |   295,890 |
| Retained / capacity           | 256 / 256 |
| Dropped                       |         0 |

Both failed calls stopped before evaluation, explaining the two-call
difference between `slSetConstants` and `slEvaluateFeature`. The observed
duplicated-constants rate inside that capture was approximately 0.001352%, or
13.5 failures per million `slSetConstants` calls. This rate describes only
that bounded capture and is not a general reliability estimate.

The last pinned failure, sequence 134158, correlates the previous successful
use and the rejected use:

| Trace field                  | Previous successful constants |          Rejected constants |
| ---------------------------- | ----------------------------: | --------------------------: |
| CS frame                     |                         71586 |                       71587 |
| Streamline frame-token value |                         71586 |                       71586 |
| Frame-token address          |          `0x00007FFD9AFF8D88` |        `0x00007FFD9AFF8D88` |
| Resolved viewport            |                          4608 |                        4608 |
| Eye                          |                             0 |                           0 |
| Result                       |                         `eOk` | `eErrorDuplicatedConstants` |
| Constants sequence           |                        134154 |                      134158 |
| Compositor cycle             |                         67362 |                       67363 |

The previous evaluation, sequence 134155, also succeeded for frame/token 71586. On the rejected call, the CS frame and camera-dependent constants had
advanced, but the Streamline token had not. The changed-fields mask was
`0x00001E7000C00001`: frame, jitter X/Y, clip-to-previous and
previous-to-clip matrices, constants jitter offset, camera position, and
camera up/right/forward changed. The frame-token field did not change.

This rules out the constants cache treating the call as an exact duplicate.
The cache includes both CS frame and frame token, so the new CS frame made the
signature different and allowed `slSetConstants` to run. Streamline then
rejected the already-used token/viewport pair.

The resource identities, viewport geometry, output dimensions, quality mode,
preset, and HDR state remained stable across the inspected failures. There is
no evidence here for allocation churn, a dimension mismatch, an options-cache
mismatch, a duplicated C++ call hidden by the DevBench instrumentation, or an
`slEvaluateFeature` failure.

## Completed simple-COC-5 repeat

A separate, valid `simple coc 5` repeat on the same exact producer completed
all 20 measured transitions. Its fresh bounded DLSS trace recorded four more
`eErrorDuplicatedConstants` failures at CS frames 265900, 267898, 271985, and 276836. The capture counted 59,670 `slSetConstants` calls, 59,666
`slEvaluateFeature` calls, four duplicated-constants failures, zero evaluation
failures, zero constants-cache reuses, and zero dropped records.

The pinned frame 271985 repeated the same signature as the earlier evidence:
the rejected eye-0 submit-stage foveated-center call used token 271984 after
that token and viewport had succeeded on frame 271984. This independent
bounded repeat upgrades the symptom from sparse observations to repeatable
active-session evidence. Acquisition-side tracing is still required to prove
the proposed cross-thread publication interleaving.

The repeat also demonstrates separation from render-scale latch behavior. All
20 strict destination transitions completed, all 20 resource-publication
receipts matched current generation, dimensions, device, context, and deferred
setup, and there were no hard transition failures, OOM events, or device-loss
events. The four vendor rejections contributed four vendor-failure stretch eye
observations and safe presentation fallback, but they did not prevent the
render-scale controller from reaching `Active`.

The repeat log was archived before analysis at:

`D:\Coding\GitHub\CS logs\c881bade50b2__20260828T092119112Z__636d24e3__CommunityShaders.log`

The source and archive were both 4,728,556 bytes with SHA-256
`68c537deac1d29fda7016c7d5e7f34a0baf86d11d3fdd8a5421211f91da791f8`.

## RC166-family confirmation

A later `RC166-23-g86d21fc0a` producer, build ID
`734dc711de78c63fc753e8ca270f6f74fa174fc438c239adf571c2e4e7f6b22f`,
independently logged three lifetime eye-0 submit-stage foveated-center failures
at frames 59991, 69488, and 85373. Frame 85373 occurred after the fresh stress
session was armed but before measured transition 1. It therefore contributed
one fresh session vendor-failure observation while every measured transition
retained a zero vendor-failure delta.

The DLSS trace lane armed after that event and recorded 55,014
`slSetConstants` calls, 55,014 evaluations, zero duplicated-constants
failures, zero evaluation failures, and zero dropped records. This ordering is
preserved explicitly: a zero bounded-trace count does not erase the earlier
archived-log event.

The failure diagnostic again reported `foveatedConfigured=true`,
`peripheryTAAConfigured=true`, an active foveated/periphery-TAA plan, and
`preparedFoveatedEncode=true`. Presentation recovered, and the subsequent
simple-COC-5 assay completed all 20 strict transitions with zero hard failures,
OOM events, device-loss events, or per-transition vendor-failure deltas. This
is further evidence that the token symptom is independent of the later
Main-VR compositor-hold latency.

The RC166 CommunityShaders log was archived before analysis at:

`D:\Coding\GitHub\CS logs\734dc711de78__20260828T094731584Z__05ee69c6__CommunityShaders.log`

The source and archive were both 684,054 bytes with SHA-256
`381549807b065df1b75ff68d82e597570a308375fd1e97948aeb06617d29bbe8`.

## Qualification metadata caveat

The qualification receipt preserved the requested settings correctly:

-   `foveatedVendorDispatch=true`
-   `peripheryTAAEnable=true`
-   foveated center area 0.3
-   periphery-TAA center area 0.3 and outer scale 0.7

However, its `physical` object reported both `foveatedVendorDispatch=false`
and `peripheryTAA=false`, marked the object valid, and emitted
`physical_active_contract_mismatch`. That physical-contract result contradicted
the producer's active-plan and actual-dispatch evidence above. It must not be
used to conclude that foveation was inactive.

This independent DevBench instrumentation defect is recorded locally as
`AUTO-20260828-090804611-2D0F0D48`. The expected correction is either to make
the live physical values reflect actual dispatch or to expose stable-contract
metadata and live execution as separately named fields.

## COC run validity

The scenario requested editor ID `Dragonsreach`, while the preserved protocol
requires `WhiterunDragonsreach`. The current cell remained
`WindhelmExterior01`, so the run emitted no destination event and therefore no
render-scale sample. It was not appended to the comparison CSV.

The scenario also continued after semantic `qualification_wait` outcomes of
`timeout`, `error`, and `cancelled` even though its steps were marked
successful and `continueOnError=false`. That separate automation defect is
recorded as `AUTO-20260828-085832764-05B7BF29`. It does not change the foveated
vendor evidence, which came directly from the exact producer log and bounded
DLSS trace.

## Leading root-cause hypothesis

The leading hypothesis is a synchronization gap in the shared frame-token
provider. It is an inference from the trace and source, not yet a captured
thread interleaving.

`Streamline::EnsureFrameToken()` is used by both DLSS constants submission and
the early Reflex sleep path. It owns one shared `FrameChecker` and one shared
raw `sl::FrameToken*`. `FrameChecker::IsNewFrame()` publishes its new
`last_frame` value before `EnsureFrameToken()` calls `slGetNewFrameToken()`, and
the pair is not protected by a mutex or an atomic publication protocol.

A compatible interleaving is:

1. The Reflex-side caller observes CS frame 71587 and updates
   `FrameChecker::last_frame` to 71587.
2. Before that caller has acquired and published the new Streamline token, the
   render-side caller enters `EnsureFrameToken()`.
3. The render-side caller sees that frame 71587 is no longer new and returns
   the still-published token 71586.
4. Foveated eye 0 submits frame-71587 constants with token 71586 to viewport 4608. That token/viewport pair was already used successfully, so Streamline
   returns `eErrorDuplicatedConstants`.

This sequence explains the one-frame-stale token, rarity, first-eye failure,
and successful safety fallback. The current trace does not identify which
caller acquired or reused each token, so a cross-thread race is not yet
proven. Other causes that can return a stale token must remain open until
acquisition-side tracing or a deterministic test confirms the interleaving.

The issue was exposed on the render-scale-active submit-stage foveated path,
but it is not evidence that render-scale activation or foveation activation
failed. The shared token provider is also used by non-foveated DLSS and Reflex;
the captured failures are foveated-specific observations, while the suspected
ownership defect is broader.

## Development direction

A correction should make the CS frame number and Streamline token one coherent
published value:

-   Capture the requested CS frame by value.
-   Serialize token acquisition and recheck the frame while serialized.
-   Acquire into a local token pointer, then publish the frame/token pair only
    after `slGetNewFrameToken()` succeeds.
-   Return a local snapshot containing both frame and token to each consumer.
    Build constants and diagnostics from that same frame snapshot.
-   Reset the pair under the same synchronization used for acquisition.
-   Do not hold the acquisition lock across `slSetConstants`,
    `slEvaluateFeature`, PCL marker calls, or `slReflexSleep`.
-   Preserve one token per CS frame across Reflex and DLSS. Do not allocate an
    independent token per feature as a shortcut.

Acquisition tracing should record the requesting consumer (`reflex` or
`dlss`), requested CS frame, returned token value and address, acquisition or
reuse decision, and thread ID. This would directly confirm or reject the
leading interleaving without high-frequency image capture.

## Required validation for a correction

1. Add a deterministic concurrency test with an injectable token provider.
   Pause one caller after it observes the new CS frame but before token
   publication, then request the same frame from a second caller. Both must
   receive the newly committed token; neither may receive the previous token.
2. Verify failure handling does not publish a new frame paired with an old or
   null token.
3. Run a bounded DLSS trace with Reflex and submit-stage foveation active.
   Require zero duplicated-constants and evaluation failures, and confirm a
   token changes between consecutive CS frames while remaining consistent
   among consumers of one frame.
4. Exercise full-eye and foveated VR dispatch, menu and loading transitions,
   and render-scale relatches. Also validate the shared non-VR Streamline path
   for SE/AE so synchronization is not accidentally VR-only.
5. Retain the current full-eye stretch fallback and 30-frame vendor retry
   backoff until the corrected path demonstrates stable vendor dispatch.

That invalid scenario was subsequently rerun with the exact destination
`WhiterunDragonsreach`. The completed repeat is recorded as a separate,
rightmost build column in the render-scale comparison ledger; the invalid run
remains excluded.
