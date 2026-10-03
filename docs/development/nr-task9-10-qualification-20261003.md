# Task 9 measured-plan experiment and Task 10 compact-input candidate

This checkpoint adds session-only DevBench experiments; production behavior
and defaults are unchanged. Task 9 remains open for measured profile and
held-out runtime qualification. Task 10 remains open for the new adapter's
live quality, lifetime, whole-pipeline cost and memory comparison. Native
replay alone cannot promote compact storage.

## Live evidence and restoration

Local evidence: `build/validation/nr-task9-10-live-20261003/`.
Skyrim PID 49476 started at 2026-10-03T06:30:46.9807851Z. The exact enabled
AIO's physical DLL, adjacent manifest and build receipt matched producer
`587f560a843bdd9bbf629a565cef5087a28fd1daa674f4c166f9b80903546c23`,
compiled source `6b09366539ed50c345f48013a097043968ee62d7` with dirty digest
`04ea3da857f9f688c0411761d5f154064dc451edb380435679d573f71755cc42`.
DLL SHA-256 `843e09074977cfcd165638bbb02dfe296c8e6e4ff0a857e9ac3914b0e69dba0b`,
31,620,608 bytes. No enabled loose duplicate, Overwrite DLL or unmanaged
Data DLL superseded it. This is the Task 8 producer, not the new code.

Three 300-frame A profiler windows and 96 exact finalized stereo companions
were retained. Region-limit 1/2/1 selected the same one-region-per-eye plan
throughout: six eligible actors, 1,965,056 pixels per eye. This is a useful
repeatability bracket, not a split/merge performance comparison.

| Window       | Native GPU mean ms | Inclusive NR mean ms |
| ------------ | -----------------: | -------------------: |
| A1 before    |            34.5231 |              35.4327 |
| A2 candidate |            35.0655 |              36.0115 |
| A1 after     |            34.9674 |              35.8413 |

Baseline drift was 1.287% native and 1.153% inclusive NR. The candidate
was +0.3203 ms native and +0.3744 ms inclusive against the bracket mean.
These clocks overlap; never sum them. No measured profile is adopted.
The prior Task 8 actual two/four-region result remains the useful count
comparison; this scene supplies no new four-region or alternate-partition
qualification.

A-to-C-to-A and capture completed with 28,704 native evaluations, zero NR
failures, device removals, quarantines or stereo failures. Caller resets
were expected on stateless C. Native export first rejected capture outside
Developer Mode; those receipts remain preserved. The user then enabled
Debug. Two A frames, four stationary C frames and four C frames after
restoring AI were exported completely with no writer failure. Four frames
are not a general moving-scene or moving-camera quality campaign.

Native manifest SHA-256 values:

-   A: `0d25f31a0c0a7b048c1074e40c7cde2c79b8ef83ff6f7e85c5856d397e55ce60`
-   Stationary C: `b7479c57e4248b62b79e31d0b7c89516f942e5a493846f937a9732722d40f17a`
-   AI-running C: `87d3b39db66d9e41f2cd04e64fd24558a3f6e241840f74b1c4ea9f0c452a843d`

Settings, AI, camera/freecam, colour experiments and profiler state were
restored, with fingerprint `5d35db3364a6062e1e9cd0d3819c14de`.
All owned captures were inactive. Normal console `qqq` was dispatched and
PID exit verified at 06:57:29Z. MO2 exited normally at 06:57:50Z; the
recovery session and access lease were released by 06:58:19Z. SteamVR's
pre-existing processes were preserved. No installation or relaunch occurred.
The user authorizes future switches to Debug when required; restore the
previous level afterward when the available control supports doing so.

## Compiler-idle native replay

