# VR map panels retained after closing the map

## Cause and correction

The committed VR menu layer could survive after the last menu closed.
`BeginVRMenuFinalCompositeFrame` used the presentation context, including
its 30-frame tail, to decide whether old menu pixels remained valid. That
tail protects presentation routing through a transition; it does not prove
that a menu still owns the pixels.

The close event already invalidates the layer, but `State::isMapMenuOpen`
is refreshed once per frame. A native consumer can capture and commit again
while that cached state still says the map is open. Without another event,
the committed layer then survives until the presentation tail expires.

Invalidate a retained layer when the explicit menu context ends. Reuse the
existing explicit-context and tail predicates, evaluating menu state once
per call. Keep the presentation tail, upscaler settings,
and the first eye's immutable presentation decision unchanged. The second
eye keeps the same decision; the next frame retires closed-menu content.
The existing invalidation also clears desktop retained-pair validity.

The first runtime candidate left a second stale-state window: a delivered
map-close event could still be overridden by cached `isMapMenuOpen` while
admitting a new capture. The follow-up uses one atomic map-event state
(unknown, closed, open), shared by presentation and capture context checks.
A known VR close wins immediately; unknown state falls back to the frame
cache. SE/AE retain their event-open-or-cached-open behavior. Initial event
registration uses compare-exchange so a concurrent edge cannot be replaced
by the initial UI sample. The loading reset restores unknown state.

The expanded extracted-production test covers close with a stale open
cache, reopen with a stale closed cache, unknown-state fallback and non-VR
behavior. Substituting the first candidate's actual context query fails
with `published map close was overridden by cached open state`. The
follow-up passes with MSVC `/O2 /W4 /WX` and separately with AddressSanitizer
(`/O1 /fsanitize=address /Zi`). Results and source hashes are preserved in
`close-state-validation.json` under the local evidence directory.

The remaining sampled distortion has not yet been traced to an
exact submit branch: its acquisition metadata reports full-size per-eye
sources, and the existing Info-level log has no transaction-debug records.
This follow-up addresses a demonstrated state bug. The follow-up runtime
capture below confirms that a brief sky/geometry distortion still occurs;
the close-state correction does not establish an artifact-free visual fix.
Five later traced closes were clean. The user accepted provisional closure
after these repeats; the earlier observations and unresolved cause remain
part of the result.

The follow-up testing AIO was built from clean source
`058c0ac312090029ff8ff6ab085771d925fdfff2`, with SE, AE, VR and DevBench
enabled in Release. Its producer Build ID is
`a4a0d54243f248eae4749adcc6429309809eced0c6e0461f3e72841cb3918780`.
The DLL is 29,047,296 bytes with SHA-256
`c1e8da9147ad083126b4a2af4cae5324b8c490458d5e19f75851eac4c2b440fa`.
The delivered archive is
`close-state-delivery/CSX-VR-Map-Menu-Fix-058c0ac31-DevBench-AIO.7z`
under the local evidence directory, 90,431,325 bytes, SHA-256
`c12eabf900d8ff9e8df856cbd7d7117cbdfff87e7ddc7bb24555df9104926154`.
All 362 extracted files match staging; the DLL matches its adjacent
manifest, and no compiled shader cache is included. The existing separate
DevBench host remains required. The user installed this package and the
follow-up runtime check below verified its identity. Later amendments add
tests and evidence without changing production code; the recorded compiled
source identity remains authoritative.

## Baseline reproduction

The user supplied a running, unmodified game. DevBench captured native
OpenVR submissions with both eyes, rather than a desktop mirror. The scene
was `WhiterunDragonsreach`, player position approximately
`(-448, -128, -318)`. The actual selected profile was
`Codex Task - 20260919t055629z-tracy-guardian-main-vr-1bf5803a`.

The physical tracked set had a fixed, level HMD and no controllers. A
bounded synthetic tracked set pitched the HMD down by 0.55 radians and
provided two neutral controllers so the map panels were visible. It
automatically released and restored the controller indices. The map was
opened and closed through the documented engine-menu DevBench actions.

