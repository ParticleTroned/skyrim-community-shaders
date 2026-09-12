# VR render-scale iteration records

The optional runtime FSR shared-guide path and its persistent in-game
toggle are documented in [Runtime FSR shared guide inputs](fsr-shared-guides.md).
The DevBench A/B action updates the same preference; Save Settings
preserves either choice across game restarts.
It removes eligible full-eye guide staging copies while preserving the
existing interop fences, copied fallback and quarantined ownership. This
implementation has no performance claim or runtime qualification result;
new measurements must use the existing comparison ledger and reporting
workflow.

## September 11: all PR73 tables compare against PR66

The user selected measured PR66 a09e1cc77 as the reference for both
previous PR73 d9780bb74 and new PR73 c73bae9a7. All nine PR tables now
show all three builds: means/SE, each pass, health, stretch, every route,
completion/relatch frames and milliseconds, identities/packages and memory.
Both sets of deltas use PR66. Prior measurements remain separate columns.

New PR73 mean strict completion is 790.202 versus 800.231 ms for PR66
(-1.253%); full-pass stretch duration is 5138.086 versus 4549.605 ms
(+12.935%). New rows 3, 8, 16, 19, 21, 26 and 30 are slower in both
passes. All three builds retain terminal 66 PASS and Task 2 counts
66/0/0, with applicable health MET/MET. Formal change assessment remains
INCONCLUSIVE because scene/toolchain context differs, a full matching
fixture fingerprint is unavailable and no tolerance policy was specified.

Complete comparison and table values reconstruct exactly from the ledger;
143 verified same-ledger references preserve repeated JSON subtrees.
All historical cells remain unchanged and all 1,056 paired timing cells
pass. Comparison took 2.916 s and complete
ledger retention/verification took 23.705 s.
See [the corrected report](pr73-readiness-nvidia-comparison-20260911.md) for the uniform PR66 comparison
and complete validation. Prior comparisons remain historical evidence.

## September 11: PR73 previous/new measurement comparison

The user requested preservation of the previous PR73 measurement and an
additional comparison with readiness source c73bae9a7. Previous source
d9780bb74 and new source c73bae9a7 share main-VR base ef7c366dd.
Mean strict completion changed 870.905 -> 790.202 ms
(-9.267%); both pass means are lower. Full-pass
stretch duration changed 5744.173 -> 5138.086 ms
(-10.551%). Both runs retain terminal
66 PASS, Task 2 counts 66/0/0 and applicable health MET/MET.
Scene matching and a tolerance policy remain unavailable; the formal
assessment and separate memory classification remain INCONCLUSIVE.

Every previous ledger cell is preserved. Additional detail rows retain
the entire comparison and three-build/per-route statistics with exact
reconstruction; all 1,056 paired timing cells pass. Previous PR66/PR73
tables remain intact. See [the full comparison](pr73-readiness-nvidia-comparison-20260911.md)
for both passes, route regressions, stretch maxima, retry reasons,
memory predicates, complete provenance and assessment limits.

## September 11: PR73 readiness c73bae9a7 NVIDIA tuning

Run `renderscale-tuning-nvidia-2026-09-11T05-18-34-649Z` completed 33 + 33
transitions, terminal 66 PASS; Task 2 counts 66 PASS / 0 FAIL /
0 INCONCLUSIVE. Both passes meet applicable health checks with no counted
failure observations. Physical DLL/manifest/AIO verification matches clean
Release source c73bae9a7, main-VR base ef7c366dd and runtime Build ID
d098079db31d. Captures are inactive, journal flushed and reporting COMPLETE.

Against pinned previous measured main-VR 7c8e3e656, strict means are
779.947 / 800.456 ms (-9.938% / -3.916%).
Reference fidelity/vendor observations on rows 26 and 28 are absent.
Retry counts are 9 / 10; 31 selected stretch transitions recovered.
Full-pass stretch is 79 / 93 frames and 4675.398 / 5600.774 ms.
The fixed cutoff remains diagnostic and the proven-native terminal gate
remains a contract mismatch. Formal change assessment and memory
classification are separately INCONCLUSIVE; scene matching and a declared
tolerance policy are unavailable. Per-route regressions remain explicit.

The canonical ledger retains every summary and comparison field, verified
by exact reconstruction, with 1,056 paired numeric timing cells audited
and every historical cell preserved. Finalization took
17.791 s; comparison took
4.096 s; complete ledger reporting took
12.637 s. See the [durable report](nvidia-renderscale-tuning-pr73-c73bae9a7-20260911.md)
for all pass/transition tables, memory predicates, gate observations,
provenance, evidence links and validation receipts. Raw evidence stays local.

## September 10: PR65 baseline comparison across both repeats

The clearer [PR65 comparison](pr65-nvidia-baseline-repeat-means-20260910.md) keeps the
older renderer baseline compiled as 390fdea25 and groups the four main-VR
passes from 348803c18 and 7c8e3e656, as requested. Mean strict completion
is 834.274 ms baseline versus 799.212 ms main-VR, a 4.203% reduction.
The repeat means are 748.880 and 849.543 ms, so direction is inconsistent
between repeats. Trailing SE columns retain baseline, each repeat and
combined-pass dispersion; combined pass-based SE is 30.754 ms, while SE
across the two repeat means is 50.332 ms. Neither is a significance claim.

All 132 main-VR transitions passed at terminal, but recovered fidelity and
vendor failures recur at rows 26 and 28 in all four passes; the baseline
had none. Health remains DOES_NOT_MEET_STANDARD. This regrouping adds no
measurements, preserves the existing ledger, and audits 1,584 distinct
numeric timing cells across the three retained runs.

## September 10: PR66 source 80f83d2dd on main-VR 7c8e3e656

Run `nvidia-2026-09-10T13-36-15-291Z` completed 33 + 33
transitions in one process. All 66 terminal render checks passed; Task 2
counts are 66 PASS / 0 FAIL / 0 INCONCLUSIVE. Reporting and retry evidence
are COMPLETE; captures are verified inactive and all 442 journal records
are flushed. Physical DLL hash/size, adjacent manifest and AIO receipt
match runtime Build ID `3a3898d47e65`, clean Release source `80f83d2dd`.
Git confirms main-VR base `7c8e3e656`; no reporting bridge was backported.

Against the preceding relevant main-VR run `nvidia-20260910T124329625Z`,
strict means are 871.699 / 809.922 ms (+0.657% / -2.780%).
Relatch proof means are 826.449 / 726.564 ms and 14.040 / 12.440 frames;
strict means are 15.485 / 14.455 frames. Stretch totals are 88 / 65 frames
and 5733.872 / 4581.027 ms, with 20 / 17 episodes and no active tail.
All 33 selected stretch transitions recovered. Owned retries are 11 / 6;
viewport waits and settling/promotion intervals remain individually retained.

Recovered fidelity/vendor-fallback observations on rows 26 and 28 recur
in both passes and in the reference. Full-history assessment remains
DOES_NOT_MEET_STANDARD; device-loss, OOM, producer-terminal,
vendor-native-qualification and credible-liveness failure counts are zero.
The fixed stretch cutoff is diagnostic and the proven-native terminal
scaled-presentation gate is a contract mismatch; raw gates remain visible.
Memory classification is inconclusive. Game hour and vcpkg toolchain-file
hash differ, and there is no versioned tolerance policy. Timing deltas
are descriptive; no steady-state GPU/FPS or neutral/improvement claim follows.

The canonical ledger retains every historical cell and adds 528 numeric
timing cells plus 66 full timing/retry records. One comparison verified
1,056 paired timing cells in 3.476 s. See the
[durable report](nvidia-renderscale-tuning-pr66-80f83d2dd-20260910.md) for all
pass/transition comparisons, memory predicates, limits and evidence links.
Raw evidence stays local. PR inclusion remains the user's decision.

The ledger detail supplement adds 211 metric rows in the same run column,
covering all 66 complete transitions, both complete pass health records,
memory/resources/profiler details, evidence gaps and comparison deltas.
The finalized summary was reconstructed from ledger cells with exact field
equality. Every prior cell and column remains intact. The comparison was
reused after hash validation and all 1,056 numeric timing cells were audited;
the supplement took 2.544 seconds. See the report's complete-ledger coverage
section and validation receipt. Historical runs' new detail rows remain
explicitly unpopulated, without implying unavailable source telemetry.

## September 10: independent PR65 repeat on main-VR 7c8e3e656

Run `nvidia-20260910T124329625Z` completed all 33 + 33
transitions in one process. All 66 terminal checks passed; Task 2 counts
are 66 PASS / 0 FAIL / 0 INCONCLUSIVE. Candidate reporting and detailed
retry telemetry are COMPLETE. Captures are verified inactive; all 444
journal records were flushed. Physical DLL, manifest, AIO receipt and
package payload match runtime Build ID `9e88a559cc66`, clean Release
source/main-VR `7c8e3e656`.

Mean strict completion was 866.009 / 833.078 ms,
12.872% / 14.040% above the preceding measured main-VR
`348803c18`. Relatch proof means were 807.337 / 763.765 ms and
13.920 / 13.840 frames; strict means were 15.576 / 15.455 frames.
Stretch totaled 89 / 83 frames and 5719.962 / 5383.107 ms, with no active
tail. The imposed stretch cutoff remains diagnostic. Owned retries were
10 / 10; detailed viewport waits and guard/promotion intervals are retained.

Recovered fidelity/vendor findings on rows 26 and 28 recurred in both
passes and in the reference. Full-history assessment is
DOES_NOT_MEET_STANDARD; all five terminal failure categories are zero.
Memory classification is inconclusive. Scene time differs and there is
no versioned tolerance policy; timing deltas remain descriptive. The older
reference lacks detailed retry telemetry despite its retained COMPLETE
label, so that paired diagnostic comparison remains unavailable.

The canonical ledger preserves every historical cell and adds 528 numeric
timing cells plus all 66 full timing/retry records. One comparison verified
1,056 paired numeric cells in 2.654 s.
See the [durable report](nvidia-renderscale-tuning-7c8e3e656-20260910.md) for every
pass/transition, failure/recovery, memory result, evidence link and stage
timing. Raw evidence remains local. The user requested this comparison
as an independent repeat on PR65; previous run columns remain intact.

## September 10: NVIDIA tuning of PR66 review merge 89e396458

Run `renderscale-tuning-nvidia-2026-09-10T11-53-09-916Z` completed
33 + 33 transitions on clean Release source `89e396458`, the PR66
review merge on main-VR base `348803c18`. Physical DLL, adjacent manifest,
AIO receipt/archive and runtime Build ID `880b0ef46dd9` match. Both passes
verified capture cleanup inactive and all 444 journal records were flushed.

All 66 terminal render checks passed; per-transition Task 2 counts are
66 PASS / 0 FAIL / 0 INCONCLUSIVE. Recovered fidelity/vendor failures at
rows 26 and 28 recurred in both passes and also exist in the reference.
The full-history assessment is DOES_NOT_MEET_STANDARD. Mean strict latency
was 865.774 / 835.988 ms, 12.842% / 14.438% above measured main-VR
`348803c18`. Relatch proof means were 812.873 / 782.504 ms; strict means
were 15.515 / 15.455 frames. Allowed stretch totaled 83 / 82 frames and
5324.710 / 5291.078 ms. The imposed stretch cutoff remains diagnostic.

Memory classification is inconclusive. Reporting remains incomplete
because detailed retry telemetry is unavailable; the old baseline's
retained COMPLETE label does not satisfy the current retry contract.
Scene time differs and no versioned tolerance was supplied, so timing
deltas remain descriptive. No new affected health routes were observed.

The existing ledger preserves every old cell and adds all 528 measured
timing cells plus all 66 full timing records. The one generated comparison
audited 1,056 baseline/candidate timing cells in 2.618 seconds. Full pass
and transition tables, health gates, memory, telemetry, evidence links
and the reporting limitation are in the
[durable run report](nvidia-renderscale-tuning-pr66-89e396458-20260910.md). Raw evidence remains local; PR
inclusion is user-decided.

## September 10: fast reporting execution and verified reuse

