> Historical report, privacy-filtered for this export. Statements about
> current process state and planned work describe the original session.
> See the bundle README for later findings and omitted source files.

## Why

Skyrim VR's native shadow loop uses raw light pointers while loading
workers can release their scene owners. A retained capture showed a
light's destruction completing before its native render call returned.
The existing LLF frame snapshot protects LLF consumers; this native loop
needs ownership that lasts through its own render calls.

This addresses the observed lifetime hazard. It does not establish the
complete cause of the separately observed driver-command hang.

## What changed

-   Retain active and pending scene-light owners and copy accumulated render
    order under the scene's light queue lock. Keep local references through
    the entire native loop, independently of LLF frame/load resets.
-   Reuse the existing snapshot helper for the same five owning lists.
    Native capture omits unused active enumeration and copies render order
    in one range assignment to avoid repeated vector growth.
-   Replace the native selection loop with bounded, ownership-checked
    virtual dispatch while preserving the native index and return frame.
-   Extend the production-function ownership harness and native-hook tests,
    and document evidence, behavior and validation limits.

Seven files only. No shadow journal, driver recorder, decoder, exception
observer or diagnostic DevBench/CMake additions are included. The branch
starts from current main-VR, which already contains PR #93.

## Runtime and failure behavior

The executable patch applies only to Skyrim VR 1.4.15. All 53 original
loop bytes must match before either write; an unsupported runtime or
changed site leaves the loop untouched and reports the refusal.

Unknown owners, non-shadow objects or non-advancing indices end the pass.
Capture allocation failure skips the pass. Virtual rendering and final
reference releases occur outside the queue lock; rendering exceptions
propagate with local ownership unwinding. Existing render virtual hooks
remain in the call path. SE and AE receive no new executable patch.

The snapshot pins light lifetime; it does not synchronize arbitrary light
field or other engine-object mutations. Snapshot allocation and reference
counting have a cost. Performance neutrality remains unverified.

## Validation

Clean source: `cddabac0c7563ed7886246470bd250de92e72030`.
Build ID:
`d8c33e2807eacc0101b746a8b37e161e4f2da89720f3dd9aa867ebdc6f3229c0`.

Passed:

-   Universal Release DLL build with SE, AE, VR and DevBench enabled,
    automatic deployment and packaging disabled; exit 0.
-   SceneLightSnapshot and VRSceneGuards: both passed in 0.12 seconds.
    Coverage includes concurrent teardown during rendering, pending owners,
    raw-array clearing, stale/non-shadow entries, index bounds/progress,
    exception release, 13 allocation-failure cases and 477 hook assertions.
-   Seven-file scoped pre-commit checks and staged diff whitespace checks.
-   Linked DLL manifest verification: 28,757,504 bytes, SHA-256
    `3ee063d9ed332a61653e91a9d56ff2a3fdb15d7e182f0a43bad7fab96935b636`.
    The native guard marker is present; `driver_command_start` and
    `shadow_lifetime_start` are absent.

Build and test invocations:

```powershell
pwsh [REDACTED_LOCAL_PATH]
pwsh [REDACTED_LOCAL_PATH]
& '[REDACTED_LOCAL_PATH]' --test-dir [REDACTED_LOCAL_PATH]'^(SceneLightSnapshot|VRSceneGuards)$' --output-on-failure -V
```

The build script invokes the isolated checkout's `tools/cmake.ps1` with
`--build`, the build directory above, `--config Release`, the requested
targets, and `--parallel 4 -- /p:CL_MPCount=2`. Exact configure/build
scripts, logs, hashes and review evidence are preserved locally under
`[REDACTED_LOCAL_PATH]`; the final receipt is
`validation.json`.

Existing dependency warnings remain: FidelityFX's deprecated CMP0116
policy and HDE64 C4701. Neither failed the build.

Not run: in-game testing of this native-loop correction, matched
performance measurements, SE/AE runtime checks or shader assertions
(no HLSL changed). PR #93's 70 COCs used a DLL without this correction
and are not qualification for this PR. Keep this PR as a draft pending
its own runtime and performance evidence.
