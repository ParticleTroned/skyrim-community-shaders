# Resumable release cache worker

`tools/release_worker.py` checkpoints the canonical shader compiler and
uses `build-shader-cache.py` and `build-fomod-package.py` for validation,
managed packs and installer assembly. It builds shipped SE/AE and VR
caches with both Water compatibility variants. It never creates a tag,
updates a GitHub release, publishes assets or changes power settings.

Use Windows with Python 3.12 or newer, the pinned environment from
`tools/shader-cache-requirements.txt`, FXC, PowerShell 7 and optionally
7-Zip. The production core must already have its original universal
Release DLL and valid `CSX.BuildManifest.json`, with DevBench, Tracy and
NVPerf disabled. This worker does not build the DLL.

## Prepare and run

Keep each release under an ignored task directory. Extract a Git archive
of the exact release commit into a `source` subdirectory, and extract the
production AIO into a separate `core` subdirectory. Use the repository
Git and CMake wrappers to create and extract the archive. Preserve the
tag and producer manifest. Do not use a mutable checkout as the source.

Copy these files from `tools/` into a `worker` subdirectory of the task:

-   `release_worker.py`, `release_worker_support.py`,
    `release_worker_package.py`, `shader_compile_checkpoints.py`;
-   `build_provenance.py`, `shader_bytecode.py`;
-   `ReleaseWorker.Common.ps1`, `Register-ReleaseWorker.ps1`,
    `Start-ReleaseWorker.ps1`.

Keep that worker bundle and its Python environment unchanged. The shared
provenance and bytecode helpers must match the frozen source. This
prevents later checkout changes from altering an interrupted release.
Store the environment alongside the task, outside temporary directories;
the worker checks its installed hlslkit revision against the source pin.

Create a local JSON configuration with absolute paths. The following
values describe the required fields; replace every path and identity:

```json
{
    "repository": "<task>/source",
    "taskRoot": "<task>",
    "core": "<task>/core",
    "workspace": "<task>/workspace",
    "state": "<task>/state",
    "output": "<task>/cache",
    "sourceCommit": "<full release commit ID>",
    "pluginVersion": "CSX <version>-VR",
    "fxc": "<SDK>/fxc.exe",
    "jobs": 8,
    "python": "<pinned environment>/Scripts/python.exe",
    "pwsh": "<PowerShell>/pwsh.exe",
    "taskName": "CSX-Release-<unique name>"
}
```

Writable directories must be distinct, strictly inside the task directory
and separate from source/core inputs. Junctions and symbolic links are
rejected. `jobs` accepts integers from 1 through 64. Tune concurrency for
the machine's memory and battery budget.

For a complete AIO, add `package` with `version` (`csx<version>`),
`sevenZip`, `pwsh` and `archiveName`
(`CSX_AIO-<version>-Production-ShaderCache-FOMOD.7z`). Executable fields
are absolute paths. Omit `package` to stop after both runtime caches.

Run the copied Python worker with `--config <config.json>`, or run its
copied `Register-ReleaseWorker.ps1 -Config <config.json>` to register and
start a task for the current interactive user. Registration never takes
over a task with a different action, principal or ownership description.
It repairs the settings of its own task when invoked again.

## Recovery and limits

The scheduled task starts at logon and retries every five minutes. It
allows battery operation, has no execution time limit, does not wake the
laptop and does not prevent sleep. Windows suspends compilation during
sleep. After shutdown, interruption or process failure, the next run
reuses verified completed compiler checkpoints. Logon is required after
a reboot. No work executes while the computer is asleep or powered off.

All writable destinations have OS ownership locks. A Windows job object
terminates compiler descendants when their Python owner dies. Stopping
the scheduled launcher can leave Python running; check the exact PID and
command in `state/status.json` before stopping an owned process. The
status is a phase receipt, not a liveness heartbeat. Duplicate workers
exit with code 75; the scheduled launcher treats that as a benign skip.

Checkpoints verify compiler inputs, byte count, SHA-256 and shader stage.
Missing, corrupt or previously adopted raw outputs are recompiled.
Unchanged non-Water permutations can be reused between compatibility
variants. Rejected and interrupted output is retained; do not delete
checkpoints to recover from a quiet compiler. Compilation can be slow.

Input changes fail closed. Use a new task directory for different source,
core, compiler or worker implementations. Logs and atomic phase receipts
remain under `state`. After completion the launcher disables only its
verified owned task. Cleanup failure does not invalidate completed work.

Packaging fingerprints all core/cache bytes and installer inputs. Each
new archive is tested, fully extracted with CMake, structurally validated
and compared byte for byte with its stage before publication locally.
Stale archives are retained separately. Review CI results, release notes
and required attestations before any separate draft update or publishing
step; successful local packaging does not imply successful hosted CI.

## Focused checks

Run `python -B tests/release_worker_test.py` and
`pwsh -File tests/release_worker_task_test.ps1`. Set `CSX_TEST_FXC` to the
FXC executable and run `python -B tests/release_worker_compiler_test.py`
in the pinned environment for real interruption/restart coverage.