| Setting                               | Observed close result                                           | Native sequence                        |
| ------------------------------------- | --------------------------------------------------------------- | -------------------------------------- |
| DLSS K Native AA, VR Render Scale off | No retained panels in sampled world frames                      | `086291db-c165-cac2-f28e-6a60b988d585` |
| DLSS K Quality, VR Render Scale on    | Local-map panel and controls remain over the world in both eyes | `96d77ef0-00ad-dc96-471f-c75525833c3e` |
| None, Native AA, VR Render Scale off  | No retained panels in sampled world frames                      | `055566f8-fde8-22f9-3a80-b963c5889b1b` |

Every sequence acquired and wrote all 32 requested frames, without drops
or failures. Each frame contains a 3024 x 1680 side-by-side BMP and an
acquisition receipt. DLSS Quality rendered each eye at 1008 x 1120 and
presented at 1512 x 1680. None and Native AA used 1512 x 1680 per eye.

The affected sequence has map content in sample 3, engine frame 44018.
Samples 4 through 10 show residue in both eyes, from engine frame 44025
through 44053. Sample 11, engine frame 44057, is clean. The observed
residue spans at least 28 engine frames and 1058.609 ms. The first clean
sample is 1218.332 ms after the first affected sample. This is consistent
with the 30-frame tail; it is not an exact menu-close latency measurement.
Capture overhead and sampling cadence preclude claiming the user's
approximately 0.8 seconds as the measured duration here.

Menu-state receipts reported only HUD Menu 100 ms after the close command.
Original runtime settings were restored and verified: DLSS K Native AA,
VR Render Scale off, configured FSR runtime FSR4. Synthetic input was
verified inactive with controller indices restored. No save was written.

Baseline producer:

-   Source: `ed3817a41704532cc12e5942246d87e14dc75374`, clean.
-   Build ID: `665fc37219006d0bc0d6b697a35ae22ea4a057b8d4b7efb098daa2ad59726dc7`.
-   DLL SHA-256: `c85c12c304956e3a0364731db505154fc696a0ca6329458793b7c021e4ec19bb`.
-   DLL size: 29,045,760 bytes.
-   Enabled provider: `CSX-MainVR-ed3817a41-DevBench-AIO`.

The physical DLL matches the adjacent manifest and runtime Build ID.
Direct Overwrite and unmanaged Data checks found no competing DLL. A
matching AIO build receipt was not located; the retained shadow-admission
receipt describes another build and was explicitly excluded. The requested
base is `df9f377a53d237518c1b671b7be1085c9a65b69d`,
`refactor(render): reduce shadow admission work`. Its `Upscaling.cpp`,
`Upscaling.h`, and `State.cpp` are identical to the runtime producer's
versions. This establishes the affected source path, not complete binary
equivalence between the two commits.

## Candidate runtime validation

The user installed the candidate AIO and launched the same selected profile.
At the user's explicit request, DevBench loaded the named Dragonsreach save
`Save1_3FC115E3_0_507269736F6E6572_WhiterunDragonsreach_000006_20260910143626_1_1`.
The `postLoadGame` event and scene inspection confirmed the destination and
the same player position. This was a user-directed save load, not a new
qualified automation fixture. No save was written.

The running producer, enabled loose DLL, adjacent manifest and preserved
AIO build receipt agree:

-   Compiled source: `c38071341143418dac47d71eafc1dcdc212f57b7`, clean.
-   Build ID: `a870dbd2459a637a092db5f4618dfee62aab81a548c36c97db09f22a1afa89bc`.
-   DLL SHA-256: `7b941480a6fa698c75d7bdc5119803dfc8d5c2865f565ebfb60d727706dd7cd0`.
-   DLL size: 29,046,272 bytes; Release with DevBench bridge enabled.
-   Enabled provider: `CSX-VR-Map-Menu-Fix-c38071341-DevBench-AIO`.
-   AIO SHA-256: `bd2fc59a5781b264a5012202d355f275d102aa9f4cd04c5ee2260c26c58ac199`.

