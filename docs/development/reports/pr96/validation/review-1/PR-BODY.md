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
-   Reuse the existing snapshot helper for all five owning lists. Native
    capture omits unused active enumeration, copies order in one range
    assignment, and avoids allocation and retention for empty passes.
-   Replace the native selection loop with bounded, ownership-checked
    virtual dispatch while preserving the native index and return frame.
-   Verify the complete 184-byte native function before patching, including
    the register/stack setup and continuation restores the adapter needs.
-   Extend production-function tests and document evidence and limits.

Seven files only. No shadow journal, driver recorder, decoder, exception
observer or diagnostic DevBench/CMake additions are included. The branch
starts from main-VR with PR #93 already merged.

## Runtime and failure behavior

The executable patch applies only to Skyrim VR 1.4.15. An unsupported
runtime or changed function leaves the function untouched and reports the
refusal. Existing modifications to the surrounding frame are rejected
because its stack/register contract cannot then be assumed.

Unknown owners, non-shadow objects or non-advancing/wrapped indices end
the pass. Capture allocation failure skips the pass. Virtual rendering
and final reference releases occur outside the queue lock; rendering
exceptions propagate with local ownership unwinding. Existing render
virtual hooks remain in the call path. SE and AE receive no new patch.

The snapshot pins light lifetime; it does not synchronize arbitrary light
field or other engine-object mutations. Nonempty snapshot allocation and
reference counting have a cost. Performance neutrality remains unverified.

## Validation

Adversarial review covered scope, correctness, robustness and DRY. It
corrected incomplete instruction admission and needless empty-pass
capture, and strengthened branch-target and lifetime regression coverage.
No actionable source-review findings remain within the stated contract.

Clean source: `90d545008c1dda2b0d9e9d91b82021281698bbcd`.
Build ID:
`cd983f022fb67aa4ff83b251aa5b63adaa499b6d44ea568089f1f4d83719e4fe`.

Passed:

-   Universal Release DLL build with SE, AE, VR and DevBench enabled,
    automatic deployment and packaging disabled; exit 0.
-   SceneLightSnapshot and VRSceneGuards: both passed in 0.16 seconds.
    Coverage includes later lights surviving concurrent teardown during
    the first render, pending owners, raw-array clearing, stale/non-shadow
    entries, bounds/progress/wraparound, final destruction on exceptions,
    allocation-free empty passes, 13 allocation failures and 743 assertions.
-   Every-byte native-function mismatch rejection, installed branch
    destinations, and execution of the installer-generated adapter to
    verify arguments, stack alignment and native return identity.
-   All 184 instruction fixture bytes match the retained PID 22880 image.
-   Seven-file scoped pre-commit checks and staged diff whitespace checks.
-   Linked DLL manifest verification: 28,760,064 bytes, SHA-256
    `f24eb30a25d204d94666288f6550e9f687b61bbc40267bb14f649be66129e298`.
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
targets, and `--parallel 4 -- /p:CL_MPCount=2`. Exact review, logs and
identities are preserved under `[REDACTED_LOCAL_PATH]`;
the final receipt is `validation.json`. Initial configure and build
receipts remain under `[REDACTED_LOCAL_PATH]`.

Not run: in-game testing of this native-loop correction, matched
performance measurements, SE/AE runtime checks or shader assertions
(no HLSL changed). PR #93's 70 COCs used a DLL without this correction
and are not qualification for this PR. The adapter harness does not
qualify engine exception-handler scopes. Keep this PR as a draft pending
its own runtime and performance evidence.
