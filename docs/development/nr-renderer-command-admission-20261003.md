# NR renderer-command admission — 2026-10-03

Tasks 9/10 remain open. The RegionCompact producer did not admit any NR
configuration in this run, so it supplies no new compact/composition or
performance qualification. Task 11 remains at its recorded ownership audit.

## Live evidence

The physical enabled DLL, adjacent manifest and producer agree on Build ID
`22b28b37248b509ba2c2584ce8fe87f1b6ae5704839fbefcf3295b34f10bffba`,
SHA-256 `597f4e0e84a48d247965438bd53d1c08ec21c03628d8f7bd212919b62f12f116`,
31,681,024 bytes. Compiled base was
`7b89e1aee490b6301db46d55c6271830a2c1fa1c`, dirty digest
`3c4ec751aead25d00377c6bc725b0d76c35cd2e8d93a7e6347af7061386642b2`.
The committed implementation is `cb49c8b7be5c40673ee444add926ec1a5460748a`.
Only one enabled loose provider was found; Overwrite and unmanaged Data
had no competing DLL. Raw evidence remains local under
`build/validation/nr-task9-11-regioncompact-20261003/`.

Skyrim PID 20328 started at `2026-10-03T17:30:39.4512494Z`. The Bannered
Mare HMD screenshot shows several characters. Camera inspection used the
documented `camera get` action; no camera, free-camera, AI or scene mutation
was issued. The user's stationary view was preserved.

Sixteen NR configure requests returned `renderer_busy` and explicitly
`mutationApplied=false`. Frames advanced; the read-only readiness API
responded. NR remained off with zero native evaluations, resource rebuilds,
failures, removals or quarantines. `fov_not_configured` describes the
initial inactive B configuration, not the reason C mutation was rejected.
This run did not reproduce a GPU fault.

The guard originated in `5ed4e57dd`, before the compact/composition change.
The [Task 4/5 record](nr-task4-5-qualification-20261002.md) already preserves
eight unpaused contention rejections and console-assisted successful
transitions. A single nonblocking lock attempt from an SKSE task is safe on
rejection but does not provide reliable unpaused command admission.

Diagnostic frame evidence was enabled and restored to false. The profiler
remained disabled and inactive; the attributed HMD screenshot completed.
The normal `qqq` command returned a queued receipt and the exact game
process exited. A queued receipt alone was correctly not accepted as a
completed-command proof by the controller. Exact MO2 graceful recovery
`20261003T180031Z-nr-regioncompact-finished-live-4c729eb3` then closed MO2;
its session and access `access-20261003T174159Z-d428b9d6def8` were released.
No deployment, relaunch, forced termination or cache removal occurred.

## Correction and review

NR configure/cycle/reset and FOV configure/cycle now reserve one command
and execute at the completed render-frame boundary under the existing
native renderer lock. Contention leaves the same request pending for a
later boundary; rendering never waits to acquire ownership. The API worker
waits for admission, without occupying an SKSE task or changing the camera.

Unclaimed commands expire after five seconds and are cancelled through the
shared dispatch state before returning `renderer_command_timeout` with no
mutation. A boundary retaining a cancelled request cannot execute it later.
Concurrent commands return `renderer_command_busy` without mutation.
Missing renderer/context returns `renderer_unavailable`. Once execution
claims a command, the response carries its actual result; an execution
exception reports unknown mutation state. A lost transport response still
requires observation before any new mutation.

The queue, frame hook and action descriptions are DevBench-only. Bridge-off
production has no new per-frame check, lock, allocation or GPU operation.
The existing retirement/fence logic and rendering settings are unchanged.
SE/AE and VR share the same Present boundary and native-lock accessor.
No runtime performance gain or cross-runtime gameplay pass is claimed.

Adversarial coverage exercises unavailable/mismatched contexts, a contended
boundary followed by admission on its native owner, recursive ownership,
concurrent and nested requests, exception cleanup, no-frame timeout,
cancelled retained requests, and execution crossing the admission deadline.
The focused compiled regression passed, followed by the complete validation
below. A replacement-DLL live transition test remains required.

## Controller and discovery findings

The earlier input-capabilities contract fix was already in automation dev
`5c9ddfe`. Camera reads exposed a separate missing typed observation adapter.
Automation dev `d51f000` adds that adapter, documentation and packaged copies;
295 controller assertions pass, including malformed values, optional older
telemetry and exclusion of camera mutations. The corrected controller also
read camera state from the same live game. Feedback
`AUTO-20261003-180649014-CD11EA1D` records its resolution. The installed
plugin cache was not replaced during this run.

The direct host catalog lacked late-registered NR tools although the live
controller inventory contained them. The user authorized the maintained
controller as the single subsequent transport. DevBench source already
advertises tool-list changes and emits registration notifications; a missing
server notification has not been established. Feedback
`AUTO-20261003-174018838-4F3DF137` retains that open discovery finding. No
DevBench server source change or server PR is claimed.

The next live run must first verify unpaused command admission and both-eye
health, then qualify limits 2/3/4, stable compact retention, reset behavior
and matched full/compact/full windows. Task 9 still retains alternative
split/reanchor search and held-out calibration gates; Task 10 retains full
pipeline and output-quality gates. This control correction closes neither.

## Replacement validation and archive

`pwsh ./tools/validate-local.ps1 -OutputDirectory build/validation/nr-renderer-command-validation-20261003`
passed the universal Release DLL, all 227 controller/shader tests with no
failures or skips, preset tests/check, whitespace, source stability and
DLL provenance checks in 481.23 seconds. Main compilation took 139.72
seconds; CTest took 150.88 seconds. The earlier focused test-fixture include
order/external-header failures and their successful correction are retained
in the evidence directory. Scoped pre-commit passed after normalization.
SE/AE gameplay, a complete bridge-off DLL build and new-DLL live behavior
were not tested; compiled-out production scope follows the source guards.

Producer Build ID:
`47675771439e43f55b8bd82fc92fd1369ad4a4fffec82b0d280f0d77dfb80276`.
Compiled source base: `cb49c8b7be5c40673ee444add926ec1a5460748a`, dirty digest
`c3e1c4c6a6ebb3ed47707f28fab83c4e321087b9acb900877ef44f28e10fd434`.
DLL: 31,686,144 bytes, SHA-256
`0311eae2a1929178019f09a3cf102bccc9c94a398a8e2548372c2486525dedc1`.
The source snapshot, patch, changed files and manifest were preserved under
the live evidence directory's `aio/` before appending this result. This
later report and eventual commit do not replace the compiled identity.

`pwsh ./tools/cmake.ps1 --install build/ALL --config Release` staged the
validated producer. The archive under `dist/` is
`CSX_AIO-main-vr-nr-NR-RendererCommands-DevBench-20261003-47675771439e.7z`,
199,369,327 bytes, SHA-256
`57562398c4a8cf7bd1b3cf85749fad45bc99ecb45cf3de88d84bd17bb761905c`.
7-Zip integrity and full extraction passed. All 386 extracted files match
the staged sizes and SHA-256 values; the DLL/PDB/manifest match the producer
and the bundled NR runtime retains its pinned identity. DevBench is on,
Tracy off and SE/AE/VR compiled. No personal settings or compiled shader
cache is included. Adjacent receipt/checksum files preserve the inventory
and validation links. The archive was not deployed and Skyrim was not
relaunched by this task.
