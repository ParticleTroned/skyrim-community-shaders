# Depth-culling contribution to the main-VR CPU/GPU regression

This appendix retains the first seven, culling-enabled runs. Read the
[completed eight-run handover](README.md) and [subsequent off control](depth-off.md)
for the final recommendation. Local evidence paths below are locators;
their derived results and provenance are retained in ledger 0004.

Completed 16 September 2026. **The objective remains lower main-VR CPU cost and frame-time noise, plus lower GPU cost with DLSS enabled. Depth culling is one candidate, not the whole problem.** RC166 is only the user's illustrative counterexample; it is not substituted for the established benchmark baseline. Save 13 is one diagnostic case, not the optimization target by itself.

The seven culling-mode runs show no repeatable overall winner. Balanced recovery has a small direct measured cost; native frustum work varies by scene and by executing thread. Other CPU work and the DLSS GPU path remain separate unresolved targets. The later depth-off control establishes an observed net benefit from keeping culling enabled; see [the completed on/off analysis](depth-off.md). Balanced is not established as the fastest mode.

This comparison concerns culling modes on one build, not the original pre-regression baseline. Balanced 1 is the reference for the deltas below. All seven runs used the unchanged gameft-sw protocol with WPR enabled. No production code, settings, measurement formulas, or measurement windows were changed during this analysis.

[Full fpsVR table: means, P95/P99, spikes, saved stability and health](tables.md) | [48-window WPR metrics CSV (including the later depth-off control)](wpr-metrics.csv)

The earlier generated comparison assigned sorted save labels to execution-order rows. That presentation error is corrected using actual receipt names: execution order 08, 11, 09, 12, 10, 13. All 42 names now agree with the saved WPR windows. Source measurements were unchanged. The superseded output is retained separately for audit.

## What culling explains, and what it does not

Three distinct costs must stay separate: (1) native scene/frustum traversal on the CPU, (2) native depth/occlusion processing that can save subsequent rendering, and (3) CSX Balanced temporal recovery. The live Ghidra ranges identify a few general sphere/frustum routines, not the entire culling pipeline or a GPU pass. All three selectable modes retain native depth culling. Switching among them cannot by itself answer whether native depth culling is worth its CPU/GPU overhead.

| Save / mode | Baseline render CPU | Corrected render CPU | Delta  | Baseline all threads | Corrected all threads | Delta  |
| ----------- | ------------------- | -------------------- | ------ | -------------------- | --------------------- | ------ |
| 08 / DLAA   | 0.040               | 0.032                | -0.008 | 0.087                | 0.076                 | -0.011 |
| 09 / DLAA   | 0.000               | 0.000                | +0.000 | 0.013                | 0.020                 | +0.007 |
| 10 / DLAA   | 0.000               | 0.000                | +0.000 | 0.000                | 0.000                 | +0.000 |
| 11 / DLSS   | 0.000               | 0.000                | +0.000 | 0.542                | 0.458                 | -0.084 |
| 12 / DLSS   | 0.000               | 0.201                | +0.201 | 0.478                | 2.115                 | +1.638 |
| 13 / DLSS   | 0.000               | 0.627                | +0.627 | 3.956                | 3.715                 | -0.241 |

This table reuses the established matched cross-build analysis, not a newly selected baseline: compiled baseline a1a11fe0d722fd6dbc18315a023701e0ec4a84de (renderer base 190c28a39a52c2bace475d1143a124c5893ac741), and corrected source 933a4e2540f962ddc3d2ce656523d1f8c5982ef4. The latter has the same Git tree as 503fbfc92b5a9a58f5ba14a2343c3b0b119c40dc; its distinct runtime/build identity is retained. Values are sampled CPU ms per recorded frame in only the three identified native ranges. All-thread CPU is summed work, not elapsed frame time. Zero sampled weight is not proof of zero execution. [Full established cross-build report](../cpu-dlss-regression-handover-20260916.md).

