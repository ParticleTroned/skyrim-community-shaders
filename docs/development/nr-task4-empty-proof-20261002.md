# Task 4: current character-empty proof

## Follow-up qualification

The [subsequent Task 4/5 record](nr-task4-5-qualification-20261002.md)
preserves the healthy C/mode-transition and asymmetric-eye results,
clean shutdown, remaining live gaps, and the early-guide correction.
The prior incident below remains historical evidence; its exact GPU
cause is not inferred from the later successful run.

Source implementation on `main-vr-nr`, based on `86af01ed7`. Compiled and
in-game acceptance are separate gates; no timing improvement is claimed.

**Current status: implementation and offline validation complete; live
acceptance remains open.** A/B empty-work checks passed, then a GPU
watchdog incident ended Skyrim during B-to-C. Neither Task 4 closure nor
a fix for that incident is claimed. The earlier checkpoints below retain
their original producer identities.

Completed early GPU category bounds previously discarded `tight.empty`
and retained a nonempty CPU fallback. Preparation now accepts a completed,
valid current-source category superset as a distinct empty proof. It uses
the existing nonblocking query/map and crop/jitter/feather mapping. Pending,
failed, stale, mismatched or malformed results retain conservative work.
The reduction is not exact final visible occupancy: depth or distance can
still remove nonempty category support afterward.

CPU selection, GPU category-superset emptiness and diagnostic forced-zero
have separate kinds. Proof belongs to the existing prepared slot identity:
source frame, capture serial, settings, generation, eye/route, crop, jitter
and immutable content serial. Logical CPU-empty captures also receive a
serial. Reuse and finalization validate that identity; disposition accepts
the matching proof without calling GPU proof CPU proof. Delayed diagnostic
mask counters never authorize a production skip. DevBench and frozen
evidence retain the kind and serial; offline joins reject replacement.

NoWork clears a previously dirty mask once, retains an already uniform-zero
mask, and skips its mask dispatch. The existing native admission gate then
avoids entering Renderer: no native initialization, feature creation,
evaluation, native transport copies or colour preparation/composition for
that eye. Discovery/category capture and prerequisites needed to determine
current visibility remain distinct costs. This does not claim that the
earlier full-resolution guide preparation is eliminated, or that GPU bounds
will always be ready. No new waits, readbacks, clocks or GPU passes are added.

Both-empty delivers ordinary source/DLSS, with no enhanced-image success.
One-empty preserves the other eye's evaluation and pair publication.
Resources remain allocated; re-entry carries a model reset until successful
evaluation, including reuse of a frozen source. When both eyes evaluate,
their reset requests are synchronized. An eye remaining empty does not reset
the other eye continuously. Success-based `PreDlssInputHistory` is unchanged.

The culling audit retains selected draw-geometry bounds, nearest selected
surface distance in existing units, independent eye projections and the
full-eye uncertain fallback. Nonfinite computed distance cannot prove an
actor outside range. Actor/head bounds remain detail-admission inputs;
adding a joint/rest-pose approximation would not tighten the authoritative
current selected-draw support. No whole-scene scan, LOS-based empty proof,
donor-eye rectangle, equipment category or new ranking policy is introduced.
Hair/skin admission retains its existing actor-bound fallback when no face
anchor is visible; equipment categories still belong to Task 11.

Regression additions exercise the production proof/disposition methods,
source/capture replacement, frozen-source re-entry, asymmetric receipts,
diagnostic separation and successful/failed reset consumption. An extracted
production dispatch fixture checks mono/stereo, both scheduling modes,
every empty-eye combination, backend admission counts and failed
finalization. Existing mask-bound fixtures cover every sampling tap with
odd crops, jitter and feathering, plus malformed tiles. These C++ fixtures
are registered for the next authorized build, not reported as executed.

Source validation receipts are retained under
`build/validation/nr-task4-20261002/`. All **11 final commands passed**:
the multi-ROI, DevBench and submit-pair CMake contracts; six prepared-selection
source/preprocessing tests; current-context and bridge-gating checks;
colour-source checks; 34 transaction-evidence tests; production fixture
extraction; preset verification; and `git diff --check`. Exact arguments,
durations and logs are in `source-checks-final.json`. Scoped pre-commit passed
all applicable hooks after formatting; YAML, Gersemi and screenshot-schema
hooks had no matching files. The initial sandbox cache write denial was
resolved by the approved run against the existing repository tool cache.

The generated presets retain revision 8 and every setting value; only their
source-contract fingerprint and corresponding report hashes changed.
The initial source-only checkpoint did not run compiled tests, build a
DLL/AIO, install, launch the game, measure native performance or push.
The next acceptance run must distinguish CPU/GPU/unknown cases, verify zero
native init/create/evaluate for NoWork, and exercise entry/disappearance,
range changes, frozen sources and asymmetric eyes in VR A/B/C and mono A/C.

