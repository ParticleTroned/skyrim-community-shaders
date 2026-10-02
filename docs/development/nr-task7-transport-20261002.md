# Task 6 live follow-up and Task 7 transport candidate — 2026-10-02

## Acceptance

Task 6's new DLL passed the live functional checks, including the standalone
GPU-mask-support toggle. Its matched prior-build performance criterion
remains open: the current scene contained seven eligible actors instead of
five, and the evaluated rectangles differ. Lower CPU setup measurements are
not a controlled improvement-or-neutral result.

Task 7 now has a default-off, DevBench-only shared-source implementation for
the existing two regions per eye. Its native output, memory, lifetime and
total-cost qualification requires the new AIO. The current game contained
Task 6, so every Task 7 live measurement below is the private-transport
reference. Neither Task 7 closure nor a performance gain is claimed.

## Pinned live session

-   Build ID: `6646e62e56d3f88b7583ce3f714dfafd6e0f24ccb17e6ee259ab90a337cdc262`.
-   Compiled base: `64a22a5f1184c02477bed909ba4ff3462e44d2bf`;
    dirty digest: `4c7dded50db4f5ecb40860927f1a3c7c3ae5ad0dd7b2a98349d12ffe501ee7b1`.
-   DLL: 31,236,096 bytes; SHA-256:
    `1dbc6e9e2547089999051d106ae7d0c18f8de54db8eecff7ea7be237aa8f5426`.
-   Sole enabled loose provider:
    `CSX_AIO-main-vr-nr-NR-Task6-DevBench-20261002-6646e62e56d3`.
    Physical DLL, adjacent manifest, archive receipt and runtime producer
    matched. Overwrite and unmanaged Data had no replacement DLL.
-   Skyrim PID 40700, start `2026-10-02T20:28:46.2506573Z`; existing null HMD,
    Bannered Mare. AI paused, free camera held, FOV 0.95, range 30 metres and
    minimum face size 1 for the controlled windows. Those temporary settings
    were restored before shutdown.

Evidence lives in `build/validation/nr-task6-7-live-20261002/`.
`qualification-audit.json` verifies 18 complete sequences, 288 unique stereo
transactions, 576 eye records, all 288 artifact hashes, and ten completed
300-frame profiler windows. The 32×16 sequence images carry attribution;
they do not qualify stereo image quality. Full command receipts and raw
timers remain local.

## Task 6 results on the new DLL

| Check                                         | Result                                                                                                                      |
| --------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------- |
| Standalone GPU-support false→true→false       | Both one-field requests accepted first try; unrelated NR/character settings unchanged                                       |
| A and B                                       | Two native calls, 4,886,784 evaluated pixels per sampled stereo transaction                                                 |
| C                                             | Two calls, 2,257,920 pixels                                                                                                 |
| Multi-ROI with savings gate off               | Four calls, 1,711,104 pixels                                                                                                |
| Six cumulative 2-unit camera moves and return | All sampled transactions retained four calls and 1,711,104 pixels                                                           |
| Range exclusion                               | Native evaluations stayed at 22,204 while category captures advanced to frame 44,755; both eyes had current CPU empty proof |
| Re-entry                                      | Four successful calls resumed                                                                                               |
| Face-only / all categories                    | Four calls; 1,064,960 / 1,711,104 pixels                                                                                    |
| Two accepted B→C transitions                  | No watchdog, renderer failure or quarantine                                                                                 |

Every sampled source made one timed bounds-readiness decision; the peer
eye reused its result without another timed poll. All 288 sampled decisions
were pending and both eyes retained current CPU coverage. The final lifetime
totals were 20,685 stereo polls, 20,684 pending results, one ready result,
two eye uses and 41,368 eye fallbacks. There was no ring-busy or failed read.
The single ready result occurred outside the exact captured sequences.
Frozen-source/producer-completes-between-consumers coverage remains the
compiled policy and queue-gated WARP tests; this live run did not manufacture
a frozen-world source by opening a menu.

The ten surrounding status intervals contain respectively 554, 550, 541,
548, 745, 668, 671, 771, 737 and 719 polls in audit label order. In every
interval, polls equal queued stereo sources and successful stereo batches;
eye fallbacks equal twice that count. These intervals exceed their 300-frame
profiler windows and are not 300-frame readiness denominators. Evidence-off
readback clocks correctly report unavailable and zero duration.

Each timing row has 300 resolved profiler frames. GPU values are pass self
times; CPU ROI setup covers the frame's eye preparation. These are not
whole-game frame times and overlapping scopes must not be summed.

