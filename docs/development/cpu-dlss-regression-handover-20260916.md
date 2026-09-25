# CPU / DLSS regression review handover — 2026-09-16

Repository: `ParticleTroned/skyrim-community-shaders`

The subsequent [eight-run depth-culling comparison](depth-culling-comparison-20260916/README.md)
adds complete portable timing, health, WPR, settings and provenance records
for the exact 503fbfc build. Its Balanced 1 reference is internal to that
campaign; the historical baseline below remains unchanged. It also refines
the native-frustum interpretation: increased rendering-thread samples do
not necessarily mean increased all-thread work. The sections below retain
the earlier cross-build findings and their distinct build identities.

Investigation branch: `perf/cpu-dlss-regression-20260916`

This handover summarizes existing measurements and analysis. It records no
new benchmark, settings change, or performance optimization. Exact build
hashes, Build IDs, PDB identities, receipt locations and limitations are in
the [investigation record](cpu-dlss-regression-investigation-20260916.md).
Earlier sections there retain historical status, not current task state.

## Correct interpretation of the fixes

The first large performance correction was the PARTIAL REVERSAL OF PR93:
removing permanent per-call D3D11 multithread protection while preserving
native renderer ownership and the loading-menu guard. PR93 added roughly
0.975–2.084 sampled CPU ms per recorded frame in D3D11 critical-section
entry/exit versus the traced baseline. That signature returned near baseline
after the partial reversal. Sampled lock execution is not blocked wait.

The subsequent native stereo-submit hook correction fixed a real ABI and
boundary-proof bug. It must remain. It has NO DEMONSTRATED SAVE-13 PERFORMANCE
GAIN: partial reversal initially measured 15.826 ms CPU, but an unchanged-DLL
repeat measured 13.449 ms; the hook-corrected build measured 13.480 ms.
The apparent 2.346 ms improvement disappears against that repeat. The two
fixes must not be credited interchangeably.

## Exact measurement identities and protocol

-   Traced baseline compiled source:
    `a1a11fe0d722fd6dbc18315a023701e0ec4a84de`; recorded renderer base:
    `190c28a39a52c2bace475d1143a124c5893ac741`.
-   Partial-reversal compiled source, including the clean repeat:
    `5614c45180096acf6edde0d2b972a27d6a56a91a`.
-   Latest measured hook-corrected compiled source:
    `933a4e2540f962ddc3d2ce656523d1f8c5982ef4`.
    Its tracked Git tree equals the investigation starting source:
    `503fbfc92b5a9a58f5ba14a2343c3b0b119c40dc`.
    This is source equivalence, not a measurement of a newly built 503fbfc DLL.
-   Protocol: unchanged `gameft-sw`, DevBench enabled, WPR stack/wait recording,
    saves `08, 11, 09, 12, 10, 13` in that order, 60-second holds. Means and
    P95/P99/spike statistics use the saved final-ten-second windows `[50,60)`.
    Saves 08/09/10 are DLAA; 11/12/13 are DLSS with render scale.
-   Do not pool these data with untraced `game-ft`, treat different scenes as
    independent repeats, or infer identity from archive names/repository HEAD.

## Remaining timing differences

Saved fpsVR final-ten-second means, milliseconds. Deltas are latest measured
hook-corrected build minus traced baseline; positive means slower.

| Save / mode | CPU baseline | CPU latest | CPU delta | GPU baseline | GPU latest | GPU delta |
| ----------- | -----------: | ---------: | --------: | -----------: | ---------: | --------: |
| 08 / DLAA   |        4.664 |      6.544 |    +1.880 |       10.410 |      9.684 |    -0.726 |
| 09 / DLAA   |        3.602 |      4.511 |    +0.909 |        9.537 |      9.227 |    -0.310 |
| 10 / DLAA   |        2.937 |      3.515 |    +0.578 |       10.036 |      9.488 |    -0.548 |
| 11 / DLSS   |        7.784 |      8.192 |    +0.407 |        7.835 |      8.391 |    +0.556 |
| 12 / DLSS   |        6.854 |      7.328 |    +0.474 |        7.576 |      8.532 |    +0.956 |
| 13 / DLSS   |        8.675 |     13.480 |    +4.805 |       10.259 |     11.881 |    +1.622 |

Deltas were calculated before display rounding. CPU 0.35 ms / GPU 0.15 ms
are user-selected screening allowances, not confidence intervals. Only the
partial build has this clean six-save repeat; baseline/latest repeatability
is not established. Scene content prevents attributing the pattern to AA
mode alone. Earlier traced PR65/PR92 already show a save-13 CPU residual
before PR93; removing PR93 cannot remove every earlier regression.

## WPR execution and burst findings

Save 13 latest versus baseline, per recorded fpsVR frame:

-   Rendering-thread running time: +1.7361 ms.
-   Ready delay: -0.0034 ms; blocked waiting: -1.1293 ms.
-   Exclusive native Skyrim sampled execution: +1.3863 ms.

The leading signature is more execution, not sustained blocking or ready
delay. Scheduler durations, sampled CPU weights and fpsVR CPU latency are
different quantities; they do not arithmetically decompose one another.

DLAA also has distributed overhead: exclusive CSX sampled execution rises
about 0.316–0.448 ms/frame across 08/09/10. Native, D3D and system-module
contributions remain separate; inclusive parent stacks cannot be added.

