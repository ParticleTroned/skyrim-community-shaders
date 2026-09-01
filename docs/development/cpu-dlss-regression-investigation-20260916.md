# CPU / DLSS regression investigation — 2026-09-16

For the current cross-machine summary, read the
[review handover](cpu-dlss-regression-handover-20260916.md). The sections
below retain their original investigation chronology. The partial PR93
reversal removed the large locking regression; the native stereo-hook fix
corrected a bug without a demonstrated save-13 performance gain. Subsequent
telemetry gating is committed and code-validated, but not benchmarked.

Initial evidence and investigation plan only. No implementation, benchmark,
build, deployment or settings change is part of this investigation.

**Current evidence does not establish a return to baseline.** The large
PR93 D3D11 per-call locking signature is near baseline after its partial
reversal. The native-submit correction restores accepted stereo boundaries.
Residual CPU and DLSS-scene GPU costs remain; their causes are not established.

**Source inspection alone does not prove runtime cost.** Sampled stacks
are statistical attribution, not direct function timers. A visible code
path, an unsampled symbol, or a lower single-run mean is not causal proof.

## Repository and exact starting source

The user-selected analysis base is
`503fbfc92b5a9a58f5ba14a2343c3b0b119c40dc`.
The dedicated branch is `perf/cpu-dlss-regression-20260916`, at
that exact commit. Initial branch: `main-VR`; initial HEAD: the
same commit. Initial `git status --short`: empty.

The latest measured DLL identifies compiled source
`933a4e2540f962ddc3d2ce656523d1f8c5982ef4`, not the current branch name.
Both commits have Git tree
`ab66f408d3f9cd0b2c242fad24d74bad69a9f0a2`.
This proves tracked-source equivalence, not a newly built or measured
503fbfc binary. Its measured Build ID remains
`dada666c72752a0a578ba0c8b21c6dbd80ce59696e7f36629a83ee7b4f6a640e`.

The existing `build/ALL/Release/CSX.BuildManifest.json` is stale
relative to HEAD: it identifies `5614c45180096acf6edde0d2b972a27d6a56a91a`
and Build ID `c2b3b90877a88ec2a4a0fb1251f5e70c6d3c4686cfb6cd39d9dbf831dbfaf281`.
Do not use that output as a current-HEAD DLL.

Recorded recursive submodules, all at their recorded pins with no dirty
or mismatch prefix:

| Path                                 | Checked-out commit                       |
| ------------------------------------ | ---------------------------------------- |
| extern/CommonLibSSE-NG               | 70c1acd5261210982bd52f6d4468a082fe04d798 |
| extern/CommonLibSSE-NG/extern/openvr | 60eb187801956ad277f1cae6680e3a410ee0873b |
| extern/FidelityFX-SDK                | e65b2530631f2afb9a9ac753884926e49e20d608 |
| extern/Streamline-DX12               | 2122257e0fce486f91b385aa63b9a09b0a34b363 |
| extern/nvapi                         | 9b181ea572f680327fe01a14a0f1f41c78034104 |

Inherited bisect metadata remains present and untouched. Its historical
first-bad label is f5d49d4, but the later measured comparisons do not
support that as a common CPU/GPU onset. No bisect reset, worktree removal,
or user-output cleanup was performed.

Read before source analysis:
[ownership review](graphics-context-ownership-review-20260915.md),
[material guard](native-lighting-material-guard.md),
[gameft-sw protocol](gameft-sw.md), and repository AGENTS.md.

## Local evidence from the preceding investigation

These are retained local reports from this chat's experiments, not new
measurements. Their raw receipts, timing boundaries and definitions remain
unchanged. Links under build/ refer to local, unversioned evidence.

| Evidence                                                                                                                                                                                                                | Coverage and interpretation                                                                                                                                                                                                                         |
| ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [Three-save comparison](../../build/bisect/measurements/game-ft-20260915T111417765Z/comparison.md) and [spike/P95/P99 analysis](../../build/bisect/measurements/game-ft-20260915T111417765Z/spike-pattern-deltas.md)    | 13 retained game-ft runs, including two 9d552d7 runs; saves 05/07/08. Four earlier setup summaries lack the complete same-save/provenance/spike-group contract.                                                                                     |
| [Repeat comparison](../../build/bisect/measurements/game-ft-20260915T111417765Z/repeat-comparison.md)                                                                                                                   | Same verified 9d552d7 DLL and save hashes; observed CPU differences +0.345/+0.381/-0.343 ms for 05/07/08. Two repeats do not estimate a population variance.                                                                                        |
| [All six-save builds](../../build/bisect/measurements/gameft-sw-all-six-save-builds/comparison-all-builds.md)                                                                                                           | Eight run columns / seven build identities, including the first b46f attempt whose WPR failed. That attempt cannot serve as a matched traced control.                                                                                               |
| [Five-build comparison](../../build/bisect/measurements/gameft-sw-20260915T211541758Z/five-run-comparison.md) and [PR93 WPR analysis](../../build/bisect/measurements/gameft-sw-20260915T211541758Z/analysis-report.md) | Baseline, PR65, PR92, PR93 and b46f with diagnostics. PR92 compiled as d512926d793f05fe9db28fff5666c1566a9b88b4; PR93 compiled as f556c86e5296da8a41e3305a4b95507e0ba39671. Renderer labels are not substituted for compiled commits.               |
| [PR92 / PR93 / partial reversal](../../build/bisect/measurements/gameft-sw-20260915T223347887Z-be57afea/comparison-pr92-pr93-fix-complete.md)                                                                           | Confirms removal of the large sampled D3D locking signature, with residual cost and save-11 limitations retained.                                                                                                                                   |
| [Baseline / partial reversal / latest](../../build/bisect/measurements/gameft-sw-20260915T234651019Z-dd4d7c3e/comparison-three-builds-complete.md)                                                                      | Full timing, percentiles, spike groups, health and WPR comparisons for the three exact traced builds below.                                                                                                                                         |
| [Earlier native-submit investigation](../../build/investigations/early-dlss-20260915/findings.md)                                                                                                                       | Historical hook ABI defect and failing production-hook regression. Its then-uncommitted wording describes that investigation's date, not today's fix status. Its archive-index.json preserves 121 evidence receipts under D:/Coding/GitHub/CS logs. |
| [Latest freshness / targeted-stack audit](../../build/bisect/measurements/gameft-sw-20260915T234651019Z-dd4d7c3e/stack-wait/analysis/isolated-freshness-stack-audit.md)                                                 | Existing producer counter deltas and full sampled/precise stack exports, including attribution limits.                                                                                                                                              |

### Historical patterns to preserve

-   In the three-save game-ft series, save 07 first stays above the user's
    +0.35 ms CPU screening allowance at 2fd637c; the previous tested
    45157af is +0.277 ms. This locates a tested interval, not a causal commit.
    Save 05 has no matching persistent early CPU mean increase.
-   Native save 08 CPU bursts are intermittent: large P99/duty changes at
    d5ae839, 2fd637c, e4cfd8f and b46f, but not consistently at intervening
    builds or both 9d552d7 repeats. Fewer isolated spikes can coexist with
    longer slow-frame groups.
-   Three-save GPU changes are scene dependent. Save 05 first exceeds
    +0.15 ms at 358069e but later exceptions exist; save 07 has no sustained
    mean increase; native save 08 has no mean above that allowance.
-   The traced six-save series separately shows save-13 CPU degradation
    already at PR65 (+2.744 ms) and PR92 (+3.244 ms), before PR93
    (+10.044 ms). PR65 GPU deltas are +0.520/+0.885/+1.058 ms for
    11/12/13. Those are evidence of an earlier problem, not proof that
    PR65 alone or AA mode alone caused it.
-   PR93 adds approximately 0.975–2.084 sampled CPU ms per recorded frame
    in D3D11 critical-section entry/exit versus its traced baseline.
    The partial reversal and latest build are back near baseline for that
    category. Sampled lock execution is not blocked wait and must not be
    added to running time.
