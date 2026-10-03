# NR cost-command correction and retained Task 9/10 evidence

The current live run qualifies the earlier unpaused NR/FOV command fix,
Mode C three/four-region composition, and the tested compact retention
transitions. It also reproduces a separate `nr_cost` admission defect.
That control now shares the completed-frame command queue. The corrective
DLL passes offline validation and still needs its own focused live check.

Task 9 is not implemented in full: alternative actor splits/reanchors and
complete calibration remain outstanding. The tested Task 10 compact
candidate is rejected for production promotion because it regresses GPU
time. This negative result does not require repeated tests until it wins.
Broader moving-image quality and native residency remain unqualified;
they must not be represented as passed or silently removed from the spec.
Task 11 remains at its verified-ownership/category-packing audit.

## Producer and evidence

Local evidence root:
`build/validation/nr-task9-11-renderercommands-20261003/`.
Raw controller receipts, screenshots, execution companions, profiler
captures, configuration restoration and shutdown receipts remain local.
`audit.json` preserves all 384 stereo joins and six profiler windows;
`timers.csv` preserves the available per-pass GPU/CPU statistics.

| Live identity     | Value                                                                   |
| ----------------- | ----------------------------------------------------------------------- |
| Skyrim PID/start  | `53820` / `2026-10-03T18:38:51.7672591Z`                                |
| Producer Build ID | `47675771439e43f55b8bd82fc92fd1369ad4a4fffec82b0d280f0d77dfb80276`      |
| Compiled source   | `cb49c8b7be5c40673ee444add926ec1a5460748a`, dirty                       |
| Dirty digest      | `c3e1c4c6a6ebb3ed47707f28fab83c4e321087b9acb900877ef44f28e10fd434`      |
| DLL SHA-256       | `0311eae2a1929178019f09a3cf102bccc9c94a398a8e2548372c2486525dedc1`      |
| DLL bytes         | `31686144`                                                              |
| Enabled mod       | `CSX_AIO-main-vr-nr-NR-RendererCommands-DevBench-20261003-47675771439e` |

The exact enabled physical DLL under `C:/Modlists/Synergy/mods/` matched
its adjacent manifest and AIO receipt. Loose enabled providers, Overwrite
and unmanaged Data were checked; the virtual module path was not used as
sole proof. This compiled source identity is retained even though its
correction was subsequently committed as `aafa534c1`.

The sole live transport was the maintained DevBench controller at
`build/worktrees/automation-nr-controls-20261003/`, automation commit
`d51f000`, using the user's existing controller authorization. The direct
tool catalog still did not expose the NR tools. The corrected camera-read
response worked. No DevBench-server or automation source change was needed
for the newly reproduced CSX mutex failure.

## Live results

-   All 29 NR/FOV configure/reset requests passed without pausing the game.
    A, B and C transitions committed in both eyes. FOV `0.95` was exercised
    and the original disabled/`0.3` state was restored.
-   The broad face/skin/hair fixture selected one or two regions despite
    limits of three/four. Those limits alone are not higher-count proof.
    A face-only fixture produced actual **4/4, 3/3, then 4/4** C regions,
    with 32 exact stereo joins for each checkpoint and successful native,
    output and outer-pipeline commits. A/B observed two regions per eye;
    this run does not establish four-region A/B qualification.
-   Twelve 32-frame sequences yielded 384 unique, exact native-HMD/source
    frame joins, with fallback rejected. These use 16x16 structural burst
    images: they prove attribution and commit behavior, not full-image
    temporal/perceptual quality. Full HMD stills and the inspected scene
    preview are retained separately.
-   A proven-empty C selection advanced 259 world frames with no change to
    attempts, native evaluations, initialization, rebuilds, depth/control
    copies or output commits. Re-entry resumed four-region rendering.
-   The run ended with 129,405 successful native evaluations and 45,275
    successful stereo attempts; native/stereo failures, device removals
    and quarantines were all zero. The 68 total rebuilds span deliberate
    transitions, explicit resets and growth; they are not all churn.

Compact retention checkpoint counters were 48 at 768x768, 50 on expansion
to full 1008x1120 fallback, and still 50 after further frames and shrink.
An explicit reset cleared the slots; the next small request reached 52
with 256x256 compact slots. Later broader bounds may grow those slots.
The tested fallback does not oscillate back to compact automatically.
Preserve Source and unsupported A-mode requests retained full coordinates.

