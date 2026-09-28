# Task 3A live checks and native mode-transition failure

## Result and producer

The sampled ROI contracts passed, but this live run **failed runtime
stability**. A GPU hang occurred after changing from reduced-resolution
NR (C) back to foveated NR (B). Task 3A is not fully runtime-qualified.

The user started Skyrim VR PID `29768`; DevBench screenshot session
`0d8000bb-cc6c-5af2-624a-5a4cfbd8bc25`. The enabled test AIO was
`CSX_AIO-main-vr-nr-Task3A-DevBench-20260927-ee833efd718c`:

-   Build ID: `ee833efd718c902575a8a0ccc13c3424ceb01b71f1df522b6e3cfcd5c0ceec76`.
-   Source: `7c4effa2749bb8d3cf123818ad6cf06bdc03a7cf`, dirty digest
    `b1fac7c3d17db45542f8130e4bb9b2789b2fdd8aeaebdd9145ad3eab298b99a6`.
-   Physical DLL: 31,032,832 bytes, SHA-256
    `11859685c04181344fa352ccc968b4fc0c2be0f54691faf8f1ca01b59173039a`.

The physical DLL, adjacent manifest and AIO receipt agree. The exact
enabled profile has one loose provider, with no Overwrite or unmanaged
Data replacement. The earlier PID `30896` run was interrupted by a
user-controlled restart and is separate evidence.

Local evidence is retained in
`build/validation/nr-task3a-live-20260928-pid29768/`. Raw tool envelopes
preserve their original JSON strings and 64-bit identities. The local
`audit_task3a.py` uses the maintained transaction validator, adds ROI
containment, preparation/execution agreement, capacity, disjointness,
native outcome and NoWork checks, and verifies PNG sizes and hashes.

## Successful samples and limitations

FOV was enabled at the user's requested `0.95`. Character selection,
visual isolation and the default multi-ROI savings gate remained enabled.
Colour mode remained `legacy_raw`; no colour tuning or performance
campaign ran. DLSS K/native AA was the initial profile; C used temporary
DLSS K/Quality with Render Scale enabled and a 1008x1120 render eye.

| Mode and case                         | Source frame | Physical evaluations | Sample result                                                      |
| ------------------------------------- | -----------: | -------------------: | ------------------------------------------------------------------ |
| B, multi-ROI off                      |        50280 |                    2 | ROI contract and stereo commit passed                              |
| B, multi-ROI on                       |        52982 |                    4 | Two disjoint regions per eye; four eligible actors per eye         |
| A, retained main-route transaction    |        61045 |                    2 | ROI contract and stereo commit passed                              |
| C, multi-ROI enabled, single fallback |        63207 |                    2 | ROI contract, jittered domain and native resets passed             |
| C, attributed HMD capture             |        64088 |                    2 | Immutable acquisition/delayed join and both artifact hashes passed |
| C, forced-zero selection              |        66439 |                    0 | Both eyes NoWork, no evaluations and empty ROI descriptor lists    |

The A record at frame 61045 is retained in `c-multi-status.tool.json`;
its own producer identity is authoritative. Earlier A samples at frames
59186 and 59868 had no renderer/preparation evidence and remain
inconclusive. Repeated retention of frame 61045 is not another sample.

All evaluated descriptors preserve ownership equal to inference and the
retained envelope, contain conservative sampling support, and fit their
allocation. B's split uses slots 0/4 and 1/5 with contexts
`(1152,640,360,832)` and `(448,192,640,896)` on the left, and
`(1088,640,424,832)` and `(448,192,640,896)` on the right.
The full-eye capacity remains 1512x1680. C retains a 1008x1120 capacity
and resets native inference; DLSS performs reconstruction afterward.

The B screenshot completed but lacks NR attribution:
`submission_frame_unmatched` for both eyes and no capture diagnostics.
The C screenshot has exact attribution and its delayed join passes.
This is not a matched visual baseline, temporal/stereo quality assessment,
lighting/alpha proof, or performance comparison. No live SE/AE,
one-eye-empty, GPU ready/pending/ready, repeated-source, nonzero full-eye
crop-origin, overlap rejection, or injected rollback campaign ran.

## Failure sequence

The preserved `CommunityShaders-failure.log` provides the ordering
(local time, Europe/Berlin):

1. C inference and the forced-zero NoWork sample completed without any
   recorded failures. The success counter reached 32,434.
2. At approximately 10:33:59, `nr_configure` restored
   `mode=foveated` and `characterMaskTestMode=authored`. It reported an
   accepted insertion transition with history reset and no backend
   retirement. DLSS Quality/Render Scale remained active.
3. At 10:34:09.143, frame 67431, the renderer first observed
   `DXGI_ERROR_DEVICE_HUNG` (`0x887A0006`) after the D3D11 colour-input
   copy on submit slot 2. The native backend entered quarantine and
   intentionally retained unsafe ownership.