All 362 extracted AIO files match staging. Direct enabled-provider,
Overwrite and unmanaged Data checks found no competing loose DLL.
The source identity above remains the measured compile identity when this
report is folded into the single implementation commit.

DLSS K Quality, VR Render Scale on, and the same native stereo capture and
tracked-pose recipe exercised four local-map closes. All 128 requested
frames were acquired and written without failures or drops; both eyes of
every frame were reviewed. The panels and local map were visible before
closing. No captured world frame retained them afterward.

| Native sequence                        | First world sample / engine frame | Transition observation                                                      |
| -------------------------------------- | --------------------------------- | --------------------------------------------------------------------------- |
| `5aca8c16-71ec-a582-f05b-6fb01dc82647` | 4 / 50001                         | No distorted transition image sampled                                       |
| `347d70e4-23e8-15ce-9899-1bc0f66eac17` | 5 / 51821                         | Sample 4, frame 51815, has distorted menu fragments over sky                |
| `ab264272-86c1-7d9e-5a16-377c7f9cf827` | 5 / 55224                         | Sample 4, frame 55220, has distorted menu fragments over sky                |
| `c7a0dbe1-a416-53db-ec3e-89061d0b2dd5` | 4 / 59966                         | First world sample has a colour/lighting transient, without retained panels |

The result is a **partial visual pass**. The baseline's sustained residue
over the world is absent in these candidate samples, but two runs still
contain a distorted transition image. Capture sampling is not every engine
frame, so this does not establish a single-engine-frame duration. The
remaining transition artifact has not been attributed or corrected.

The first exploratory capture, `3cc0e2ac-1e2f-e1c4-33ec-52e7978e89f8`,
showed the world map without visible controls and is excluded from the
panel-lifetime verdict. Attempts to switch the later runs to world-map mode
still displayed the local-map panel. Their original evidence labels contain
`world`; the observed content and classifications above take precedence.
Separate world-map control coverage therefore remains incomplete.

`Centered Messages Box VR` was enabled, but the exact
`skyvr_hmd_info.nif` draw was not independently identified. These captures
do not prove that specific mesh case. No matched native CPU/GPU comparison
was collected, and capture overhead prevents a performance claim.

Runtime settings were restored and verified as DLSS K Native AA, VR Render
Scale off, configured FSR4. The completed restoration operation, inactive
tracked input with restored controller indices, empty held-key set, HUD-only
menu state and loaded Dragonsreach scene are preserved. The user-owned game
was left running. Full envelopes, original images, acquisition metadata,
artifact hashes and review classifications are in
`live-candidate/validation-result.json` under the local evidence directory.

## Follow-up runtime check

The user launched the follow-up AIO in the same selected profile and
Dragonsreach scene. No save was loaded or written during this check.
PID 5872 started at `2026-09-25T12:05:46.4847319Z`. The runtime producer,
enabled physical DLL, adjacent manifest and AIO receipt agree on the
compiled source `058c0ac312090029ff8ff6ab085771d925fdfff2` and Build ID
`a4a0d54243f248eae4749adcc6429309809eced0c6e0461f3e72841cb3918780`.
Direct checks of every enabled loose provider, Overwrite and unmanaged
Data found only the intended DLL.

Three local-map closes with visible controls used DLSS K Quality and VR
Render Scale on. Two reference closes used None at native resolution.
All five sequences acquired and wrote 32 stereo frames without drops or
failures, and both eyes of every image were reviewed. The sustained panel
residue is absent in all three DLSS runs, but one contains the remaining
sky/geometry flash. Neither None reference contains a sampled flash.

| Native sequence                        | Setting      | First world sample / frame | Observation                                                       |
| -------------------------------------- | ------------ | -------------------------- | ----------------------------------------------------------------- |
| `c6eb23de-2ccf-581d-bffa-e6331d35aec6` | DLSS Quality | 5 / 28845                  | Clean sampled close                                               |
| `00b6c0a1-bbfc-c581-720a-a60f50a61600` | DLSS Quality | 5 / 31018                  | Sample 4 / 31013 has sky/geometry distortion without map panels   |
| `fd8fa99c-e4f5-caef-d51a-3b29cc3446c6` | DLSS Quality | 5 / 32831                  | Clean sampled close; attempted keyboard toggle remained local map |
| `b5eac0e1-582b-2890-6213-863d2a1b355a` | None         | 5 / 36353                  | Clean sampled close                                               |
| `5ae17de2-8588-a5ed-645b-55aeaa221126` | None         | 5 / 39376                  | Clean sampled close                                               |