The initial 12-process run overlapped compilation. It is preserved as
preliminary evidence and does not qualify performance. In particular, its
768 baseline changed from 10.234 to 6.354 ms. At the user's instruction the
campaign was repeated after all builds and GPU regression tests finished.
Each new process retained a no-Skyrim/no-active-compiler check. Inputs,
provider, GPU/driver, source hashes and exact replay executable identity
were matched within each bracket. Native provider: 310.8.0, SHA-256
`8270b350cd82de5ce89806872cdd6b6a9249b80836b91bbeb3573470744cc206`;
RTX 5070 Ti Laptop GPU, driver 610.88 / 32.0.16.1088.

The first idle repeat used eight warmups and 64 measured static samples.
All 768 static samples completed, but all four timing brackets exceeded
the existing 5% drift bound. Their baseline drift was 10.79%, 5.87%, 13.87%
and 27.22% respectively. GPU P-states varied between P4 and P8 at process
boundaries. Per-evaluation GPU intervals also varied, so this was not just
extra time around the native batch. Clock/pacing dependence is a possible
confound, not an established root cause. These timings remain rejected;
compiler absence alone did not qualify them.

The exact original short protocol (three warmups, eight measured samples)
was then repeated, followed by one warmup plus three consecutive stateless
C samples per layout. All 21 processes completed. Every static comparison
passed the input/provenance checks and the 5% baseline-drift gate. This
scoped short-run result does not erase the long-run instability or authorize
a general cost model. Timings below are native submission GPU milliseconds,
not full CSX frame cost. Memory is median DXGI process local usage after
submission, including driver/provider and replay allocations; it is not a
separately measured provider heap size.

| Bucket | Full before ms | Compact ms | Full after ms | Drift | Full / compact MiB | Static output |
| ------ | -------------: | ---------: | ------------: | ----: | -----------------: | ------------- |
| 128    |         4.3645 |     4.3963 |        4.3761 | 0.27% |    967.45 / 683.20 | different     |
| 256    |         4.3805 |     4.4054 |        4.4146 | 0.78% |    968.20 / 688.45 | bitwise equal |
| 512    |         5.0585 |     4.9764 |        5.0114 | 0.94% |    971.20 / 737.07 | bitwise equal |
| 768    |         6.3841 |     6.2915 |        6.3048 | 1.26% |    976.20 / 834.07 | bitwise equal |

For admitted 256/512/768 buckets the short-run native differences are
+0.0078, -0.0586 and -0.0529 ms against the bracket mean. There is no
substantial native speedup; local usage falls by 279.75, 234.125 and
142.125 MiB. The result supports measuring a compact adapter, not promoting
one. Static outputs match both references for all 128 evaluated eye crops
per large-bucket comparison in the longer run and all 16 in the short run.
128 outputs differ in every matched comparison and are excluded.

The new short sequence retained evaluated crops correctly after fixing a
replay-only reporting bug that initially retained whole resources for
capacity-temporal cases. Both 256 and 768 pass their three measured frames
with bitwise equal stereo outputs. In the 512 full-coordinate before/after
runs, frame index 2 has one pixel equal to its sentinel in the right eye;
there are no nonfinite pixels and all native calls succeed. Both baseline
runs therefore retain only two accepted timing samples, and their normal
comparisons remain unavailable. All six raw eye crops nevertheless match
the compact run exactly, including that frame. This narrows the issue to
an ambiguous write-coverage observation; it does not justify silently
waiving the guard. The complete 512 sequence remains unqualified.

The extended campaign stopped at that rejected sample (16 completed
processes); its five unattempted sequence cases are not labeled passes.
The corrected original-protocol campaign completed all 21 processes and
preserved its two rejected 512 observations. The wrapper's initial wrong
acceptedSamples field and the original full-resource/crop metadata failure
are also retained; completed native results were not deleted or relabeled.

Evidence is in `idle-repeat/` and `idle-original-protocol/`, including raw
binary outputs, processes/preflight records, complete results, audits and
`sequence-512-investigation.json`. Crops are backend capacity probes from
the captured scene, not a character-face perceptual qualification. Neither
four captured frames nor bitwise native equality establishes in-game
composition, longer temporal quality or moving-camera behavior.

