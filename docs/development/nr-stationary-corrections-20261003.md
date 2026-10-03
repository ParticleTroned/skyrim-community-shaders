# Stationary-scene NR composition and compact retention corrections

## Scope and producer

This run continued on the user's existing Bannered Mare view. No camera,
AI, save/load, DLL deployment or game relaunch command was sent. The tested
producer was `f0acc3926842a84b507d0657b0fa4936cf45286d9627e8524ac807cca7a8f3b6`,
compiled from `46fd070fabb565f4827510e475e589f6470f4547` plus dirty source
digest `3834acbea5a4868fc35a904c7c6f6a6b0e642ed61e4e5675afeb7b52d1913210`.
The installed 31,678,976-byte DLL SHA-256 was
`96bdf41dcfb47902ce10e16bed107b5635e78b94ea431e8a9536c23a166684d7`.
The enabled physical AIO provider, adjacent manifest and producer matched;
Overwrite and unmanaged Data did not supply another DLL.

Complete local receipts, submitted HMD frames, input bundle and replay results
remain under `build/validation/nr-task9-11-stationary-20261003/`.
The laptop was suspended during the session. These intervals are not GPU
watchdog evidence. The ending camera observation differs from the initial
position even though no camera mutation was sent; this is not a completely
fixed-pose campaign.

## Mode C composition failure

The native renderer successfully evaluated three regions per eye while the
outer route published `normal_dlss_pair`, `outputCommitted=false` and work
outcome `failed` for both eyes. At source frame 49959, all six evaluations
finished without a native failure. The output selection helper still rejected
`computeRegions.count > 2`, so it discarded the prepared selection and requested
a history reset after successful native work. Returning the configured limit
to two restored `neural_pair` and committed output in both eyes.

The correction validates output coverage using the same compile-time capacity
as the plan storage. It checks every rectangle and every pair for containment,
bounds and overlap before any composition write. Its pixel sum still controls
the baseline copy, so gaps between produced regions retain ordinary input.
Production retains the two-region ceiling; higher counts remain DevBench-only.
SE/AE use this same reduced-resolution helper without a VR-specific assumption.
There are no new GPU resources, shader dispatches, copies or synchronization.

The regression covers one through four output regions, the eight-region
experimental ceiling, implicit single output, invalid counts, invalid support,
out-of-bounds rectangles and overlap between later regions. Modes A/B committed
stereo output with the requested limit four, but the observed plans used only
two regions per eye; these observations do not qualify four actual regions.

## Compact allocation churn

Current GPU mask bounds could produce a 768x512 context while pending bounds
required a 896x1120 conservative envelope. The latter exceeds the largest
compact bucket. Retaining only the currently allocated compact width lost
that retention when full storage was selected, allowing the next frame to
shrink back to compact and then grow again. The cumulative resource-rebuild
counter rose from 2 to 434 during the captured compact interval and reached
454 while disabling it and settling. This blocks Task 10 qualification.

The correction retains allocation policy independently per physical slot.
Compact capacity can grow, and successful full-coordinate fallback after the
first compact allocation latches full coordinates until a successful explicit
`nr_reset`. Cold full-coordinate frames leave compact eligible once current
bounds arrive; otherwise cold pending bounds would disable the experiment
before it ever allocated compact storage. Failed allocation
or speculative candidate scoring cannot commit this state. Automatic backend
retirement preserves it. The state, its update and its status fields are
compiled only with the DevBench bridge. Compact inputs remain off by default.
The status fields are `compactStorage`, `compactMinimumSide` and
`compactFullCoordinateFallback`; the registered setting description explains
the reset behavior. Production has no added retention state or update work.

Tests alternate current and pending bounds 100 times after full fallback,
verify monotonic compact growth, independent slots, nonmutating candidates
and explicit reset. A/B compact requests retained their full capacities and
stable resource counts. Empty-range C bypass advanced frames without native
evaluations or resource/copy growth; re-entry resumed evaluations. An enabled
cost selector without a profile reported `unknown_cost_fallback` and did not
adopt a production profile.

## Measurements and limitations

Three complete 300-frame in-game windows recorded NR stereo GPU self time
7.911 ms (full reference), 28.349 ms (compact), and 44.731 ms (resumed full
reference). The compact CPU self time was 22.463 ms. Severe baseline drift,
suspension and allocation churn reject this as a comparative performance
qualification. A separate interrupted capture completed its 300 samples but
remains labeled suspension-affected. No cost model or performance improvement
is adopted from these windows.

The session ended with 60,106 native evaluations, 23,262 successful native
stereo batches and zero native failures, stereo failures, device removals or
quarantines. Those counters do not erase the outer composition failures.
Original NR and FOV settings were restored, as verified by the original
configuration fingerprint; screenshot/profiler/native-input owners were
inactive. Skyrim accepted `qqq` and exited normally. MO2 closed through its
exact recovery controller, the session was released and access was yielded.
The camera-restoration assertion failed because the observed position had
changed; no corrective camera command was sent.