For the affected local-map run, the clean samples at frames 31006 and
31018 bracket the flash by 12 engine-frame increments and 281.140 ms.
The first clean world sample is 157.790 ms after the bad sample. These are
sampling bounds, not an exact duration or a single-engine-frame claim.
Both bad-frame source planes are 1512 x 1680, with full submitted bounds.
The controls are absent in the bad image; clearing retained UI pixels
therefore does not by itself explain or correct the remaining geometry.

An excluded exploratory world-map close without visible controls also
captured the flash: `2eb83fea-5bb5-aa0e-5e78-598fca735f0e`, sample 4,
frame 21599. Its clean bracket is frames 21592-21604, 313.355 ms.
It is evidence of the flash, but not world-map panel lifetime coverage.
Separate world-map controls and the exact `skyvr_hmd_info.nif` draw remain
unqualified. Advancing game time and NPC activity also preclude treating
the sequential None comparisons as an identical-frame causal experiment.

The full-session recorder rejected the installed host's missing guarded
recording schema. No recorder was started; bounded screenshot, scenario,
input-cleanup and identity receipts remain preserved. Local feedback
receipts are `AUTO-20260925-122955716-70762E91` for process-start timestamp
comparison and `AUTO-20260925-122956441-3DA1A487` for host compatibility.

The initial comparison restored DLSS K Native AA, Render Scale off and
the FSR4 preference, verified inactive input with restored controller
indices, and left the user-owned game running in Dragonsreach. Complete
source frames, hashes, per-frame acquisition data, classifications and
restoration are retained in `live-close-state/validation-result.json`.
The verdict remains **partial, not an artifact-free visual pass**.
Native CPU/GPU performance neutrality remains unmeasured.

### Diagnostic repeats and provisional closure

Five subsequent DLSS K Quality local-map closes used the same compiled
source and settings, with opt-in DLSS dispatch tracing. Each wrote all 32
stereo samples with zero failures or drops. Both eyes of all 160 images
were reviewed and all original artifact sizes and SHA-256 values verified.
No retained map panels or sky/geometry flash appears in these images.

| Native sequence                        | First world sample / frame | History reset frame, both eyes |
| -------------------------------------- | -------------------------- | ------------------------------ |
| `27ce49a8-6e95-3453-3d55-092548be2943` | 5 / 63685                  | 63683                          |
| `0aedda81-b4bf-ddb0-2fed-e176bf962ea4` | 5 / 66784                  | 66781                          |
| `6835b5fe-4475-9178-2d6e-ffe919924a2c` | 4 / 70855                  | 70853                          |
| `425f1516-1042-6492-b50d-5c29450f1e9b` | 5 / 74389                  | 74387                          |
| `95e1a030-778c-8cb0-061b-7ea80a83d1a1` | 5 / 81534                  | 81533                          |

Every trace summary reports zero evaluation and duplicate-constants
failures. Each retained close window includes a successful history reset
for both eyes at the transition back to the Dragonsreach camera, with a
near plane of 13. The 256-record rings overwrote older records before the
close; the preserved summaries identify that loss. These successful closes
do not explain the earlier failing image, which has no matching dispatch
trace. No additional production change separates the affected and clean
follow-up runs. Tracing can perturb timing, and sampled images cannot rule
out a flash between acquisitions.

The user accepted treating the fix as provisionally successful if the
latest repeats no longer reproduced the issue. This is a provisional local
acceptance, not proof that the remaining intermittent flash is corrected.
`live-close-state/diagnosis-result.json` retains classifications, exact
frame identities, complete trace-envelope paths and hashes, all counters,
limitations and final restoration. Operation 5 completed successfully at
state revision 11, restoring the original DLSS K Native AA, Render Scale
off and FSR4 preference. The owned trace and synthetic input are inactive,
controller indices restored, no keyboard keys held, and only HUD Menu is
open. The game remains running in Dragonsreach; no save was written.