The subsequent explicit AIO request authorizes compiled validation and a
DevBench-enabled test archive. Initial compilation exposed an include-order
error in the new dispatch fixture; its eye-mask helper now precedes the
extracted dispatch body. The build, test and archive receipts are retained
under `build/validation/nr-task4-aio-20261002/`, including the initial failed
attempt. The corrected universal Release DLL and all **218/218 tests**
passed, with no missing, disabled or skipped tests. Preset regression,
generated-preset verification, source stability and canonical DLL manifest
verification also passed in `validation-final/summary.json`.

The producer is `86af01ed7551d68de9e40ea27bd0576b87a665b2` plus the
uncommitted implementation and fixture correction, with Build ID
`95c9d742f7447fe5e3f5ade4b9d87b33eb116561018c01b1ad49740617b8ac0c`.
The source snapshot, patch and new task files are retained with the build
evidence; these validation notes were updated after that producer snapshot.
DevBench is ON, Tracy and automatic deployment are OFF, and the pinned NR
runtime is included. No prebuilt shader cache is included.

Archive:
`dist/CSX_AIO-main-vr-nr-Task4-DevBench-20261002-95c9d742f744.7z`.
Its adjacent `.receipt.json` and `.7z.sha256` record 199,035,992 bytes,
successful archive integrity testing and all 385 extracted files matching
staging by size and SHA-256. The packaged DLL, PDB and manifest match the
producer. The previous AIO staging directory is preserved under the evidence
root. Installation and in-game acceptance remain pending; no commit or push
was made.

## October 2 live acceptance attempt

The physical installed DLL, adjacent manifest and AIO receipt matched
Build ID `95c9d742f744` above: 31,209,984 bytes, SHA-256
`06d4d4dedf4d502fb2d27c0dbcb5ee17a03ae108de713eb019850b276a5c0846`.
The exact enabled provider was
`CSX_AIO-main-vr-nr-Task4-DevBench-20261002-95c9d742f744`, with no DLL
replacement in Overwrite or unmanaged Data. PID 66896 started at
`2026-10-02T14:22:07.2600347Z` and reported the same producer. All controls
used the bundled DevBench controller, pinned identity and no automatic
mutation retries.

The Bannered Mare view showed three nearby NPCs; status admitted four or
five actors. Owned freecam held position; AI was not changed. This was not
a frozen-input quality/performance comparison. Inputs were 1008x1120 and
native outputs 1512x1680 per eye. Multi-ROI/current-context stayed off.
A used full resolution; B used foveated mode with FOV enabled and centre
scale 0.95. Frame evidence was enabled, but the bounded profiler had not
yet been enabled when the game exited.

### Empty work and re-entry

Cold A began with all three categories disabled. At frame 22026 both eyes
had current CPU-selection proofs and empty-bypass receipts. Native
runtime/interop initializations, resource rebuilds, attempts, evaluations,
depth/control copies and output commits were all zero. All eight counters
also stayed unchanged throughout each of these advancing-frame windows:

| Case                        | Proof           | Start frame | End frame | Native evaluation count throughout |
| --------------------------- | --------------- | ----------: | --------: | ---------------------------------: |
| A, range reduced to 0.1 m   | CPU selection   |       24289 |     24522 |                               1754 |
| A, diagnostic forced zero   | Diagnostic zero |       24931 |     25166 |                               1984 |
| A, camera turned to ceiling | CPU selection   |       30697 |     31315 |                              11790 |
| B, range reduced to 0.1 m   | CPU selection   |       33756 |     34002 |                              15970 |
| B, diagnostic forced zero   | Diagnostic zero |       34687 |     34938 |                              16196 |

Returning to authored selection, the original view or 30 m resumed both
eyes. Resource rebuilds stayed at two. Re-entry frames 24735, 25373,
31588, 34474 and 35152 recorded 1896/2122/11940/16096/16324 evaluations
and 4/6/10/16/18 caller history resets respectively. These demonstrate
resource retention and reset/resumption, not temporal image quality.

Four terminal screenshot receipts retain native left/right images and a
preview; all 12 files match receipt hashes and sizes. The offline importer
joins all four acquisitions to their finalized companions. Visible frames
26389 (A) and 33510 (B) report successful work in both eyes. Empty frames
30887 (A, ceiling) and 34160 (B, range) report NoWork, zero renderer
executions, zero mask dispatch/clear pixels and successful ordinary DLSS
in both eyes. Inspection confirms continued ordinary scene delivery,
without claiming matched stereo fidelity qualification.

Empty native work does not imply free discovery. B's range-empty source
still captured 1,058,868 category pixels, copied 8,470,944 logical bytes
and dispatched 143,360 early-bounds threads. Observed detection/capture
CPU times were 0.0115/0.0326 ms for that single source, not stable averages
or independent per-eye costs. A's ceiling source avoided category capture
and bounds dispatch, but still entered `full_resolution_guide_preparation`.
GPU time was unavailable (`profiler_disabled`), not measured zero. This
earlier guide boundary needs qualification for avoidable empty work.
Native transport skipping is proven; complete application-stage elimination
and performance neutrality are not.