-   Do not pool game-ft with gameft-sw. Their tracing conditions, baseline
    binaries and save groups differ. The supplied CPU 0.35 ms / GPU
    0.15 ms allowances are screening thresholds, not confidence intervals.
    Show per-save paired deltas; descriptive medians/ranges may supplement
    them, but three distinct scenes are not three exchangeable repeats.

## Exact tested-build provenance

Identity below comes from preserved producers, manifests and receipts;
it is not inferred from archive names, branch names or repository HEAD.
The three traced CSX DLLs and retained PDBs were rehashed during this
inspection and match their retained identities.

| Field                                | Traced baseline                                                    | Partial PR93 reversal                                              | Native stereo-hook correction                                      |
| ------------------------------------ | ------------------------------------------------------------------ | ------------------------------------------------------------------ | ------------------------------------------------------------------ |
| Run directory                        | `gameft-sw-20260915T183826985Z`                                    | `gameft-sw-20260915T223347887Z-be57afea`                           | `gameft-sw-20260915T234651019Z-dd4d7c3e`                           |
| Compiled source, clean               | `a1a11fe0d722fd6dbc18315a023701e0ec4a84de`                         | `5614c45180096acf6edde0d2b972a27d6a56a91a`                         | `933a4e2540f962ddc3d2ce656523d1f8c5982ef4`                         |
| Renderer base recorded in provenance | `190c28a39a52c2bace475d1143a124c5893ac741`                         | `5614c45180096acf6edde0d2b972a27d6a56a91a`                         | `933a4e2540f962ddc3d2ce656523d1f8c5982ef4`                         |
| Build ID                             | `ee97357e1005ab9fc04025938a1875e7af689c1c4409a0df74e756b6b2974ca6` | `c2b3b90877a88ec2a4a0fb1251f5e70c6d3c4686cfb6cd39d9dbf831dbfaf281` | `dada666c72752a0a578ba0c8b21c6dbd80ce59696e7f36629a83ee7b4f6a640e` |
| CommunityShaders.dll SHA-256         | `9376CA0C6F228FCDE3F29AF45E271CC36FC3B847432C3CA01D5809F4743D9141` | `E2B89B03881C8AFA6B90F0F316B3B8C45F79FD677A61C8F5C013843F196358B4` | `C9D28C153C9E6CA9157FA9566EFC5BB0CC49921FFDE3B8AE6011B9B7CEF56591` |
| DLL bytes                            | 27793408                                                           | 28884480                                                           | 28889600                                                           |
| Retained PDB SHA-256                 | `4A289D685B8E0F8258A4FCBF07CD8A7DB1E5A3F0E8D091EFC94EAE9A6339C017` | `277AFF1ABD035EB05934D32818187FDB7C5542EB89F1A712B036AF57408F5493` | `48D9024B74B4925021B0144E939D4A2A8193B2196C7DEC6E390B2883A712EF1C` |
| PDB bytes                            | 120016896                                                          | 137089024                                                          | 124366848                                                          |

All three: Release, VR runtime, SE/AE/VR enabled, DevBench ON, Tracy OFF,
MultiThreadedDLL; CMake 4.4.1, MSVC 19.51.36256.0, Visual Studio 18 2026
x64, Windows SDK 10.0.26100.0, x64-windows-static-md.
The complete toolchain and dependency identities remain in each manifest.

### Traced baseline receipt locations

-   Physical DLL: `D:\Skyrim Mods\mods\CSX_baseline-a1a11fe0d722-20260915-DevBench-AIO-gameft-sw\SKSE\Plugins\CommunityShaders.dll`
-   [Final provenance](../../build/bisect/measurements/gameft-sw-20260915T183826985Z/provenance.json),
    [build manifest](../../build/bisect/measurements/gameft-sw-20260915T183826985Z/CSX.BuildManifest.json),
    [AIO receipt](../../build/bisect/measurements/gameft-sw-20260915T183826985Z/aio-receipt.json),
    [retained matching PDB](../../build/bisect/measurements/gameft-sw-20260915T183826985Z/stack-wait/CommunityShaders.pdb),
    [timing/health summary](../../build/bisect/measurements/gameft-sw-20260915T183826985Z/quick-summary.json).
-   Recorded archive: `D:/Coding/GitHub/skyrim-community-shaders/dist/CSX_baseline-a1a11fe0d722-20260915-DevBench-AIO.7z`.
-   Receipt archive SHA-256: `e7c2e59b2a584236e4e5859a8366e30782323929fcf98a9a1d939b24731b7fe0`.
-   Archive recheck: missing at the recorded location and the bounded downloads alternate; the retained installed DLL/PDB and manifest remain available.

### Partial PR93 reversal receipt locations

-   Physical DLL: `D:\Skyrim Mods\mods\CSX_AIO-main-VR-5614c4518-gameft-sw-DevBenchON-NoShaderCache-NoFOMOD-PR93revert\SKSE\Plugins\CommunityShaders.dll`
-   [Final provenance](../../build/bisect/measurements/gameft-sw-20260915T223347887Z-be57afea/provenance.json),
    [build manifest](../../build/bisect/measurements/gameft-sw-20260915T223347887Z-be57afea/CSX.BuildManifest.json),
    [AIO receipt](../../build/bisect/measurements/gameft-sw-20260915T223347887Z-be57afea/aio-receipt.json),
    [retained matching PDB](../../build/bisect/measurements/gameft-sw-20260915T223347887Z-be57afea/stack-wait/CommunityShaders.pdb),
    [timing/health summary](../../build/bisect/measurements/gameft-sw-20260915T223347887Z-be57afea/quick-summary.json).
-   Recorded archive: `D:\FireFox-downloads\CSX_AIO-main-VR-5614c4518-gameft-sw-DevBenchON-NoShaderCache-NoFOMOD.7z`.
-   Receipt archive SHA-256: `b5dc196e9abb3b591ca506d547b63e5bd1a1eb0a1e8e33ccbe823eb2f11ff35d`.
-   Archive recheck: present; current hash matches receipt.

### Native stereo-hook correction receipt locations

-   Physical DLL: `D:\Skyrim Mods\mods\CSX_AIO-933a4e254-DevBench-gameft-sw-NoFOMOD - gameft-sw - repairPR65\SKSE\Plugins\CommunityShaders.dll`
-   [Final provenance](../../build/bisect/measurements/gameft-sw-20260915T234651019Z-dd4d7c3e/provenance.json),
    [build manifest](../../build/bisect/measurements/gameft-sw-20260915T234651019Z-dd4d7c3e/CSX.BuildManifest.json),
    [AIO receipt](../../build/bisect/measurements/gameft-sw-20260915T234651019Z-dd4d7c3e/aio-receipt.json),
    [retained matching PDB](../../build/bisect/measurements/gameft-sw-20260915T234651019Z-dd4d7c3e/stack-wait/CommunityShaders.pdb),
    [timing/health summary](../../build/bisect/measurements/gameft-sw-20260915T234651019Z-dd4d7c3e/quick-summary.json).
-   Recorded archive: `D:\Coding\GitHub\skyrim-community-shaders\dist\CSX_AIO-933a4e254-DevBench-gameft-sw-NoFOMOD.7z`.
-   Receipt archive SHA-256: `64f932a35e54d0eae8c6d89e0133c51040a2d8d2e178f78c839ecec8936b428b`.
-   Archive recheck: present; current hash matches receipt.

### Original untraced baseline is a separate build