**Save 12 provides evidence of increased total work in these frustum routines (+1.638 summed CPU ms/frame), while Save 13 shows a change in thread distribution: about +0.627 ms/frame on the rendering thread, but -0.241 across all threads.** The earlier statement about no baseline matches applied to the rendering thread, not the whole process. Thus the native hotspot on Save 13 cannot be called newly introduced total culling work. Saves 08–11 do not show a broad increase in these identified routines. This is why the remaining CPU regression must be investigated beyond culling.

Save 10 is a useful noise control in the new campaign: native frustum ranges have no observed samples, yet CPU P99 is 10.1–11.7 ms and the saved isolated-spike rate is 6.7–25.8/s across the seven runs. Legacy 1 has 24.4 isolated spikes/s there with recovery disabled. This contradicts recovery being necessary for the general noise pattern, although unmeasured/inlined culling work is not excluded. Preserve this common CPU-noise issue separately from the scene-dependent native-frustum contribution.

The established cross-build GPU deltas are -0.726/-0.310/-0.548 ms in DLAA saves 08/09/10, versus +0.556/+0.956/+1.622 ms in DLSS saves 11/12/13. This mode-dependent residual remains a separate priority. WPR CPU stacks do not measure GPU pass duration. Existing late DLSS counters show matching input/guide/copy/sanitization/vendor-eye counts and no persistent freshness rejection or duplicate retry; they do not establish that preparation, copies or inference are cheap. One evaluation per eye is expected.

## Ranked work to reduce main-VR cost

| Track                               | Evidence now                                                                                                                   | Next discriminating check / safe optimization boundary                                                                                                                                                                                                                               |
| ----------------------------------- | ------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| CPU floor and general noise         | Distributed CSX/native/D3D cost; established DLAA CSX leaf delta +0.316–0.448 ms/frame. Noise persists with recovery disabled. | Compare recurring slow and normal frames across saves, focusing on actual render/draw state work and worker interference. Preserve the PR93 ownership correction; do not restore per-call D3D protection.                                                                            |
| Native frustum/culling contribution | Save 12 total frustum work increases; Save 13 render-thread share increases without greater total frustum work.                | Measure effective culling state, calls/objects/passes and thread distribution. A same-build Legacy-with-depth-culling-on/off control can test net benefit while keeping temporal recovery disabled. This control subsequently completed; see [the depth-off analysis](depth-off.md). |
| DLSS GPU residual                   | All three recorded DLSS scenes remain slower on GPU; corresponding DLAA scenes do not.                                         | Obtain GPU timings for preparation/copies, guides, vendor evaluation and final composition, plus queue gaps. CPU sample weights cannot choose the guilty GPU stage. Keep colour-managed prepared input readable and retain two expected eye evaluations.                             |
| Safe material admission             | About 0.028–0.128 sampled ms/frame in the established corrected build.                                                         | Audit repeated checks of the same current draw and prove equivalent-safe consolidation. This is worth reducing but cannot explain the full residual. No stale pointer cache or removal of malformed-material checks.                                                                 |
| Light ownership and storage         | Named retention is about 0.014–0.022 sampled ms/frame; total inlined reference/locking cost is not fully isolated.             | Only optimize measured allocation/hash/reference repetition while retaining ownership until raw-pointer consumers finish.                                                                                                                                                            |
| Balanced recovery                   | Clean full-run timed scope totals 12.06–14.73 ms; downstream draws not attributed.                                             | Demoted as the sole broad regression. Measure per-save promotion and draw consequences before changing its 64-object selection or conservative motion bounds.                                                                                                                        |

Net culling benefit requires both sides: time spent doing culling and time saved by rejected rendering. The subsequent on/off comparison supplies observed net frame-time effects, while CPU-only WPR cannot attribute individual GPU pass costs. Use an explicitly agreed separate GPU/culling diagnostic capture if needed; the saved gameft-sw protocol and its frame-time formulas remain unchanged. Optimize the proven cost with one variable per build, then validate against the existing baseline across DLAA and DLSS saves, including means, tails, noise, rendering health and visual correctness.