## Performance result: reject the tested compact candidate

Each window contains 300 completed profiler frames. These are self times
for `Upscaling::DLSSNeuralRenderingStereo`, not whole-frame time or a
separately measured native-only kernel. CPU and GPU clocks are separate.
No compilation overlapped these measurements; all 132 recorded performance
guards passed, including absence of the standalone temporal probe.

| Window                |   GPU mean ms |  CPU mean ms | Rebuild delta |
| --------------------- | ------------: | -----------: | ------------: |
| Broad full before     | 11.2542800903 | 1.5734970570 |             0 |
| Broad compact request | 12.9702987671 | 3.3751320839 |             4 |
| Broad full after      | 11.0102386475 | 1.5785117149 |             0 |
| Small full before     |  5.0592827797 | 1.6354854107 |             0 |
| Small compact         |  8.2104778290 | 1.5871379375 |             0 |
| Small full after      |  5.2424688339 | 1.5948671103 |             0 |

The broad compact-request interval includes growth and full-coordinate
fallback, so it is not a steady compact comparison. The small face-only,
three-metre fixture selected one actor. At the recorded checkpoint its
owned rectangle was 192x192 at (448,512), while both actual retained compact
capacities were 768x768. Current category bounds can require a larger
bucket. Source density was unchanged and the whole bucket was evaluated.

Against the small full-coordinate bracket midpoint of 5.1508758068 ms,
compact adds 3.0596020222 ms (**59.40%**). Baseline drift is 3.56%.
The excess alone consumes 27.5% of an 11.11 ms/90 Hz frame budget.
Logical active transport storage falls from 36,126,720 to 18,874,368 bytes
(47.76% less), but native private allocation bytes are unavailable.
That field is not zero and these are not total GPU residency measurements.

No compact production promotion or automatic measured-profile adoption
follows. Prior bitwise replay evidence remains useful; this control-only
correction does not justify rerunning the same offline GPU campaign.

## Reproduced cost-control failure and correction

With NR running, `nr_cost configure enabled=true` and status succeeded.
Disabling it then returned `renderer_busy`, `mutationApplied=false` in
`measured-policy-disable.json`. `MeasuredPlanControl` was entered directly
on the API worker and its internal planner-state `try_to_lock` could race
normal inference. This is separate from native renderer ownership used by
the previously corrected NR/FOV controls. The guard correctly rejected
mutation; it was not an observed game-rendering or device failure.

All four cost actions now use the existing completed-frame renderer queue
through `RunRendererCommand`. Request data is captured by value. The
single-command reservation, native ownership, admission deadline,
cancellation without late mutation, exception reporting and actual
completion response are reused. The internal planner mutex still fails
closed for an independent owner. The registered tool description states
the scheduling and timeout contract.

The declaration, implementation and caller remain inside
`DEVBENCH_BRIDGE_ENABLED`. No new production frame hook, polling, GPU wait,
resource, cost policy or settings surface was added. The schema stays the
same; only the source fingerprint and three generated preset compatibility
markers change. Actual rendering preset values are unchanged.

Adversarial review covered scope, lock ordering, callback lifetime,
concurrent admission, cancellation, error/mutation reporting and reuse.
The extracted scheduler tests cover these behaviors; the source-entry
guard now also checks that the real cost handler uses this dispatcher.
Live verification of this new handler path remains pending on the new DLL.

## Restoration and offline validation

NR was disabled through the normal accepted control before restoring the
old cost control. This allowed cleanup but is not the permanent fix or a
claim that live cost-command qualification passed. An invalid empty profile
was correctly rejected with `mutationApplied=false` and no profile loaded.

Original settings match fingerprint `4f9d26a4169698e79c873cf379f47728`;
NR, cost policy, capture and profiler are off as originally configured.
No AI, camera, movement or save/load commands were sent. Camera pitch/yaw
were unchanged; natural positional drift was approximately 0.60 game units.
The first colour-restoration request contained a read-only diagnostic key
and was rejected. A separate final preset request corrected custom-preset
selection; exact restoration was verified afterward. Those rejected runner
requests remain in the evidence, not counted as successful controls.