The three-save comparison baseline is
`1595cffd6a0a770020074c3d5481d45502df58a7`, Build ID
`4d6c024a3b20630db3a9b2a0280cc3dff1c51c9f1e4e1f6a4c3b2b67f725bdd5`.
This identity is in the
[retained runtime producer](../../build/bisect/measurements/game-ft-20260915T080517974Z/main-menu-health.json),
not inferred from its label. Its matched
[rebuild receipt](../../build/bisect/evidence/baseline-1595cffd-20260915T032620198Z/rebuilt/aio-receipt.json)
and [manifest](../../build/bisect/evidence/baseline-1595cffd-20260915T032620198Z/rebuilt/CSX.BuildManifest.json)
record DLL SHA-256 `6b0a80c3562fce99bab923506eeb311fdb8f7f3afd601555ccc026472938cc6f`
(27771904 bytes).
The retained installed DLL matches that hash now. Installed PDB:
`2969D8834C533A10EFCE37BC2BEC2A091F89E7F7DEF1B4A18B9B9558F0B719CE`
(128053248 bytes), under
`D:/Skyrim Mods/mods/CSX_AIO-Baseline-1595cffd-DevBenchON-NoShaderCache-NoFOMOD/SKSE/Plugins/CommunityShaders.pdb`;
a PDB is also retained in the receipt directory's verified-payload tree.

Its retained run is game-ft-20260915T080517974Z, saves 05/07/08:
DLSS + render scale for 05/07, native DLAA for 08. It is not the traced
190c28a-derived binary and is not numerically pooled with it.
A complete before/after feature-settings snapshot and finalized
provenance.json are absent in this older run directory; current installed
hashes are not a substitute for a missing historical full-provider audit.
The original user-reported 190c28a launch has no separately identified
measurement/DLL receipt in the inspected evidence; the available six-save
baseline is its explicitly identified a1a11fe diagnostic derivative.

## Streamline headers and packaged runtime

Current headers: 2.14.1, submodule
`2122257e0fce486f91b385aa63b9a09b0a34b363`.
[cmake/Streamline-Runtime.cmake](../../cmake/Streamline-Runtime.cmake)
pins the 2.14.1 production bin/x64 runtime archive SHA-256
`92C4D954631A1710DA86CA3FA8D5034F2B9503838C95FC4AE977AE149319781B`.

Both baseline manifests instead pin header commit
`e8aaa6eaac968711fb62473d4ae8256dde20919b`;
its sl_version.h identifies 2.12.0. The traced baseline's historical
CMake pin is 2.12.0, archive SHA-256
`F5C0A3D870707DDDC3570FB4BCD3655CF48A8A68C3A9D342910CFA21B77DCF48`.

The following are actual file hashes and Windows file/product versions
read from the identified installed mod bundles' Shaders/Upscaling/Streamline
directories. Partial and latest bundles match each other in all six
files. The original untraced baseline matches the traced baseline's six
files. Comma-separated file-version values are shown as dotted versions.

| DLL               | Baseline file / product version | Baseline SHA-256                                                   | Partial / latest file / product version | Partial / latest SHA-256                                           |
| ----------------- | ------------------------------- | ------------------------------------------------------------------ | --------------------------------------- | ------------------------------------------------------------------ |
| nvngx_dlss.dll    | 310.7.0.0 / 310.7.0.0           | `BE6E434A94CA32499515EB62CA0E6C274526055D568D0426E4C652DCDFB6EE6E` | 310.9.1.0 / 310.9.1.0                   | `3975567B8943C53ACCE397F2B72380092F84F162D00B0D2C7D08A1025C563983` |
| sl.common.dll     | 2.12.0.0 / 2.12.0.0             | `C57930EF5A8A3FE9BE85EFDF71A61D8107C1148E8A6AED456464547128F7F4AE` | 2.14.1.0 / 2.14.1.0                     | `82924A8954DD671E09351C5DE0EB87AD0EB25B944CC9F9AB955CA1D9950DE15D` |
| sl.dlss.dll       | 2.12.0.0 / 2.12.0.0             | `A997022D2B93601E0EEFC3DDB3067C36DF386DD3163AE71E11095191FB14F8E4` | 2.14.1.0 / 2.14.1.0                     | `73BF52C0CFAA5900A8F3F4A91306E4625E7CCA696DFB305AAE44C9F97B582E1F` |
| sl.interposer.dll | 2.12.0.0 / 2.12.0.0             | `2A79DB6857AE8C75BBD871A9489C48BC6A39F7FCC88B9B02AFD53D0376CBEC66` | 2.14.1.0 / 2.14.1.0                     | `8C87C9499461DA561EDD529AA9BF7831D67D7B94EBB1C1A5ED54EF4934E1EA4C` |
| sl.pcl.dll        | 2.12.0.0 / 2.12.0.0             | `699AB461E64E95189A7FE6A21C79AD237CF56B60EA748CB6C840CD5431BA91D1` | 2.14.1.0 / 2.14.1.0                     | `F13D51CFA05F4CD514DF2026049E2DB8ADF359221713170AD386FD499915B582` |
| sl.reflex.dll     | 2.12.0.0 / 2.12.0.0             | `7E6E4CCC4B561BD449FB0DA90709D9B96B08C3F6F4697362CAAA359E72A58A67` | 2.14.1.0 / 2.14.1.0                     | `0CE9725E3E03EA9E7F81D008B57F33EE365973D2E349131C8B1C3E3378FE2DB0` |

These prove inspected bundle contents, not historical per-process vendor
module hashes or effective DLSS feature versions. Loaded-module paths,
any runtime override/update and feature-version query results for the
measured process still need correlation with retained evidence.
The runtime/header change is a real comparison variable; it is not yet a
proven source of regression.

## Protocol, saves and settings

All three traced runs use gameft-sw-v1, save order
**08, 11, 09, 12, 10, 13**, 60 seconds from the recorded world-entry
boundary per save. Means, P95/P99 and spike metrics use only the final
[50,60) seconds; settling uses the existing whole-minute definitions.
No raw-window segmentation or estimator was changed for this report.

Groups are consecutive samples at or above the tail median +2 ms,
including single-sample groups. Isolated spikes are a subset, not
additional disjoint events. Duty is the fraction of samples, not elapsed
time. These relative thresholds can hide a broad baseline shift, so
means and percentiles must accompany group metrics.

WPR 10.0.26100.7705, executable SHA-256
`9C57EEFED070655C09B64C46CAEEF5E0B5E3E61170D9D60D6247CA2BEB84F6DD`.
The identical trace profile SHA-256 is
`136A9FEEDF5211A5097C3F15AE1299AC9288D1A2F1F9AC277AA0ECC16D9860B4`.
The three preserved base-script hashes match:

| Script                             | SHA-256                                                            |
| ---------------------------------- | ------------------------------------------------------------------ |
| Compare-GameFtRuns.ps1             | `988475D39F8FAFC39DD5E115E5C419FB517576BC1FEE2FB5C71A30F2FDFC8F17` |
| Show-SaveLoadTimingQuickReport.ps1 | `1D08975EB1DBD027DD362E0F61FF06F67ADD1E0B63B1D8785F8D66DD9941983B` |
| Invoke-SaveLoadTimingV2.ps1        | `63E515ED8335EEFBA7BFA1D07FDAD04F451F90D8B79FC4B9FB86FB8419861CC9` |

| Save | Scene in exact save filename | Per-save measured routing                                    | Engine / output dimensions |
| ---- | ---------------------------- | ------------------------------------------------------------ | -------------------------- |
| 08   | WhiterunDragonsreach         | Native owner, dlss/q0 (DLAA), render scale inactive          | 4936x2740 / 4936x2740      |
| 09   | WhiterunJorrvaskr            | Native owner, dlss/q0 (DLAA), render scale inactive          | 4936x2740 / 4936x2740      |
| 10   | WhiterunBanneredMare         | Native owner, dlss/q0 (DLAA), render scale inactive          | 4936x2740 / 4936x2740      |
| 11   | WhiterunWorld                | VRRenderScaleMode, dlss/q1, SubmitStageIntermediate, latched | 4192x2328 / 4936x2740      |
| 12   | Tamriel, save 12             | VRRenderScaleMode, dlss/q1, SubmitStageIntermediate, latched | 4192x2328 / 4936x2740      |
| 13   | Tamriel, save 13             | VRRenderScaleMode, dlss/q1, SubmitStageIntermediate, latched | 4192x2328 / 4936x2740      |

