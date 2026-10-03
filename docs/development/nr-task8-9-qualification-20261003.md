# Task 8 live qualification and Task 9 cost admission

## Outcome

The Task 8 DLL successfully executed four independent regions per eye in
full-resolution A and foveated B, including Managed, Preserve source and
Original colour modes. Four regions per eye is an accepted **tested capacity**,
not an accepted performance default. The production/default limit remains two.

**Task 8 is not closed in its entirety.** The available reduced-resolution C
view selected one, two or three regions per eye; it did not execute four.
Current-source GPU bounds were pending, so these live planner observations
exercise geometry fallback. They do not qualify the GPU-component planner's
four-region live path, an asymmetric empty eye, or moving stereo image quality.
The prior offline C/mono/count, crossing, capacity and failure tests remain
applicable; they are not relabelled as live-game evidence.

Task 9 now has a maintained offline final-plan bracket reporter and a complete
matched local cost observation. **Task 9 remains open:** no calibrated runtime
cost selector, held-out prediction result, transition-cost profile or resident
memory constraint has been qualified. No production profile is installed.

## Producer and scene

-   Branch source at session start: `bf17955d39b4afac640c22ecbfd9796550dfac27`.
-   Runtime Build ID:
    `587f560a843bdd9bbf629a565cef5087a28fd1daa674f4c166f9b80903546c23`.
-   Compiled base: `6b09366539ed50c345f48013a097043968ee62d7`;
    dirty digest: `04ea3da857f9f688c0411761d5f154064dc451edb380435679d573f71755cc42`.
-   Physical DLL: 31,620,608 bytes; SHA-256:
    `843e09074977cfcd165638bbb02dfe296c8e6e4ff0a857e9ac3914b0e69dba0b`.
-   Enabled mod:
    `CSX_AIO-main-vr-nr-NR-Task8-DevBench-20261003-587f560a843b`.
    Its adjacent manifest, AIO receipt and runtime producer agree. It is the
    only enabled loose DLL provider; Overwrite and unmanaged Data have none.
-   Skyrim PID 66816, started `2026-10-03T05:21:30.2521033Z`; Bannered Mare;
    SteamVR null HMD, 1512×1680 per eye, DLSS input 1008×1120 per eye.
-   RTX 5070 Ti Laptop GPU; installed NVIDIA driver `32.0.16.1088`.
    DevBench on, Tracy off; shared NR source transport off.
-   Existing user-launched MO2 profile:
    `Codex Task - 20260919t055629z-tracy-guardian-main-vr-1bf5803a`.
    No profile/package deployment, save loading or save writing occurred.

Evidence is retained locally under
`build/validation/nr-task8-9-live-20261003/`. `physical-identity.json`,
the guarded controller receipts and screenshot producer records preserve
the executable/source identity. The earlier archive and producer details
remain in [the Task 7/8 record](nr-task7-8-qualification-20261003.md).

## Live checks

The session ended with **184,732 successful native evaluations**, zero native
failures, stereo failures, device removals, quarantines or reset failures.
There were 44 resource rebuilds and seven runtime initializations across
intentional transitions. The Windows System event check covers process start
through exit: no `nvlddmkm` events or Display warnings/errors; one informational
Auto HDR notification is retained.

`live-audit.json` retains 800 attributed stereo acquisitions. All frozen
structural records passed producer, successful-call, slot and disjoint owned
output checks. **672** also have exact finalized delayed companions. Four
64-frame sequences exceeded the documented 32-key companion-retention limit:
their remaining **128** records explicitly report
`capture_retention_capacity_exhausted`. They are excluded from complete cost
admission, preserved unchanged, and followed by new bounded 32-frame runs.
Thirteen bounded profiler captures completed **3,900 frames** in total
(300 frames per window); see the individual capture receipts.

Checks included:

-   A/B four independent contexts per eye, eight successful native calls and
    separate physical history banks in the same frame. Stable A/B windows had
    no history resets after warm-up. Colour mode changes retained eight calls.
-   A→B→C and C→A retirement/entry; C retains its intentional per-evaluation
    NR reset policy while DLSS owns temporal reconstruction.
-   Two/four/two and four/one/four limit changes. A dense/overlapping C view
    correctly retained a complete enclosing context instead of forcing count.
-   Camera changes produced C counts of three, two and one per eye. Range
    exclusion produced verified zero work in both eyes: evaluation, copy,
    initialization and rebuild counters stayed unchanged across advancing frames.
-   Re-entry, category/range changes, all-category selection, GPU support-mask
    dispatch toggle, master Off/On and resumed AI/population observations.

The timing fixture used a fixed elevated camera, paused AI, five selected
face/hair actors, skin off, adaptive selection on, zero semantic margin and
feather, and the experimental savings gate off. These settings were identical
inside each bracket except the region limit. This is a controlled capacity
fixture, not a claim that every visible NPC was selected. All-category checks
were separate. Atlas sequences are attribution thumbnails, not full-quality
temporal image evidence; native eye PNGs are also preserved. No blinded HMD
quality verdict is claimed.