## CPU means: final 10 seconds, milliseconds

| Save / mode | Balanced 1 | Performance     | Legacy 1        | Balanced 2\*    | Balanced 3      | Balanced 4      | Legacy 2        |
| ----------- | ---------- | --------------- | --------------- | --------------- | --------------- | --------------- | --------------- |
| 08 / DLAA   | 4.766      | 5.291 (+0.525)  | 5.477 (+0.711)  | 5.370 (+0.604)  | 5.780 (+1.014)  | 5.757 (+0.991)  | 5.768 (+1.002)  |
| 09 / DLAA   | 6.116      | 5.849 (-0.267)  | 5.804 (-0.312)  | 6.004 (-0.112)  | 6.000 (-0.116)  | 5.937 (-0.179)  | 5.919 (-0.197)  |
| 10 / DLAA   | 7.390      | 7.066 (-0.324)  | 7.184 (-0.206)  | 6.918 (-0.472)  | 6.433 (-0.957)  | 6.175 (-1.215)  | 5.935 (-1.455)  |
| 11 / DLSS   | 8.753      | 8.603 (-0.150)  | 8.687 (-0.066)  | 8.668 (-0.085)  | 8.778 (+0.025)  | 8.782 (+0.029)  | 8.238 (-0.515)  |
| 12 / DLSS   | 8.826      | 8.412 (-0.414)  | 8.467 (-0.359)  | 8.676 (-0.150)  | 8.925 (+0.099)  | 8.759 (-0.067)  | 9.048 (+0.222)  |
| 13 / DLSS   | 13.410     | 17.565 (+4.155) | 13.199 (-0.211) | 18.764 (+5.354) | 19.769 (+6.359) | 19.693 (+6.283) | 18.174 (+4.764) |

## GPU means: final 10 seconds, milliseconds

| Save / mode | Balanced 1 | Performance     | Legacy 1        | Balanced 2\*    | Balanced 3      | Balanced 4      | Legacy 2        |
| ----------- | ---------- | --------------- | --------------- | --------------- | --------------- | --------------- | --------------- |
| 08 / DLAA   | 9.270      | 9.642 (+0.372)  | 9.652 (+0.382)  | 9.799 (+0.529)  | 9.814 (+0.544)  | 9.857 (+0.587)  | 9.828 (+0.558)  |
| 09 / DLAA   | 8.294      | 8.601 (+0.307)  | 8.576 (+0.282)  | 8.094 (-0.200)  | 7.950 (-0.344)  | 7.965 (-0.329)  | 7.868 (-0.426)  |
| 10 / DLAA   | 9.244      | 9.261 (+0.017)  | 9.205 (-0.039)  | 8.761 (-0.483)  | 8.361 (-0.883)  | 8.494 (-0.750)  | 8.055 (-1.189)  |
| 11 / DLSS   | 8.434      | 8.721 (+0.287)  | 8.758 (+0.324)  | 8.423 (-0.011)  | 8.195 (-0.239)  | 8.027 (-0.407)  | 8.082 (-0.352)  |
| 12 / DLSS   | 9.248      | 9.198 (-0.050)  | 9.250 (+0.002)  | 8.971 (-0.277)  | 8.868 (-0.380)  | 8.797 (-0.451)  | 8.866 (-0.382)  |
| 13 / DLSS   | 12.322     | 13.288 (+0.966) | 12.686 (+0.364) | 13.093 (+0.771) | 13.421 (+1.099) | 13.128 (+0.806) | 12.429 (+0.107) |

**DLAA:** Save 08 gets slower across the sequence, Save 09 is comparatively steady, and Save 10 gets faster even among repeated Balanced runs. This is not a consistent mode-specific signature. **DLSS:** Saves 11/12 have mixed changes; Save 13 is the outstanding CPU problem. Its clean Balanced CPU range is 13.410–19.769 ms and Legacy range is 13.199–18.174 ms. These spreads exceed the previously suggested 0.35 ms CPU / 0.15 ms GPU indicative variation bands. Those bands are not statistical confidence intervals. Different saves are different workloads, not independent replicates to pool into a mean and standard error.