All owned captures were inactive before `qqq`. Skyrim's exact PID was
verified absent at `2026-10-03T19:10:31.5366882Z`; MO2 then closed through
File/Exit. SteamVR and session evidence were preserved. Session and access
ownership were released. Nothing was installed or relaunched.

`pwsh ./tools/validate-local.ps1 -OutputDirectory build/validation/nr-cost-command-validation-20261003`
passed the universal SE/AE/VR DevBench DLL build, all 227 discovered tests
(zero failures/skips), preset tests/checks, diff check, manifest/artifact
verification and unchanged-source verification. Scoped pre-commit passed.
Initial sandbox hook-cache and CMake argument-form failures were corrected
and preserved; they are not reported as passed invocations. This is not
live SE/AE validation or a bridge-off binary comparison.

The local `compiled-source/` snapshot retains the source patch and files
used by the build before this report was added. The new archive is
`dist/CSX_AIO-main-vr-nr-NR-CostControl-DevBench-20261003-48a11df59671.7z`;
its adjacent `.receipt.json` preserves the complete verification record.

| Corrective producer     | Value                                                                            |
| ----------------------- | -------------------------------------------------------------------------------- |
| Build ID                | `48a11df59671139bd70d3253535d38158ab6a50bf0771ff4dbd6561a2e7c528e`               |
| Compiled source         | `aafa534c137bb7b66c203ff353e1ccc23f7394b8`, dirty                                |
| Dirty digest            | `226b199ab8e3a888796aa02e57a7a5acb65702308850571d6b65da4fa93aefba`               |
| DLL SHA-256 / bytes     | `b9130fed512bb438b2ac141702e2648ad95a295d702c04e4059bc307b3dd1dd7` / `31688192`  |
| Archive SHA-256 / bytes | `be23ff893a866dd29260ff904d13051b885e85334843e01a2f54ccbff6b08938` / `199366008` |

DevBench is enabled, Tracy disabled, and SE/AE/VR enabled. Archive integrity
and all 386 extracted file sizes/SHA-256 hashes passed, including exact
producer DLL/PDB/manifest and the pinned NR runtime. No personal
`SettingsUser.json` or shader cache is included. No deployment occurred.

## Closure checklist and order of work

| Item                                     | Current result                                                 | Remaining action                                                                                                                                                                                               |
| ---------------------------------------- | -------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Prior NR/FOV admission defect            | Passed live                                                    | Do not repeat the full campaign solely for this fix.                                                                                                                                                           |
| C three/four-region outer composition    | Passed for the actual counts above                             | Preserve these fixtures and attribution limits.                                                                                                                                                                |
| Tested compact growth/fallback retention | Passed live                                                    | Broader movement/quality is not established.                                                                                                                                                                   |
| Cost-control mutex race                  | Fixed; offline validation passed                               | Focused unpaused enable/status/disable and profile-rejection check on the corrective DLL.                                                                                                                      |
| Task 9 candidate generation              | Current plan and local merges only                             | Implement bounded alternative actor splits and reanchors offline before claiming search completion. Preserve required context, exclusive output, final layout keys and stable identities.                      |
| Task 9 calibration                       | No qualified profile                                           | Match final keys to baseline brackets, transition/CPU/residency evidence and held-out prediction errors. Missing evidence must retain the existing heuristic.                                                  |
| Task 10 tested compact candidate         | Rejected for promotion: slower despite lower transport storage | Retain the negative result and default-off experiment; do not repeat until a specific implementation change or unresolved hypothesis justifies it. Full moving-image/residency qualification remains unpassed. |
| Task 11 equipment categories             | Ownership/packing audit and face/skin/hair baseline only       | Implement verified equipment attribution and the complete CPU/HLSL/UI/DevBench contract; no equipment pass is claimed.                                                                                         |

Earlier closure attempts mixed corrective runtime testing with unfinished
Task 9 implementation. Another broad in-game repeat cannot supply missing
candidate-generation code. Complete that code and its offline invariants
before requesting a full calibration session. Rejecting a compact candidate
is a valid experimental outcome; it is not permission to relabel the
remaining quality/residency requirements as passed. The small cost-command
retest is a distinct corrective-DLL check, not Task 9 completion.
