# VR render-scale iteration records

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
[`vr-render-scale-comparison-ledger.csv`](vr-render-scale-comparison-ledger.csv).
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