## Adversarial review

-   The initial draft added a second menu-context query while retaining a
    layer. The reviewed implementation reuses one result. A query-budget
    test rejects repeated scans in open-menu, close, and world paths.
-   Close-frame recapture, context ending without an invalidation notification, reopening, and
    replacing the map with another menu retire or replace the correct layer.
-   A close between eye submissions preserves the first eye's decision and
    defers retirement until the next frame. A resolution-plan change still
    invalidates incompatible content and invokes the existing fail-open
    behavior even between eyes.
-   Seven failed-capture cases retain an open menu's last complete layer,
    then discard it after closing: incomplete rendering, an open draw scope,
    unequal operation counts, missing map capture, missing SRV, missing
    texture, and a mismatched plan generation.
-   Desktop retention is cleared with the layer. CSX menu opening keeps its
    existing invalidation. Non-VR state does not acquire VR tail ownership.
-   Presentation and capture reuse one map-context predicate. Its event
    state uses one atomic load and avoids polling the live MapMenu in the
    steady path. It introduces no resources, allocation, locking, shader
    change, settings surface, or event-thread graphics work. The existing
    invalidation and stereo policy remain authoritative.

The final review rechecked the complete production diff against
`df9f377a53d237518c1b671b7be1085c9a65b69d`, including initialization,
load reset, menu-event publication, render-thread consumption, stereo
deferral, capture eligibility, plan changes and failed seals. No additional
production defect was identified. The existing shared context predicate,
layer invalidation and stereo policy avoid a second implementation of
those rules. D3D resource lifetime, shader code and runtime settings are
unchanged. The non-VR query retains event-open-or-cache semantics; the
non-loading presentation helper's callers are VR-only.

The review found validation gaps and closed them using extracted production
code. The harness now exercises all 18 initial-publication combinations:
unknown/open/closed state, open/closed UI sample, and no event/open/close
event during that sample. It covers absent cached state on VR and non-VR,
checks the non-loading context after a stale-cache close, and verifies all
five forms of unfinished work both inside and outside the presentation
tail. A delivered context change still poisons unfinished work inside the
tail. A compile-time assertion verifies the byte-sized event atomic is
lock-free on the test target.

The expanded `/O2 /W4 /WX` harness and `/O1 /fsanitize=address /Zi` harness
pass; all three focused Release CTests also pass. Replacing the initial
compare-exchange with a plain store fails with
`initial UI sample replaced an authoritative map event`. Bypassing the
tail guard fails the unfinished-work assertion. The first candidate's
context query still fails the stale-cache-close regression. Exact results
and source/test hashes are in `final-review-validation.json`,
`final-review-mutations.json` and `final-review-ctest.xml`.

The context-ended test is not proof of recovery from a lost MapMenu event:
an authoritative open event remains open until a close or load reset, as
the prior event-based path did. The final review introduces only test and
documentation changes. Production source is identical to the tested
`058c0ac312090029ff8ff6ab085771d925fdfff2` package, so the existing offline
cost measurements below still apply. They do not prove native frame-time
neutrality.

The `main-vr-nr` integration retains its existing neural-menu query epoch
increment in the extracted context-change callback. Its standalone harness
supplies that counter alongside the other mocked runtime globals. This is
a test-only adaptation; neural rendering and the production fix are
unchanged by the integration.

## Offline performance check

For the first candidate, the same controller harness compiled extracted baseline and reviewed
production functions with MSVC `/O2 /W4 /WX`, substituting engine queries
and texture objects. Eight alternating-order rounds measured three steady
paths, ten million calls per executable and case. All 48 measured loops
allocated zero times. These are controller costs, not game frame times.

| Path                            | Baseline median cycles/call (range) | Reviewed median cycles/call (range) |
| ------------------------------- | ----------------------------------- | ----------------------------------- |
| World, no retained layer        | 15.31 (15.05-15.48)                 | 15.25 (15.09-15.31)                 |
| Open menu, retained layer       | 15.08 (14.87-15.30)                 | 14.88 (14.77-16.29)                 |
| Stereo decision already latched | 3.64 (3.61-3.66)                    | 3.69 (3.51-3.88)                    |