The explicit UTC System event query from 06:30:46Z through 08:05:59Z found
zero nvlddmkm/Display-4101 faults. Display 4127 is retained as informational
Auto HDR guidance. This is separate from the replay admission failures.

## Implemented experiment contracts

`communityshaders.nr_cost` offers status, configure, load_profile and
clear_profile. It is bridge-only, off by default, session-only and does not
benchmark alternatives in production. The current validated partition and
bounded local merges are candidates, including the complete enclosure.
There are at most 16 candidates per eye and 256 joint plans. The original
upstream partition remains available for splitting a prior selected merge;
this is not exhaustive repartitioning of the actor set. Alternative actor
splits/reanchors beyond the existing upstream planner remain Task 9 work.
Merged histories derive from canonical original membership; untouched
banks retain their identities. Coverage checks and the existing higher-count
geometry guard remain authoritative.

Costs use exact final native layout, capacity, viewport, origin, route,
format, reset, transport and tuning keys, including all actual bucket work.
Profiles also match the exact build, provider, parameter core, GPU, driver
and colour settings. Unknown keys or stale identity use the labeled existing
heuristic. Selection requires a gain beyond uncertainty and amortized
transition cost, no worse CPU critical path or GPU tail, and admitted
resident memory. Coverage repair overrides dwell. CPU and GPU clocks are
never added. There is no hard-coded GPU time, universal area cap, cadence
reduction, downsampling or character exclusion.

Profile imports require bounded observations and separate unique baseline
and held-out SHA-256 identifiers plus declared qualification gates. These
are caller declarations, not independent verification of source evidence.
No qualified profile is shipped and productionProfileAdopted stays false.
Synthetic policy tests cannot establish a calibrated model. Capture freezes
selected reasons, keys and predictions with the actual execution; capacity
fallback remains visible. Prediction-error and complete pipeline/residency
qualification require corresponding live observations.

`communityshaders.nr_color` adds `experiments.compactInputs`, off by default
and absent from production builds. The adapter admits only stateless C,
legacy/raw colour, equal colour/depth/motion/output grids and no provider
control mask. Unsupported contexts preserve full-coordinate coverage.
Buckets are 256, 512 and 768 square pixels. The 128 bucket is excluded because
matched native outputs differed. Selection never resamples or drops output:
the full bucket is copied, initialized and evaluated; only owned output is
committed at its original crop origin. The exact outer character/category
mask still governs composition. Read-only bucket contexts may overlap;
output ownership cannot. Full-eye normalized motion keeps the original
full-eye pixel scale. Every C invocation resets native history.

Persistent slots reuse the same capacity across ordinary movement. Capacity
changes and recoverable creation failures use the existing both-API retirement
and rejection latch. Unsafe/device failures cannot retry automatically.
There is no new per-movement global idle, fixed-page atlas or in-place alias.
Managed colour, unequal guide ratios and temporal A/B use the existing path.
All new planning/compact work is behind the bridge and explicit opt-in.

The replay tool reuses its existing capacity cases, adding bounded
`--capacity-size` and stateless `--capacity-temporal` controls. Captured
pixels, scale, provider and source identity are retained. The execution
parser checks full-bucket initialization, source/local origins, ownership,
reset and delayed-plan identity. The new depth shader is covered by typed
copy equivalence with nonzero origins and poisoned spare capacity.

## Review and local validation

Adversarial review covered scope, coverage/ownership, history identity,
capacity rejection, source/local coordinate transforms, fast-math-safe cost
validation, API failure reporting and reuse of existing retirement/replay
paths. Corrections made during review include canonical merge-history keys,
exclusion of the output-changing 128 bucket, full initialization and owned
copy accounting, exact compact-source keys, honest indeterminate mutation
reporting, frozen selected-plan evidence and bounded capture retention.
No production profile, settings persistence, resampling or extra model call
was added. Current candidate enumeration is an explicit developer workload;
keep it disabled for an uninstrumented performance baseline.