| Route | Evidence | ROI setup CPU ms | Mask GPU ms | NR stereo pass GPU ms |
| ----- | -------- | ---------------: | ----------: | --------------------: |
| A     | off      |         0.015552 |    0.502150 |             45.409363 |
| A     | on       |         0.015203 |    0.506974 |             45.240299 |
| B     | off      |         0.015401 |    0.508420 |             45.473804 |
| B     | on       |         0.015045 |    0.501283 |             45.602997 |
| C     | off      |         0.016264 |    0.231361 |             23.895082 |
| C     | on       |         0.015789 |    0.222243 |             23.573803 |

The older five-actor CPU values were 0.018290/0.017539/0.019544 ms, but
mask/evaluation area also changed. No cross-build performance verdict is
derived from those values. Evidence-on windows include detailed telemetry
and are not production-overhead measurements.

Final counters: 55,698 successful evaluations and output commits, 20,685
successful stereo batches, 16 successful resets, 20 resource rebuilds and
eight runtime initializations. Failures, device removals, quarantines,
stereo failures, reset failures and validation failures were all zero.
The 47,946 caller-history resets retain C's existing stateless policy.

Original NR, character, FOV, colour, profiler, AI, camera and menu state was
restored. `qqq` exited the pinned Skyrim process. Exact MO2 File→Exit cleanup
closed PID 38560, and both recovery-session ownership and access lease
`access-20261002T203937Z-4c72a0157005` were released. The retained task profile
and pre-existing SteamVR processes were preserved.

## Task 7 private-transport reference

All rows use four native calls and 1,711,104 pixels in C. Native queue means
come from separate 16-frame exact sequences; reconstruction means come from
300-frame profiler windows. They are deliberately not added together.

| Colour mode     | Exact native queue ms | Logical copy bytes/frame | Retained slot texture bytes | Reconstruction GPU ms |
| --------------- | --------------------: | -----------------------: | --------------------------: | --------------------: |
| Legacy raw      |             25.515125 |               20,533,248 |                  72,253,440 |           Not entered |
| Preserve source |             25.743188 |               27,377,664 |                  86,671,360 |              0.908683 |
| Neural lighting |             25.357938 |               27,377,664 |                  86,671,360 |              0.904836 |

No allocation occurred inside those sampled steady-state sequences and
their explicit CPU wait totals were zero. Command-begin CPU latency was
0.269–0.324 ms, separate from explicit waits and native GPU time. D3D11
per-copy timestamps were unavailable in the exact sequences because their
profiler was inactive. Retained bytes describe logical external textures,
not physical driver or native-model residency. They do not prove a VRAM
budget or an anticipated frame-time benefit.

## Task 7 implementation and adversarial review

`communityshaders.nr_color` accepts
`{"action":"configure","experiments":{"sharedSourceTransport":true}}`.
The experiment defaults false, is not persisted, and cannot be enabled in
a build without `DEVBENCH_BRIDGE`. It is frozen with the existing source
colour configuration and does not independently enable colour processing.

Compatible same-eye regions borrow prepared colour, depth, motion and
control-mask transport through shared RAII references. Matching includes
device/context, logical route and eye, frame/source/generation, capture and
configuration epochs, original resource identities, grids, formats, crop,
jitter, insertion and feature contract. The enclosing batch supplies one
frozen profile/exposure configuration. Native handles, region history,
private output, colour baseline and reconstruction result remain separate.

Existing validated context copies prepare each required rectangle before
the batch's native calls. Their union is not replaced by a gap-spanning
hull. The existing disjoint context/output adapter remains in force;
unknown native readable footprints remain explicitly unknown. Prepared P
is read-only throughout native evaluation, reconstruction and measurement.
Sharing does not alter selection, shape, count, resolution or cadence.

Transitions deduplicate native resource identity and required state only
when sharing is enabled. Conflicting states fail closed and use the
existing abort/quarantine path. Batch submission and serialized native
parameter access are unchanged; no per-ROI round trip is introduced.

Storage remains lazy and bounded by the existing eight physical slots,
with one exact capacity per slot and no speculative next-capacity prewarm.
Ordinary contained motion retains compatible resources. Ownership or
capacity changes use the existing D3D12 completion plus D3D11 consumption
tail wait before native release/replacement. No fence or lease is removed.

`nr_status.sourceTransport` deduplicates actual allocations, separates
active, cached-only and lease-only texture bytes, lists private outputs and
histories, and reports native resident slots separately. Logical-byte scope
excludes exposure/query/buffer and native allocations; physical residency
is not measured. Native bytes are unavailable, not zero. Quarantine reports
unknown abandoned ownership rather than claiming that clearing an object
made its memory available. Existing execution evidence charges creation
attempts, creation CPU time and newly allocated private/transport bytes.

A bounded rejection ledger keys device, physical slot and the complete
resource contract. Pressure and unsupported-capacity failures remain
rejected across frame, mode and target-FPS edits. Unsafe provider failures
block automatic retry; saturation cannot evict a rejection. Only an
explicit `nr_reset` after successful fenced retirement clears the ledger;
quarantine cannot pass that reset. No automatic smaller/cheaper fallback or
unmeasured native-memory admission heuristic is introduced.