These are engine/output dimensions, not an assertion about foveated
vendor input extents. DLAA also executes the DLSS provider; absence of
submit-stage render-scale counters does not mean no vendor execution.
The DLAA and DLSS groups use different scenes: mode and scene are
confounded. Save 13 is retained individually.

All six ESS and co-save hashes in the partial/latest provenance match
the traced baseline. Exact filenames/paths and byte lengths are in the
linked provenance; immutable content identities follow:

| Save | ESS SHA-256                                                        | SKSE co-save SHA-256                                               |
| ---- | ------------------------------------------------------------------ | ------------------------------------------------------------------ |
| 08   | `B56CFDDDD01B8411586D49CDD713A29D2454410E3658CAFB64DB71F3ECA142AA` | `B3F337751570316DFE7AA0833B9D764CE53BE92538FB196E9935F881321918C2` |
| 11   | `DBE174B740CF74EB13B188E8F008F0D211C617DC9DBDC1AEE93DA74D9E64E7F3` | `C0C0A3CDD73CE14441583AECF9F34593B7E8CF0290AF60A50FEAE5463BFF1A5E` |
| 09   | `BDFF8548C6942C6613DC1E09FA969975240797AF3E23C0E3AFE5CACD18C587B9` | `A8D0449E8061D580F2DEFAB9220CCF1F984613E169042CC8B4139E786B0D8043` |
| 12   | `F4260D348C90B3E197C12FF2973D8DAC85484904F5243BC4B34B36B18D0DDEF8` | `ACD3B084F4E3AD6DA90E97FACC742A4DF207A1510B5A809C9B116BFF29F212E2` |
| 10   | `A3129A64459F10FCF119988A3A8D1B30F575B9EF5967DF4F41540442A4633C7F` | `2E7BBD101926A4F06DFCD66E336603733FA39AF9716E48306C189BB78EE05FA0` |
| 13   | `DEE2A1F3AB1B22EDA07391129036B2E3CA5BCFA8E1E2653BC5D5D38FBA8B4FA3` | `3C2E2D284E9055EFC658B1C216C56B98C31978A86171BD6EC79745E7457F6873` |

Full effective feature snapshots are retained before/after each run,
including failures and unsupported-service reasons. Feature arrays match
within each run. Partial and latest feature settings match each other.
Endpoint equality does not prove unchanged transient settings during
every hold; per-save routing above comes from health receipts.

Shared relevant settings: dlssPreset=1, dlssSharpener=1,
foveatedVendorDispatch=true, foveatedCenterArea=0.30000001192092896,
horizontal scale=1, eye offsets=0, mask visualization=false;
periphery TAA enabled with center=0.30000001192092896,
feather=0.05000000074505806, outer scale=0.699999988079071.
Sharpness DLSS/FSR=0.8999999761581421; Reflex low latency=true,
boost=false, markers=true, FPS limiter=false; frame generation mode=0;
FSR4 runtime=false; structured/pipeline diagnostics=false.
Snapshot qualityMode=1 is not substituted for per-save q0/q1 execution.

LightLimitFix is loaded (3-5-1); particle lights, detection, culling and
optimization are enabled, max distance=6000, particles/emitter=256.
Contact shadows and particle contact shadows are disabled.
TruePBR is loaded (1-0-5), but settings=null and
settingsStatus=not_applicable: actual enabled state and per-draw
material ownership are not established by that feature snapshot alone.

Baseline-to-corrected schema/settings additions remain visible:
ScreenSpaceGI.ExperimentalOCUEffectFoveation=false;
Wetterness.PuddleMaskMode=1; Upscaling.fsrSharedGuideInputs=true,
fsrTemporalTuning (disabled, with its retained coefficients),
motionAdaptiveRCAS=false and associated controls,
renderScaleLinkedToUpscaling=true. Do not equate an absent old field with
false or claim that all additions are render-neutral without evidence.
Some existing settings-comparison.json reports use the intervening b46f
run as reference; their empty common-field diff is not proof that the
original baseline's complete schema is identical.

## Confirmed timing and health facts

Existing saved final-10-second means, milliseconds; parentheses are
arithmetic deltas versus the traced baseline. These are selected evidence
from the complete linked report, not a new finalized assay or replacement
for its complete timing/health evidence.

| Mode / save | CPU baseline | CPU partial (delta) | CPU latest (delta) | GPU baseline | GPU partial (delta) | GPU latest (delta) |
| ----------- | -----------: | ------------------: | -----------------: | -----------: | ------------------: | -----------------: |
| DLAA 08     |        4.664 |      5.150 (+0.486) |     6.544 (+1.880) |       10.410 |      9.835 (-0.575) |     9.684 (-0.726) |
| DLAA 09     |        3.602 |      5.506 (+1.904) |     4.511 (+0.909) |        9.537 |      9.176 (-0.361) |     9.227 (-0.310) |
| DLAA 10     |        2.937 |      3.380 (+0.443) |     3.515 (+0.578) |       10.036 |      9.610 (-0.426) |     9.488 (-0.548) |
| DLSS 11     |        7.784 |      8.304 (+0.520) |     8.192 (+0.408) |        7.835 |      8.191 (+0.356) |     8.391 (+0.556) |
| DLSS 12     |        6.854 |      7.093 (+0.239) |     7.328 (+0.474) |        7.576 |      8.976 (+1.400) |     8.532 (+0.956) |
| DLSS 13     |        8.675 |     15.826 (+7.151) |    13.480 (+4.805) |       10.259 |     12.446 (+2.187) |    11.881 (+1.622) |

Latest CPU exceeds the user's +0.35 ms allowance in every scene.
Latest GPU is below baseline in the three DLAA scenes and above
+0.15 ms in all three DLSS scenes. Relative to the partial reversal,
save 13 improves by 2.346 ms CPU and 0.565 ms GPU, but GPU slow-frame
share rises from 12.87% to 18.51%. This is a mixed outcome, not a
uniform smoothness improvement or a confirmed new regression boundary.

Latest final health and six precise accounting checks pass in the
retained final analysis. Partial save 11 has a distinct incomplete-stereo
terminal failure and a precise accounting warning:
10005.4254 ms reconstructed for a 10000 ms window, exceeding the saved
5 ms tolerance by 0.4254 ms. Its precise results remain starred per the
user's instruction, not promoted to a pass. Both traces report zero
lost events/buffers and cover all six holds.

Some startup wrapper/packaging receipts retain pending or UNTESTED
states; use the completed analysis-status.json, provenance and trace
results to determine final run status. Baseline services that did not
exist (accepted draws/OCU/preset compatibility) remain unsupported,
not zero. Timing/health does not substitute for the physical-HMD visual
qualification or prove the original COC hang's complete cause.

## Existing implementation and isolated evidence

Source was inspected at 503fbfc; the identical tracked tree links it to
the measured 933a4e2 renderer. No optimization was implemented.

