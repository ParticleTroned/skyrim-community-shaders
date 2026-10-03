# Task 7 transport qualification and Task 8 capacity — 2026-10-03

## Decision and remaining acceptance

Task 7's same-session transport comparison is complete. Shared transport
preserves the tested native outputs and reduces retained logical transport
texture bytes by 37.5%. It does not establish a performance improvement:
live NR pass means are almost unchanged and several offline shared cases
are slower. Sharing remains a default-off DevBench experiment. This is a
bounded correctness/memory result, not production promotion or proof of
lower total GPU frame time or native residency. Those acceptance criteria
remain unqualified; the measurement campaign is not a reason to enable it.

Task 8 generalizes both planners and execution to adaptive 1–4 regions,
with eight as a DevBench ceiling. Offline native and controller coverage
is available. **Task 8 remains open for the new DLL's live qualification.**
The installed Task 7 DLL could not exercise the newly added region limit.
No installation or relaunch was performed. Task 6's unmatched prior-build
performance comparison is not silently closed by these measurements.

## Installed producer and live evidence

-   Build ID: `a1dc0b067333b00b77048bf7d3c3e608371a93599f47d5b60bfdfa0fecaaaeba`.
-   Compiled base: `4d3e54856d83c3b7df7128b780a0ed545d67c769`;
    dirty digest: `75fc0ee0a4b1dd70fc6d22020c204f7b4ff6944020d4111291835464c25b6d7d`.
-   DLL: 31,269,888 bytes; SHA-256
    `3ebedbfe71c5fc17c1128eedd096d981f105815537f6e235ce0e9b41b8576d1c`.
-   Sole enabled loose provider:
    `CSX_AIO-main-vr-nr-NR-Task7-DevBench-20261002-a1dc0b067333`.
    Physical DLL, adjacent manifest, AIO receipt and runtime producer matched.
    Overwrite and unmanaged Data had no competing provider.
-   Skyrim PID 8504, started `2026-10-02T22:42:05.1826928Z`; RTX 5070 Ti
    Laptop GPU; NR 310.8 SHA-256
    `8270b350cd82de5ce89806872cdd6b6a9249b80836b91bbeb3573470744cc206`.

Raw evidence is local in `build/validation/nr-task7-8-live-20261003/`.
`audit.py` verifies ten complete 300-frame profiler windows, 19 complete
16-frame sequences, every committed image size/hash, and all **304/304**
source/execution joins. `audited-summary.json` retains timers, counters,
resource identities and 21 successful final offline process records.

The Bannered Mare scene contained five eligible actors. AI was paused and
the camera held for comparisons. The controlled C workload evaluated
`[0,320,960,800]` and `[448,64,192,256]` per eye: four native calls and
1,634,304 pixels total. The enclosing rectangle is not evaluated area.
Private/shared plans, counts and output ownership matched in each window.
FOV 0.95 was a temporary test setting, not a new default.

## Same-session transport comparison

| Retained logical texture bytes                     |    Private |     Shared |
| -------------------------------------------------- | ---------: | ---------: |
| Prepared colour/depth/motion inputs                | 54,190,080 | 27,095,040 |
| Private native outputs                             | 18,063,360 | 18,063,360 |
| Transport total                                    | 72,253,440 | 45,158,400 |
| Private colour baseline/results, legacy            |          0 |          0 |
| Private colour baseline/results, preserve/lighting | 13,565,952 | 13,565,952 |

The transport reduction is 27,095,040 bytes (37.5%). Private colour storage
is additional and unchanged. Four outputs and four histories remain
independent; source identities reduce from four to two. Steady windows had
zero rebuilds, cached-only bytes and lease-only retained bytes. Native
allocation bytes remain unknown, and physical driver residency was not
measured. Sharing does not eliminate required copies of disjoint contexts
or reduce the evaluated rectangles/calls.

The table below reports NR stereo pass GPU means, each over 300 resolved
frames. These are pass timings, not whole-game GPU frame times. Overlapping
NR/reconstruction scopes must not be summed.

| Colour mode     | Private before, ms |            Shared, ms | Private after, ms |
| --------------- | -----------------: | --------------------: | ----------------: |
| Legacy raw      |          24.822834 | 24.857660 / 24.879004 |         24.933434 |
| Preserve source |          25.327063 |             25.323082 |         25.378708 |
| Neural lighting |          25.325613 |             25.407309 |         25.297585 |

The experiment does not meet an improvement claim. Character-mask passes
were about 0.147–0.150 ms, composition 0.081–0.089 ms and colour
reconstruction 0.856–0.878 ms when entered. Complete individual timers and
CPU enqueue/native counters remain in the audited summary. Surrounding
status intervals have different denominators from the 300-frame windows.

## Live functional results and limitations

-   Shared transport retained four calls and resource-rebuild count 32
    through camera offsets 2, 4, 6 and return to zero.
-   Range 0.1 admitted current empty proof and no native work. Re-entry
    resumed four calls without resource rebuild. Face-only/all-category
    checks and shared A→B→C transitions passed.