The latest adjacent comparison, Legacy 2 minus Balanced 4, is -1.519 ms CPU and -0.699 ms GPU on Save 13. That is a potentially useful result, but a single pair with different worker load does not isolate a culling-mode benefit. The first Balanced run was also fast. Performance mode was slow on Save 13 despite doing no recovery.

## Save 13: WPR separates execution from waiting

| Run          | fpsVR CPU | Render running | Render ready | Render waiting | Native frustum, all threads | Native frustum, render |
| ------------ | --------- | -------------- | ------------ | -------------- | --------------------------- | ---------------------- |
| Balanced 1   | 13.410    | 16.272         | 0.049        | 0.804          | 3.984                       | 0.555                  |
| Performance  | 17.565    | 17.468         | 0.048        | 0.472          | 3.973                       | 0.661                  |
| Legacy 1     | 13.199    | 15.725         | 0.048        | 1.251          | 4.126                       | 0.316                  |
| Balanced 2\* | 18.764    | 17.643         | 0.049        | 0.393          | 4.523                       | 0.777                  |
| Balanced 3   | 19.769    | 17.983         | 0.118        | 0.390          | 4.268                       | 0.742                  |
| Balanced 4   | 19.693    | 17.413         | 0.048        | 0.366          | 4.072                       | 0.676                  |
| Legacy 2     | 18.174    | 17.039         | 0.095        | 0.350          | 4.529                       | 0.772                  |

All values above are ms per recorded fpsVR frame; WPR running/ready/waiting totals are clipped to the exact same 10-second window and divided by its raw frame count. They are not the fpsVR CPU-frametime definition. Native-frustum values are sampled CPU execution, summed across the stated threads, not wall-clock delay. Rendering-thread ready time is only 0.048–0.118 ms/frame. Slower runs generally have more execution and less waiting, rather than a large new render-thread lock wait.

The native sphere/frustum regions remain approximately 3.97–4.53 ms/frame across all threads on Save 13, including Legacy. More of that work appears on the rendering thread in several slower runs, but the relatively small aggregate difference cannot explain the whole CPU increase. These are native scene/frustum routines, not proof that CSX depth recovery called them. Native culling remains enabled in Legacy; zero recovery counters do not mean zero native culling work.

## Save 13: where the extra sampled CPU occurs

| Run          | Native Skyrim | CSX   | D3D11 | CBP   | HDT-SMP | Kernel | Kernel under CBP/HDT parents | All process threads |
| ------------ | ------------- | ----- | ----- | ----- | ------- | ------ | ---------------------------- | ------------------- |
| Balanced 1   | 21.819        | 2.222 | 1.519 | 1.666 | 0.029   | 4.652  | 1.526                        | 43.049              |
| Performance  | 24.845        | 2.398 | 1.591 | 2.067 | 0.033   | 5.523  | 1.889                        | 48.863              |
| Legacy 1     | 19.163        | 2.223 | 1.476 | 0.832 | 0.036   | 3.599  | 1.155                        | 37.274              |
| Balanced 2\* | 24.978        | 2.376 | 1.678 | 2.038 | 0.025   | 5.499  | 1.883                        | 48.903              |
| Balanced 3   | 24.968        | 2.496 | 1.646 | 1.967 | 0.800   | 9.635  | 5.726                        | 54.273              |
| Balanced 4   | 23.980        | 2.435 | 1.631 | 1.851 | 0.032   | 5.155  | 1.643                        | 47.447              |
| Legacy 2     | 24.930        | 2.218 | 1.488 | 1.724 | 3.229   | 23.334 | 18.540                       | 69.023              |