| Concern                         | Production owner / path inspected                                                                                                                                                                          | Evidence and safety boundary                                                                                                                                                                                                                                                                                                      |
| ------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Light ownership                 | [SceneLightSnapshot.h](../../src/Features/LightLimitFix/SceneLightSnapshot.h); Reset, GetSceneLightSnapshot, RenderVRShadowLights and Prepass in [LightLimitFix.cpp](../../src/Features/LightLimitFix.cpp) | RetainScene copies owners from all five engine lists under lightQueueLock. Reset clears cached snapshots; shadow rendering also has a local snapshot. Allocation/hash/reference work exists. These lifetimes differ: raw pass pointers are lookup keys, not ownership sources. Preserve references until every consumer finishes. |
| Material admission              | ProbeVRLightingMaterial / ShouldSkipInvalidVRLightingMaterial and immediate-render entries in [Hooks.cpp](../../src/Hooks.cpp); [TruePBR.cpp](../../src/TruePBR.cpp), [TruePBR.h](../../src/TruePBR.h)     | One immediate route checks admission before calling another checked path; replay also needs validation. Terrain/particle callbacks intervene, so removing a check requires proving validity across them. UsesCustomMaterialSetup tests actual TruePBR conditions. SE/AE bypass this VR guard.                                     |
| Native stereo submission        | WaitGetPoses, native BSOpenVR pair hook and eye submit in [InSceneOverlay.cpp](../../src/Features/VR/InSceneOverlay.cpp)                                                                                   | Raw-texture ABI correction constructs the expected descriptor/default flags. Thread/generation/cycle/resource proofs and post-pair relatch service remain. Native renderer ownership must remain.                                                                                                                                 |
| Presentation / relatch          | g_vrRenderScalePresentationWorkMutex and ServiceVRRenderScaleRelatchAtFrameBoundary in [Upscaling.cpp](../../src/Features/Upscaling.cpp)                                                                   | Queue mutex is acquired before the relatch no-work return. Tiny lock samples and no matched blocked waits do not justify removing ownership. Several inlined locks share hook stacks.                                                                                                                                             |
| Prepared colour, guides, output | Upscaling producer, eye-mask selection, copy/sanitize/encode/fallback; [Streamline.cpp](../../src/Features/Upscaling/Streamline.cpp) EvaluateDLSS                                                          | Separate prepared input/output are tagged; one vendor evaluation per eye is expected. Colour-managed DLSS/NR input must remain readable for later work.                                                                                                                                                                           |
| Draw observers                  | [AcceptedDrawRegistry.cpp](../../src/Api/AcceptedDrawRegistry.cpp) and caller admission                                                                                                                    | Atomic no-observer fast path, separate registration/dispatch lifetimes. Partial/latest endpoint snapshots show zero subscribers/events/callbacks/replays/faults and complete observer snapshots. This is not proof of zero fast-path cost or no transient registration. Baseline service absence is not zero.                     |
| Runtime packaging               | [Streamline-Runtime.cmake](../../cmake/Streamline-Runtime.cmake) and pinned headers                                                                                                                        | Bundle difference is verified. Treat it as a lower-priority compatibility control, not an assumed cause or permanent downgrade proposal.                                                                                                                                                                                          |

### Latest freshness checks

The existing audit differences health-49 and health-59 receipts over
approximately 9.972–10.032 seconds. Those are **not** the exact fpsVR/WPR
[50,60) windows.

-   All outer-boundary rejection categories are zero at the final lifetime
    snapshot: 89,698 accepted boundaries, including pre-assay activity.
    Vendor retries are zero.
-   Late DLSS saves 11/12/13 each have matching accepted-input, guide,
    colour-copy, sanitization and vendor-attempt eye counts:
    2022 / 2102 / 1164. Flags/resource/world-frame rejections in these
    late intervals are zero.
-   Lifetime producer world-frame rejections total 1,346, during loading
    save 09 (724) and save 10 (622), not growing in steady windows.
    Lifetime values must not be charged to the last ten seconds.
-   Prepared-eye masks are not exported. Source selects 0x3 for an accepted
    peer proof, otherwise the current-eye bit. Counters support restored
    pair preparation but do not supply a measured mask histogram.
-   Current-eye fallback completion count is not exported. Input/output
    fallback reuse hits are zero; these are not normal pair-sharing counts
    or counts of fallback encodes.
-   Native DLAA does not advance these render-scale submit counters.
    That does not mean native DLAA skipped vendor execution.

### Latest targeted WPR checks

The retained audit scanned 1,169,662 sampled rows and 434,560
render-thread precise rows and reproduced the six saved window totals.
The following are overlapping inclusive sampled CPU milliseconds per
recorded fpsVR frame, **not direct function timings**:

| Path                                        | Observed latest evidence                                           | Interpretation                                                                                                 |
| ------------------------------------------- | ------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------- |
| RetainScene                                 | 0.014–0.022 ms/frame                                               | Real ownership/allocation work, small versus multi-ms residuals.                                               |
| Explicit snapshot try_emplace               | 0–0.002 ms/frame sampled                                           | Sparse and inlined; no sample does not mean no cost.                                                           |
| Light queue spin lock                       | Two late-window samples, both save 11; eight across complete holds | No evidence of sustained spinning. No blocked wait does not exclude spinning.                                  |
| Explicit NiPointer / NiRefObject operations | 0.007–0.038 ms/frame                                               | Overlaps retention; do not add inclusive categories.                                                           |
| ShouldSkip / Probe union                    | 0.028–0.128 ms/frame                                               | Largest specifically requested resolved path, but baseline delta and safe consolidation scope remain unproven. |
| Direct presentation-hook lock candidates    | About 5.012 ms across all six ten-second tails                     | No matching blocked waits in tails or full holds; exact lock object unavailable from stacks alone.             |
| Relatch service                             | No separately resolved sample                                      | Short/inlined work can be attributed to its caller; independent cost unavailable, not zero.                    |

Broad RenderVRShadowLights inclusive time (about 2.38–2.40 seconds per
ten-second DLAA window) includes actual native shadow rendering. It must
not be called snapshot overhead. Approximately 0.38–0.99% of render-thread
sampled time lacks stacks; SkyrimVR private symbols are unavailable.
Exact CSX symbols resolve the targeted paths. This WPR profile does not
trace GPU execution.

## Hypotheses and expected signatures

Priority follows actionable code evidence. The user wants the runtime
difference tested, but as a lower-priority control: updating remains
necessary, and retaining an old runtime is not the proposed solution.

| Hypothesis                                               | CPU-only / common-mode signature                                        | DLSS-specific / GPU signature                                        | Discriminating evidence                                                                                                                              |
| -------------------------------------------------------- | ----------------------------------------------------------------------- | -------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------- |
| Repeated safe material admission contributes             | Active per-draw CPU cost in either AA mode, scaling with eligible draws | No direct extra DLSS GPU work; secondary scheduling effects possible | Match baseline guard samples and admission counts; current ~0.028–0.128 ms/frame cannot simply be equated to the full residual.                      |
| Owning light snapshots cause churn                       | Heap/reference work in both modes, scaling with captured lights         | No direct extra inference; possible delivery-cadence effect          | Separate cached-prepass/local-shadow lifetimes, counts and allocation cost. Current retention samples are small.                                     |
| Prepared input / guide scheduling remains expensive      | Preparation/driver work in DLSS render-scale submit                     | Extra preparation/copy/guide GPU duration or queue gaps in that path | Pair proof now accepts; persistent freshness rejection is unsupported. Need direct masks/fallback counts and GPU pass attribution, preserving input. |
| Native rendering / scheduling contributes                | More active CPU, ready delay or scene-dependent long groups             | Scene complexity or reduced CPU supply may affect GPU delivery       | Keep save 13 separate; correlate saved slow groups with stacks and scheduler states. Inclusive native rendering is not CSX bookkeeping.              |
| Presentation or relatch contention remains               | Matching blocked waits or spin samples at the owning path and phase     | Indirect queue starvation                                            | Existing steady evidence does not support sustained contention; reopen only on event-correlated evidence.                                            |
| Vendor version / effective feature selection contributes | Vendor/Reflex CPU work could change in both native DLAA and DLSS        | Provider GPU cost could differ by mode                               | First resolve historical loaded modules/features, then a compatible one-variable control; packaging alone proves no cost.                            |
| Accepted-draw callbacks contribute                       | Callback/replay counts and matching stacks                              | Observer/scene dependent                                             | No workload at retained endpoints. Lower priority unless actual runtime observer activity is demonstrated.                                           |

A CPU-only source cost can affect both modes without raising GPU execution
time. A DLSS preparation issue should track the submit path and eye
preparation evidence. A generic DLSS library change may affect DLAA too.
Interior versus exterior saves do not isolate these signatures.

## Missing evidence

1. Repeated matched six-save runs of unchanged corrected source, with
   thermal/clock/background-load context. Three-save repeat variation
   cannot supply confidence intervals for different scenes/tracing.
2. Identically filtered baseline/partial/latest stack attribution and
   event-correlated slow-group windows. Latest-only sampled costs are
   not regression deltas. fpsVR CPU time, sampled execution, ready delay
   and blocked wait are different quantities.