The ranges overlap. The small latched-path median increase is within the
observed variation; no material controller regression was established.
The expensive menu-query count is unchanged, no GPU operation is added,
and closed-menu compositing ends earlier. Native CPU/GPU frame-time
neutrality still requires the candidate in-game comparison. These numbers
precede the authoritative close-state follow-up and are not measurements
of that follow-up.

The follow-up skips context queries for an already latched stereo decision
and for frames with neither a committed layer nor unsealed menu work.
Unsealed transactions still validate context, including recognized-only
and presentation-started work. Plan changes still invalidate between eyes.
The map-context query itself has one atomic load and no live UI scan.

Eight alternating-order rounds against the first candidate's extracted
context query and frame-entry function measured ten million calls per case.
The open-map case retains an actual harness layer; all 48 loops allocate
zero times. `close-state-performance.json` preserves the individual results.

| Path                            | First candidate median cycles/call (range) | Follow-up median cycles/call (range) |
| ------------------------------- | ------------------------------------------ | ------------------------------------ |
| World, no retained layer        | 18.30 (15.35-19.11)                        | 14.99 (14.75-15.71)                  |
| Open map, retained layer        | 14.92 (14.78-15.63)                        | 15.32 (15.19-16.22)                  |
| Stereo decision already latched | 4.02 (3.62-4.59)                           | 3.15 (3.07-4.29)                     |

The open-map median increases by 0.40 cycles and the ranges overlap. World
and latched paths improve in this controller harness. No D3D operation or
allocation is added. These measurements do not establish native CPU/GPU
frame-time neutrality; that comparison remains outstanding.

## Validation and remaining limits

-   The expanded lifetime test fails against extracted baseline source with
    `closed map layer survived in the presentation tail`.
-   The reviewed test passes with MSVC `/O2 /W4 /WX` and separately with
    AddressSanitizer (`/fsanitize=address /Zi`).
-   Release controller tests: `VRMenuLayerLifetime`, `VRVendorRelatchPolicy`,
    and `VRMenuPointer` passed with
    `ctest --test-dir ../vmap -C Release -R '^(VRMenuLayerLifetime|VRVendorRelatchPolicy|VRMenuPointer)$' --output-on-failure`.
-   Universal SE/AE/VR Release DLL compilation with DevBench support passed.
    Reviewed build log: `reviewed-build.log`; CTest result:
    `reviewed-ctest.xml` in the evidence directory.
-   Scoped whitespace, line-ending, clang-format and Markdown checks passed.
    New CMake files were checked directly with gersemi. Root CMake formatting
    would rewrite unrelated legacy content, so that rewrite was reverted and
    its gersemi hook excluded. The root change is one test include line.
-   The baseline runtime reproduces local-map residue in both eyes. Four
    candidate local-map runs did not retain panels over the world. Two
    sampled distorted transition images prevent an artifact-free pass.
-   The follow-up has no sustained panel residue in eight qualified DLSS
    local-map closes. One of the first three contains a geometry flash;
    five subsequent traced closes are clean. Provisional acceptance does
    not erase the earlier flash or qualify native performance.
-   Separate world-map mode, FSR, the specific Centered Messages Box VR mesh,
    and native SE/AE behavior have not been qualified by this capture.
-   The full render-scale PR qualification has not run. This focused local
    reproduction is not a `csx-render-scale-pr-v1` pass or release approval.

Local raw evidence is retained under `build/vr-map-menu-tests` in the main
checkout: `live-baseline`, `live-candidate` and `live-close-state` contain DevBench
envelopes, source frames, manifests, and derived contact sheets;
`baseline` and `reviewed`
contain extracted functions and executables; `bench.cpp` and
`performance.json` preserve the performance harness and all individual
results. The isolated worktree is `build/vr-map-menu-src` and its CMake
build is `build/vmap`. Raw evidence is not versioned.