These are exclusive sampled leaf-module CPU weights summed over Skyrim threads, normalized by recorded frame count. The kernel-parent column is a subset of Kernel and must not be added again. In Legacy 2, kernel samples with CBP/HDT-SMP ancestors account for about 18.54 ms of summed worker CPU per recorded frame, versus about 1.15 ms in Legacy 1. The stacks pass through the physics modules into KernelBase/ntdll/kernel code. This demonstrates very different worker activity, not the exact blocked API or proof that physics explains every fpsVR spike. Public kernel and private physics symbols were unavailable. Profiling/context-switch overhead may also scale with that activity despite an identical WPR profile.

The native increase is spread over multiple ranges. Relative to Legacy 1, the 0x2BC000 page contributes approximately +0.335 ms/frame in Balanced 4 and +0.422 in Legacy 2; 0xDA5000 contributes more rendering-thread execution in both. Four-KiB pages are address buckets, not identified functions. The preserved contexts JSON contains the full page distribution and worker-thread weights. No speculative native patch is justified by those buckets.

## Direct Balanced recovery cost

| Run          | Attempts | Objects scanned | Promotions | Total scope ms | Mean per attempt ms | Max attempt ms |
| ------------ | -------- | --------------- | ---------- | -------------- | ------------------- | -------------- |
| Balanced 1   | 124      | 497603          | 7936       | 14.7335        | 0.1188              | 0.1884         |
| Performance  | 0        | 0               | 0          | 0.0000         | not applicable      | 0.0000         |
| Legacy 1     | 0        | 0               | 0          | 0.0000         | not applicable      | 0.0000         |
| Balanced 2\* | 502      | 1863747         | 32064      | 52.3297        | 0.1042              | 0.3021         |
| Balanced 3   | 106      | 424159          | 6784       | 12.0627        | 0.1138              | 0.2669         |
| Balanced 4   | 112      | 448046          | 7168       | 13.0449        | 0.1165              | 0.1637         |
| Legacy 2     | 0        | 0               | 0          | 0.0000         | not applicable      | 0.0000         |

These counters span each complete recorded run, including transitions and other time outside the comparison tails. Clean Balanced runs spent only 12.06–14.73 ms total in the timed recovery scope, averaging 0.114–0.119 ms per attempt. That cannot directly explain a sustained several-ms/frame CPU difference. The scope excludes earlier pose/coherence bookkeeping, final counter publication, and the downstream cost of rendering promoted objects. It is not a bound on all culling or rendering cost.

No samples explicitly named RecoverHighRiskObjects or CaptureProducerPose appeared in the matched tails; sampling/inlining prevents interpreting this as zero execution. Inclusive temporal-hook contexts, which also contain native child calls, were at most about 0.067 ms/frame in these tails. Every clean Balanced recovery attempt reached the 64-object promotion cap. Many more candidates were eligible. This is evidence to examine visual coverage and downstream draw cost, not evidence that the cap should be increased or the coherence threshold loosened. No invalid transform or motion-envelope counters were reported.

## Native frustum sample cost across saves

| Save / mode | Balanced 1 | Performance | Legacy 1 | Balanced 2\* | Balanced 3 | Balanced 4 | Legacy 2 |
| ----------- | ---------- | ----------- | -------- | ------------ | ---------- | ---------- | -------- |
| 08 / DLAA   | 0.082      | 0.095       | 0.087    | 0.092        | 0.073      | 0.082      | 0.072    |
| 09 / DLAA   | 0.024      | 0.020       | 0.013    | 0.012        | 0.010      | 0.011      | 0.009    |
| 10 / DLAA   | 0.000      | 0.000       | 0.000    | 0.000        | 0.000      | 0.000      | 0.000    |
| 11 / DLSS   | 0.455      | 0.386       | 0.460    | 0.483        | 0.482      | 0.463      | 0.634    |
| 12 / DLSS   | 2.219      | 2.189       | 2.295    | 1.914        | 1.572      | 1.493      | 0.823    |
| 13 / DLSS   | 3.984      | 3.973       | 4.126    | 4.523        | 4.268      | 4.072      | 4.529    |