3. Direct eye-mask distribution and current-eye fallback completion
   counts; independently sampled atomics, rejections and reuse hits
   do not supply them.
4. GPU pass/queue timing for prepared colour, guides, vendor execution
   and postprocessing. CPU-only WPR cannot establish a shader or
   inference cause.
5. Historical loaded vendor module paths/hashes and effective feature
   version/preset/model, plus foveated dispatch dimensions. Packaged
   versions and a configured numeric preset are insufficient.
6. TruePBR enabled/per-draw material state, admission counts and exact
   light-consumption lifetimes. Matching shader/cache and visual evidence
   is required for a behavior-neutral optimization claim.
7. Traced baseline archive missing at its recorded path; installed
   DLL/PDB and retained manifest/trace evidence remain. Older game-ft
   settings/provenance gaps remain separate. No inferred commit fills
   a missing identity.
8. Partial save 11 health/accounting failures; PR65 precise-window and
   final-settings limitations; first b46f failed WPR. Latest passes do
   not retroactively validate earlier failures.
9. Neither PR93's originating-machine 70 COCs nor later successful COCs
   proves the original corrupting instruction, every third-party writer,
   screenshot/flowmap regeneration, live SE/AE behavior or canonical
   physical-HMD visual qualification.

## Proposed one-variable test order

This is a plan only. Stop for document review before implementation or
running new benchmarks.

1. **Use retained evidence first.** Compare baseline, partial and latest
   WPR with identical categories and [50,60) boundaries; correlate saved
   slow groups with running/ready/wait states. Separate light ownership
   from inclusive native shadow work. Resolve provenance gaps without
   changing measurement windows or protocol.
2. **Repeat an unchanged control.** Pin 503fbfc tracked source and any
   subsequently authorized new compiled identity, current runtime,
   settings and saves. Use unchanged gameft-sw and the same six-save
   order. Retain every scene and repeated pair; present timing/health
   before provenance as the saved protocol requires.
3. **Measure actionable CPU work.** If existing stacks cannot resolve
   it, choose one diagnostic family: material-admission counts/timing,
   or light-retention counts/timing if baseline evidence makes that the
   larger candidate. Use bounded diagnostics compiled only with
   DEVBENCH_BRIDGE_ENABLED; no hot-path text logging. Quantify diagnostic
   overhead against the unchanged control. Do not alter protections.
4. **Measure DLSS preparation separately.** A separate diagnostic build
   may expose masks, fallback completions and bounded GPU timings for
   preparation/vendor work. Use an equally instrumented control. Keep
   colour-managed prepared input, per-eye execution and ownership intact.
5. **Test the runtime control at lower priority.** Resolve actual
   loaded modules first. If compatible, vary one vendor version axis
   with identical CSX source/settings/protocol. Do not change headers,
   all Streamline DLLs and DLSS simultaneously and claim a single-DLL
   result. If compatibility requires a matched set, explicitly treat
   the bundle as the variable and avoid component-level attribution.
   Do not mix unsupported binaries. Seek a forward-compatible remedy,
   not a permanent downgrade.
6. **Only then implement a measured candidate.** First construct a
   focused test reaching the production policy. Consolidate material
   admission only with equivalent validation across callbacks/replay;
   reuse light storage only while preserving all owners until the last
   raw-pointer consumer. Separate these into independent functional
   commits/builds. No speculative lock, timeout or scheduling changes.

For a later runtime change, run focused tests and complete supported
validation, then matched timing and applicable transition/stereo/visual
qualification. Keep the canonical scene/profile matrix and health
requirements unchanged.

## Constraints retained

-   Preserve user changes, caches, outputs, presets, defaults, shader quality
    and render scale; no unrelated reformatting.
-   Never reintroduce ID3D11Multithread::SetMultithreadProtected.
    Preserve the native renderer critical section and try-only loading
    guard.
-   Preserve malformed-light/material protections and owning references
    throughout raw-light consumption.
-   Preserve colour-managed DLSS/NR prepared input for later readers;
    no in-place output shortcut.
-   One vendor evaluation per eye is expected.
-   Diagnostics only under DEVBENCH_BRIDGE_ENABLED or an explicit
    diagnostics-only build flag; no normal per-frame/per-draw logging.
-   Do not pool traced and untraced timings. One functional change per
    commit and one variable per build, after recorded existing evidence.

## Evidence fingerprints

These identify the exact inspected local inputs; full settings and
dependency identities remain in the receipts. They are not rewritten by
this investigation.

| Build                         | Evidence file                                                                                                           |  Bytes | SHA-256                                                            |
| ----------------------------- | ----------------------------------------------------------------------------------------------------------------------- | -----: | ------------------------------------------------------------------ |
| Traced baseline               | [provenance.json](../../build/bisect/measurements/gameft-sw-20260915T183826985Z/provenance.json)                        |   6746 | `1F7E9910B6E43F2A331C26CDFFF7ED01804DAD43C38E517660ABC3DE89BF4BE2` |
| Traced baseline               | [aio-receipt.json](../../build/bisect/measurements/gameft-sw-20260915T183826985Z/aio-receipt.json)                      |   2410 | `4AB0E127A9DB931F9C7D887BD415597F1D4DF624B59527D9A41142C7BF442430` |
| Traced baseline               | [CSX.BuildManifest.json](../../build/bisect/measurements/gameft-sw-20260915T183826985Z/CSX.BuildManifest.json)          |   4253 | `678315417B84896CE3F596F29A78F13717D83AF70D640212C223A6C57F760328` |
| Traced baseline               | [gameft-sw.json](../../build/bisect/measurements/gameft-sw-20260915T183826985Z/gameft-sw.json)                          |   2393 | `1D851D34B561408D48A26F4557417394856A85054CF8FEE0077401708F000787` |
| Traced baseline               | [quick-summary.json](../../build/bisect/measurements/gameft-sw-20260915T183826985Z/quick-summary.json)                  | 278889 | `CE57D13D55A09928E5EB517E5E55A69910D92E7D1A1B6113860202E9172285D2` |
| Traced baseline               | [stack-wait/before.json](../../build/bisect/measurements/gameft-sw-20260915T183826985Z/stack-wait/before.json)          |  84249 | `6620F46DD2765D2369C1C8150A6F9B46B3D1C438DB57A9621246920009167EFB` |
| Traced baseline               | [stack-wait/after.json](../../build/bisect/measurements/gameft-sw-20260915T183826985Z/stack-wait/after.json)            |  84249 | `05AE618C7D1F99CFAF2617470BEA181887BD12463E45E6A630C99B544FC5D676` |
| Partial PR93 reversal         | [provenance.json](../../build/bisect/measurements/gameft-sw-20260915T223347887Z-be57afea/provenance.json)               |   6836 | `AA06DE36ABE25AE28E1B54A4499A283A7DEC50B2511769CF52C87BBABE365601` |
| Partial PR93 reversal         | [aio-receipt.json](../../build/bisect/measurements/gameft-sw-20260915T223347887Z-be57afea/aio-receipt.json)             |   1818 | `01D5E634B8BAC542EF15A14F0928F6B87BE6B7726D232C0BE37A0EEA1E95385B` |
| Partial PR93 reversal         | [CSX.BuildManifest.json](../../build/bisect/measurements/gameft-sw-20260915T223347887Z-be57afea/CSX.BuildManifest.json) |   4228 | `C6855A9AD7AF0085C855CFBB98D70E7941D895D0287C0BF71C6CB08F39D4DDB5` |
| Partial PR93 reversal         | [gameft-sw.json](../../build/bisect/measurements/gameft-sw-20260915T223347887Z-be57afea/gameft-sw.json)                 |   1652 | `87BEB01A928E602F53BDE8F34FDE747C92B7300FB93B3EC0B228846F817B55DF` |
| Partial PR93 reversal         | [quick-summary.json](../../build/bisect/measurements/gameft-sw-20260915T223347887Z-be57afea/quick-summary.json)         | 286042 | `879072E7E214F6877A08DBBD8675F53EC222D0BB12AA3901448739B2100C5E40` |
| Partial PR93 reversal         | [stack-wait/before.json](../../build/bisect/measurements/gameft-sw-20260915T223347887Z-be57afea/stack-wait/before.json) |  85539 | `83C0764C5DB20CA1E18EBC5EDD6DDC6F86BC953C913677348C67CA13C70EB61B` |
| Partial PR93 reversal         | [stack-wait/after.json](../../build/bisect/measurements/gameft-sw-20260915T223347887Z-be57afea/stack-wait/after.json)   |  85537 | `D8B623DCA142BA801FBE442DF7EED521DFB838BABE0242B58579A0CBA0DF832E` |
| Native stereo-hook correction | [provenance.json](../../build/bisect/measurements/gameft-sw-20260915T234651019Z-dd4d7c3e/provenance.json)               |   6899 | `1372B32AE9F01AF8ED9F69E0BEDE67C04A9CED1CFBD87364E6854EE08733C970` |
| Native stereo-hook correction | [aio-receipt.json](../../build/bisect/measurements/gameft-sw-20260915T234651019Z-dd4d7c3e/aio-receipt.json)             |   6264 | `A62851215DC11F808CEBCDA80CBD79818D73530EF357307F469595B67F4E3D96` |
| Native stereo-hook correction | [CSX.BuildManifest.json](../../build/bisect/measurements/gameft-sw-20260915T234651019Z-dd4d7c3e/CSX.BuildManifest.json) |   4228 | `F598233A2EE4F558C72EEBEE146D515E6CB5F7C112940303FDE9CBD00B3C7F80` |
| Native stereo-hook correction | [gameft-sw.json](../../build/bisect/measurements/gameft-sw-20260915T234651019Z-dd4d7c3e/gameft-sw.json)                 |   1839 | `531EF7705DE9A63BF2F89567A42578714040CA8352806B1473AA88A0EB3E7B46` |
| Native stereo-hook correction | [quick-summary.json](../../build/bisect/measurements/gameft-sw-20260915T234651019Z-dd4d7c3e/quick-summary.json)         | 291451 | `4BCB5FFD4B27772C701EC253408162F472450CDD86891A03AC5422D316B1373C` |
| Native stereo-hook correction | [stack-wait/before.json](../../build/bisect/measurements/gameft-sw-20260915T234651019Z-dd4d7c3e/stack-wait/before.json) |  85537 | `A03C1BC3769EF6C02880A529C6A1646E43893A32A26C012FA299DA54DC782582` |
| Native stereo-hook correction | [stack-wait/after.json](../../build/bisect/measurements/gameft-sw-20260915T234651019Z-dd4d7c3e/stack-wait/after.json)   |  85537 | `47668AF43E78632D371F974B2259822408C1B99CAB8C31C4DC22BE097D1E6984` |