4. The DLSS native-AA/Render Scale-off restoration occurred later, around
   10:34:47. It is not the initial failing transition. The earlier progress
   message associating the fault with that restoration was too broad.
5. Final counters retain one failure, one device-removal event, one
   quarantine, 32,456 successful evaluations, 10,716 quarantined bypasses,
   5,359 failed stereo attempts and two failed resets. Configuration
   restoration does not clear these failures.

The logged copy stage is the first observation of an asynchronous device
failure, not proof that the copy command caused it. No driver-internal
fault location or pre-Task-3 control run establishes causation.

NR is disabled, its original B mode, authored selection and multi-ROI-off
setting are restored, and capture evidence is disabled. The public
upscaling API reports the original DLSS K/native-AA, Render Scale-off and
FSR4 preference. FOV remains enabled at `0.95` as requested. Capture
ownership is inactive, with four completed artifacts and zero failed
artifacts. The process remained responsive to DevBench, but the removed
device and unsafe-abandoned NR backend are not recovered. Another native
NR test requires a fresh process.

After that final snapshot, the process exited and DevBench became
unreachable. The exit cause is not established. Character rendering was
still enabled under disabled NR in the last successful receipt; a request
to restore its initial disabled setting received a transport error and
was not replayed. No settings-save, game-close or restart action was issued.

## Targeted transition fix

The user requested a fix after this failure. The settings transition
previously treated a healthy C-to-A/B change as history-only. The targeted
change now requires complete native backend retirement when crossing the
pre-DLSS/post-DLSS input boundary, in either direction. It uses the existing
bounded GPU retirement, native feature release and backend teardown. The
next evaluation initializes a new backend only after successful retirement.
Failed retirement rejects an enabled mode change and preserves unsafe
resources; disabling NR remains accepted. Healthy A/B and same-domain
mask transitions retain their existing behavior.

The shared policy applies to SE/AE/VR. DevBench's registered description,
reset-attempt prediction and response schema describe the same behavior.
The extracted controller regression covers all mode pairs, both runtime
classes, a world frame or no frame, successful and failed retirement,
rollback, retained unsafe resources and transition-frame invalidation.
This addresses backend reuse across the failing boundary; preventing the
observed driver hang remains subject to a fresh-instance replay.

The preset source fingerprint changes because the reviewed implementation
and policy header are inventoried. Serialized settings, defaults, preset
revision 8 and the three tier payloads remain unchanged outside their
generated compatibility metadata.

Local toolkit feedback was recorded for the B screenshot attribution gap
(`AUTO-20260928-084553596-B7A75606`) and the NR transition failure
(`AUTO-20260928-084554117-378CD99B`); receipts are retained alongside the
raw evidence. Neither report was published externally.

## Fix build validation

`pwsh ./tools/validate-local.ps1 -OutputDirectory build/validation/nr-domain-transition-20260928-build` completed
successfully in 462.14 seconds. The universal Release DLL compiled with
DevBench ON, Tracy OFF and automatic deployment OFF. All 206 CTests
passed, with zero failures, missing tests or skips; `ShaderTests` passed
191 assertions. Preset-generator tests, generated-preset checks, DLL
manifest verification and the unchanged source snapshot check passed.
The CMake CMP0116 deprecation warning remains in the configure log.

The compiled producer, before this validation-note update, is:

-   Build ID: `3f647eeab4cb7f34f7fad943bbe91eeff169f1b9538be691befb6af35cdb3bd4`.
-   Source: `7c4effa2749bb8d3cf123818ad6cf06bdc03a7cf`, dirty digest
    `efaa19f226f8493fadd28fcf65e3bc80ca4a8afc5bdddd19ea937770b1fb333b`.
-   DLL: 31,033,344 bytes, SHA-256
    `c0bb5d48e69b92b29bca8fd6d125c2c313f9812f9826649a19fe5625fde373dc`.

The test package is
`CSX_AIO-main-vr-nr-NRModeFix-DevBench-20260928-3f647eeab4cb.7z`.
Its adjacent receipt and SHA-256 file retain archive identity. Packaging
evidence is under
`build/validation/nr-domain-transition-20260928-package/`.
Packaging did not deploy the archive. The user subsequently installed it;
the fresh-process replay below records its live result. The old test
archive and prior AIO staging are preserved.

Archive integrity and all 385 extracted file sizes/hashes match the fresh
staging tree. The archive is 198,910,969 bytes; SHA-256
`b4b2dadfc6f19645e56591c1b1ec8be89187f19c73a450f039102d9f90dcf586`.
Scoped pre-commit checks and `git diff --check` passed. The earlier
read-only hook-cache attempt, corrected bridge line endings and expected
initial preset fingerprint mismatch are retained as precheck outcomes,
not runtime failures.