Units are summed CPU ms per recorded frame, not GPU depth-culling time. Save 12 native frustum cost decreases substantially during the sequence even while its fpsVR CPU mean does not consistently fall. Save 13 stays expensive in every mode. This further argues against using one sampled culling hotspot as a substitute for the complete frame-time result.

## Tail spikes and percentiles: Save 13

| CPU run      | Mean ms | P95 ms | P99 ms | Single spikes/s | All spike events/s | Above-threshold samples % |
| ------------ | ------- | ------ | ------ | --------------- | ------------------ | ------------------------- |
| Balanced 1   | 13.410  | 22.300 | 26.417 | 0.7             | 2.4                | 30.82                     |
| Performance  | 17.565  | 26.525 | 29.145 | 0.2             | 3.8                | 41.01                     |
| Legacy 1     | 13.199  | 24.065 | 26.700 | 0.7             | 2.2                | 36.39                     |
| Balanced 2\* | 18.764  | 26.600 | 28.900 | 0.2             | 4.0                | 40.87                     |
| Balanced 3   | 19.769  | 27.400 | 29.860 | 0.5             | 6.0                | 39.37                     |
| Balanced 4   | 19.693  | 27.100 | 29.600 | 0.2             | 3.7                | 36.19                     |
| Legacy 2     | 18.174  | 26.100 | 28.200 | 0.4             | 3.4                | 41.26                     |

| GPU run      | Mean ms | P95 ms | P99 ms | Single spikes/s | All spike events/s | Above-threshold samples % |
| ------------ | ------- | ------ | ------ | --------------- | ------------------ | ------------------------- |
| Balanced 1   | 12.322  | 14.885 | 17.819 | 3.0             | 3.4                | 9.08                      |
| Performance  | 13.288  | 16.625 | 19.300 | 5.6             | 6.6                | 13.85                     |
| Legacy 1     | 12.686  | 14.965 | 16.665 | 3.3             | 3.7                | 6.97                      |
| Balanced 2\* | 13.093  | 16.240 | 18.200 | 5.5             | 6.2                | 13.38                     |
| Balanced 3   | 13.421  | 16.900 | 19.300 | 5.9             | 6.4                | 12.38                     |
| Balanced 4   | 13.128  | 16.300 | 18.500 | 6.6             | 6.8                | 12.48                     |
| Legacy 2     | 12.429  | 15.700 | 17.629 | 6.1             | 6.2                | 11.01                     |

All use the unchanged saved final-10-second rules. A spike is at least the same tail median +2 ms; an event is a consecutive run above that threshold, including one-sample events. The separate single-spike measure requires lower neighbors. Do not add those rates together. Thresholds move with each tail median, so a slower baseline can have fewer isolated spikes. Percentiles and means remain necessary. The saved all-save comparison supplies the remaining five saves. No causal claim is made from the auxiliary WPR samples between spike-group timestamp endpoints, which are not complete CPU-frame intervals.

## Recommendation and next steps

| Priority | Action                                                                          | Reason / required proof                                                                                                                                                                                                                                                                                 |
| -------- | ------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1        | Keep Balanced as the default; retain Legacy/Performance as diagnostic choices.  | Balanced supplies motion recovery; neither alternative demonstrated a repeatable overall speed win. This run set did not validate visual correctness.                                                                                                                                                   |
| 2        | Investigate the variable native/physics-worker workload before tuning recovery. | Compare worker counts, active physics work and native job callers in the same saved tails. Resolve public kernel/private module symbols where available. The slower Legacy repeat is a direct control showing recovery is not necessary for the problem.                                                |
| 3        | Attribute recovery to each measured save using DevBench-only counters.          | Record per-save miss/promotion deltas and resulting visible-object/draw/job work using existing health boundaries. Whole-run recovery counters cannot attribute downstream work to a particular tail. This is a proposed extension, not an implemented protocol change.                                 |
| 4        | Test Balanced visibility under controlled head motion in both DLAA and DLSS.    | The 64-object cap saturated. Check disocclusion and coverage of the objects left out before changing selection priority, bounds, budget or coherence. A 64-object cap bounds count, not GPU cost.                                                                                                       |
| 5        | Optimize only the demonstrated component, one variable per build.               | If scan cost dominates a controlled motion case, investigate safe frame-valid bounds reuse or tighter conservative bounds. If extra draws dominate, improve candidate selection while preserving visibility. If native/physics work dominates, optimize that proven path rather than removing recovery. |