## Validation and repository status

Repository identity, recursive submodules and tree equivalence were
checked through tools/git.ps1. Physical CSX DLL/PDB and six vendor DLL
hashes/versions were inspected read-only; archive hashes were checked
where available. All findings above reuse retained evidence.

No DLL/controller/shader/runtime test was built or run in this
documentation task. No new pass is claimed from prior validation.
Only this Markdown document is added on the investigation branch.
No source, settings, presets, protocol scripts, existing evidence or
submodule state was changed. The initial handoff was uncommitted; the
user subsequently authorized committing and pushing this document on
the investigation branch.

Documentation validation commands:

```powershell
pwsh ./tools/pre-commit.ps1 run --files docs/development/cpu-dlss-regression-investigation-20260916.md
pwsh ./tools/git.ps1 diff --check
pwsh ./tools/git.ps1 status --short
```

Scoped whitespace, line-ending and prettier checks passed; YAML,
clang-format and gersemi had no applicable files. Diff check passed.
All 50 distinct local Markdown link targets exist. Initial handoff status,
before the subsequently authorized documentation commit:

```text
?? docs/development/cpu-dlss-regression-investigation-20260916.md
```

## Matched trace follow-up — 2026-09-16

This section supplements the original investigation above. Historical
measurements and their receipts remain unchanged. The analysis branch
remains `perf/cpu-dlss-regression-20260916`, based on documentation commit
`da783bd0a9e55b7e55210288f4301bba6837dc0c`. No production source, defaults,
presets, rendering settings, saved measurement windows or protocol changed.
No build, deployment, commit or push was performed for this follow-up.

The immutable local outputs are in
[cpu-dlss-cross-build-differential-20260916](../../build/bisect/measurements/cpu-dlss-cross-build-differential-20260916/analysis-report.md).
Read the [full per-save comparison](../../build/bisect/measurements/cpu-dlss-cross-build-differential-20260916/per-save-cross-build.csv),
[every CPU slow group](../../build/bisect/measurements/cpu-dlss-cross-build-differential-20260916/slow-group-attribution.csv),
[thread running/ready/wait analysis](../../build/bisect/measurements/cpu-dlss-cross-build-differential-20260916/ready-wait-analysis.csv),
[identity and clock evidence](../../build/bisect/measurements/cpu-dlss-cross-build-differential-20260916/evidence-verification.json),
[validation receipt](../../build/bisect/measurements/cpu-dlss-cross-build-differential-20260916/validation.json)
and [output hashes](../../build/bisect/measurements/cpu-dlss-cross-build-differential-20260916/output-manifest.json).
These local artifacts are not versioned raw evidence.

### Original trace gap and separate clean repeat

The user deleted the original partial-reversal ETL, sampled/precise exports
and raw fpsVR data. Their retained aggregate receipts remain usable only
for the values they actually contain. New stack categories, private
addresses and per-group attribution for that original run are unavailable,
not zero. Original partial save 11 retains both its terminal incomplete
stereo failure and 10,005.4254 ms reconstructed in a 10,000 ms window,
exceeding the saved 5 ms tolerance by 0.4254 ms.

The separate clean repeat is
[gameft-sw-20260916T075210921Z-74778e53](../../build/bisect/measurements/gameft-sw-20260916T075210921Z-74778e53/provenance.json).
Its source is `5614c45180096acf6edde0d2b972a27d6a56a91a`, Build ID
`c2b3b90877a88ec2a4a0fb1251f5e70c6d3c4686cfb6cd39d9dbf831dbfaf281`,
and DLL SHA-256
`E2B89B03881C8AFA6B90F0F316B3B8C45F79FD677A61C8F5C013843F196358B4`.
Benchmark PID 19804 and process-start ticks 639251418512056706 are retained.
Its verified ETL SHA-256 is
`F1CF37F7B662948FC1660CEBDAAE3C45395DB7BD7EF9B9BB6D932E77594605B7`.
All six terminal health results and rendering-thread accounting checks pass.
This repeat does not replace or repair the deleted original measurement.

The earlier attempt `gameft-sw-20260916T074003659Z-81d118ac` overlapped
offline export/analysis activity and is excluded from clean comparisons.
The pause/interference records are preserved in the evidence verification.
Owned analysis work was stopped before the clean repeat and resumed only
after the user confirmed its completion.

Exact saved final-ten-second CPU means, milliseconds:

| Save / mode | Baseline | Original partial | Clean partial repeat | Latest hook | Latest minus baseline |
| ----------- | -------: | ---------------: | -------------------: | ----------: | --------------------: |
| 08 / DLAA   |    4.664 |            5.150 |                6.047 |       6.544 |                +1.880 |
| 09 / DLAA   |    3.602 |            5.506 |                5.536 |       4.511 |                +0.909 |
| 10 / DLAA   |    2.937 |            3.380 |                3.350 |       3.515 |                +0.578 |
| 11 / DLSS   |    7.784 |          8.304\* |                7.884 |       8.192 |                +0.407 |
| 12 / DLSS   |    6.854 |            7.093 |                7.607 |       7.328 |                +0.474 |
| 13 / DLSS   |    8.675 |           15.826 |               13.449 |      13.480 |                +4.805 |

