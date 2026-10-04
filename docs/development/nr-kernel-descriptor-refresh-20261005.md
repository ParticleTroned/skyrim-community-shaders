# Live kernel descriptor-refresh failure

The 4/5 October retest admitted the dynamic native layouts but did not
submit private kernels. Pipeline C completed three original stereo frames,
then the first deferred frame encountered native descriptor refresh.
The old guard rejected it and the provider threw before a kernel launch.
NR was quarantined; Skyrim remained responsive and reported no device
removal. A/B and private-kernel performance could not be qualified after
that irreversible quarantine.

## Attributed evidence

The producer was Build ID
`94bb7d95b1961fa8f9fd62841a6dd1c33cccee4f92a84074db571b2059aef5a8`,
compiled from `7434dcbed2de360a6fa7e858b1175f271c125168` with dirty digest
`81d47a9fdfc258a47626131376b7a89d8dd9ab21228b437bc85f1ba0f00add30`.
Its physical DLL SHA-256 was
`eaee647d75d235c9fd0b2a12c60fa6b431185fb02b115ca16a19da3bbd67d313`,
size 31,981,568 bytes. The adjacent manifest and AIO receipt matched.
Exact-path checks across enabled loose mods, Overwrite and unmanaged Data
found one CSX DLL provider and one NR provider. All 21 kernel payload files
matched their packaging receipt.

PID 63064 started at `2026-10-04T22:43:06.9893450Z`. The loaded camera was
retained, FOV set to 0.95, and AI disabled for stationary observation.
The scene detected two eligible actors. The last successful C frame used
320 x 320 and 128 x 128 contexts per eye: 237,568 stereo inference pixels.
Its complete compatible pairing was `[0,3]`, `[1,2]`; all four graph-family
identities matched the qualified 158-stage chain. This proves the earlier
fixed-layout pairing restriction is resolved for this scene.

Submitted counters were three warmup frames, twelve successful native
evaluations/commits, 1,896 logical and physical launches, zero private
launches and zero batched frames. One renderer failure and one quarantine
occurred. Three warmup GPU samples are insufficient for a performance
claim and are excluded from comparison with the 5–6 ms offline reference.

Read-only inspection used the exact producer DLL/PDB and live process
start-time identity to recover the retained probe error:
`kernel pair descriptor cache mutation invalidates deferred replay`.
The failing sample observed one independent-descriptor conversion and one
UAV-info call, both returning NVAPI_OK. Native and private submission
attempts were both zero. The guard failure was subsequently treated as an
unsafe provider exception, obscuring the original rejection in telemetry.
This is a CSX integration failure, not evidence of a GPU watchdog fault.

AI was restored on before console `qqq`. The game process exited and
RootBuilder's active BuildData record disappeared; MO2 remained open.
The quit request lost its HTTP response as the process exited. That receipt
is preserved separately from the independently verified game closure.
No preset or user configuration was saved.

Local evidence is retained under
`build/validation/nr-kernel-live-retest-20261004T224700Z`, including live
calls, log snapshots, physical identity, retained probe state and shutdown
proof. Raw evidence remains local.

## Repair and qualification boundary

When a native descriptor refresh occurs before any native kernel or
deferred command has been recorded, the entire frame continues through
the original provider path. The normal submitted-frame owners and fence
retirement still apply. `descriptorRefreshFrames` distinguishes these
original frames from private execution; the reason is visible in the UI
and DevBench status. No additional model evaluation or GPU work is added.

A refresh after recording begins still rejects deferred submission.
Known rejection propagation cannot erase the original error or falsely
classify an unsubmitted list as uncertain GPU work. Independent retry
requires the existing exact thread/list checks, zero native/private
submission attempts, successful real-list abort, GPU idle and complete
feature/probe epoch retirement. Native API errors, foreign ownership,
unknown exceptions and uncertain submission continue to fail closed.

This repair does not authorize changing CUDA descriptor mappings during a
private schedule. If a pipeline refreshes descriptors every frame it may
remain on original kernels. Private execution requires an unchanged
descriptor epoch or separately qualified descriptor ownership; simply
ignoring the guard would not establish equivalent output.

The new DLL must be tested in a fresh live session. Admission alone is not
batching: require private-launch and batched-frame deltas, stable native
contexts, matched baseline timing windows and output comparisons. A/B/C
private execution, live output equivalence and performance remain open.

## Validation

Focused Release builds of the VR DevBench DLL, replay executable and
NeuralRendering UI/Controls succeeded. UI and Controls tests passed.
Provider hook and descriptor-guard regressions passed, covering all five
descriptor APIs, original forwarding after early refresh, late refresh,
prior submission, foreign-thread calls, native API error, rejection
propagation and reset of frame-local refresh state. These CPU tests do not
substitute for new-DLL live qualification.

The isolated packaging review removed two unrelated UI-availability lines
that entered the first repair commit from a concurrent working-tree edit.
That guard depended on an uncommitted runtime API; the runtime work remains
local in the shared checkout. The descriptor repair does not depend on it.

The preset guard hashes complete settings-owner files, including DevBench
descriptions. Only `NeuralRenderingFeature.cpp` changed from the previously
reviewed owner inventory. Its descriptor-refresh description and diagnostic
counter do not change settings keys, defaults, loading or saving. The
maintained generator therefore refreshes only the revision-8 source
fingerprint and the resulting three settings hashes; graphics values, base,
tier overrides, revision and existing user-owned preset archives remain
unchanged. The reviewed fingerprint is
`727E977DB57874EF077B6F291E3D82EAAFD43954E0B37A28D43DAABDCE473A93`.