For a causal mode comparison, retain identical build/settings/protocol and controlled HMD pose, and counterbalance mode order after the worker-load difference is understood. If assessing tracing overhead, run a separately identified untraced campaign; do not pool game-ft and gameft-sw as equivalent. No source change is recommended solely from these samples.

## Health, provenance and evidence limits

All 42 final per-save lifecycle summaries report successful completion. Final stretch is inactive, stereo complete, and no owner/retirement debt is reported. This is operational end-state health, not canonical physical-HMD release qualification. Balanced 2\* Save 13 retains the uncertain user interaction and extra stretch episode around seconds 29–34; it is not used as clean confirmation. No completed stretch episode is silently treated as active at measurement end.

| Identity        | Value                                                                                                                                                   |
| --------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Tested source   | 503fbfc92b5a9a58f5ba14a2343c3b0b119c40dc                                                                                                                |
| Build ID        | caf9feac1bce8608281b02ddeadbc5b507c68fb739a04044d5f57a6c18b905dd                                                                                        |
| DLL SHA-256     | C8D41C226BF900B2032C8AE505DAB512476F4B37CCD44E5C22E2BC588695D8D0                                                                                        |
| PDB SHA-256     | E82DA0EC720EC6B55BD0C10877B12EECD69FAC6A36F1C03004BFE8FEA0A441F8                                                                                        |
| Configuration   | Universal Release; DevBench ON; Tracy OFF; same AIO in all seven runs                                                                                   |
| Analysis branch | perf/cpu-dlss-regression-20260916 at 9ad20d60a80cd09eef0f51cf1735ebcfefb5efec (not the tested DLL source)                                               |
| Settings        | Saved feature settings before/after match except the intended culling mode flags. Info logging was user-confirmed; actual logger level is not exported. |
| Mode            | 08/09/10 DLAA, 11/12/13 DLSS with render scale; confirmed in per-save health.                                                                           |

The tested 503fbfc build predates the later diagnostic-gating cleanup on the investigation branch. All seven runs share the same enabled diagnostic state. Save/world state, NPC/physics activity and physical HMD pose are not proven identical merely because settings match.

WPR used the saved world-entry UTC +50 to +60 seconds, mapped against the actual ETL header origin. Raw fpsVR frame counts and means were independently checked against the saved reporter. World-entry markers are observations, not exact engine transition timestamps. Clock scatter is retained in each windows.json. Rendering-thread identity was cross-checked against CSX call stacks for every save. All seven trace-statistics checks report zero lost events/buffers and present sample/context-switch/ready-thread events. Exact CSX PDB resolution succeeded; some sample stacks are missing and native/vendor/kernel private functions remain unresolved.

Small scheduler interval reconstruction excesses remain in the per-window evidence; they do not change fpsVR means or support a material lock-cost claim. The user accepted Balanced 1 Save 12 as a pass despite +7.9251 ms accounting excess over 10,000 ms. No raw values or saved tolerance were changed.

Native function interpretation comes from the preserved live-read Ghidra project NativeCPUHotRanges (local evidence locator: `D:/Coding/GitHub/GhidraProjects/CPU-DLSS-20260916-pid21340/NativeCPUHotRanges.gpr`; retained results and hashes are in the ledger). That capture was taken with CSX source 5614c45180096acf6edde0d2b972a27d6a56a91a, not 503fbfc. The native Skyrim executable version 1.4.15.0, timestamp 0x5AEADAA0 and image size 60,133,376 match all seven traces. Each captured range was read twice with matching hashes; no static executable was substituted. Identical in-memory patch bytes in each later run are not proven. Native semantic labels remain live-code interpretation rather than private symbols.