`*` Original partial save 11 carries both warnings described above. Exact
GPU means, P95/P99, spikes, groups and all six comparison directions are
preserved in the complete CSV. No game-ft results are pooled with these
gameft-sw values; the different saves are not interchangeable replicates.

**The apparent 2.346 ms save-13 improvement from the hook correction is
not repeatably established.** The unchanged partial DLL improves by
2.377 ms in its repeat and is only 0.031 ms below latest. Both corrected
builds still show the large baseline residual. This does not invalidate
the ownership correction or establish its exact performance effect.

### Execution attribution and live native snapshot

Save 13 primarily executes more work: latest versus baseline rendering
thread time per recorded frame changes by +1.7361 ms running,
-0.0034 ms ready and -1.1293 ms waiting. These scheduler quantities are
distinct from fpsVR CPU latency and cannot arithmetically decompose it.
Native Skyrim leaf execution increases by about 1.3863 sampled ms/frame.
Sustained presentation-mutex contention, scheduler ready delay and named
Streamline/NGX CPU work are demoted as the main explanation of that residual.

Material admission remains a worthwhile smaller optimisation target at
about 0.03–0.13 sampled ms/frame. It is too small to explain the full
residual, not too small to improve safely. The baseline lacks the same
guard, so its equivalent-safety cost is unmeasured. Named light retention
is roughly 0.014–0.022 ms/frame in latest; reference/lock inlining and the
absence of the same baseline ownership path prevent a complete cost claim.
No removal of either protection is justified.

At the user's request, a new main-menu process, PID 21340, supplied live
code bytes from the same partial-reversal Build ID. This is not benchmark
PID 19804 or a dump of save-13 objects. Each captured code range was read
twice with identical hashes. No static disk executable was imported.
The reusable Ghidra project and snapshots are preserved at:

```text
D:\Coding\GitHub\GhidraProjects\CPU-DLSS-20260916-pid21340\NativeCPUHotRanges.gpr
```

Keep its `.gpr`, `.rep`, snapshots and metadata together. This is a
targeted code snapshot with PE header and complete unwind directory, not
a full-process dump. The [live-code evidence](../../build/bisect/measurements/cpu-dlss-cross-build-differential-20260916/native-live-evidence.json)
and [sampled region weights](../../build/bisect/measurements/cpu-dlss-cross-build-differential-20260916/native-live-regions.csv)
retain every matched caller stack and capture hash.

Live RVA `0xDA54B0` performs six-plane bounding-sphere tests;
`0xDA33C0` traverses compound-frustum operators. This semantic inference
matches existing CommonLib layouts and live instructions, not private
Skyrim symbols. Save 13 contributes respectively 0.5052 and 0.1214
sampled ms/frame in latest, versus 0.5193 and 0.1169 in the clean partial
repeat. No samples matched those exact ranges in baseline. The routines
are disjoint, but zero matches are not proof of zero execution.

Some parent stacks contain the native depth-render chain wrapped by
VolumetricLighting and TerrainBlending. Many are incomplete. A wrapper's
presence does not establish that feature as the cause. Nearby hot native
code performs job dispatch and cooperative wait/help operations; its CPU
samples remain executing work, not kernel blocked time.

### Three-mode depth-culling control

The user identified Legacy, Performance and Balanced as an additional
candidate. All retained before/after settings select **Balanced**, with
both culling preferences enabled and minimum occludee extent 10.
The [settings evidence](../../build/bisect/measurements/cpu-dlss-cross-build-differential-20260916/depth-culling-settings.json)
does not establish per-save effective activation or promotion counts.
Those counters were not retained by these captures.

Bounded recovery and Legacy were added on 2026-08-26, before baseline.
Their policy and activation headers are unchanged between the compared
sources. PR69 (`1afb9eca9c89814d794dae4d18eb1626aafd1209`) adds recovery
telemetry. The current recovery scope has no DevBench compile guard;
its timing/counter writer defaults enabled on motion-envelope misses.
That existing production-cost path remains a separate measurement target.
No instrumentation or source correction was made in this analysis.

The [identically filtered temporal samples](../../build/bisect/measurements/cpu-dlss-cross-build-differential-20260916/depth-culling-sampled.csv)
show no broad direct increase; latest save 13 has only three samples,
about 0.0052 ms/frame. This does not bound downstream work: Balanced can
promote up to 64 objects whose subsequent culling/rendering costs occur
outside the recovery hook. A later render-scale interaction remains
possible, but the native hotspot alone does not prove that relationship.

The next one-variable sequence should use the exact latest DLL and vendor
bundle with unchanged gameft-sw, save order `08, 11, 09, 12, 10, 13`:
repeat Balanced, measure Performance, then repeat Balanced. Keep all
other settings and telemetry state fixed. Record existing menu status
and recovery counters outside measured holds; add no in-hold polling or
window changes. Compare each save separately with health and visual
artifacts retained. No new build is needed. A separate Balanced telemetry
on/off pair can isolate PR69 observer cost; Performance versus Legacy
can subsequently isolate producer-pose work. These are proposed controls,
not a default change or an acceptable missing-geometry tradeoff.

### Validation and remaining limits

The immutable output audit passed: 558 comparison rows and six delta
directions, 1,863 exact CPU slow groups, 2,899 thread/window rows and
106,293 unresolved-symbol/address rows. All 24 windows remain exactly
`[50,60)`. Selected live instruction samples matched original stack rows
by timestamp, thread, count and weight. Retained receipt/raw fpsVR hashes
match; system ready-interval coverage matches render-only exports.
The original partial missing-data and save-11 warnings remain explicit.

The original core extractions used `analyze(label, run)` from the saved
`cross-build-differential-20260916.py` helper. The fresh repeat used an
isolated derived run so its original receipts remained unchanged.
Additional offline analysis and validation commands:

```powershell
python build/cpu-burst-diagnostics/cross-build-scheduler-20260916.py baseline
python build/cpu-burst-diagnostics/cross-build-scheduler-20260916.py latest
python build/cpu-burst-diagnostics/cross-build-scheduler-20260916.py repeat
python build/cpu-burst-diagnostics/cross-build-addresses-20260916.py baseline
python build/cpu-burst-diagnostics/cross-build-addresses-20260916.py latest
python build/cpu-burst-diagnostics/cross-build-addresses-20260916.py repeat
python build/cpu-burst-diagnostics/analyze-live-native-hotspots-20260916.py
python build/cpu-burst-diagnostics/analyze-depth-culling-20260916.py
python build/cpu-burst-diagnostics/write-cross-build-report-20260916.py
python build/cpu-burst-diagnostics/finalize-cross-build-report-20260916.py
```

Export profiles/results and the isolated repeat-derived analysis remain
in the local scratch evidence. Scripts refuse to overwrite finalized
outputs; commands document execution, not an instruction to replay them
against the same destination. No controller, shader or runtime tests
were run because no production code changed.

Remaining limits include deleted original partial raw data; incomplete
private/inlined symbols and caller stacks; no historical live-byte or
argument capture; sampling quantization and unequal frame counts; single
baseline/latest observations; uncertain additional fpsVR-to-ETW latency
and unavailable raw ETL header QPC; singleton groups without elapsed
intervals; endpoint-only settings/health/observer evidence; incomplete
recovery counters; scheduler concurrency without causal ownership; CPU
traces without GPU packet attribution; vendor-version differences; and
uncontrolled pose, scene, thermal and external-process variation.
The report details each limitation. Current evidence prioritizes culling
controls and safe material-admission investigation, but does not yet
justify an implementation intended to remove the main regression.

Final scoped documentation validation passed using
`pwsh ./tools/pre-commit.ps1 run --files docs/development/cpu-dlss-regression-investigation-20260916.md`
and `pwsh ./tools/git.ps1 diff --check`. Prettier formatted the appended
section on its first pass; the second pass passed. No historical lines
were changed. YAML, clang-format and gersemi had no applicable files.
Final working-tree status contains only this uncommitted Markdown change:

```text
 M docs/development/cpu-dlss-regression-investigation-20260916.md
```