## Task 9 matched cost result

`cost-bracket-final/report.json` contains three new 32-frame exact joined
windows, each inside its own 300-frame profiler capture. All samples are
unique, and the initial final native plan is restored in the trailing baseline.
CPU, D3D11 and D3D12 clocks are never added together.

| Observation                 | Two/eye before | Four/eye | Two/eye after |
| --------------------------- | -------------: | -------: | ------------: |
| Actual stereo native calls  |              4 |        8 |             4 |
| Total evaluated pixels      |      1,556,480 |  933,888 |     1,556,480 |
| Native D3D12 mean, ms       |        23.3842 |  35.0340 |       23.4004 |
| Native median, ms           |        23.5750 |  34.2975 |       23.0595 |
| Native observed minimum, ms |        20.4550 |  28.6030 |       20.8670 |
| Native sample p95, ms       |        26.2180 |  43.7100 |       26.7560 |
| Inclusive NR D3D11 mean, ms |        24.1515 |  35.8493 |       24.1374 |
| Inclusive NR D3D11 p95, ms  |        26.9595 |  44.4457 |       27.5130 |

The four-region case evaluates **40% fewer pixels**, but costs about **49.8%
more native time** and **48.5% more inclusive NR time**. The NR-pass delta is
11.7048 ms, already greater than an entire 90 Hz frame budget of 11.1111 ms.
These are instrumented NR scopes, not total application frame times. Baseline
drift is 0.0694% native and 0.0583% inclusive NR. The observed minimum is a
sample floor, not a portable hardware lower bound. Serial dependence is not
estimated; the report's intervals remain descriptive.

This supports keeping fewer calls for this fixture and confirms Task 2's
central finding: reduced mask/context area does not imply proportional cost.
It does not supply a general linear area model, a universal halo, or an
automatic production profile. Camera/settings equality does not prove equal
input pixels; animations and shading still change. Held-out scenes, exact
backend/driver/runtime calibration, whole-frame CPU critical-path evidence,
native residency and creation/transition costs remain unqualified.

## Maintained tooling and review

[`cost_report.py`](../../tools/nr-color/cost_report.py) consumes three completed
sequence manifests in A/B/A order:

```text
python tools/nr-color/cost_report.py before/sequence.json candidate/sequence.json after/sequence.json --output new-report-directory
```

It reuses the existing immutable execution join and statistics helpers. It
preserves source manifests byte-for-byte with hashes, unique observations,
unavailable durations, actual shapes, allocation/guide grids, reset state,
copy work and final inference/ownership rectangles. It rejects incomplete
windows, repeated source transactions, nonchronological brackets, changed
producer/camera/settings, unrestored baseline geometry and baseline drift
above the explicit 5% diagnostic limit. It never installs a runtime profile.
Final native keys exclude semantic support movement that stays inside an
unchanged provider footprint; raw manifests retain that movement.

Adversarial review corrected handling of profiler `ready` versus native
`complete` states, partial delayed-companion capacity, republished duplicate
transactions, and semantic-support versus native-geometry identity. Tests
exercise those failures, unavailable-versus-zero timing, wrong joins, capacity
and reset changes, baseline drift and false promotion. No new runtime resource,
shader, per-frame work, preset default or DevBench mutation was added.

Validation: CMake `ALL` configuration passed; registered
`NeuralFinalPlanCostReport` (15 Python cases) and `NeuralReplayReport`
(26 cases) passed. Existing `character_multi_roi`, `character_mask_roi`
and `neural_region_capacity` controller tests passed. The execution-join suite
passed 37 cases. Scoped pre-commit and whitespace checks are recorded with
the commit. No new DLL build was required for Python/report-only changes.

## Restoration and continuation

Original NR/FOV/colour configuration fingerprint
`5d35db3364a6062e1e9cd0d3819c14de` was restored exactly. Explicit intensity
fields first selected Custom; a separate preset-3 restoration recovered the
original preset identity and exact fingerprint. AI was restored on, the original
camera pose/free-camera state restored, profiler disabled and owned screenshot
and native captures verified inactive. Skyrim exited through console `qqq`;
exit was verified at `2026-10-03T06:07:01Z`. Exact MO2 owner 60224 closed
without force. Recovery session and access lease were released; the user's
SteamVR null runtime and caches were preserved.

The workspace journal scan initially exceeded its default 20,000-file budget;
a bounded 100,000-file inspection succeeded. Local feedback
`AUTO-20261003-053123330-0FB7E98F` records the discovery limitation. It did
not invalidate game identity or the independent bounded profiler API.

Next, reuse this DLL for a C fixture admitting four disjoint regions and a
current-ready GPU-bounds case, with asymmetric-eye and moving-scene evidence.
Then implement and qualify Task 9's cost-based runtime selector against
complete final-plan/transition/residency profiles, retaining the existing
heuristic for unknown costs. The observed two-versus-four bracket is useful
calibration evidence, not permission to invent missing costs. No replacement
AIO, installation or restart was performed in this continuation.