-   Legacy, preserve-source and neural-lighting colour modes passed.
    An attempted nonidentity profile on C's RGBA8 processing path was
    rejected by its existing float-resource validation guard. It produced
    **6,814 validation/stereo failures**, with zero native evaluation,
    command, retirement or reset failures. Restoring identity resumed work.
    This does not qualify nonidentity transforms on RGBA8.
-   Final counters: **120,438 successful native evaluations and commits**,
    11 successful resets, zero failed resets, device removals and quarantines.
    The validation failures above remain part of the result.
-   A console-paused sequence stopped at 1/16 with `paused_during_burst`.
    It is not a successful frozen-source test. Same-source and delayed
    consumption coverage remains the compiled policy/queue tests.
-   A fresh native-input capture was refused because the session was not
    in Developer Mode. Offline work uses the preserved Task 2 captures,
    not an unattributed replacement from this scene.
-   Native eye images and previews were retained. Initial/shared C preview
    inspection found no obvious gross seam or brightness defect. This is
    visual triage, not blinded colour, alpha or temporal quality acceptance.

`finish.py` asserted restoration of original NR/character/FOV/colour,
experiment, profiler, AI and camera settings and HUD-only menu state.
`qqq` closed the exact Skyrim process normally. MO2 PID 15040 closed via
the owned recovery controller's File→Exit flow, without force. Recovery
ownership and access `access-20261002T224631Z-b16ad631b565` were released;
the retained profile and pre-existing SteamVR processes were preserved.

## Offline native qualification

Game absence was checked before every native process. Input C is the
complete four-frame Task 2 capture at 1008×1120 RGBA8 per eye:
`build/validation/nr-task2-20261002/input-C/manifest.json`, SHA-256
`e15a65274b8c256c41d1cdf9675b37f5fc9edad0c0ab47445a49f2b594c626be`.
A/B inputs are `build/validation/nr-task2-20261001/input-{A,B}/manifest.json`.
Replay reports pin input/payload, runtime, adapter, executable and compiled
source hashes. The measured executable SHA-256 is
`01befc87fc3fa45ac8a6659956471e7d55306c01a0c8aeea35e3ed55c1889f22`.
Its embedded source inventory is the measurement identity, not the later
DLL's source identity.

The final `verified-*`, `mono-*` and `route-*` records contain 21 successful
processes: three warmups and eight measured samples each (168 measured
samples). Every retained sample has all actual per-call GPU timings, the
expected number of output crops, verified crop sizes/hashes, unchanged
prepared P, and clean session shutdown. Saved C private/shared outputs
match byte-for-byte for transport and duplicate four/eight cases; A/B
four-region outputs also match. The mono fixture extracts one real captured
eye and makes four/eight actual calls; it is not SE/AE gameplay evidence.

At constant 131,072 evaluated pixels per eye, C native GPU sums were:

| Regions per eye | Stereo calls | Mean native evaluation, ms |
| --------------: | -----------: | -------------------------: |
|               1 |            2 |                   4.514000 |
|               2 |            4 |                   8.738875 |
|               4 |            8 |                  19.607000 |
|               8 |           16 |                  40.208750 |

This reinforces Task 2's fixed/invocation-cost finding. It is not a
production cost model. Duplicate C four-region private/shared means were
19.400875/21.296000 ms; eight-region means were 39.998000/57.680625 ms.
A and B four-region means were respectively 18.178750/21.385875 and
19.157875/20.844000 ms. Mono four/eight private/shared means were
8.735750/8.719875 and 19.596000/21.538250 ms. These short sequential
microbenchmarks qualify completion/output, not a robust general performance
estimate. The observed slower shared cases forbid automatic promotion.

### Rejected small-rectangle stress case

The earlier `offline-12-calls-eight` test attempted eight 64×64 rectangles
per eye. Native create/evaluate calls returned success, but the first GPU
completion failed; no valid timing or output sample exists. Shutdown could
not prove idle and correctly abandoned native ownership. Four nvlddmkm
event-153 records at `2026-10-03T01:43:12+02:00` are preserved. Their message
is unavailable; the provider/driver's internal cause is unresolved.

That shape was not retried. The executable no longer offers that case,
and higher-count renderer plans with any dimension below 128 now merge to
their validated per-eye enclosure before native work. This is a conservative
exclusion of an unqualified envelope, not a claimed provider bug fix.
The replacement constant-area cases above passed with dimensions ≥128.
The first campaign also omitted output files for the new axes; it cannot
support hash-equality claims. The corrected retained-output campaign is the
only basis for the equality result above. Failed evidence is preserved.

## Task 8 implementation and adversarial review

`nr_configure` accepts session-only `experimentalRegionLimit` from 1 to 8.
It defaults to 2 and is not persisted. Four is the capacity goal; eight
remains experimental. Increasing the limit never requires filling it.
All new higher-count controls, fallback/rejection state and enlarged native,
plan, colour-measurement and GPU-query storage require the DevBench build.
Bridge-off builds retain two regions, eight physical slots and four calls.
There is no new normal UI control or default, resolution or cadence change.