| Label        | Run / exact evidence                                                                                                                                                                                                         | ETL SHA-256                                                      |
| ------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------- |
| Balanced 1   | gameft-sw-20260916T152832143Z-3828ffef (local evidence locator: `D:/Coding/GitHub/skyrim-community-shaders/build/bisect/measurements/gameft-sw-20260916T152832143Z-3828ffef`; retained results and hashes are in the ledger) | F048E775FE0DEEA5DBE68FE9DE5D5FE036C651B767768A117075C6B25A68E501 |
| Performance  | gameft-sw-20260916T154518977Z-a6e09be6 (local evidence locator: `D:/Coding/GitHub/skyrim-community-shaders/build/bisect/measurements/gameft-sw-20260916T154518977Z-a6e09be6`; retained results and hashes are in the ledger) | E6D5E713AAE4B0B0C09FADA5512219AD650B31E37C3782C44A34C08AED3B4B23 |
| Legacy 1     | gameft-sw-20260916T155929972Z-2a6a7693 (local evidence locator: `D:/Coding/GitHub/skyrim-community-shaders/build/bisect/measurements/gameft-sw-20260916T155929972Z-2a6a7693`; retained results and hashes are in the ledger) | E2571DCB2826EEFCBC3950282EC047D3AF41823B305B7F2D8E7300B8E58338A7 |
| Balanced 2\* | gameft-sw-20260916T161424154Z-401a5499 (local evidence locator: `D:/Coding/GitHub/skyrim-community-shaders/build/bisect/measurements/gameft-sw-20260916T161424154Z-401a5499`; retained results and hashes are in the ledger) | 5F5F90876248CCB798EDC8C9F142A47D5D8458BA994673274018D151BA21D4A7 |
| Balanced 3   | gameft-sw-20260916T163602908Z-a26f2ef1 (local evidence locator: `D:/Coding/GitHub/skyrim-community-shaders/build/bisect/measurements/gameft-sw-20260916T163602908Z-a26f2ef1`; retained results and hashes are in the ledger) | F61E62E1489DE32A3BACF7848CFBD3BE47F7719E0FC528266BE2348C65493DEC |
| Balanced 4   | gameft-sw-20260916T165146902Z-36b2c165 (local evidence locator: `D:/Coding/GitHub/skyrim-community-shaders/build/bisect/measurements/gameft-sw-20260916T165146902Z-36b2c165`; retained results and hashes are in the ledger) | 04C3C82552E906ADFC9F12BAFCACDD4AAD5630EBBC6F22EECA30D8B7BF8CA3E3 |
| Legacy 2     | gameft-sw-20260916T172315555Z-43f348bd (local evidence locator: `D:/Coding/GitHub/skyrim-community-shaders/build/bisect/measurements/gameft-sw-20260916T172315555Z-43f348bd`; retained results and hashes are in the ledger) | A6ADF810B4E9D711A6095166E810CAB80B40FFA2D705A0CB012D32CB531C4F26 |

Archived ETL and fpsVR paths, sizes and verified hashes are retained in each run's verified-archives.json; the analysis manifest pins ETL copies under D:/Coding/GitHub/CS logs. Input export hashes, exact windows, full scheduler states, native pages, stack contexts and trace-quality receipts accompany the report. Raw trees remain local.

## Analysis validation

Executed: culling-wpr-20260916.py --parallel; analyze-culling-wpr-20260916.py; culling-wpr-contexts-20260916.py; report-culling-wpr-20260916.py. WPA/xperf exports completed for all seven runs. Forty-two saved-name/window associations, all provenance checks, the shared native-image identity, and all final lifecycle-success flags were checked while generating this report. Means/percentiles were reused from the unchanged saved quick summaries. No source build or CTest was needed or claimed for this analysis-only work. Git tracked working tree remained clean.