Review corrected stale borrowed ownership when a region loses its matching
source, same-owner allocation replacement, truthful rebuild reporting,
the exact capacity recorded for colour allocation failure, and retained
memory reporting after quarantine. Bridge-disabled tests retain a false
experiment gate. No production HLSL or queue/lifetime protocol changed.

## Required next live qualification

Install the Task 7 test AIO manually. Hold one scene and alternate private
and shared transport in the same process. Pin the installed DLL identity
before testing. Verify shared source identities with distinct native slots,
histories and outputs; compare exact plans and current support, source reuse,
drift, empty/re-entry, category changes, A/B/C and colour/profile transitions.
Repeat changes to exercise retirement and bounded resource/lease growth.

Collect matched memory, creation/copy work, preparation, native execution,
reconstruction/composition and complete frame-cost evidence independently.
Check native shared-versus-private image results at full resolution and
preserved alpha/exposure/lighting. Exercise queue-delayed consumption and
failure/recovery safely in standalone tests; do not provoke a live watchdog.
Four regions per eye, overlap halos and backend aliasing remain later work.
SE/AE gameplay and native shared-transport behaviour are not yet qualified.

## Offline validation and producer

`pwsh tools/validate-local.ps1 -OutputDirectory build/validation/nr-task7-verified-20261002`
passed end to end: universal SE/AE/VR Release build, **223/223 tests**, zero
skipped, preset generator/rollback tests, generator `-Check`, diff check,
DLL identity and unchanged-source checks. DevBench is on; Tracy and
automatic deployment are off. The run took 509.13 seconds.

`ctest --test-dir build/nr-task7-color-tests -C Release --output-on-failure --output-junit task7-color-results.xml`
passed **21/21 tests**. WARP runs real production preparation and
reconstruction shaders for two disjoint, differently shaped regions and
three transforms/modes. Private and shared P reconstruct bit-identically,
including alpha; the gap remains untouched and P remains immutable through
all reconstructions. The neural output is a deterministic fixture, not an
NGX validation. The compiled on/off source-transport tests cover source and
binding identity, rejected sharing, barrier conflicts/capacity, and
persistent/saturated rejection ledgers. Existing source-transaction,
queue-gated readiness, retirement and abort checks passed in the full suite.

Scoped pre-commit hooks passed. The settings fingerprint covers whole
source files, including the edited colour/feature declarations. Review
confirmed unchanged persisted keys, defaults and migrations. Revision 8
and the base remain unchanged; regenerating the three tiers changed only
their compatibility fingerprint, with corresponding policy/report hashes.
Existing preset archives and local Open Shaders notes were left untouched.

The initial standalone colour targets were mistakenly requested from the
main build and reported MSB1009; the two transport targets had built. A
separate `tests/neural_color` configuration then built and passed all 21
colour tests. Two preliminary configure invocations lost the drive path
inside the wrapper's attached `-D` argument; separate `-D` and `name=value`
arguments corrected that invocation. Formatting hooks made their expected
first-pass edits before the successful final checks. These preliminary
command failures do not represent failed tests or a failed final runner.

Validated producer:

-   Build ID: `a1dc0b067333b00b77048bf7d3c3e608371a93599f47d5b60bfdfa0fecaaaeba`.
-   Compiled base: `4d3e54856d83c3b7df7128b780a0ed545d67c769`;
    dirty digest: `75fc0ee0a4b1dd70fc6d22020c204f7b4ff6944020d4111291835464c25b6d7d`.
-   DLL: 31,269,888 bytes; SHA-256:
    `3ebedbfe71c5fc17c1128eedd096d981f105815537f6e235ce0e9b41b8576d1c`.
-   NR 310.8 runtime SHA-256:
    `8270b350cd82de5ce89806872cdd6b6a9249b80836b91bbeb3573470744cc206`.
-   All 23 compiled changed/new files, exact copies, hashes, tracked patch,
    manifest and package receipt are retained in
    `build/validation/nr-task7-aio-20261002/`. This report, the Task 6 status
    update and the progress record were added after compilation. Their later
    commit does not replace the preserved compiled identity.

Archive:
`dist/CSX_AIO-main-vr-nr-NR-Task7-DevBench-20261002-a1dc0b067333.7z`.
It contains 385 files, is 199,159,313 bytes, and has SHA-256
`4eacc9cca0dc8a301122d69598c65effd1049ed0e0b754d8af3af5a66437407e`.
Archive integrity, all 385 extracted file sizes/hashes, producer DLL/PDB/
manifest and pinned NR runtime checks passed. All 23 compiled changed/new
files still matched their preserved hashes after the documentation update.
The adjacent `.receipt.json` and `.7z.sha256` preserve package verification.
No shader cache or personal preset settings are included. The archive was
not installed and Skyrim was not relaunched.