Both actor and GPU-support planners use bounded candidates with complete
selected coverage, strict area savings, look-ahead and local overlap repair.
Actor lifetimes remain authoritative where known. Category pixels use
eye-local spatial tracks and explicit confidence; ambiguity resets identity.
Unchanged groups retain sparse physical history banks through population
changes. Disconnected support from one actor can split. Overflow retains
conservative coverage, and independent eye geometry remains the baseline.

Execution, masks, outcomes, serialization, exposure/colour samples and GPU
query/readback capacities use the centralized bounds. Slot 31 is checked;
slot 32 cannot wrap a shift. Actual call count is distinct from eye count.
Native input capture intentionally retains its existing full initialized
one-context-per-eye contract; execution evidence can describe all 16 calls.

Recoverable higher-count allocation/native-creation rejection uses one
enclosing context per actual eye only after existing D3D12 and D3D11-tail
retirement. Unsafe evaluation/completion/device failures do not retry.
Rejected capacity stays reduced until an explicit successful fenced reset.
Attempted count and actual fallback execution remain distinguishable.
Entry changes the history key; fallback does not add a reset every frame.

Review corrected sparse-slot reporting, the old eight-call report limit,
per-evaluation GPU timing admission, replay output retention, the creation
rejection key's call count, stale two-region tests and output-schema limits.
Small-geometry rejection is separate from pressure rejection. Resource
state transitions and retirement reuse existing helpers; no HLSL, native
aliasing, eye staggering or new fence assumption is introduced.

## Next in-game session

Install the Task 8 DevBench AIO manually, then verify its physical DLL and
runtime producer. Start with `experimentalRegionLimit: 4`, sharing off,
and retain the default savings gate unless deliberately measuring its
alternative. Obtain four actual independent regions where scene geometry
allows; fewer admitted regions is not proof of four-region execution.
Compare two/four with matched source and coverage, then test camera motion,
crossings, entry/exit, empty/asymmetric eyes, range/category changes and
A/B/C/colour transitions. Check histories, actual banks/calls, fallback
receipts, GPU faults, exact stereo images and complete costs. Eight remains
optional developer qualification, not the production target. The rejected
64×64 higher-count shape must stay excluded.

## Final validation and producer

`pwsh tools/validate-local.ps1 -OutputDirectory build/validation/nr-task8-verified-20261003`
passed in 528.56 seconds:
universal SE/AE/VR Release DLL, all **224/224** registered tests, controller
and shader groups, preset generator tests/check, whitespace and manifest
verification. No registered test was missing, disabled or skipped; source
snapshots were unchanged and matched the linked producer. The separate
`build/nr-task7-color-tests` rebuild and **21/21** colour tests passed,
including WARP reconstruction/exposure and sparse measurement batches.
Final replay-harness compilation passed; its CommonLib dependency warnings
remain in `replay-final-build.log`. It was not substituted for the measured
executable identity above or used to relabel earlier measurements.

The first full attempt passed 223/224 and exposed an obsolete two-region
source contract. Its evidence remains in `nr-task8-final-20261003`; the
contract now checks the bounded count, sparse banks and actual timing
capacity. Earlier compile errors and a Python discovery invocation with
missing CMake arguments are retained in the local logs, not counted as
passing validation. The maintained final invocations above supersede them.
Scoped pre-commit checks passed. Only preset compatibility fingerprints
changed; the new session setting is absent from persisted preset values.

The new producer is:

-   Build ID: `587f560a843bdd9bbf629a565cef5087a28fd1daa674f4c166f9b80903546c23`.
-   Compiled base: `6b09366539ed50c345f48013a097043968ee62d7`;
    dirty digest: `04ea3da857f9f688c0411761d5f154064dc451edb380435679d573f71755cc42`.
-   DLL: 31,620,608 bytes; SHA-256
    `843e09074977cfcd165638bbb02dfe296c8e6e4ff0a857e9ac3914b0e69dba0b`.
-   `DEVBENCH_BRIDGE=ON`, `TRACY_SUPPORT=OFF`, SE/AE/VR enabled and automatic
    deployment disabled. The NR runtime hash is unchanged.

The preserved compiled patch and per-file hashes in
`build/validation/nr-task8-aio-20261003/` distinguish that producer from the
final documentation/commit added after validation. Bridge-on/off controller
tests check capacity defaults; a complete bridge-off DLL and SE/AE gameplay
were not run. No performance-neutral claim is made for unmeasured new code.

The test archive is
[`CSX_AIO-main-vr-nr-NR-Task8-DevBench-20261003-587f560a843b.7z`](../../dist/CSX_AIO-main-vr-nr-NR-Task8-DevBench-20261003-587f560a843b.7z).
It contains 385 files, is 199,306,012 bytes and has SHA-256
`37d09ca7036f9228a06b38ea4ecb5c53ae35a8b311d4eb75e1f0cf47e819509b`.
Archive integrity and all **385/385** extracted file sizes/hashes passed.
DLL, manifest and PDB match the validated producer. The adjacent receipt
retains the build options, runtime hash and complete validation reference.
No personal `SettingsUser.json` or shader cache is included; existing
user caches and presets are preserved. The archive has not been deployed.