### Terminal failure and cleanup

At 16:42:13 local time, B-to-C configuration reported successful backend
retirement and insertion-point transition. The log then reports runtime
initialization, slot 2 creation at 1008x1120 and an NGX evaluate return at
16:42:14.991. An accepted API return does not prove GPU completion. The
next status timed out. Windows recorded NVIDIA event 153 at 16:42:17,
a current LiveKernelEvent 141 watchdog dump and Skyrim/VR-compositor
fast-fail reports. Skyrim exited before another NR snapshot or shutdown
command. Root cause remains unresolved; this is not classified as only a
collector error or attributed to Task 4 without causal evidence.

Preserved pre-incident counters show zero NR failures, device removals,
quarantines and stereo failures; they do not override the terminal fault.
An earlier transient Final-LDR blend warning at frame 32456 during the FOV
change preserved normal DLSS and remains in the evidence.

The game did **not** shut down cleanly. No forced termination, relaunch,
DLL installation or settings-save command was issued. Live restoration
was impossible after exit. All owned screenshots completed and no bounded
profiler capture was started. Normal exact-MO2 recovery closed MO2,
completed RootBuilder cleanup and released the task access lease. Recovery
session: `20261002T145517Z-nr-task4-crash-cleanup-c891604c`.

Evidence: `build/validation/nr-task4-live-20261002/`, including controller
journals, initial settings, installed identity, screenshots, `live-audit.json`,
game log and Windows application/system/driver records. `audit-live.py`
verifies counter windows and transaction joins; screenshot hash verification
has its own receipt. Camera/FOV response-classification ambiguity was
recorded locally as `AUTO-20261002-144335790-1065B838`. Applied mutations
were read back rather than blindly retried. This toolkit issue remains
separate from the GPU fault.

## Adversarial review and final offline validation

-   **Scope:** retained selected-draw distance, unit conversion, per-eye
    projection and uncertain full-eye fallback. No whole-scene scan, LOS
    empty proof, joint-box coverage assumption, donor-eye coordinates,
    ranking or equipment category was added. Current geometry support stays
    authoritative; equipment expansion remains Task 11. Intentional
    full-scene dispatch is structurally unchanged.
-   **Correctness:** reviewed proof creation, capture/policy binding, reuse,
    finalization, receipts and native admission. Extracted production
    fixtures cover stale/missing proofs, diagnostic separation, mono/stereo
    empty combinations, both scheduling modes, asymmetric receipts and
    failed/successful reset consumption. Mapping fixtures exercise jitter,
    odd crops and feather neighborhoods. They do not replace live coverage.
-   **Robustness:** corrected a contradictory API description. The offline
    importer now rejects non-boolean preparation states and attached source
    capture serial/frame/epoch mismatches. Two regression methods bring its
    suite to 36 passing tests. Legacy records and explicitly unavailable
    source evidence remain supported. No speculative driver workaround was
    added without causal evidence.
-   **DRY/cost:** reused existing mask mapping, ROI planning, native admission,
    identity comparators and reset paths. Proof predicates/names are
    centralized. No new GPU pass, readback, wait or production timing
    instrumentation was added. Post-live review fixes affect the DevBench
    descriptor and offline tools/tests only.

Final command:

```powershell
pwsh -NoProfile -File tools/validate-local.ps1 -OutputDirectory build/validation/nr-task4-offline-final-20261002
```

The universal Release DLL and **218/218 tests passed**, with no missing,
disabled, skipped or failed tests. Coverage includes production/DevBench
prepared-selection fixtures, dispatch/culling/history/controller checks
and the shader suite. Preset regression/check, source stability and DLL
manifest verification passed. The initial attempt failed at vcpkg cache
permissions; its receipt remains in `nr-task4-offline-20261002`. The
approved cache-access retry passed.

Final offline producer: `86af01ed7` plus the reviewed diff, Build ID
`74daaafeeb67eb667994e2728abe95fd4f0e7ad520723d84fa09de989106d036`,
dirty digest
`befcff84b4d8739d70210933c6b5e8c383ee66819baa50b2314f36fe627c11f0`.
It was not installed or run in Skyrim. These final reporting paragraphs
postdate its snapshot; the live producer remains `95c9d742f744`.

### Remaining acceptance

1. Isolate fresh-process C from B-to-C with the installed producer, then
   compare capture off/on after establishing a safe baseline. Resolve the
   watchdog before calling transitions qualified.
2. Complete live C empty/re-entry, frozen-source, completed GPU-empty,
   asymmetric-eye, occlusion and intentional full-scene control cases.
   Pending GPU bounds remain conservative: the final B sample had 16,317
   pending fallbacks, four ready readbacks and seven uses, without proving
   GPU-empty bypass. Compiled mono A/C coverage is not SE/AE live evidence.
3. Obtain bounded application/discovery timings and qualify the earlier
   full-resolution guide stage, restore settings and exit normally.

Restart authorization was requested because the user reserves restarts
for explicit direction. Offline passing tests do not close these live gaps.