The initial complete validation passed all 227 tests, then stopped at the
preset source-fingerprint guard. The saved settings schema did not change:
only the declared source hash, the generated report and the three preset
compatibility markers were refreshed. All preset values and the user's
three untracked preset archives were preserved. The test-fixture correction
for the first measured-plan test, one empty CTest filter invocation and the
shared-vcpkg permission failure remain in the local logs; none is reported
as a passed check.

## Required next live qualification

1. Install this AIO and return to a reproducible NPC scene. Verify the exact
   physical DLL, producer Build ID and manifest before taking samples.
2. Exercise nr_cost enable/status/disable, unknown/stale profile rejection,
   final geometry and frozen decision joins. No profile is adopted until
   complete calibration, held-out prediction, transition, CPU and native
   residency evidence supports it. Complete the remaining candidate-search
   work before calling Task 9 closed.
3. In stateless C/raw/equal-grid character mode, bracket compact off/on/off.
   Require actual compactSource records: requested compact=true with a
   full-coordinate fallback is not a compact measurement. Preserve output
   ownership, category strengths, masks, source density and native cadence.
4. Test eligible bucket sizes, edges and movement, independent stereo
   output, mode/colour/guide-ratio fallbacks and explicit safe recovery.
   Compare attributed native and headset output, complete pipeline/CPU costs,
   native/process memory and reset/rebuild behavior. Do not repeat the known
   unsafe small-shape/high-count probe.
5. Restore settings and close the game normally when live work is complete.
   Promote only a correct net win. A memory reduction with unchanged native
   cost is a useful result but is not a proven whole-frame speedup.

## Validation and testing AIO

`pwsh tools/validate-local.ps1 -OutputDirectory build/validation/nr-task9-10-final-20261003`
passed: universal Release DevBench DLL, all 227 repository tests, preset
generator tests/check, whitespace and exact artifact manifest verification.
The separate colour/WARP suite passed 21/21. After the final offline-only
parser/replay corrections, focused checks passed 39 transaction-evidence,
15 cost-report, 26 replay-report and 12 replay-input tests. The replay
executable was rebuilt before the final 21-process campaign. No runtime
DLL code changed after its successful full validation.

Bridge-on/off controller tests cover disabled defaults. A complete bridge-off
DLL and SE/AE gameplay were not run; new runtime branches are bridge-only
and the universal bridge-on DLL compiled successfully. No new DLL has yet
been installed or tested in game. All claims about live behavior above
belong to producer 587f560a843b.

Testing producer `7abf7b871612c28fedd2d148417985b0be27d9b93c68ce278abd77abdc70644f`: compiled base
`7f519617789f012500c49a3ad4809a5a8bcd7632`, dirty digest
`2bb701adf4d63721f6f8fb5176ea3acf7c5743fc8190240881c3e6572fe4e6ab`. DLL SHA-256
`626c621ca82ea82d02939f6a30df5225620d6f392388f0aab01df1d3c22690d1`, 31,675,392 bytes.

The source snapshot, patch, copied changed files, manifests, archive
inventory and extraction receipts are retained in
`build/validation/nr-task9-10-aio-20261003/`. Subsequent offline reporting
corrections and this handover are distinguished from the compiled DLL
identity. The AIO includes DevBench, universal SE/AE/VR support and the
admitted NR runtime; Tracy is off. It contains no personal SettingsUser.json
or shader cache. No deployment or relaunch was performed.

Testing archive:
`dist/CSX_AIO-main-vr-nr-NR-Task9-10-DevBench-20261003-7abf7b871612.7z`
(199,365,168 bytes, 386 files). SHA-256
`6e093483b4610a8498cac2d2334018098ef6781eda0a4dc34d7d8b82df36c35a`.
7-Zip integrity, complete extracted SHA-256/size inventory, packaged DLL/PDB/manifest
and compact shader parity all passed. The adjacent receipt preserves the
compiled identity separately from the final source/documentation commit.