## Fix replay in Dragonsreach: failed runtime stability

The user installed the fix AIO and requested a live test in Dragonsreach.
PID `21648` reports Build ID `3f647eeab4cb`. Its physical DLL, adjacent
manifest, exact compiled source/digest and AIO receipt match the producer
above. The enabled profile has one loose DLL provider, with no Overwrite
or unmanaged Data replacement. Evidence is retained under
`build/validation/nr-modefix-live-20260928-pid21648/`.

The initial state was NR off, B selected, character selection off,
multi-ROI off and DLSS K/native AA with Render Scale off. FOV was enabled
at the user's requested `0.95`. The test enabled character isolation,
multi-ROI and CPU frame evidence, keeping the default savings gate and
`legacy_raw` colour policy. DLSS K/Quality with Render Scale on completed
through the public API, producing 1008x1120 render eyes and 1512x1680
display eyes. Two eligible characters per eye were subsequently observed.

| Check                                | Evidence                                                                  | Result                                               |
| ------------------------------------ | ------------------------------------------------------------------------- | ---------------------------------------------------- |
| B to C                               | Frame 30783; retirement attempted and succeeded                           | C evaluated both eyes at frame 30869                 |
| C forced-zero selection              | Frame 31076                                                               | Both eyes NoWork; evaluation counter unchanged       |
| C to B, restoring authored selection | Frame 31237; retirement attempted and succeeded                           | Both eyes resumed; zero failures through frame 31738 |
| B to A                               | Frame 31785; history reset, no full retirement requested by configuration | A evaluated both eyes at frame 31876                 |
| A to C                               | Frame 32082, after device removal                                         | Retirement failed; transition rejected; A retained   |

The C-to-B return was followed by 1,004 successful evaluations through
frame 31738. Its subsequent B-to-A switch occurred about 59 seconds after
the C-to-B configuration. The log then shows backend reinitialization and
creation/evaluation of additional multi-ROI slots 6 and 7. At
11:14:57.280 local time, frame 32063, the renderer observed
`DXGI_ERROR_DEVICE_HUNG` (`0x887A0006`) after the D3D11 colour-input copy
on the A submit route. The A-to-C retirement rejection was logged later,
at 11:14:58.314. No original-DLSS-profile restoration preceded this fault.

The run therefore **fails runtime stability**: retiring native ownership
at the pre/post-DLSS boundary is insufficient to resolve the hang. This
does not establish the driver-internal cause, prove B-to-A caused it, or
show that the additional ROI slots caused it. The first failed copy is
an observation of asynchronous device removal, not fault localization.

The offline audit passes seven distinct successful A/B/C ROI samples and
three NoWork samples. It verifies containment, capacities, disjointness,
preparation/execution agreement and conservative floor/ceiling mapping
into reduced-resolution depth/motion guides. The earlier run's equal-grid
audit assumption was adapted for this run's scaled B/A guide inputs.
Two transition receipts lack preparation evidence and remain inconclusive;
two post-failure records lack renderer evidence. Retained earlier route
records are deduplicated by their own transaction identity. These sampled
passes do not qualify the failed run or establish visual equivalence.

Final counters preserve 3,442 successful evaluations, one failure, one
device removal, one quarantine, 926 quarantined bypasses, 464 failed stereo
attempts and two failed resets. Disabling NR succeeded even though backend
retirement failed; unsafe ownership remains retained. CPU frame evidence
is disabled. No screenshots or profiler captures were started in this
replay. DevBench still answered from PID 21648 at frame 33281.

Further renderer mutations stopped after the fault. Runtime settings
remain A selected with NR off, character selection and multi-ROI on,
FOV `0.95`, and temporary DLSS K/Quality with Render Scale on. No settings
save, game close or restart was issued. Restoring menu configuration would
not recover the removed device; another NR test needs a fresh process.

Next: investigate native resource/feature lifetime around the A/B route
and changing ROI layouts, using independent single-ROI/multi-ROI controls
in fresh processes. Preserve the accepted C-to-B retirement evidence,
but do not treat it as proof that the hang is fixed. Complete remaining
3A/3B qualification before starting Task 3C. No live SE/AE, matched visual
baseline or performance comparison ran.

The subsequent [lifetime isolation record](nr-lifetime-isolation-20260928.md)
retains the bridge-only, default-off diagnostic addition and verified AIO
`490462394cae`. The user subsequently installed it: A1 passed, followed by
the [Bannered Mare functional matrix](nr-task3a-bannered-mare-20260928.md)
in the same healthy process. Those checks did not reproduce the hang and
do not establish its cause. The unresolved device-removal result above is
unchanged; installation and restarts remain under the user's direction.