The four-frame C input capture contains 144,506,880 payload bytes and passed
input/hash and storage validation. With the game and compilation stopped,
separate full/compact/full processes replayed the exact captured inputs. Each
case used one warmup and three measured consecutive stereo samples, resetting
each evaluation. Native runtime was 310.8.0, SHA-256
`8270b350cd82de5ce89806872cdd6b6a9249b80836b91bbeb3573470744cc206`.

| Crop                            | Full before, us | Compact, us | Full after, us |
| ------------------------------- | --------------: | ----------: | -------------: |
| 256x256                         |        4331.000 |    4334.000 |       4325.000 |
| 512x512                         |        4881.000 |    4859.333 |       4866.333 |
| 768x768, complementary sentinel |        6179.333 |    6164.667 |       6154.000 |

All accepted compact/full pairs have matching input contracts and bitwise
equal native output crops in both eyes for all three measured frames. Saved
output sizes and hashes were verified. The original 768 full-storage cases
were rejected for an ambiguous RGBA8 output-sentinel match during warmup.
Complementary-sentinel repeats passed; all eight raw outputs of each original
case match its complementary repeat. The original rejected samples remain.
The replay reporter rejects direct full-before/full-after comparison because
its logical context names repeat between processes; the local launch journal
preserves their separate invocations. Compact/full comparisons pass that
contract. The table is descriptive native timing, not an in-game cost or a
quality/performance promotion. Compact cost is effectively unchanged here.

## Tooling and remaining qualification

The installed automation cache rejected valid input capabilities because it
predated `vr-automation` commit `d93b64e`. That fix already belongs to `dev`.
Its semantic suite passes 263/263 and source/package module hashes match.
Feedback `AUTO-20261003-123637102-38C55F19` is linked to the earlier fixed
report; no duplicate validator implementation was added. Marketplace cache
refresh is tracked separately in automation commit `5c9ddfe`.

Both NR corrections need a new-DLL live test. For composition, require three
and four actual regions per eye plus outer `neural_pair`/committed output, not
only native successes. For compact retention, alternate current/pending bounds
and prove stable resource counts after growth/full fallback, then explicitly
reset and verify that smaller storage becomes eligible again. Repeat timing
with stable bracketing baselines and assess output separately. Task 9's wider
candidate search and held-out calibration gates, Task 10's whole-pipeline and
moving-output gates, and Task 11 implementation remain open.

## Replacement build and validation

`pwsh ./tools/validate-local.ps1 -OutputDirectory build/validation/nr-stationary-final-20261003`
passed all 227 discovered tests, with zero failed, disabled, skipped or
unbuilt tests. The universal SE/AE/VR Release DLL, controller/shader builds,
unified-preset tests/check and producer verification passed. DevBench is on;
Tracy and automatic deployment are off. Preset changes update only the source
policy fingerprint and generated report; no rendering settings changed.

Replacement Build ID is `22b28b37248b509ba2c2584ce8fe87f1b6ae5704839fbefcf3295b34f10bffba`.
Its compiled source base is `7b89e1aee490b6301db46d55c6271830a2c1fa1c`, with dirty digest
`3c4ec751aead25d00377c6bc725b0d76c35cd2e8d93a7e6347af7061386642b2`. The compiled patch, changed files and manifest are
preserved under the local evidence directory's `aio/` before this final report
addition. The later documentation/commit is not misidentified as compiled
source. DLL SHA-256 is `597f4e0e84a48d247965438bd53d1c08ec21c03628d8f7bd212919b62f12f116`;
size is 31,681,024 bytes.

`pwsh ./tools/cmake.ps1 --install build/ALL --config Release` staged the AIO.
The replacement archive is
`CSX_AIO-main-vr-nr-NR-RegionCompact-DevBench-20261003-22b28b37248b.7z`
under `dist/`, with 199,354,704 bytes and SHA-256
`28dcaf45c49f63ae9eb7c8c58e4de054aa6bf5594f402cf29c48596b093d7946`. Archive integrity passed, and all
386 extracted files match the staged sizes and SHA-256 hashes.
The DLL, PDB and manifest match the producer; the pinned NR runtime hash also
matches. No personal settings or shader cache is included. Adjacent receipt
and checksum files preserve the complete inventory and provenance. No game
installation or relaunch was performed.

The local automation `dev` branch contains cache-refresh commit `5c9ddfe`.
Marketplace and installed cache now use `0.9.0+codex.20261003165312`;
214/214 installed files match the packaged source. The installed semantic
suite passes 263/263, and plugin validation passes. This includes the existing
typed input-capabilities outcome fix. A full Codex host restart is required
to load the refreshed catalog. No remote branch was pushed.