Long CPU slow groups in 08/13 predominantly execute work. Examples span
40 frames / about 0.700 seconds in 08 and 66 frames / about 1.167 seconds
in 13. One 08 group overlaps the normal health request; others do not.
Overlap alone is not causation. Save 13 retains 18.51% GPU slow-frame share.
Fewer isolated spikes or a lower mean can coexist with longer groups.
Keep the saved P95/P99, isolated/grouped spike and slow-frame definitions.

## Live Ghidra findings

Only live captured code was analyzed; no static disk Skyrim executable was
imported. A new main-menu process, PID 21340, ran the same partial-reversal
Build ID as clean benchmark PID 19804. Each code range was read twice with
matching hashes. This was a targeted code snapshot with PE header and unwind
directory, not a full-process dump or save-13 object-state capture.

Reusable local project:

```text
D:\Coding\GitHub\GhidraProjects\CPU-DLSS-20260916-pid21340\NativeCPUHotRanges.gpr
```

Keep the `.gpr`, `.rep`, snapshots and metadata together.

| Live Skyrim RVA | Inferred operation                  | Latest save 13 sampled ms/frame | Clean partial repeat |
| --------------- | ----------------------------------- | ------------------------------: | -------------------: |
| `0xDA54B0`      | Six-plane bounding-sphere tests     |                          0.5052 |               0.5193 |
| `0xDA33C0`      | Compound-frustum operator traversal |                          0.1214 |               0.1169 |

These are semantic identifications from live instructions and CommonLib
layouts, not private Skyrim symbol names. No baseline samples matched these
exact ranges; that does not prove zero execution. Some parent stacks pass
through native depth rendering wrapped by VolumetricLighting/TerrainBlending;
that does not identify either feature as the cause. Nearby native job/helper
code executes work and must not be mislabeled as blocked waiting.

## Ranked remaining investigations

1. **Native culling/rendering workload.** All retained settings select
   Balanced, exterior/interior culling enabled, minimum extent 10. The modes
   predate baseline, and their policy/activation headers are unchanged across
   compared sources. Direct temporal-hook samples show no broad increase,
   but Balanced can promote up to 64 objects whose later rendering cost lies
   outside the hook. Effective per-save activation/promotion counts were not
   retained. Test Balanced -> Performance -> Balanced on one exact corrected
   DLL/vendor bundle, with unchanged protocol and fixed telemetry state.
   Read existing status/counters outside holds. Preserve visual evidence;
   missing geometry is not an acceptable performance remedy.
2. **Material admission and other CSX CPU overhead.** Guard union samples
   are 0.028–0.128 ms/frame: worth improving safely, insufficient to explain
   the whole residual. Establish actual repeated checks and an equivalent
   safe path before changing code. Replay/callback boundaries can invalidate
   earlier admission. Do not remove malformed-material protections or cache
   pointer validity across frames.
3. **DLSS-scene GPU residual.** First inspect the culling control's GPU
   response. If unresolved, use a separate, equally instrumented GPU-pass
   capture to distinguish geometry, prepared colour/copies, guides, vendor
   work and postprocessing. Existing CPU WPR contains no GPU packet evidence.
   Freshness receipts show no steady rejection/retry pattern; prepared-eye
   masks and completed current-eye fallback counts remain unmeasured.
4. **Secondary controls.** Named light retention costs about 0.014–0.022
   sampled ms/frame. Preserve ownership until every raw-pointer consumer
   finishes. Sustained presentation-mutex contention, relatch blocking and
   accepted-draw observer activity are not supported as primary causes.
   Baseline Streamline/DLSS 2.12.0/310.7 versus corrected 2.14.1/310.9.1
   remains a lower-priority compatible-bundle control, not a proven cause
   or a proposal to stay on old libraries.

## Subsequent cleanup and validation

-   `f70f29103e282e79d63b216ab2b43ad6bbd8ba33`: gate depth-culling telemetry.
-   `779aed553759c38288e5f07f6077bde9ea7b78ce`: gate upscaling diagnostics.

Both preserve functional rendering/ownership decisions. DevBench builds
still retain diagnostics; these commits do not establish a benchmark gain.
The later commit passed Universal Release DLL/controller/shader builds,
124/124 CTests, compilation of 14 production translation units with DevBench
OFF, and 42 symbol assertions. No separately linked OFF DLL or in-game
performance/visual qualification is claimed. See the
[diagnostics boundary and validation](upscaling-diagnostics-boundary.md).

## Evidence access, limitations and constraints

The investigation document carries the cross-machine findings and exact
identities. Detailed local differential outputs are under:

```text
build/bisect/measurements/cpu-dlss-cross-build-differential-20260916/
```

This contains `analysis-report.md`, `per-save-cross-build.csv`,
`slow-group-attribution.csv`, `ready-wait-analysis.csv`,
`native-live-evidence.json`, `native-live-regions.csv`,
`depth-culling-settings.json`, validation and output hashes.

Raw traces, CSVs, binaries and the Ghidra project remain local, not part of
the Git push. Original partial-reversal raw evidence was deleted; retained
aggregates cannot reconstruct it. The separate clean repeat is
`gameft-sw-20260916T075210921Z-74778e53`. Original partial save 11 retains
its incomplete-stereo failure and accounting warning; all six clean-repeat
and latest final health/accounting results pass. Full physical-HMD visual
qualification is a separate requirement.

Preserve native renderer ownership, the loading-menu guard, light/material
safety, prepared colour input and one DLSS evaluation per eye. Never
reintroduce permanent D3D11 protection. Diagnostics must be DevBench-only.
Use one functional variable per benchmark build; do not change the saved
protocol. Source inspection alone does not prove runtime cost or causation.