The repository reporting wrapper now supports one invocation for candidate
finalization, validation/publication of a prepared append-only ledger update,
and comparison. It indexes ledger metrics once, reuses only content-verified
outputs, and records stage timings and limitations. The
[reporting workflow](vr-render-scale-comparison-reporting.md#fast-routine-completion)
and `AGENTS.md` require brief useful progress updates without redundant work.
All comparison detail remains required; PR inclusion stays user-decided.

On copied baseline evidence (renderer `1595cffd`, main-VR equivalent
`9074b4676`, compiled `390fdea25`), compared with measured `348803c18`, the
final offline check took 21.476 seconds for generation and 2.253 seconds for
verified reuse. An earlier invocation took 63.779/7.688 seconds; these are
observations, not latency guarantees. The full 871,216,257-byte scalar CSV
remained byte-identical, and both invocations checked all 1,056 ledger timing
cells. The existing ledger was unchanged. Older retry-detail limitations
remained explicit. Live cleanup and new DLL inspection were not replayed.

Sixteen focused tests passed, including cache invalidation, concurrent input
changes, historical-cell preservation, stale ledger rejection, missing retry
details, unflushed-worker rejection and late helper diagnostics. Evidence:
`artifacts/reporting-performance-20260910/pipeline-validation.json`.
The prepared-ledger publication path was tested with temporary fixtures;
the existing canonical ledger was audited without applying another update.

## September 10: automatic comparison and separate change assessment

Every future ledger update includes the
[detailed side-by-side reporting contract](vr-render-scale-comparison-reporting.md)
automatically. PR inclusion remains the user's decision. Execution,
terminal results, full-history health and improvement-or-neutral assessment
are independent; unmet change standards do not relabel a completed test.

The maintained offline finalizer and comparison reporter now expose all
recovered failures and raw cumulative gates. The imposed two-frame stretch
cutoff is diagnostic only, and the proven native-target presentation gate
mismatch is separate from actual health failures. The baseline has no
counted adverse switch-health findings; head's recovered failures at rows
26 and 28 mean `DOES_NOT_MEET_STANDARD`, despite lower mean milliseconds.

The side-by-side tables include relatch-proof and strict-completion frames
and milliseconds, stretch episode counts, total frames and duration, with
baseline/candidate deltas for every pass and transition. In pass 2, head
uses 508 strict-completion frames versus 494 and 77 stretch frames versus
73, while stretch time falls from 4,805.654 ms to 4,310.798 ms. Relatch proof
has 25 measured boundaries per pass; missing/inapplicable boundaries are
not substituted with zero. See the [updated comparison](nvidia-renderscale-baseline-vs-head-20260910.md).

Automation source commit `5ab021f12362a4229556334f05a92ef41f5958af` is
integrated into its local `dev` branch. The repository comparison wrapper
uses that maintained source directly. Finalizer/comparison regression
tests, four ledger tests, both skill validations, package validation and
source/package parity passed. The integrated reporter rechecked 132
receipts and all 1,056 existing numeric timing cells. The installed plugin
cache was not rotated, and this work did not publish or modify a PR.

## September 10: old-baseline versus measured-head health audit

The [complete per-transition comparison](nvidia-renderscale-baseline-vs-head-20260910.md)
compares renderer base `1595cffd` (main-VR renderer equivalent `9074b4676`),
compiled as `390fdea25` with the preparation bridge backport, against the
earlier measured main-VR head `348803c18`. Head mean strict latency is
8.497% lower in pass 1 and 11.992% lower in pass 2. These are descriptive
transition measurements, with environmental limits recorded in the report.

The raw-receipt audit qualifies the terminal-PASS statements below.
Head transitions 26 and 28 each record two fidelity mismatches and one
vendor-failure stretch eye observation in both passes, then recover.
The old baseline records zero for these counters. Both raw cumulative
records reject the fixed stretch-frame cutoff and scaled-presentation gate
at the final native target. These are retained diagnostics, excluded from
health assessment for the reasons above. Head additionally fails the
applicable fidelity and fallback gates. Stored terminal and Task 2
verdicts remain separate from these findings.

The comparison validates all 132 retained rows and 1,056 numeric timing
cells against the existing ledger, without changing its historical cells.
Memory remains inconclusive, and no resolved GPU frame timings exist.
The report includes exact source commits, Build IDs, DLL identities,
per-pass telemetry, retry evidence, and an interactive comparison.

## September 10: 1595cffd comparison baseline completed

Run `renderscale-tuning-nvidia-20260910T090649592Z` completed 33 + 33
transitions on clean Release source
`390fdea2533ae447849c5f07766c796872996a61`, the `1595cffd` comparison
baseline with tuning preparation backported. All 66 render rows passed;
per-transition Task 2 counts are 66 PASS / 0 FAIL / 0 INCONCLUSIVE.
Strict completion averaged 838.490 ms and 830.058 ms. Pacing was VALID,
with maximum client dispatch gap 54.370 ms. All 32 stretch-selected
rows recovered; producer retries were 10 and 9.

All task-owned captures are verified inactive, the complete 444-record
journal is flushed, and the worker published COMPLETE and exited.
Physical DLL, adjacent manifest, AIO receipt, archive, and extracted
payload identities match. Memory classification is inconclusive;
reporting is complete after the omitted packaged memory summary was
reconstructed from six preserved boundaries. The existing ledger gained
one column containing all 66 strict timings, 528 numeric timing cells,
and complete timing groups, with all 887 rows and historical cells
preserved. See the [run report](nvidia-renderscale-tuning-390fdea25-20260910.md).

## September 10: selected recent ledger run

At the user's request, the five September 9 run columns were removed from
the comparison ledger: the early dirty build, both PR66 attempts, and both
previous `348803c18` runs. Today's `nvidia-2026-09-10T07-19-50-405Z` is the
only retained run from September 9-10. All 15 older run columns, today's
complete column, and all 887 metric rows are unchanged. Earlier reports
below describe the historical ledger before this selection; raw evidence
and historical comparison reports remain retained.

## September 10: completed NVIDIA two-pass assay

Run `nvidia-2026-09-10T07-19-50-405Z` completed 33 + 33 transitions on
clean source `348803c1831c8cd71ceb75a58143ad0bcb00abb2`. All render rows
passed; per-transition Task 2 counts are 66 PASS / 0 FAIL / 0 INCONCLUSIVE.
Pacing was VALID, with maximum client dispatch gap 40.695 ms. Strict
completion averaged 767.246 ms and 730.514 ms in the two passes. All
31 stretch-selected rows recovered; producer retry counts were 8 and 10.

Both passes verified telemetry inactive. Memory classification is
inconclusive, with private memory and system commit decreasing in both
passes. The deployed DLL hash matches the AIO payload and build manifest;
the manifest Build ID matches the running producer. Reporting is complete:
the full journal was independently validated and flushed, and all captures
are inactive. The stalled worker was subsequently recovered by cancelling
its aborted notification reader. It published COMPLETE, exited, and released
its own endpoint lock at 08:19:19 UTC, with the evidence journal unchanged.
The [run report](nvidia-renderscale-tuning-20260910.md) records these limits.
The existing comparison ledger gained one column containing all 66 strict
timings, 528 numeric timing cells, and full numeric timing groups. All
historical cells and the 887 existing metric rows were verified intact.

## September 9–10: interrupted NVIDIA cadence run

Run `nv-mtuivutb` retained 39 transitions (33 + 6) on clean source
`348803c1831c8cd71ceb75a58143ad0bcb00abb2`. Every retained render and
per-transition Task 2 result is PASS, but orchestration interruption and long
gaps leave stress cadence unqualified and memory `repeat_not_completed`.
The [run report](nvidia-renderscale-tuning-nv-mtuivutb.md) records the evidence,
timing definitions, and writer benchmark. All 39 strict timings and available
presentation, cleanup, tail, frame, and QPC timings were added to the existing
comparison ledger with historical cells verified intact. No live rerun was
performed while correcting the worker and evidence queue.

Continue updating the existing
[VR render-scale comparison ledger](vr-render-scale-ledger.md)
in place. Preserve its build columns and historical values, and append
available numeric timings for each measured transition and pass. Record
the route, timing definition and units, and distinguish missing receipts,
failed measurements, and transitions not run. Run reports explain the
results and link to this ledger; they do not establish another ledger.

The NVIDIA matrix uses metric names such as
`tuning_pass1_transition_01_dlss_hoshipa_to_none_strict_ms`: pass, ordinal,
source, destination, and dispatch-relative strict-completion milliseconds.
The five-second pre-dispatch wait is excluded. The September 9 backfill
preserves 198 available numeric timings across four existing build columns,
including all 66 timings of the interrupted and continued current run.

## Completed NVIDIA transitions with interrupted repeat: 9 September 2026

Run `nv-mtud89dr`, continued as `nv-mtud89dr-s2`, retained all 66
transitions on clean `main-VR` source
`348803c1831c8cd71ceb75a58143ad0bcb00abb2` (`v3.19.0-pr28` +274).
Both passes have 33 render PASS rows; Task 2 counts are 66 PASS / 0 FAIL /
0 INCONCLUSIVE, and all 24 DLSS trace windows are complete. The repeat
was interrupted after row 8 and continued at row 9 in the same game
process with new telemetry captures. It is recorded as one repeat pursued
across two runs. Uninterrupted-assay reporting remains INCOMPLETE, and
continuous-repeat memory confirmation is inconclusive.

Strict completion averaged 792.385 ms in pass 1 and 833.175 ms in the
combined repeat. The previous PR66 run `nv-mtu8nhph` averaged 794.846 ms
over the same 33 first-pass transitions; the current difference is -0.31%.
These are transition latencies, and session conditions prevent attributing
the difference solely to PR66. The prior PR66 repeat retained only rows 1-9.

The ledger labels now distinguish relative revisions and measured source
commits. PR66 branch `fix/vr-dlss-eye-bound-dilation` is
`85ee7d4a572544410fa29463d5d704b50984610f`; its measured merge is
`89e3964582730b92c1885eaef8f865e2d1d02ea9`. The RC166 tag/base is
`b95958cc0c119625d8a7dd9657442c92122f240d`; the historical measured
RC166 +13 baseline records source
`94165e2e70db2bbefd878aecbfa7733ee336ab63`, whose object is unavailable
in this checkout. Existing historical metric cells are preserved.
See the [transition table and evidence summary](nvidia-renderscale-tuning-nv-mtud89dr.md).

That table also includes the RC166 +13 baseline and the latest complete
September 2-3 run, `nvidia-mtlid7m3` (September 3, 13:00 UTC), on source
`539705004aa993816b79773fa287f91324312fbc` with uncommitted changes
(`v3.19.0-pr39` +230; recorded RC166 +510). RC166 has no matched
33-transition assay, and the September 3 per-transition timing bundle is
unavailable in the inspected local evidence locations. Both references
used 2468x2740 output per eye. The added columns state these limitations;
no historical timing values or percentages are inferred.
The committed record was checked: `539705004` preserves earlier ledger and
summary data, including August 29 timing tables. The September 3 run's
column was added by `190c28a39`; its timing cells already refer to an
external evidence bundle rather than containing numeric timings.

## Updated NVIDIA tuning: 9 September 2026

Run `nvidia-mtu6cpo8` on clean source `89e3964582730b92c1885eaef8f865e2d1d02ea9`
completed 33 first-pass transitions with render PASS and Task 2 counts
33 PASS / 0 FAIL / 0 INCONCLUSIVE. The second baseline timed out after
20 seconds under memory-deferred admission, so execution is INTERRUPTED
and all 33 repeat rows are NOT RUN. The 17 presentation-stretch rows
recovered; first-pass strict completion averaged 2569.260 ms with a
16264.205 ms maximum. Memory confirmation is `repeat_not_completed`.
See [the retained failure summary](nvidia-renderscale-tuning-nvidia-mtu6cpo8.md)
and the uniquely headed partial column in the comparison ledger.

The VR render-scale controller can capture a bounded CSX-menu stress session and write a versioned JSON record for an MCP/Ghidra optimization loop. The capture observes user-driven changes; it never changes render-scale settings itself.

## 2026-09-10: submit contract integration review

Rebased the submit input candidate onto `main-VR` at `ef7c366d`, retaining
exact input freshness proofs, source COM ownership, and deferred FSR
handling. The extracted batch cache retains dispatch evidence on reuse.
Deferred presentation restores the color contract when resource replacement
clears admission and rejects conflicting color metadata. Production-path
regressions cover that recovery, Linear rejection, captured host temporal
scalars, and normalized frame-zero evidence with distinct raw cache keys.
The deferred color regression failed before the correction and passed after.

This review generated no runtime measurement or candidate qualification
report. The comparison ledger remains unchanged; the earlier build receipts
do not qualify the rebased candidate for visual quality or performance.

## 2026-09-09: submit input contracts

The `fix/vr-submit-input-contracts` candidate freezes stereo camera metadata
before post-processing and makes submit color transfer/range explicit.
The scope, fallback behavior, and validation cases are recorded in
[VR submit input contracts](vr-submit-input-contracts.md).
Adversarial review tightened logical-frame admission, preserved FSR batch
dispatch evidence across desktop Present, and prevented failed DLSS fallback
from reopening old token publication. Snapshot ownership is private and
dispatch jitter selection is shared. Regressions cover these production
policy and cache boundaries.
This is an implementation and policy-test record, not a runtime measurement.
The comparison ledger has no new candidate column because an exact fixture
and accepted baseline are not configured for this run. Runtime qualification
must precede any visual-quality, stability, or performance claim.

The VR render-scale controller can capture a bounded CSX-menu stress session and write a versioned JSON record for an MCP/Ghidra optimization loop. The capture observes user-driven changes; it never changes render-scale settings itself.

## Capture workflow

1. Enable CSX developer mode.
2. Open **Upscaling > Render Pipeline > Render Scale Stress Capture**.
3. Select **Start Capture**.
4. Exercise the same fixed scenario for every candidate build. At minimum, perform two render-scale changes. Include repeated preset changes and a fast-travel cycle when evaluating memory recovery.
5. Wait for the final change to reach a stable in-world presentation, then select **Stop Capture**.
6. Read the new record from `Data/SKSE/Plugins/CommunityShaders/Diagnostics/VRRenderScale/`.

Use identical save, location, CSX profile, change order, dwell frames, HMD resolution, backend, and graphics settings when comparing iterations. Prioritize the production workload in this order: repeated same-backend resolution changes (for example Hoshipa/Quality), DLSS/DLAA and FSR/Native-AA activation changes, then a fixed-profile DLSS/FSR alternating series as a lower-priority backend-handoff stress oracle. Run each matrix as a separate capture so exact-profile memory grouping remains attributable.

## DevBench automation

### Submit-input freshness

`communityshaders.renderscale status` includes `submitInputFreshness` when
the DevBench bridge is enabled. Its fixed process-lifetime counters report
outer-boundary acceptance and rejection reasons. `methods.fsr`,
`methods.dlss`, and `methods.other` each expose producer-proof outcomes and
work counts for guide encoding, color copies, input sanitation, vendor
attempts/retries, and fallback preparation/output reuse. Compare two snapshots
from the same process; individual counters are sampled independently.

A matching descriptor address is insufficient: nested submissions must name
the same non-null DirectX resource captured at the outer boundary. Copied
descriptors can qualify. When peer proof fails, reuse of one observed eye is
bounded by the correlated outer scope, compositor cycle, frame, source and
guide resources, region, method, generation, flags, and color space. Such reuse
cannot admit peer reads or stereo dispatch. Missing outer scope disables
reuse because an in-place producer rewrite cannot be excluded. Reset,
resource destruction, device loss, and attempted input replacement retire
the corresponding cache claims before their resources can change.

The PR65 repair has no new runtime comparison entry in
`vr-render-scale-comparison-ledger.csv`: live qualification is pending a
released MO2 owner and a configured verified fixture. Offline policy and
composition coverage does not establish hook correctness, observed proof
acceptance, visual quality, or GPU performance in Skyrim.

### Deferred FSR eye dispatch

An FSR eye whose provider is still preparing resources returns `Deferred`.
The submit path presents ordinary stretch for the remainder of that
compositor cycle and retries on a later cycle. It does not record a failed
vendor evaluation or authorize reads from an unproven peer eye. Genuine
provider and device failures retain their existing failure handling.

See [the deferred-eye repair record](vr-fsr-deferred-eye-dispatch.md) for
the cold-entry failure mechanism and validation limits. This implementation
has no new measured entry in `vr-render-scale-comparison-ledger.csv`:
builds, tests, and runtime qualification are deferred by the operator while
another workload is running. No candidate timing or qualification result
has been inferred from the source change.

### Render Scale selection link

The optional [Render Scale selection link](vr-render-scale-link.md) separates
remembered user intent from quality-gated physical activation. The
`set_render_scale_link` action takes Boolean `enabled` and requires developer
mode plus an active stress capture. Status exposes `renderScaleSelectionPolicy`;
ordinary explicit `apply` profiles remain authoritative. The isolated forward
port from `72b04290b` has no new live qualification or performance measurement.
Consequently, no candidate measurement is added to the comparison ledger, and
its historical results must not be treated as evidence for this change.

The 2026-09-10 rebase onto `1afb9eca9` also preserves independent saved
preferences when performance-measurement restoration rejects a physical
transition. The rejection remains guarded and is logged with its reason.
See the [link validation record](vr-render-scale-link.md#validation) for the
24 restore scenarios and focused checks. This source review adds no runtime
measurement or qualification result; the existing ledger remains unchanged.

### Controller actions

Step 17 exposes the capture contract through the external devbench host used by
Open Shaders. The bridge is built by default through `DEVBENCH_BRIDGE=ON`, is
inert when the devbench SKSE plugin is absent, and can be omitted completely
with `DEVBENCH_BRIDGE=OFF`.

The registered tool is `communityshaders.renderscale`:

-   `status` returns a compact live snapshot of the controller profiles, VRAM
    pressure, retirement queue, post-load recovery, backend generations,
    current metrics, both-eye fidelity, and compositor-accepted per-eye
    presentation paths;
-   `record` returns the complete schema-v13 record without changing capture
    state;
-   `start` begins a new fixed-memory stress capture;
-   `apply` uses the same latest-wins transition entrypoint as a CSX-menu change.
    It requires `method` (`dlss` or `fsr`), `enabled`, `qualityMode`, and an
    optional `dlssPreset`;
-   `stop` stops the capture, writes the disk artifact, and returns the complete
    record in the tool response;
-   `reset` clears a stopped capture.
-   `qualification_status`, `qualification_begin`, `qualification_wait`, and
    `qualification_cancel` provide single-owner, server-timed transition
    measurement for the PR qualification protocol;
-   `dlss_trace_status`, `dlss_trace_start`, `dlss_trace_read`,
    `dlss_trace_stop`, and `dlss_trace_reset` expose the bounded diagnostics
    added by commit `b46edeaed14c41ad41225641c3a4943f1db25db6`;
-   `probe_start` begins a bounded load-presentation probe at the final OpenVR
    submission boundary;
-   `probe_stop` stops accepting new probe samples;
-   `probe_record` returns the retained per-eye submission timeline plus the
    correlated pre-HAM, depth-topology, and post-HAM dispatch timeline;
-   `probe_reset` clears a stopped probe.

Some Codex sessions do not expose dynamically registered DevBench tools as
first-class typed MCP calls even though DevBench has registered them and
`inspect(kind=extensions)` lists them. In that case, use DevBench's typed
`scenario` tool to dispatch the registered CS tool directly through MCP instead
of falling back to HTTP. Example:

```json
{
    "action": "run",
    "steps": [
        {
            "tool": "communityshaders.renderscale",
            "args": { "action": "probe_reset" }
        },
        {
            "tool": "communityshaders.renderscale",
            "args": { "action": "probe_start" }
        }
    ]
}
```

This is still direct DevBench MCP execution. Use REST only as a last-resort
manual fallback when DevBench MCP itself is unavailable.

Mutating actions fail closed outside Skyrim VR. `start` and `apply` require
developer mode, and `apply` also requires an active capture so an automation
client cannot make unrecorded benchmark changes. Quality modes are `0` for
native AA/DLAA, `1` Hoshipa, `2` Ultra Quality, `3` Quality, `4` Balanced, `5`
Performance, and `6` Ultra Performance; enabled render scale requires `1..6`.

For an ordinary diagnostic cycle, start capture, apply the fixed scenario
profiles, and inspect `status` until each requested epoch reaches `Active` with
the stable profile, exact backend generation, and both eyes valid. PR timing
must instead use the server-timed qualification actions described below; client
poll cadence is not a valid latency measurement. Drive a fixed fast-travel leg
through DevBench's own console tool when the scenario requires it. Stop capture
only after final recovery settles, then reject the run if any acceptance gate
fails. The record includes the build's Git description so artifacts remain
attributable to the exact candidate source.

For a memory-qualification capture, apply each of the two exact profiles at
least three times (six alternating transitions total). A shorter diagnostic
capture may satisfy the generic acceptance object while
`memoryTrend.evaluated` remains false; automation must not treat that as memory
acceptance.

## COC endurance protocol

The old fixed-delay COC batches are superseded. Use the 20-transition assay in
[Render-scale PR qualification](render-scale-pr-qualification.md). It pairs one
server-timed `qualification_begin` with one COC and one bounded
`qualification_wait`, then dispatches the next COC at the first coherent stable
observation. This avoids substituting a client polling interval or arbitrary
sleep for the transition duration.

The route still alternates only between `WindhelmExterior01` and
`WhiterunDragonsreach`; a same-cell baseline COC remains invalid. COC-only
results are judged by the qualification transition records, presentation and
fallback counters, failure state, liveness, and memory evidence. The generic
stress-capture acceptance object can still fail request-count or memory-trend
gates when no menu apply occurs, so it is retained as diagnostic evidence and
is not used to manufacture a COC pass.

### Fixed-delay optimization reference

The fixed-delay assay remains a trend reference for render-scale optimization,
even though it is not the release qualification protocol. Repeat it only with
the same mixed harness: arm the current DevBench, build-identity, Ghidra,
evidence, render-scale stress, and qualification framework, then run 20
alternating COCs between `WindhelmExterior01` and `WhiterunDragonsreach` with
one server-owned 10,000 ms wait before every COC. Record all 20 transitions in
one stress session.

Invoke the saved `$static-coc` skill with `start static coc`. The trigger uses a
strict two-command handshake: without a fresh exact-PID arm receipt it arms and
sends no COC; with a fresh receipt, the same phrase dispatches the preserved
scenario.

The reference measurements below were captured on 2026-08-27. Build IDs, not
labels or branch names, identify the binaries. The `main-VR reference` label is
the pinned comparison run; it must not be reinterpreted as the moving branch
head.

| Label             | Build ID                                                           | Source                        | Commit                                     |
| ----------------- | ------------------------------------------------------------------ | ----------------------------- | ------------------------------------------ |
| main-VR reference | `696a5f40404d25ed997c40cb0793bf931361bcf558fb7eff2982ae6cd7917805` | `v3.19.0-pr39-125-ga6ca42849` | `a6ca4284922f4fab7a79353d1462a2ce584fd20c` |
| RC166 reference   | `03c2adc0e18659ab1deb86b43062446a3794a5d6104664c6d7628c98008f7fd4` | `RC166-13-g94165e2e7`         | `94165e2e70db2bbefd878aecbfa7733ee336ab63` |

| Metric                                  | main-VR reference | RC166 reference | Optimization objective |
| --------------------------------------- | ----------------: | --------------: | ---------------------: |
| Fixed server waits                      |        200,000 ms |      200,000 ms |     Exactly 200,000 ms |
| Main scenario elapsed                   |        200,192 ms |      215,522 ms |     At most 215,522 ms |
| Harness/control overhead                |            192 ms |       15,522 ms |      At most 15,522 ms |
| Dragonsreach mean stabilization         |       68.9 frames |     24.0 frames |    At most 24.0 frames |
| Windhelm mean stabilization             |      139.3 frames |     20.0 frames |    At most 20.0 frames |
| Overall mean stabilization              |      104.1 frames |     22.0 frames |    At most 22.0 frames |
| Worst stabilization                     |        142 frames |       24 frames |      At most 24 frames |
| Recoverable retries                     |                10 |              30 |              At most 9 |
| Hard transition failures                |                 0 |               0 |              Exactly 0 |
| Session stretch-eye observations        |                40 |             428 |       Record; minimize |
| Lifetime stretch-eye observations       |                42 |             430 |       Record; minimize |
| Completed stretch episodes              |                19 |             n/a |       Record; minimize |
| Completed stretch frames                |                19 |             n/a |       Record; minimize |
| Session maximum stretch duration        |           1 frame |       18 frames |      At most 18 frames |
| Controller lifetime maximum stretch     |          2 frames |       18 frames |      At most 18 frames |
| Session vendor-failure eye observations |                 2 |               1 |              Exactly 0 |
| Post-run lifetime vendor failures       |                 6 |               1 |              Exactly 0 |
| Bounds-mismatch fallback observations   |                 0 |               0 |              Exactly 0 |
| Process-private steady-state growth     |          3.75 MiB |       11.48 MiB |      At most 11.48 MiB |

Stretch duration times the enclosing presentation-stretch interval; it does
not time a vendor-failure eye observation independently. One stereo frame can
contain more than one eye observation, so observation counts must not be
reported as frame counts. The main-VR record emitted complete episode
accounting: 19 completed episodes occupied 19 total frames and no session
episode exceeded one frame. Its controller lifetime maximum, including state
before the stress-session baseline, was two frames. RC166 emitted the
18-frame maximum but not completed episode or total-frame counters, so those
two fields remain `n/a` rather than inferred from its 428 eye observations.

The optimization goal is to preserve or improve RC166 stabilization while
reducing total recoverable retries below the pinned main-VR reference. A
candidate therefore needs no more than 24.0 Dragonsreach frames, 20.0 Windhelm
frames, 22.0 overall frames, and 24 worst-case frames, with at most 9 retries
across all 20 transitions. It must also retain zero hard transition failures,
OOMs, device losses, fidelity mismatches, vendor-failure stretches, and
bounds-mismatch fallbacks. The renderer frame counts are the primary
performance signal. Scenario elapsed time is secondary because the fixed waits
consume 200,000 ms and harness calls account for the remaining time.

#### Lifecycle pacing safety

Latency optimization must distinguish non-destructive readiness observation
from provider teardown, GPU drain, renderer-target recreation, and
post-mutation provider creation. A shared `Pending` result or retry interval
does not prove that these operations have the same lifetime requirements.

Shorter polling is admissible only for a path proven to observe immutable
state without dirtying, destroying, replacing, or publishing resources.
Destructive work must retain a proven GPU and renderer lifetime boundary, such
as the existing safe tail or an equivalent fence and ownership proof. Never
apply a generic fast-poll cadence across both classes. Validate any change with
repeated cross-provider DLSS-to-FSR and FSR-to-DLSS transitions before using its
latency result.

The two references used the following final render-scale state:

| Health metric              | main-VR reference                            | RC166 reference                      | Future-run requirement        |
| -------------------------- | -------------------------------------------- | ------------------------------------ | ----------------------------- |
| Stable profile             | DLSS Q1/K                                    | DLSS Q1/K                            | Same fixture                  |
| Render resolution          | 2096x2328                                    | 2097x2329                            | Record exact value            |
| Output resolution          | 2468x2740                                    | 2468x2740                            | `2468x2740`                   |
| Render scale               | 0.85                                         | 0.85                                 | `0.85`                        |
| Both eyes valid            | Yes                                          | Yes                                  | Yes                           |
| Latency gate               | Failed, over 120 frames                      | Passed, maximum 24                   | Pass                          |
| Fidelity gate              | Failed lifetime counters; transition count 0 | Passed; count 0                      | Pass; count 0                 |
| Presentation gate          | Failed; 2 session vendor observations        | Failed; 1 session vendor observation | Pass; 0 vendor failures       |
| Memory pressure            | Normal                                       | Normal                               | Normal                        |
| Memory trims               | 21/21 successful                             | 12/12 successful                     | No failures                   |
| Engine-target retirements  | 20 complete                                  | 20 complete                          | Complete and drained          |
| CPU/GPU performance window | Captured                                     | Did not start                        | Must start for CPU/GPU claims |

The canonical comparison ledger is
[`vr-render-scale-comparison-ledger.csv`](vr-render-scale-ledger.md).
Its first row names each build by its full source commit; append each later
measurement as a new column to the right. Do not replace the two pinned
reference columns. Use `n/a` rather than zero when a capture lane did not start
or a counter was not preserved, and do not claim a CPU/GPU improvement without
comparable valid windows.

| Date       | Candidate and Build ID                      | Scenario ms | Dragonsreach mean | Windhelm mean | Overall / worst | Retries | Session stretch observations / max frames | Vendor failures session / lifetime | Private growth | Verdict                                   |
| ---------- | ------------------------------------------- | ----------: | ----------------: | ------------: | --------------: | ------: | ----------------------------------------: | ---------------------------------: | -------------: | ----------------------------------------- |
| 2026-08-27 | main-VR reference `696a5f40404d`            |     200,192 |              68.9 |         139.3 |     104.1 / 142 |      10 |                                    40 / 1 |                              2 / 6 |       3.75 MiB | Reference                                 |
| 2026-08-27 | RC166 reference `03c2adc0e186`              |     215,522 |              24.0 |          20.0 |       22.0 / 24 |      30 |                                  428 / 18 |                              1 / 1 |      11.48 MiB | Stabilization target; retry target missed |
| 2026-08-28 | interrupted `9e1ac9755e38` / `c881bade50b2` |         n/a |               n/a |           n/a |             n/a |     n/a |                                       n/a |                                n/a |            n/a | CTD during even return COC to Windhelm    |

The `9e1ac9755e38` candidate is an interrupted assay, not a completed
comparison. At 04:54:49 UTC (05:54:49 BST), Skyrim VR crashed during an
even-numbered `coc WindhelmExterior01`. CrashLogger recorded an
`EXCEPTION_ACCESS_VIOLATION` at `SkyrimVR.exe+12FBD94` on a
`BSJobs::JobThread`, with `BSPortal`, `BSPortalGraph`, and
`BSMultiBoundRoom` in the crash context. The probable stack contained no
`CommunityShaders.dll` or `devbench.dll` frame.

The process exited before DevBench returned the scenario transcript, so the
exact transition count, strict qualification receipts, stabilization and
retry totals, preparation-stage totals, and final telemetry were not emitted.
The corresponding ledger cells are `n/a` rather than zero or a pass.

### Monitoring and optimization candidates

Use this section for repeatable observations that may justify optimization but
are not yet established defects. Give each item a stable ID, retain the build
and protocol evidence, and state the safety property that an optimization must
preserve. Valid states are `Monitor`, `Investigating`, `Ready`, and `Closed`.

#### Durable cross-machine record

The [9 September NVIDIA comparison](nvidia-renderscale-tuning-20260909.md)
preserves partial results from `nvidia-mtu2u3dm`: 57 full transition receipts,
all 24 compact trace summaries, and five memory boundaries. Available evidence
shows no functional regression against `nvidia-mtlid7m3`; cross-commit timing
and memory improvement remain unproven because resolution changed and the
second memory endpoint is missing. The matched 24-row repeat has similar mean
latency, with slower individual FSR3 rows retained explicitly. Partial evidence
is included in the ledger with its coverage and missing values identified.

The comparison ledger is the canonical aggregate measurement record. This
iteration guide retains conclusions, active investigations, and safety
invariants; compact tuning or failure documents retain reusable scenario
analysis. Commit corresponding changes to these files with `main-VR`
render-scale work, or in an immediate follow-up after runtime evidence is
collected.

Raw per-run trees under `artifacts/`, `docs/development/evidence/`, and
automation feedback storage remain local. They can be gigabytes in size and
must not be added merely to transfer aggregate results. Copy only conclusions,
build and run identity, decisive timings, verdicts, and evidence hashes into
the durable record.

| ID           | Candidate                                      | Current evidence                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          | Impact                                                                                                                                                                                                                                                                               | State           | Next check                                                                                                                                                                                                                         | Safety constraint                                                                                                                                                                                                                                            |
| ------------ | ---------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | --------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `RS-OPT-001` | Render-transition compositor hold release time | Every inspected `69aec3546` and `31e383b61` simple-COC sample released at the 1,500 ms hard deadline. The completed `9e1ac9755` repeat spent 88.3 mean frames in `Stabilizing -> Active` (90.9 warm). A fresh RC166-23 repeat reproduced the historical 24/20-frame result and spent only 1.0 mean frame in that phase (1.1 warm); the overall cross-build gap was 82.4 frames and 87.3 frames came from this phase, partly offset by RC166 taking six more frames before `Stabilizing`.                                                                                                                                                  | Approximately 1.5 seconds before the controller becomes `Active`; may extend the covered fade or black presentation.                                                                                                                                                                 | `Monitor`       | Record why coherent-stereo early release does not qualify. Repeat across load routes, then evaluate an early-release correction or a shorter transition-only deadline.                                                             | Do not reintroduce white/lavender HAM surfaces, stretched bounds, mixed-eye or mixed-generation presentation, stale resources, or partially mutated targets.                                                                                                 |
| `RS-OPT-002` | Coherent Streamline frame-token publication    | Build `c881bade50b2` logged three initial eye-0 `eErrorDuplicatedConstants` failures on isolated submit-stage foveated viewport 4608. A separate completed 20-transition repeat captured four more bounded failures at frames 265900, 267898, 271985, and 276836. The pinned frame 271985 reused token 271984 after that token/viewport succeeded on frame 271984. A fresh RC166-23 session independently logged another active-foveated failure at frame 85373 before measured transition 1; its later bounded trace recorded zero failures. See [`vr-foveated-dlss-duplicated-constants.md`](vr-foveated-dlss-duplicated-constants.md). | Each captured vendor rejection forced full-eye stretch fallback and a 30-frame retry backoff. Presentation recovered and the repeat still completed 20/20 render-scale transitions; these are vendor-dispatch failures on the active foveated path, not render-scale latch failures. | `Investigating` | Trace frame-token acquisition by caller and thread, then reproduce the publish-before-acquire interleaving in a deterministic concurrency test.                                                                                    | Publish frame and token atomically as one logical value; keep one token per CS frame across Reflex and DLSS; hold no acquisition lock across constants, evaluation, marker, or Reflex vendor calls; retain stretch fallback.                                 |
| `RS-OPT-003` | Native-AA backend identity switching           | In the first `renderscale-CSmenu` RC166-23 matrix, transitions 15 and 22 both requested DLSS native AA while FSR native AA was active. Both `apply` calls returned `queued=false`, request ID zero, and retained FSR as the requested, applying, effective, and stable method.                                                                                                                                                                                                                                                                                                                                                            | DLSS-native menu selection cannot be qualified from the inactive FSR-native state; both entries timed out with `profile_mismatch` and produced no render-scale sample.                                                                                                               | `Investigating` | Trace the CS-menu apply path where `renderScaleMode=false` and determine whether inactive profile identity must be updated without scheduling vendor work.                                                                         | Do not initialize or dispatch an inactive vendor backend, manufacture a latch event for an unchanged physical contract, or lose the authoritative last-active backend used by later activation.                                                              |
| `RS-OPT-004` | Same-cell CS-menu qualification freshness      | All four native-mode entries without a new latch request (transitions 1, 15, 16, and 22) timed out with `stale_source_observation`. The two genuine no-ops already matched their target profile; the two DLSS-native attempts also had the independent `profile_mismatch` from `RS-OPT-003`. Each timeout consumed approximately 30 seconds.                                                                                                                                                                                                                                                                                              | The COC-oriented destination-freshness condition makes a same-cell menu assay incomplete and adds approximately 120 seconds even when no COC is expected.                                                                                                                            | `Ready`         | Add a CS-menu waiter mode whose freshness evidence is the new dispatch/apply receipt and resulting profile revision; retain the existing destination event requirement for COC protocols.                                          | Never accept an old profile observation, weaken the strict COC destination check, or convert a no-request menu apply into a zero-frame render-scale sample.                                                                                                  |
| `RS-OPT-005` | NVIDIA FSR host qualification identity         | Build `c1f458f0b` completed all nine active FSR physical latches on `FSRHost`/FSR3 with `Ready` lifecycle state, current matching publication, both-eye fidelity, and stable vendor presentation. The strict waiter nevertheless rejected all nine because the public target retained the `fsr4` label and the checker required `FSR4Runtime`. RC166-23 selected the same NVIDIA `FSRHost`/FSR3 fallback and its earlier checker accepted all nine active FSR entries. The current 30-second waiter expirations are checker diagnostics, not render-scale failures; strict failure status for those entries is recorded as indeterminate. | Hides valid 5--26-frame physical latch timings behind approximately 270 seconds of timeout delay and can falsely attribute a qualification-contract mismatch to FSR relatch behavior.                                                                                                | `Investigating` | Resolve the expected physical backend from adapter/runtime eligibility and compare it with the authoritative applied backend; then repeat the exact 25-entry matrix and require strict completion for all nine active FSR entries. | Never accept a requested runtime label alone. Require the exact physical backend plus ready lifecycle/resources, current device/context/generation/dimensions publication, both-eye fidelity, stable vendor presentation, and zero terminal/vendor failures. |
| `RS-OPT-006` | Public-API qualification convergence           | The interrupted NVIDIA public-API tuning run on build `9abcbe4c435b` accepted and completed None, TAA, and DLAA operations, but all three strict waits timed out. The waiter decoded the embedded upscaling method as `unknown`; after TAA and DLAA, the direct public snapshot advanced configured/effective while requested/stable remained None. The DLAA physical records also remained inactive on backend None. See [`nvidia-renderscale-tuning-failure.md`](nvidia-renderscale-tuning-failure.md).                                                                                                                                 | A caller can receive successful operation completion while strict qualification cannot establish the same target, making transition timing unavailable and conflating provider-state convergence with qualification-decoder failures.                                                | `Investigating` | Trace the shared method decoder and operation completion publication, then repeat the exact 33-transition matrix and require coherent requested/effective/stable profiles plus the matching physical contract.                     | Never infer success from configured/effective state alone. Require authoritative profile presence, matching physical publication for vendor targets, clean lifecycle/fidelity/presentation, and zero unresolved mutation ownership.                          |

### Simple CSM memory comparison

Memory values use binary GiB. DXGI local-segment usage is the VRAM-residency
lane under the operating-system budget. Windows system commit is the
machine-wide committed-memory charge, not VRAM and not Skyrim-only memory.
Process-private bytes are therefore shown separately.

| Build                | Local VRAM start -> end (delta)  | Maximum observed VRAM                                          | Final VRAM / budget                               | System commit start -> end (delta) | Peak commit | Final commit / limit                                | Skyrim private start -> end (peak)    | Result                                                   |
| -------------------- | -------------------------------- | -------------------------------------------------------------- | ------------------------------------------------- | ---------------------------------- | ----------- | --------------------------------------------------- | ------------------------------------- | -------------------------------------------------------- |
| RC166-23 `86d21fc0a` | 10.172 -> 5.370 GiB (-4.802 GiB) | 10.172 GiB at session start; 6.506 GiB maximum transition peak | 5.359 / 22.826 GiB (23.476%); 17.468 GiB headroom | 63.936 -> 63.956 GiB (+0.021 GiB)  | 66.655 GiB  | 63.862 / 101.644 GiB (62.829%); 37.782 GiB headroom | 26.061 -> 26.812 GiB; 29.587 GiB peak | `Normal`; steady-state local/private/commit growth 0/0/0 |
| `c1f458f0b`          | 5.342 -> 5.144 GiB (-0.198 GiB)  | 6.745 GiB maximum transition/session observation               | 5.144 / 22.826 GiB (22.537%); 17.682 GiB headroom | 65.660 -> 66.924 GiB (+1.263 GiB)  | 70.039 GiB  | 66.924 / 101.644 GiB (65.841%); 34.720 GiB headroom | 27.429 -> 27.860 GiB; 30.541 GiB peak | `Normal`; steady-state local/private/commit growth 0/0/0 |

The current run ended with 0.215 GiB less local-video usage than RC166 and
0.214 GiB more local-video headroom. Its final sampled system commit was 3.062
GiB higher than RC166, but still retained 34.720 GiB of commit headroom.
Neither run met
the sustained-growth condition: the evaluated steady-state growth result was
zero for local video memory, process-private memory, and system commit.

The evidence for `RS-OPT-001` establishes the behavior for the measured
simple-COC transitions only. It does not establish that every save load, door,
fast-travel, startup, or manual-menu transition reaches the hard deadline.

`RS-OPT-002` is repeatable failure evidence, but its cross-thread cause remains
a leading hypothesis until acquisition-side tracing or a deterministic test
captures the interleaving. It concerns vendor frame-token coordination on an
active foveated path, not failure to enable foveation and not a render-scale
timing result.

## Live Ghidra/DevBench setup

Skyrim VR's Steam executable `.text` is encrypted on disk, so native crash-site
analysis must use a live process or a saved live memory dump. The local working
setup used for render-scale and LLF crash triage keeps all Ghidra projects,
cache, settings, and dumps on `D:`:

```text
D:\Coding\GitHub\codex-ghidra-live
```

Do not place persistent Ghidra projects or dumps under `C:\tmp`. Keep the helper
workspace outside the repository and untracked. It contains:

```text
Invoke-LiveGhidraDisasm.ps1
PrintDisassembly.java
README.md
dumps\
ghidra-cache\
ghidra-projects\
ghidra-settings\
```

Before collecting a live dump, confirm DevBench MCP is attached to the intended
Skyrim VR instance. `devbench_vr.inspect(kind=health)` must report
`exe: SkyrimVR.exe`, `vr: true`, the expected `pid`, and port `8921`. If the
typed MCP surface is unavailable, reconnect DevBench before falling back to REST
or manual HTTP commands.

Ghidra 12.1.2 launches successfully on the local machine when JDK 25 is forced:

```powershell
$env:JAVA_HOME = 'C:\Program Files\Eclipse Adoptium\jdk-25.0.3.9-hotspot'
$env:GHIDRA_JAVA_HOME = $env:JAVA_HOME
```

The reusable helper sets these environment variables automatically and also
redirects Ghidra config/cache to `D:\Coding\GitHub\codex-ghidra-live`.

For MCP analysis, install the repository-pinned GhidrAssistMCP extension and
register its loopback endpoint as described in
[Ghidra MCP integration](ghidra-mcp.md). Prefer the managed headless controller
so project import, reuse, readiness, and shutdown do not require the Ghidra UI.
The extension still does not replace the live dump required to obtain Skyrim
VR's decrypted executable bytes.

Dump and disassemble a live helper window:

```powershell
& 'D:\Coding\GitHub\codex-ghidra-live\Invoke-LiveGhidraDisasm.ps1' `
  -Rva 0x134C370 `
  -Length 0x700 `
  -Name shadow-helper
```

This writes:

```text
D:\Coding\GitHub\codex-ghidra-live\dumps\<timestamp>-shadow-helper.bin
D:\Coding\GitHub\codex-ghidra-live\dumps\<timestamp>-shadow-helper.meta.json
D:\Coding\GitHub\codex-ghidra-live\dumps\<timestamp>-shadow-helper.disasm.txt
```

Reuse a saved dump without a live Skyrim process by passing the `dumpBaseAddress`
from the matching `.meta.json`:

```powershell
& 'D:\Coding\GitHub\codex-ghidra-live\Invoke-LiveGhidraDisasm.ps1' `
  -DumpPath 'D:\Coding\GitHub\codex-ghidra-live\dumps\<dump>.bin' `
  -BaseAddress 0x7FF69492C370 `
  -Name shadow-helper-replay
```

For LLF shadow/culling crashes, validate the live decrypted bytes around the
reported SkyrimVR RVA before adding a guard. The guard must fail closed on
unsupported runtime, unexpected instruction bytes, or unverified branch/epilogue
targets. Prefer a second late-use guard when the entry guard is installed but the
faulting native instruction reloads a different live pointer later in the helper.

Performance builds keep `kEnableVRMenuPresentationTraceDiagnostics` false.
Changing it to true creates a dedicated forensic build with high-frequency D3D
menu detours and must not be compared against normal optimization captures.

The load-presentation probe is compiled only with `DEVBENCH_BRIDGE=ON`, requires
developer mode to start, and remains disabled until `probe_start`. While active,
it copies a 5x5 grid from each final DirectX eye texture into a 16-slot staging
ring and uses D3D11 event queries plus non-blocking maps. At the terminal
post-load stereo handoff it also uses a separate eight-slot ring to capture a
uniform 9x9 color grid immediately before and after the HMD hidden-area-mask
(HAM) clear. The probe shader is prepared by `probe_start`, and the format-bound
staging resources are prepared during the preceding black hold so compilation
or allocation does not perturb the measured release frame. The same capture is
armed for the first requested-eye submit-stage clear in the exact compositor
cycle where the bounded hold times out. A tiny diagnostic
compute pass samples the exact depth SRV and
reproduces the production clear's integer depth mapping, two-pixel dilation,
and clear decision for the same 9x9 positions. This avoids illegal partial
copies from Skyrim's live depth-stencil resource. It never reads back a
full-resolution frame, flushes, or waits synchronously for the GPU. A probe
failure or saturated diagnostic ring drops only that diagnostic sample and
does not gate the HAM clear or post-load release. It does not
emit per-frame info or debug log messages; all probe output is returned through
the DevBench tool. `probe_start` installs the existing idempotent OpenVR submit
interception immediately and fails closed if the compositor interface is not
available. The early interception remains observer-only and forwards Skyrim's
original submissions unchanged until the ordinary production render path
enables submit processing, so main-menu and loading presentation is captured
without changing it. Each record
correlates the sampled luminance grid with QPC/frame time, OpenVR submit path
and result, texture identity/format/bounds, loading and destination-world
frames, Stabilizer synchronization state, render-scale presentation path, and
the CSX HMD hidden-area-mask clear decision and its correlated HAM dispatch
sequence. Schema v5 binds hard-timeout instrumentation to the exact compositor
cycle and requested eye, then marks the first real OpenVR attempt independently
for each eye. It correlates that submitted texture with the preceding clear by
session, cycle, eye, and COM identity. `Matched`,
`NoSubmitStageClearBeforeSubmit`, `ClearFailedBeforeSubmit`,
`IdentityMismatch`, and `InvalidSubmitTexture` are retained explicitly. When
there is no submit-stage clear, the probe does not inject the diagnostic depth
pass; the existing final-submit grid remains the authoritative bright/white or
dark/black sample. This keeps the timeout probe observer-only and prevents
speculative peer-eye replay from consuming the requested eye's capture.
The exact-cycle marker remains retained until the next load lifecycle or probe
reset so a later `WaitGetPoses` cannot discard a target-cycle submit that is
still in flight; completed records contain copied provenance and do not depend
on that marker's lifetime. Handoff records expose `holdElapsedMs`,
`softDeadlineMs`, and `hardDeadlineMs` for both stereo and hard-timeout release.
The legacy timeout elapsed/budget fields remain available for hard-timeout
records, with the timeout budget equal to the hard deadline. The legacy
`firstPostTimeoutSubmit` and `timeout-release` labels also denote the
hard-timeout handoff.
`predominantlyWhite`
and `predominantlyBlack` mark broadly uniform
submitted textures. The legacy `hamWhitePattern` remains available, while the
explicit bright/dark fields classify both a bright or lavender HAM and a black
HAM. Strict white/black, broader bright/dark perimeter, and exact depth-aligned
classifiers are reported separately. The terminal `hamDispatches` records retain raw pre/post luminance and
alpha grids, center and neighborhood-minimum depth grids, exact clear-mask
decisions, signed luminance/alpha deltas, newly-black/newly-white counts, and
newly-transparent/newly-opaque counts split between masked and unmasked
samples. This distinguishes RGB black from transparent black that a later
compositor could present differently. These grids are authoritative; the polarity labels are
diagnostic heuristics. A depth-aligned black/transparent post-HAM mask is the
expected result of a successful clear; it is evidence of what CSX wrote, not by
itself proof that the compositor exposed the mask as an artifact. This
correlated record is load-presentation probe schema version 5. The main
render-scale iteration schema is version 13.

The post-load black hold uses route-specific soft deadlines of 3 seconds for an
in-game load and 6 seconds for a main-menu load. Crossing the soft deadline does
not weaken the existing release proof: vendor work continues behind the black
keepalive, `PresentationStretch` remains suppressed, and an exact repaired
vendor stereo pair can release as soon as it satisfies the existing proof. A
shared 500 ms grace places the nominal hard deadlines at 3.5 and 6.5 seconds.
Only the hard deadline invokes the established cycle-boundary fail-open and arms
the timeout probe. Device, menu, invalid-candidate, keepalive, and OpenVR failure
paths retain their earlier emergency fail-open behavior. Deadline observation
occurs at the next observed `WaitGetPoses` boundary, so a delayed boundary can
make measured elapsed time exceed the nominal hard deadline.

The hard-deadline token does not count itself as pending work. Before physical
mutation, a truthful stable contract is retained immediately. If reduced targets
remain active but that resource proof is unavailable, the exact owner may publish
one internal provider-neutral native relatch. It does not alter the selected
DLSS/FSR profile: that profile is deferred with a fresh transaction identity and
replayed after coherent native recovery. The internal worker may request one
emergency creator-service turn after two seconds, then remains covered by a
nonrenewing recoverable deadline. The current provisional policy uses a
15-second absolute deadline until the exact one-shot creator is claimed or a
later irreversible reconciliation/publication milestone is reached, a 60-second
absolute ceiling thereafter, and a 120-second
ceiling while a debugger is attached. These are suggested initial values, not
determined optima. Reversible resource/memory readiness remains on the 15-second
ceiling and cannot manufacture the longer budget. A failed creator entry or
native fallback publication stays
black-covered and retryable until the applicable deadline; it is not an
immediate integrity failure. A new load removes the old fast-path waiver and
lets the same worker use the normal safe-point gates; it does not create a second
successor. The pre-creator watchdog does not charge time while the newer
LoadingMenu serial is open. Its recoverable bound begins at that serial's
authoritative close tick, and repeated same-serial callbacks cannot move the
start tick.

A failed native-fallback publication uses a separate immutable clock beginning
at the first exact hard-deadline token. Missing clock state is initialized once
under the full owner transaction instead of becoming an unbounded retry. At
expiry, terminal ownership is claimed before releasing those locks and blocks a
new physical mutation from racing the forced-exit decision.

The emergency service turn relaxes but does not eliminate system-commit
admission. It retains the target-specific 4x projection (8x for native restore),
uses at least a 4 GiB projected addition, and relaxes only the normal dynamic
8-16 GiB reserve to a final 2 GiB reserve. Emergency admission queries system
commit both during plan evaluation and again immediately before the creator
claim. A claim-time rejection requeues without consuming the one-shot; the
immutable watchdog deadline still bounds the chain. Iteration
records expose the current post-mutation progress phase,
last progress tick, emergency-attempt state, selected terminal deadline, and
debugger state so an extended deadline can be distinguished from retry churn.
In `_DEBUG` builds only, an exact recoverable owner beyond 6.5 seconds can change
the protected opaque-black keepalive to a very dim 1.8-second blue pulse. Release
builds, including Release builds with DevBench enabled, always clear opaque black.
The Debug phase is sampled once per `{holdEpoch, compositorCycleToken}` so both
eyes use one pulse value, then encoded for the candidate's OpenVR color space and
D3D view. Eligibility never samples or publishes the incoherent game target, and
a terminal claim prevents the cue on every later compositor cycle while
preserving the already-cached stereo value for the current exact cycle.
`livenessCueActive` changes only
after a successful device-healthy clear; sticky activation count/tick/hold/cycle/
color-space fields preserve evidence after reset. The delay, period, and linear
intensity remain provisional usability values. A securely composed OpenVR
notification remains outside this change.

Start the probe at the main menu or immediately before invoking the in-game
load command. Stop it only after the destination has visibly settled, then poll
both `status.loadPresentationProbe.pendingReadbacks` and
`status.loadPresentationProbe.hamDispatchProbe.pendingReadbacks` until they
reach zero before requesting `probe_record`. The 4,096-record ring retains
about 22 seconds at
90 Hz with both eyes submitted every frame; older records are overwritten and
reported explicitly. If the HMD shows a white/lavender or black HAM but the
retained submitted textures and correlated HAM dispatch do not, correlate the
QPC interval with a SteamVR mirror recording:
that result places the artifact after the application texture boundary, in an
OpenVR/compositor layer rather than the CSX presentation texture.

Step 18 calibrates the acceptance contract against the first live MCP rapid-
switch baseline. A request that reuses the already-active physical contract can
complete on its apply frame without entering vendor stabilization. That path
emits both `Applied` and `Stable`, matching its completed transition metric, so
the stable-latency gate accepts bounded synchronous reuse while retaining the
same event evidence required from a rebuilt DLSS or FSR contract. Session start
and stop events carry zero transition counters rather than inheriting metrics
from work outside the capture boundary.

Step 19 closes the logical-versus-physical convergence gap found by the first
RC97 MCP preflight. An unchanged active CSX-menu profile is synchronous only when
the boot latch, quality, exact backend generation, backend resources, and common
vendor textures are all present. If that contract is incomplete and no recovery
is already in flight, the request enters the normal latest-wins controller and
queues an epoch-owned relatch. Zero-generation or resource-free lifecycle state
is not reported as backend-ready, preventing a missing contract from producing
a false `Applied`/`Stable` result.

Step 20 preserves the DLSS teardown result across Streamline, submit-stage, and
vendor-reset boundaries. A D3D11 idle fence that is still pending now records
`WaitingForDrain` and a bounded backend retry instead of a backend failure. Query
or Streamline resource-free errors remain `Failed`, so the acceptance contract
continues to reject genuine teardown faults while allowing expected asynchronous
GPU drain polling during DLSS/FSR handoffs and post-load recovery.

Step 21 bounds repeated backend-switch allocation churn. During an ordinary
CSX-menu relatch at the same dimensions and `Normal` memory pressure, compatible
inactive host-FSR and DLSS runtime resources remain resident and are reused when
that backend becomes active again. Recovery, post-load, resize, pending-reset,
device-loss, non-`Normal` pressure, and runtime-FSR paths retain the existing
teardown behavior. The relatch plan records warm retention and target reuse so
automation can distinguish deliberate residency from missed teardown.

Vendor dimension compatibility is independent of full D3D target readiness. A
same-dimension CSX-menu handoff may reuse the physical target layout when its
always-resident anchors and every currently resident optional target still have
the expected dimensions, and the previous contract has complete, exact both-eye
fidelity evidence. This stable-contract fallback avoids treating an absent lazy
target or optional/method-specific view as a resize signal while still requiring
the strict resource probe when stable evidence is unavailable. A true render or
display dimension change still disables warm retention.

The same guarded path preserves shared submit-stage intermediates, foveated and
menu resources, common vendor textures, and periphery-TAA allocations. Their
frame/history state is invalidated and the compatible intermediates are rebound
to the new logical contract generation without reallocating them. The stable
probe requires the always-resident main, main-copy, motion-vector, and depth
textures, while optional engine targets are dimension-checked when resident and
remain governed by the render-target creation hook when created later. Records
expose `reuseRenderTargets`, `reuseStableRenderTargets`,
`renderTargetDimensionsMatch`, `stableContractEvidenceMatches`,
`vendorDimensionsUnchanged`, and `reuseSharedSubmitResources` so automation can
distinguish strict reuse, stable-layout reuse, and shared-resource retention.
The record also reports named render-target missing/mismatch masks, named stable
contract evidence blockers, and the observed `stateScreenWidth/Height`. These
are diagnostic facts only: they do not relax dimension-changing, recovery,
pressure, retirement, or device-loss behavior.

Schema v3 introduced grouping of completed transition peaks by exact backend profile
(method, backend, quality, preset, and dimensions). Once one profile has three
samples, the memory trend can distinguish cold, warm, and repeated residency.
Exact-profile grouping prevents quality or resolution changes from being
classified as leaks. Step 25 refines the acceptance rule for schema v4 below.

Live Skyrim VR decompilation for Step 21 shows that the common
`BSShaderRenderTargets::Create` path repopulates the full engine target table;
only one special target is explicitly released by that top-level routine before
the table is rebuilt. Runtime captures then showed the same approximately
1.55-GiB repeated-profile growth for DLSS and FSR, with CSX retirement fully
drained and the allocation returning naturally after a long idle. This is
treated as deferred D3D/DXGI residency rather than backend-owned leakage.

After the second distinct rapid CSX-menu relatch, and after every further
distinct relatch within the 1,800-frame window, the controller now retires
transient CSX resources and arms a common-target memory trim. Pressure,
post-load, and low-peak native restores arm the same recovery independently of
the rapid-switch count. The trim is placed behind a D3D11 event query and is
polled without blocking; `IDXGIDevice3::Trim` runs only after the GPU crosses
that fence. Post-load admission waits for this bounded attempt to complete, but
continues safely if DXGI trim is unavailable. This keeps ordinary isolated menu
changes on the fast path while bounding deferred residency during the workloads
that can otherwise approach OOM.

The live MCP status and iteration record expose `controller.memoryTrim`,
post-load trim state, and per-transition trim counts/failures. The
`memory_trim_drained` gate rejects a capture stopped while cleanup is still
pending. A candidate still has to pass `steady_state_memory_growth`; a reported
successful trim is evidence of the attempted recovery, not a substitute for the
measured VRAM plateau.

Step 22 corrects the ordering exposed by the first Step 21 live qualification.
Six-switch DLSS and FSR Hoshipa/Quality captures preserved exact both-eye
fidelity, bounded latency, and drained retirement, but the last two peaks still
grew by approximately 1.45--1.89 GiB. DLAA/Hoshipa grew by approximately
2.20 GiB. `IDXGIDevice3::Trim` completed on every protected transition, proving
that a post-allocation trim alone cannot prevent the released and replacement
engine target tables from overlapping in WDDM residency.

Before a protected recreate, the controller unbinds texture and UAV stages,
invalidates Skyrim's matching renderer-resource caches, offers eligible common
targets to DXGI at low priority, and flushes queued D3D11 work. This path is
provider-independent but deliberately resource-scoped: it covers Skyrim render,
depth, deferred, underwater, and non-shared display targets, not DLSS/FSR runtime
resources or shared compositor textures. Device4 uses
`DXGI_OFFER_RESOURCE_FLAG_ALLOW_DECOMMIT`; Device2 remains the fallback. The
exact offered COM identities and matching device interface live in a fixed-size
transaction until Skyrim's creator reaches its checkpoint under the same unique
target-table lock. The checkpoint reconciles the table and calls the matching
`ReclaimResources*` API before global or CSX setup may read a surviving identity.

Immutable, CPU-backed, shared, and previously poisoned resources are excluded
from subsequent offers. A reclaim result of `OK` makes a surviving identity safe
for normal use. `DISCARDED`, `NOT_COMMITTED`, a failed reclaim call, or failure
to retain every unsafe identity as poison terminates synchronously at the
checkpoint; rendering cannot resume and defer replacement to another
transition. Poison retention is a defensive identity invariant, not a recovery
queue. If the Offer call itself never succeeded, no resource acquired offered
state, so that remains pre-drain failure telemetry rather than an unsafe-reclaim
terminal case.

Successfully displaced engine-target references follow a separate correctness
path: their owning COM references remain queued until a GPU event fence permits
release. `IDXGIDevice3::Trim` is an independent, best-effort residency hint
behind its own fence, not the reclaim checkpoint or the displaced-reference
lifetime barrier. Missing Trim support therefore does not make a safe retired
identity unsafe, and a successful Trim cannot make an unsafe reclaim usable.

The pre-drain remains limited to rapid relatches, native restores, pressure, and
post-load recovery, so the ordinary isolated CSX-menu and steady-state render
paths gain no scan or allocation churn. This is preventive hardening for PR10's
new fallback interactions, not evidence that OfferResources caused, or a claim
that it fixes, the RC194 screenshot. The available RC194 log supports the earlier
pre-creator memory-admission/liveness stall and never records this creator
checkpoint; the screenshot alone does not establish a cause. Pre-drain telemetry
remains authoritative alongside `steady_state_memory_growth`.

LoadingMenu edge publication is also generation-owned. Event state, serial, and
the LoadingMenu work-gate bit change under one transaction. An exact open edge
keeps a zero-cost missed-close token; if its close callback is absent, repair
requires the unchanged serial/generation, physical-open or exact load-completion
authorization, both independent menu mirrors closed, and two distinct completed
world frames. A newer event cancels the token, and there is no time-based mirror
bypass. This prevents a lost callback from leaving pre-creator admission
structurally blocked without weakening renderer-mutation exclusion during a real
load.

To evaluate this gate, complete at least three transitions to each of two exact
profiles in alternating order. Prefer two resolutions on one backend or native
AA versus an enabled profile; use DLSS versus FSR only for the backend-handoff
stress series. A one-time rise while the profiles become warm is expected; the
final same-profile delta must plateau within the bound.

Step 23 isolates the remaining Step 22 plateau failure to host FSR context
recreation. On the live NVIDIA qualification system, six-switch DLSS
Hoshipa/Quality and DLAA/Hoshipa captures passed every gate with zero measured
steady-state growth. FSR Hoshipa/Quality still grew by approximately 1.44 GiB
and 1.52 GiB for its two exact profiles, while native-AA/Hoshipa grew by
approximately 2.20 GiB. Every FSR correctness, fidelity, latency, retirement,
pre-drain, trim, OOM, and device-loss gate passed. The final relatch plans showed
that each active FSR resize destroyed and recreated two host contexts even
though those contexts were already allocated to the full per-eye display
extent.

An ordinary CSX-menu transition may now preserve those host contexts when the
previous boot contract still identifies FSR, the SDK compatibility check covers the target
render and display extents, memory pressure is `Normal`, and no runtime
upscaler, pending reset, post-load recovery, device loss, or recovery-owned
contract is active. This covers same-backend quality changes. It does not
reuse shared submit textures or engine render targets; those are still rebuilt
for the new dimensions, and FSR history is explicitly reset.
The short rapid-relatch cleanup window does not block compatible context reuse
by itself; an actual non-`Normal` DXGI pressure sample does.

The historical AMD forced-recreate path remains unchanged because the live
Step 22 evidence came from an NVIDIA DLSS-capable system and therefore cannot
qualify AMD behavior. Runtime FSR/FSR4 and incompatible host contexts also keep
their existing teardown path. The controller exposes
`reuseCompatibleHostFSRResources` in `resourcePlan`; a Step 23 FSR
qualification must observe it together with `preserveFSRResources=true`,
`destroyFSRResources=false`, and a passing steady-state growth gate before this
replacement for the generic non-AMD resize rebuild is accepted.

Step 24 follows the RC107 live qualification. Six rapid FSR Hoshipa/Quality
relatches again had zero failures, OOMs, device loss, retries, or both-eye
fidelity mismatches, and the compatible host context was preserved. However,
the final two samples of each exact profile still grew by approximately
1.37--1.40 GiB. Usage remained at approximately 10.79 GiB (11.58 GB) for a full minute
with no pending CSX retirement and with the fenced trim completed. The remaining
churn was therefore below the context layer: rapid cleanup and vendor reset
retired the per-eye textures submitted to the preserved context, while
`EnsureVRIntermediateTextures` recreated them for each contract generation.

All FSR submit inputs are now allocated to stable full-display per-eye bounds.
The host and runtime APIs continue receiving the exact active render and
upscale rectangles for every dispatch, so Hoshipa, Quality, and other profiles
do not change their effective fidelity. Compatible CSX-menu relatches preserve
the full-eye and foveated-center FSR input identities while still invalidating
frame, foveated-layout, periphery-TAA, menu-composite, and temporal history.
Engine render targets and size-dependent common textures continue to rebuild.
Actual pressure, post-load recovery, pending reset, device loss, incompatible
display bounds, and recovery-owned contracts retain the destructive path.

The compatibility policy now applies equally to SDK fallback, native/runtime
FSR3, FSR4, NVIDIA, and AMD. Every VR FSR path already creates its context and
runtime shared resources against the same full per-eye display bounds; no live
evidence supports retaining the old RC28 vendor/path-specific resize rebuild.
Path or provider changes remain independently protected by the existing reset
tracking. Native-AA reactivation can use the controller's last applied or stable
FSR method when the inactive boot snapshot no longer carries method evidence.

The resource plan exposes `reuseCompatibleFSRResources` and
`preserveCompatibleFSRIntermediates`. The legacy
`reuseCompatibleHostFSRResources` JSON member remains as a host-only alias for
existing Step 23 automation. Step 24 qualification must observe the two generic
flags together with `preserveFSRResources=true`, `destroyFSRResources=false`,
both-eye fidelity, and a passing steady-state memory-growth gate.

Step 25 follows the RC108 live qualification. Six 2.5-second DLSS
Hoshipa/Quality relatches reached 18.42 GB and `Elevated` pressure, with the two
profiles growing by approximately 1.46--1.49 GiB. FSR showed the same pattern
despite preserving its contexts and submit inputs. No capture reported an OOM,
device loss, fidelity mismatch, failed pre-drain, or undrained retirement. A
passive sample then released approximately 4.7 GB between 30 and 45 seconds and
returned to `Normal`, identifying delayed WDDM residency rather than a
backend-owned leak.

Matched 15-second-dwell controls made the distinction explicit: DLSS ended at
9.09 GB and FSR at 7.76 GB under `Normal` pressure, but the schema-v3 gate still
failed isolated upward peak oscillations of 1.49 GiB and 603 MiB respectively.
Schema v4 therefore requires growth above 256 MiB across both consecutive
same-profile intervals before classifying the trend as sustained. The record
retains all three peaks plus both individual deltas, so a decrease followed by
one WDDM rebound remains visible without being mislabeled as monotonic leakage.
Rapid captures that add a target table on every repeat still fail because both
consecutive intervals grow.

Runtime admission now protects the same transient peak. The first isolated
switch remains on the existing fast path. Once rapid-relatch memory relief is
active, or current pressure is already `Elevated` or higher, a size-changing recreate
projects its additional residency with a 50-percent estimator uncertainty
margin. If projected usage would enter the existing `Elevated` boundary, the
epoch performs its bounded cleanup and trim attempt, then waits at a 120-frame
retry cadence for WDDM headroom instead of allocating another overlapping
target table. Latest-wins request handling remains active while admission is
deferred. The rapid-relatch guard remains owned while the controller is waiting,
so clean rendering by the old contract cannot accidentally clear admission
protection. Pressure-protected transitions have a separate 3,600-frame latency
bound; ordinary transitions retain the 120-frame limit.

`resourcePlan` exposes `projectedAdditionalBytes`, `projectedUsageBytes`,
`admissionUsageLimitBytes`, `projectedResidencyGuardActive`,
`projectedResidencyDeferred`, and the existing cleanup/deferred flags. These
fields distinguish preventive backpressure from a backend stall or an actual
allocation failure.

Step 26 follows the RC110 machine-commit reproduction. Thirty uninterrupted
2.5-second DLSS Hoshipa/Quality relatches advanced cleanly through epoch 31 with
zero controller failures and exact both-eye fidelity, while DXGI local usage
rose from 5.45 GB to 15.75 GB and still reported `Normal`. Over the same run,
Skyrim private committed memory rose from approximately 21.8 GB to 72.23 GB
and Windows commit reached 108.32/109.14 GB (99.25 percent). The resulting
`STATUS_COMMITMENT_LIMIT` (`0xc000012d`) could prevent both Skyrim and unrelated
processes such as the Codex command runner from allocating before the Step 25
local-video admission boundary was reached.

The shared memory sample now records Windows commit usage, limit, headroom,
ratio, and Skyrim private usage alongside DXGI local-video residency. Ordinary
active-contract allocation overlap, including the isolated-switch path,
projects system commit at four times the resource estimate, retaining margin
above the approximately 2.8-times growth measured in RC110. Full-resolution
inactive/native restoration uses an eight-times projection because that path
showed a larger host-commit multiplier. Ordinary admission waits before the
projection reaches the lower of 75 percent of the current Windows commit limit
or an 8-GiB system reserve. This calculation is constant-time and leaves the
ordinary isolated switch unchanged while sufficient headroom exists.

A recognized VR FPS Stabilizer `PostLoadSync` door handoff instead preserves
the RC94 physical cadence. It bypasses projected local-residency ratios,
post-trim relaxation, common-target offering/decommit, engine-target
reclamation, warm-resource policy, and steady-state analysis during the
handoff. RC94 rapid-relatch cleanup remains part of the physical relatch:
memory-relief transitions retire submit intermediates, and a render-scale-off
restore tears down the active vendor contract, waits for its retired
intermediates, releases deferred targets, and only then allocates the
full-resolution replacement. That deactivation is admitted immediately so its
own reclamation cannot be blocked by pre-cleanup memory evidence. Exterior
activation fails closed when current memory evidence is missing, the device is
lost, an OOM is recent, local pressure is `High`/`Critical`, the estimated
allocation exceeds actual local headroom, or the four-times system-commit
projection would consume the hard 8-GiB reserve. Newer cleanup and recovery run
only after the destination contract becomes stable and visible; final memory
acceptance still requires recovery below the ordinary 75-percent boundary.

The first live RC94-cadence qualification exposed a presentation-release
deadlock rather than an admission or backend failure. The exterior DLSS
contract applied with matching dimensions and a ready generation, but the
combined loading/menu presentation signal and deferred transition cleanup kept
both eyes in `PresentationStretch` indefinitely, so no vendor evaluation could
promote the contract. A resolved door handoff now derives authority from the
matching LoadingMenu close frame, completed destination world frame, resolved
Stabilizer sync, applied epoch/method/generation, ready vendor runtime, and
exact submit dimensions. Once those facts hold, aggregate menu/tail and cleanup
flags cannot retain presentation-only mode. A physically open LoadingMenu, a
new load (which clears the close frame), any real non-loading menu, a pending
vendor reset, missing motion/depth inputs, or mismatched dimensions still
blocks release. This override remains valid after the controller becomes
`Active`, so a stale aggregate flag cannot re-enter stretch on the next frame.

Previous-vendor ownership resolves from the physical boot contract while that
contract is active. When it is inactive, the authoritative applied profile is
used first and the stable profile second, and only an inactive vendor-labelled
contract is accepted. Requested or applying profiles never identify previous
ownership. The resolved method is exposed as `previousVendorMethod`, so DLSS
and FSR teardown decisions remain attributable across DLAA/Native-AA handoffs.

The first guarded attempt owns one GPU-fenced trim for its epoch. Once that
trim completes, later retries only resample and wait at the existing 120-frame
pressure cadence; they do not repeatedly force cleanup. Latest-wins requests
remain available, and the previously stable physical contract continues
rendering until both local-video and system-commit admission succeed. Post-load
recovery also requires system commit below the same boundary before it admits
the fast-travel relatch.

Schema v5 exposes `systemCommit*` and `processPrivateUsage*` values in live
status, events, controller memory, transition peaks, post-load recovery, and
exact-profile memory trends. `resourcePlan` adds
`projectedSystemCommitAdditionalBytes`, `projectedSystemCommitBytes`,
`systemCommitAdmissionLimitBytes`, `systemCommitGuardActive`,
`doorHandoffHardReserveOnly`, `previousVendorMethod`, and
`systemCommitDeferred`. Acceptance
independently rejects unsafe final commit,
sustained system-commit growth, and sustained Skyrim-private growth, so a run
cannot pass merely because DXGI reports reclaimable local-video residency.

Step 27 follows the RC111 guarded qualification. Nine rapid DLSS
Hoshipa/Quality changes completed with exact both-eye fidelity before epoch 11
projected 18.62 GB of local-video use against the 18.38-GB normal admission
limit. Its successful one-shot trim left the system-commit projection safely
below the 81.85-GB hard limit, but the unchanged local guard waited 4,585 frames
and accumulated 39 pressure retries before WDDM released enough residency.
While the old Quality contract continued rendering correctly, fidelity also
reported an epoch mismatch solely because the pending Hoshipa request owned a
newer desired epoch.

After a successful pressure trim for the same epoch, local-video admission may
now use the existing `High` boundary (the lower of 87.5 percent of budget or
512 MiB of headroom). The relaxation applies only when the normal projected
limit would defer, the post-trim projection remains below that `High` boundary,
and projected Windows commit remains below the Step 26 admission limit. The
system-commit limit is never relaxed. A failed or missing trim therefore keeps
the normal local boundary, and each epoch still receives at most one forced
trim.

Fidelity observations are now owned exclusively by the physical `applied`
contract. A newer desired epoch waiting for admission no longer invalidates the
old contract that is still presented; method, generation, dimensions,
evaluation, and eye symmetry continue to be checked against that applied
contract. Schema v6 exposes `postTrimAdmissionUsageLimitBytes` and
`projectedResidencyPostTrimRelaxed` in both live status and the complete record,
so automation can distinguish normal admission, bounded post-trim admission,
and a genuine pressure deferral.

Step 28 closes the presentation-fidelity gap exposed by the RC111 manual
door-transition test. The prior fidelity record proved that a vendor evaluation
had succeeded for each eye, but it did not prove which texture OpenVR actually
accepted afterward. An intentional loading/menu stretch could therefore remain
on screen, or a source-bounds mismatch could fall back to the original submit,
while a stale successful fidelity observation still allowed the capture to
pass.

Each accepted compositor submission is now classified per eye as
`VendorEvaluated`, `PresentationStretch`, `VendorFailureStretch`, or
`BoundsMismatchOriginalFallback`. Candidate observations are discarded when
OpenVR rejects the submission, and repeated queries do not create stress-ring
events or high-frequency logs. The controller retains the latest dimensions,
method, generation, epoch, context flags, consecutive-frame count, and
monotonic path counters; capture baselines make the complete record report only
paths observed inside that session. Starting a capture also resets only the
consecutive-path measurements, so its maximum presentation-stretch duration is
attributable to that scenario without discarding the monotonic totals.

`PresentationStretch` remains valid while a loading/menu presentation or
transition cooldown deliberately protects the compositor. It must recover by
capture stop to a fresh same-frame `VendorEvaluated` submission for both eyes
whose method, epoch, generation, input extent, and output extent exactly match
the stable physical contract. `VendorFailureStretch` and
`BoundsMismatchOriginalFallback` are capture failures. Schema v7 exposes the
live paths under `controller.presentation`, session deltas under
`presentationPath`, and the `presentation_fallbacks` and
`presentation_recovered` gates.

## MCP contract

Records use schema `community-shaders.vr-render-scale.iteration` and
`schemaVersion: 13`. Schema v13 adds presentation-stretch episode duration,
active-at-stop evidence, and incomplete-stereo-cycle evidence at capture stop.
An active tail or partial two-eye cycle is a hard failure. Schema v12 adds exact
build provenance. Schema v11 adds Debug-only liveness-cue compile/success
evidence and the emergency projection multiplier/floor; its acceptance
thresholds and units are unchanged. Schema v10 added
`session.coalescedDuplicateCount`. Same-door Stabilizer retries that match the
complete pending target are counted there without creating another request
event or transition metric. An automation client should:

1. Reject unknown schema versions.
2. Check `acceptance.accepted` before comparing performance.
3. Require `memoryTrend.evaluated` for a memory comparison; a short diagnostic pass is not memory acceptance.
4. Use `acceptance.gates` to classify a failed run instead of inferring failure from log text.
5. Compare transition records by `transitionEpoch`, never by array position alone.
6. Prefer lower stable latency and fewer retries only after correctness, fidelity, OOM, device-loss, retirement, memory-recovery, and backend-readiness gates pass.
7. Retain the complete JSON artifact with the candidate commit and scenario identifier.

The event ring retains 302 entries and the transition metrics ring retains 50 transitions. Consecutive retries with the same request, epoch, profile, controller state, pressure, vendor-work-gate state, and normalized retry/failure kind share one event: `frame` is the first observation, `lastFrame` is the latest, `occurrences` is the aggregate count, and the memory and transition counters reflect the latest observation. The packed vendor-work-gate state combines its raw source mask with an ownership epoch, so a newly reacquired source remains distinct from an older owner with the same mask. A capture-overflow gate still fails if distinct events overwrite the ring, and a metrics-coverage gate fails if any captured request epoch rotates out of the metrics ring. Keep each iteration within both bounds. Retry and failure kinds remain classifiable as pressure, retirement, backend, OOM, or device loss.

## Acceptance gates

The runtime currently requires:

-   a stopped capture containing at least two accepted requests;
-   an `Active` or `Idle` terminal controller with no transition still in flight;
-   no overwritten capture events and complete per-request metric coverage;
-   no backend failures, OOM, or device loss in either metrics or classified events;
-   no more than 32 retries for one transition;
-   at least one stable transition, no more than 120 frames to stability on the ordinary fast path, and no more than 3,600 frames when pressure backpressure is recorded;
-   zero fidelity invariant mismatches across method, applied generation, dimensions, evaluation, and eye symmetry, with finalized vendor evaluation proven for both eyes;
-   no compositor-accepted vendor-failure stretch or bounds-mismatch original fallback during the capture, and a terminal vendor profile recovered to a fresh, exact `VendorEvaluated` presentation for both eyes;
-   a fully drained retirement queue with no deferred cleanup frame, outstanding fence, or capacity block;
-   no failed or pending GPU-fenced common-target memory trim;
-   valid DXGI, Windows-commit, and Skyrim-private samples, with local pressure recovered below `High`, post-load recovery complete, and final system commit below the lower of 75 percent or an 8-GiB reserve;
-   no more than 256 MiB of local-video, system-commit, or Skyrim-private growth in both consecutive same-profile peak intervals once an exact backend profile has at least three completed samples;
-   the active DLSS or FSR backend ready with exact requested, runtime, and stable contract generations.
-   no effective lifecycle-mutation deferral remaining at capture stop, including relevant source gates, post-load reset ownership, or queued/in-progress relatch ownership; raw source owners that are irrelevant to the active render-scale lifecycle do not fail acceptance.

Schema version 9 added source-resolved vendor-work-gate status to the live
snapshot, complete record, and retained stress events. Change the current
schema version if an acceptance threshold's meaning or unit changes.

## Ghidra correlation

The record lists the principal native symbols under `analysis.symbols`. In Ghidra 12.1.2, correlate regressions with these paths first:

-   `Upscaling::ApplyPendingPerfModeRenderTargetRecreate` for admission, teardown, allocation, and retry behavior;
-   `Upscaling::ApplyPendingPostLoadRuntimeReset` for fast-travel recovery ownership;
-   `Upscaling::ResetVRVendorRuntimeResources` for DLSS/FSR lifetime differences;
-   `Upscaling::ServiceVRRenderScaleMemoryTrim` for fenced common-target residency recovery;
-   `QueryVRRenderScaleSystemCommit` for Windows commit and Skyrim-private sampling;
-   `Upscaling::TryPromoteVRRenderScaleSubmitStageContract` for stable-presentation latency;
-   `Upscaling::RecordVRRenderScaleFidelityObservation` for both-eye contract failures;
-   `Upscaling::RecordVRRenderScalePresentationObservation` for the actual compositor-accepted path and terminal presentation recovery;
-   `Upscaling::EnsureVRIntermediateTextures` and
    `Upscaling::AreVRIntermediateTexturesCompatibleForFSR` for Step 24 stable
    external-resource identity validation;
-   `FidelityFX::AreFSRResourcesCompatible`, `FidelityFX::CreateFSRResources`,
    and `FidelityFX::DestroyFSRResources` for FSR context lifetime validation.

Use Ghidra to validate control flow and ownership against the shipped binary, while using the JSON record as runtime evidence. A candidate should be promoted only when repeated scenario records pass and improve the target metric without regressing another accepted backend or pressure scenario.

# NVIDIA comparison follow-up: nv-mtu8nhph (2026-09-09)

The same-build NVIDIA assay retained 42 of 66 transitions: all 33 in
pass 1 and the first nine in pass 2. All retained render and per-row Task 2
checks passed. Execution is interrupted because the orchestration cell
became unavailable; the remaining 24 destinations are NOT RUN. Guarded
cleanup verified every task-owned capture inactive.

Matched pass-1 strict transition means are 794.846 ms current,
2569.260 ms for the preceding same-build attempt, and 787.217 ms for the
earlier September 9 baseline. Current latency is 69.06% below the preceding
attempt and 0.97% above the earlier baseline. These are transition timings,
not steady-state GPU costs. Memory confirmation is `repeat_not_completed`.

See [the run comparison](nvidia-renderscale-tuning-nv-mtu8nhph.md) and the
new `nv-mtu8nhph` ledger column. Full local evidence remains under
`artifacts/renderscale-tuning/nv-mtu8nhph/`; raw evidence is not versioned.

## September 10: PR75 c615779a9 baseline interruption

Run `renderscale-tuning-nvidia-2026-09-10T14-36-07-046Z` stopped before
the first measured transition. The DLSS Hoshipa baseline timed out after
20000.9316 ms / 525 frames with operation 1 WaitingForSafePoint; all
66 matrix transitions are NOT RUN. Captures and qualification are verified
inactive, while the worker cleanup flag and ownership lock remain retained.
Physical DLL/manifest/AIO Build ID cbef73dfc7a1 matches clean PR75 source
c615779a9, based on main-VR 7c8e3e656. Six journal records are flushed.
The ledger retains the complete baseline failure, finalized summary and
comparison with exact field reconstruction and historical-cell preservation.
The prior relevant main-VR reference contributes 528 audited numeric
timings; candidate matrix timings are unavailable. See the
[complete failure report](nvidia-renderscale-tuning-pr75-c615779a9-20260910.md).

Follow-up: the original operation completed after menu protection cleared
at 16:43:11 local time; the log records Active at 16:43:13.876. This was
outside the assay and does not change its interrupted result. Automation
cleanup bookkeeping is fixed and tested in local dev commit 9d87747; the
installed plugin is unchanged. The ledger preserves the full follow-up,
and the unchanged comparison was reused with 528 timing cells audited.

## September 10: PR75 NVIDIA two-pass completion versus main-VR

Run `nvidia-2026-09-10T17-12-40-813Z`, clean PR75 c615779a9 on main-VR
7c8e3e656, completed 33+33 transitions in game PID 41824. Terminal render
PASS; Task 2 counts 66/0/0; complete reporting and verified capture cleanup.
Full-history applicable health is MET/MET, with zero fidelity or vendor
fallback observations. The main-VR reference nvidia-20260910T124329625Z
had 4 fidelity and 2 vendor-failure eye observations in each pass, on
rows 26 and 28; both routes are clean in this candidate's two passes.
Strict means are 816.198/804.581 ms versus 866.009/833.078 ms, changes
-5.752%/-3.421%. Row 25 is slower in both passes. Memory is inconclusive.
The formal improvement-or-neutral assessment remains INCONCLUSIVE due to
different weather/game hour, unavailable fixture fingerprint and no declared
tolerance policy. Raw excluded diagnostic gates remain retained.

All summary/comparison fields, 66 transitions and both passes reconstruct
exactly from the canonical ledger; 1,056 paired numeric cells passed audit.
Prior attempt evidence and historical cells remain preserved. The user
authorized a PR75 update with means and SE in the style of PR65. See
[the complete PR75 comparison](pr75-nvidia-mainvr-comparison-20260910.md).

## September 10: PR66 a09e1cc77 NVIDIA two-pass completion

Run `renderscale-tuning-nvidia-2026-09-10T18-15-33-375Z`, clean PR66 a09e1cc77
on main-VR bf4ae54a7, completed 33+33 transitions in PID 44000. Terminal
render PASS; Task 2 counts 66/0/0; applicable health MET/MET; reporting
COMPLETE, captures inactive and journal flushed. Physical DLL, manifest
and AIO receipt match producer Build ID 9b08428afdaf.

Against the previous measured main-VR reference 7c8e3e656, strict means
are 793.336/807.126 ms versus 866.009/833.078 ms (-8.392%/-3.115%).
The reference's fidelity/vendor fallback observations are absent in both
passes. Row 25 remains slower in both passes. Retries are 9/10; all
32 selected-stretch transitions recovered. Memory and formal improvement
assessment remain inconclusive; scene/toolchain, fixture fingerprint and
tolerance-policy limitations remain explicit.

All summary/comparison fields reconstruct exactly from the canonical
ledger, including every transition, pass, health gate and memory field.
All 1,056 paired numeric timing cells passed audit, with historical cells
preserved. See [the full comparison](nvidia-renderscale-tuning-pr66-a09e1cc77-20260910.md).

## September 10: PR66 compared directly with measured PR75

The user-selected reference is PR75 run `nvidia-2026-09-10T17-12-40-813Z`,
compiled from c615779a9. Candidate `renderscale-tuning-nvidia-2026-09-10T18-15-33-375Z`
is PR66 a09e1cc77 on merged PR75/main-VR bf4ae54a7. All three PR66
patches match the older published 80f83d2dd head under git range-diff;
the measured and published binaries remain explicitly distinguished.

Mean strict completion is 800.231 ms against
810.390 ms (-1.254%). Pass deltas are
-2.801%/+0.316%; health is MET/MET for both builds, with
66 terminal PASS and Task 2 counts 66/0/0 each. The repeat's stretch
duration increased. Memory and formal improvement remain inconclusive.
Every route's means, SE and original pass timings are retained, including
higher timings. This comparison preserves the prior main-VR comparison.

All 1,056 numeric timing cells passed audit. Both complete summaries,
the complete PR75 comparison and unrounded statistics reconstruct exactly
from the canonical ledger; historical cells remain intact. The user
authorized inclusion in PR66. See [the detailed comparison](pr66-vs-pr75-nvidia-comparison-20260910.md).

## September 10: PR73 NVIDIA completion compared with measured PR66

Run `nvidia-20260910T214350263Z` completed 33+33 transitions in one
game process from exact PR73 head d9780bb74 on main-VR ef7c366dd.
Terminal render PASS; Task 2 counts 66/0/0; applicable health MET/MET;
reporting COMPLETE, captures inactive and journal flushed. The physical
DLL, adjacent manifest and AIO receipt match Build ID ac724629b68f.

The user-selected reference is measured PR66 a09e1cc77 on bf4ae54a7.
PR73 averages 870.905 ms against 800.231 ms
(+8.832%), with pass changes +8.795%/+8.867%.
Both runs have zero fidelity/vendor-fallback observations. All 31 PR73
selected-stretch transitions recovered; full-pass stretch duration is
higher in both passes. Memory and formal improvement-or-neutral assessment
remain inconclusive, with scene/toolchain, fixture and policy limits retained.

All 1,056 numeric timing cells passed audit. The complete summary,
comparison and unrounded mean/SE inputs reconstruct exactly from the
canonical ledger, preserving historical cells. The reporting CSV limit
was corrected with 20 passing regression tests. The user authorized the
PR73 description update using PR66's summary, SE and expandable tables.
See [the full PR73/PR66 comparison](pr73-vs-pr66-nvidia-comparison-20260910.md).

## September 11: PR73 cf1616728 interrupted during transition 26

Run `nvidia-2026-09-11T06-52-05-183Z` retained pass-1 rows 1-25, all terminal
render PASS and Task 2 counts 25/0/0. Transition 26, DLSS Hoshipa to FSR3
Hoshipa, lost its entire scenario response; dispatch and outcome remain
ambiguous. Pass1 27-33 and pass2 1-33 are NOT RUN. The game process was
subsequently absent; transport receipts do not establish its exit cause.
Cleanup failed to connect, capture shutdown is unverified, and the
ownership lock remains retained. The full journal is flushed.

Physical DLL/manifest/AIO identity matches clean PR73 cf1616728 on
main-VR ef7c366dd, Build ID a8d2e6a5f759. For the same first 25 routes,
strict mean is 805.170 versus 834.665 ms
(-3.534%) against measured main-VR 7c8e3e656. Nine retries and
14 recovered selected-stretch transitions are retained. Full-run health,
memory and formal improvement remain inconclusive; reporting is incomplete.

Complete available summary/comparison fields reconstruct exactly from
the canonical ledger, with 728 numeric timing cells audited and historical
cells preserved. Missing transition, pass-end and repeat evidence remains
explicit. See [the complete interrupted-run report](nvidia-renderscale-tuning-pr73-cf1616728-20260911.md).

### PR73 cf1616728: crash cause follow-up

The crash log now confirms PID 34484 suffered an execute access violation
at 08:55:59 local during row26. Renderer logs confirm request27/epoch28
was applied; this supersedes the initial receipt-only dispatch uncertainty.
The failure followed memory-relief cleanup and DLSS teardown while engine
render resources were being recreated from State::Draw. The only runtime
change since successful c73bae9a7 is cf1616728 pending-drain polling, making
it a suspected regression; the invalid control-flow target is not yet
attributed to a specific source defect. Full logs, exact runtime diff and
findings are preserved in the existing run directory and canonical ledger.
Original summaries/comparisons remain unchanged; complete reconstruction
and all 728 timing cells passed the follow-up audit. See the
[updated report](nvidia-renderscale-tuning-pr73-cf1616728-20260911.md).

## September 11: PR73 readiness-deferral correction

The PR73 follow-up separates proven pre-mutation provider readiness from
other backend retries. Eligible immutable settings transitions poll after
one frame and retain proof-driven settling after successful teardown.
Mixed retries, failures, prior cleanup, memory relief, partial mutation,
quarantine and recovery retain the existing conservative behavior. All
waits remain counted, with a separate readiness subset in DevBench metrics.
See [the readiness contract](vr-submit-input-contracts.md#readiness-during-render-scale-changes).

The prior NVIDIA comparison belongs to compiled source `d9780bb74` and
remains pre-correction evidence. This implementation update adds no new
runtime measurement or timing to the comparison ledger. Policy tests and
universal compiler checks cannot establish post-fix performance or visual
robustness; the updated source still needs runtime tuning and the separate
`csx-render-scale-pr-v1` qualification with a matching accepted baseline.

## September 11: restore the measured PR73 readiness implementation

The pending-drain polling follow-up `cf1616728` is reverted at the user's
request. Runtime source and policy tests return exactly to the successful
`c73bae9a7` implementation, retaining its original conservative handling of
memory relief and Backend retries. No timing adjustment is substituted.

The later build crashed in pass 1, transition 26 (DLSS HoshiPa to FSR3
HoshiPa), after 25 completed transitions. Run
`nvidia-2026-09-11T06-52-05-183Z` and its crash logs remain preserved locally.
The exact invalid-pointer cause is unproven; the rollback restores tested
behavior without claiming the six-frame delay proves resource safety.

The latest performance data in PR73 still belongs to `c73bae9a7`: 66/66
terminal PASS, Task 2 66/0/0, and applicable health MET in both passes. The
PR66 comparison and all historical ledger cells remain unchanged. Formal
improvement assessment remains INCONCLUSIVE, and the separate render-scale
release qualification is unrun. See [the rollback record](pr73-polling-rollback-20260911.md).

## September 11: PR73 owned provider drain candidate

Source `269bded159c66f4d8b1a45a836078dfa6cdf3241` separates provider
drain observation from relatch mutation and services explicit settings
relatches after the owning native stereo call and both matching eye calls
return. Drain evidence belongs to
the exact transition, generations and provider resource revision; subsequent
provider use invalidates it. Readiness can advance only the operation's own
queued six-frame retry. A still-pending drain at the original six-frame
deadline returns to the conservative path for that epoch.

The six-frame post-replacement settling guard, its eligibility rules and
Backend retry history remain unchanged. Provider observation does not
repeat cleanup or teardown. A completed shared cleanup is retained by its
owner, while independent retirement fences, memory checks and recovery
restrictions continue to apply. This avoids the withdrawn attempt's
unqualified acceleration of the entire destructive transaction.

The owned path applies only where the existing readiness predicate already
requires Backend handling. Eligible `PreMutationReadiness` requests retain
their existing path and settling eligibility.

Nine focused portable policy tests pass with MSVC C++23 `/W4 /WX`;
the tested source/header hashes were stable throughout. Five production
translation units pass DevBench-disabled universal syntax checks, including
a final `Upscaling.cpp` recheck after the eligibility correction. Changed
C++ ranges and new files pass the pinned formatter; scoped hooks pass.
Whole-file legacy C++ and CMake formatting remains excluded. The clean
universal Release DLL and DevBench-enabled AIO pass package and producer
verification; shader tests pass 163 assertions in 1 test case.
Build ID: `9b8b7f17af7ec7012eb194ad945b45756db313352ef6b2ec84d88a50bef9247b`.
The archive is ready for manual testing. No automatic deployment, runtime
or performance result is claimed. Historical comparison ledger cells remain
unchanged. Separate `csx-render-scale-pr-v1` qualification remains unrun.
Manual coverage must include None/TAA-to-vendor and menu/native-AA callback
liveness as well as both passes and transitions 26 and 28.
See [the candidate contract and exact validation](pr73-owned-drain-manual-test-20260911.md).

## September 11: PR73 269bded15 NVIDIA measurement

Run `nv-pr73-269bded1-mtwz6pez` completed 66 transitions across both NVIDIA passes:
terminal PASS, Task 2 66/0/0,
reporting COMPLETE, and applicable health standards
MET, MET.
The owned drain path ran 18 times and observed readiness one frame after
Pending; every observed settling guard still satisfied at age six frames.
The four earlier long-stretch cases improved, while their previously short
counterpart passes regressed. Neither comparison establishes an overall
improvement; see their exact assessments and retained context limits in
[the measured report](nvidia-renderscale-tuning-pr73-269bded15-20260911.md).

The reporter's unknown-event defect was repaired offline from unchanged
raw evidence. The complete summary, both comparisons and all numeric
timings are preserved in the existing ledger; historical cells remain
unchanged. Owned capture cleanup and journal flush were verified before
the worker exited. A later external game/MO2 exit remains a separate
diagnostic with no established cause. Release qualification remains unrun.

## September 11: PR73 exact parent and drain failure coverage

The CI harness repair `e74c5c427` restores the missing drain hook and
removes a fixture-only shadowed member. Its strict MSVC build and all
12 focused controller tests passed. Follow-up `fa778f63d` adds eight
passing controller scenario groups for owned-drain deadline, failure,
invalidation and cleanup behavior, with scripted external dependencies.

Exact parent `bc077786d` is now built with the candidate's canonical
toolchain, options, dependencies and shader-cache ABI. Both archives and
their identities are verified. The operator retains deployment, MO2 and
game startup; no new runtime measurement or visual qualification was made.
Existing ledger timings remain unchanged. See the
[parent and failure validation record](pr73-exact-parent-validation-20260911.md)
for exact Build IDs, test evidence, warnings and remaining hardware scope.

Future reporting explicitly selects corrected automation `c900f176` and
finalizes each new candidate from its journal. The installed live worker
does not finalize reports; choosing a toolkit alone does not regenerate
an existing summary.

## September 11: PR73 exact parent bc077786d NVIDIA tuning

Run `renderscale-tuning-nvidia-20260911T153415138Z` completed all
33 + 33 transitions in one process. Terminal counts are 66 PASS /
0 FAIL; Task 2 counts are 66 PASS / 0 FAIL /
0 INCONCLUSIVE without an aggregate verdict.
Both passes meet applicable full-history health checks with zero counted
failure observations. Reporting is COMPLETE; captures are inactive and
the journal has zero pending evidence.

The physical DLL, adjacent manifest and AIO receipt match clean Release
source/renderer bc077786db08637eec8b4c3f718e971e58700a60,
main-VR base ef7c366dd73989b2b87751c0ef975db7c6fd310f and runtime
Build ID 0786ca167c161b413ace0cc8ef7d3a76907ef7b36ac4763c8bc3ed9ebb39cfd1. No reporting backport was applied.
Against user-pinned PR66 a09e1cc77 on main-VR bf4ae54a7, strict means are
942.965 / 807.993 ms (+18.861% /
+0.107%). Eighteen routes are slower in both passes. Pass 1's largest increase is row
14 (+825.483 ms);
pass 2's is row 15
(+444.418 ms).

Retries are 10 / 9. All
32 selected stretch
transitions recovered. Full-pass stretch is 84 /
80 frames and
5988.474 /
5198.280 ms, with no active tail.
Raw cumulative acceptance remains false: the fixed stretch cutoff is
DIAGNOSTIC_ONLY and the proven-native terminal gate a CONTRACT_MISMATCH.
No applicable health gate fails. Change assessment and memory confirmation
remain separately INCONCLUSIVE; scene/toolchain context differs, fixture
matching and an explicit tolerance policy are unavailable. Profiler totals
are unavailable timings because both passes have zero resolved samples.
Feedback AUTO-20260911-154621563-951D86E2 retains this reporting defect.
The recorded local-metadata startup deviation added no DevBench call,
mutation or measured dispatch gap.

Exact ledger reconstruction passed for every summary, comparison and
supplemental field; all 1,056 paired timing
cells pass and every historical cell is preserved. Comparison took
4.431 s; complete ledger reporting took
17.037 s including comparison. Separate finalization
elapsed time was not instrumented and is unavailable. See the
[durable report](nvidia-renderscale-tuning-pr73-parent-bc077786d-20260911.md) for every pass/route comparison, actual
relatch/strict frames and milliseconds, stretch, retry reasons, gates,
memory, complete provenance and validation receipts. The
[canonical ledger](vr-render-scale-ledger.md) retains the full
results; raw evidence remains local and PR inclusion is the user's decision.

## September 11: PR73 parent bc077786d repeat at 15:54 UTC

Run `renderscale-tuning-nvidia-20260911T155459454Z` completed
66 / 66 transitions. Terminal counts are 66 PASS /
0 FAIL; Task 2 counts are 66 / 0 / 0
(PASS / FAIL / INCONCLUSIVE), without an aggregate Task 2 verdict. Full-history
health is NO_COUNTED_FAILURES; reporting is COMPLETE.
Captures are inactive and all evidence is flushed. The physical
DLL/manifest/AIO receipt matches source `bc077786db08637eec8b4c3f718e971e58700a60`,
main-VR `ef7c366dd73989b2b87751c0ef975db7c6fd310f` and Build ID
`0786ca167c161b413ace0cc8ef7d3a76907ef7b36ac4763c8bc3ed9ebb39cfd1`.

Pass 1 strict mean is 905.409 ms: +14.127%
against pinned PR66 and -3.983% against the previous same-build
run. Pass 2 strict mean is 998.312 ms: +23.687%
against pinned PR66 and +23.554% against the previous same-build
run. Passes remain separate. PR66 assessment is
INCONCLUSIVE; same-build assessment is INCONCLUSIVE.
Memory evidence is complete; memory verdict is inconclusive.
Both complete comparisons retain context limits, every route and
pass, retries, actual relatch/strict frames and milliseconds, stretch
and full-history health. Exact ledger reconstruction passed for the
summary, both comparisons and supplementals; historical cells and
numeric timing coverage are preserved. Complete ledger reporting
took 79.393 s. See the
[durable repeat report](nvidia-renderscale-tuning-pr73-parent-bc077786d-20260911-repeat-155459.md) and
[canonical ledger](vr-render-scale-ledger.md) for full
results, diagnostics, provenance and exact validation. The previous
report remains unchanged; raw evidence remains local.

Pacing remains EXCEEDED: exact QPC cadence beyond
the five-second server wait reached 344.0369 ms,
with 3 / 64 intervals above the 250 ms budget. The
separate client-side maximum was 44.6864 ms.
Full offending intervals and feedback receipts remain in the
report and ledger without rewriting runtime classifications.

## September 11: PR73 owned release 554e484e3 NVIDIA tuning

Run `renderscale-tuning-nvidia-2026-09-11T17-09-55-165Z` completed 33 + 33 transitions.
Terminal counts are 66 PASS / 0 FAIL. Task 2 counts are 66 PASS / 0 FAIL /
0 INCONCLUSIVE, without an aggregate verdict. Full-history health is
NO_COUNTED_FAILURES and reporting COMPLETE. All captures are inactive and
the journal is flushed. Clean Release source/renderer
`554e484e3957178e2d144bf35266bfdcc0948642`, main-VR base
`ef7c366dd73989b2b87751c0ef975db7c6fd310f` and Build ID
`e7e9fa2d13f28c6727b9567741511a418c54113c190db2616bfb2c2ad1158d96` match the physical DLL, manifest and AIO receipt.

Strict means are 815.094 / 768.534 ms, +2.743% / -4.781% versus pinned
PR66. Each pass has 15 retries and 18 stretch episodes totaling 62 frames;
stretch durations are 4080.674 / 4085.360 ms. All 32 selected stretch
transitions recovered. Both applicable health standards are MET; the raw
fixed stretch/native-target gates retain their diagnostic classifications.
Comparison assessments and memory classification remain INCONCLUSIVE.
Profiler totals have no resolved samples; feedback
AUTO-20260911-154621563-951D86E2 was amended with this run.

PR73 was updated first at the user's request. The complete summary, both
comparisons and supplemental records reconstruct exactly from the existing
ledger; all 528 candidate timing cells and 1,056 cells per comparison pass
the timing audit. Historical cells are preserved. Ledger reporting took
33.558 seconds and reused both comparisons after hash checks.
See the [full durable report](nvidia-renderscale-tuning-pr73-554e484e3-20260911.md) for every pass and transition,
actual strict/relatch frames and times, stretch, retries, owned release,
memory, complete gates, provenance, limits and evidence links. The
[canonical ledger](vr-render-scale-ledger.md) retains all data;
raw evidence remains local.

## September 11: owned completion preserves presentation proof

The [focused PR73 correction](pr73-owned-release-proof-20260911.md)
separates an exact consumed provider drain from new-target preparation and
coherent stereo presentation. One observed owned Backend wait can retain
proof-driven release only after successful reset, precise detached
retirement accounting and exact physical publication. Raw retry history,
native stereo mutation boundaries and the six-frame fallback remain.
Seventeen focused tests passed; live comparison and visual qualification
remain unrun, so this is not a measured performance or release-readiness
claim. No runtime ledger cells were added or changed.

The overall integration parent is `ef7c366d`; `bc077786d` is the narrower
owned-drain change parent. A clean DevBench-enabled `ef7c366d` AIO now
passes build, shader and archive identity checks with the measured
candidate's recipe. Its canonical identity differs only in source commit.
The operator retains deployment, MO2 and game startup.

### PR73 owned-release correction: clean build verification

Source `554e484e3` now has verified universal DevBench ON and OFF builds,
75/75 CI controller tests, and 163 package shader assertions. The ON AIO
contains neither FOMOD nor a prebuilt shader cache. Production compile/link
settings match measured `269bded15` and exact integration parent `ef7c366d`;
all three archives are preserved for manual comparison. Reporter `9e3a0a9e`
is integrated into local automation dev. See the exact Build IDs, hashes,
commands and limitations in
[the correction report](pr73-owned-release-proof-20260911.md#final-clean-build-evidence).
No new runtime measurements or visual qualification are claimed, and no
runtime ledger cells were changed by this offline validation.
