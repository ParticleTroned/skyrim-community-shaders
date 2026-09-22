# NR render-scale request-lock crash

## Finding

The two September 21 evening crashes share a same-thread lock reentry in
CSX's NR resource-profile construction. The MSVC runtime throws
`std::system_error` with `resource_deadlock_would_occur` (36). The exception
crosses `IsNeuralRenderingRenderScaleAvailable() noexcept`, terminating
Skyrim with `0xc0000409`, fast-fail reason 7.

This diagnosis comes from the preserved Windows dumps, their captured
exception objects and mutex state, the matching retained production
DLL/PDB, and the production call graph. The older SKSE crash reports are
different incidents. The current `CommunityShaders.log` belongs to a later
`main-VR` session (`907e935ee`), so it cannot explain these NR crashes.

| Local time (Europe/London) | Process | Crashing thread | Captured request-mutex owner | Exception               |
| -------------------------- | ------: | --------------: | ---------------------------: | ----------------------- |
| 2026-09-21 21:50:46        |   13092 |           11356 |               11356, count 1 | `std::system_error`, 36 |
| 2026-09-21 22:12:24        |   26084 |           26080 |               26080, count 1 | `std::system_error`, 36 |

Fast-fail bypasses ordinary exception handlers, explaining why Windows
retained dumps without new SKSE crash reports. This exception code alone
does not establish a stack-buffer overwrite. See Microsoft's
[fast-fail documentation](https://learn.microsoft.com/en-us/cpp/intrinsics/fastfail).

## Build attribution and evidence

The retained NR production receipt identifies:

-   Source: `a471b62be4cbd496e0ef441ac6f88d89f52d0a54`.
-   Build ID: `f21aebb4853a8f85976459fd7c79e4e8e628a83a43e8bbf967f4a98631465645`.
-   DLL SHA-256: `b97c4fbe5ffa157a4acbeb31aed0001ff269d94287da841fedc9e412d2fd58aa`.
-   Universal Release; DevBench and Tracy disabled.
-   Retained DLL/PDB: `.tmp/a471-prod/build/fomod-stage-a471b62be/Core/SKSE/Plugins/`.
-   Receipt: `dist/CSX_3.19-VR_Production_main-vr-nr_a471b62be_FOMOD.7z.receipt.json`.

The installed AIO DLL and retained producer DLL both match that receipt's
hash. Both dumps have the same CSX PE timestamp (`1790018129`) and image
size (`24870912`), and their Streamline modules resolve into this NR AIO.
These minidumps omit CSX's code pages and CodeView record; a complete hash
of the DLL loaded at crash time cannot be recovered from them. This limit
must remain distinct from the verified on-disk artifact identity.

The two original dumps were copied without modification to
`D:/Coding/GitHub/CS logs/` and size/hash checked before analysis:

| Process | Archived filename                                                              | SHA-256                                                            |
| ------- | ------------------------------------------------------------------------------ | ------------------------------------------------------------------ |
| 13092   | `main-vr-nr-UNVERIFIED__20260921T234332732Z__95e2a276__SkyrimVR.exe.13092.dmp` | `60A676B1A1324DDCCDDF72D2776BBFA3FB32A364E78EFCA818EB80CA0690B64B` |
| 26084   | `main-vr-nr-UNVERIFIED__20260921T234332401Z__32edecb2__SkyrimVR.exe.26084.dmp` | `50F610570E40ADF311CE7A9ED7CB4924ECC08BCD5AA2CF61E23A0ACB04E9675D` |

The local analysis is under the repository root's
`build/nr-ctd-investigation-20260922/`: archive receipts, Windows events,
parsed dumps, PDB address mappings, decoded exception records and captured
mutex bytes. Stack-address scans are explicitly labelled as candidates,
not debugger-unwound stacks. MSVC exception-type metadata was decoded from
the system runtime whose PDB GUID, age, PE timestamp and image size match
the dumps.

## Production failure path

`RecordVRRenderScaleTransitionRequested()` owns
`pendingVRRenderScaleRequestMutex` while constructing the current profile:

```text
RecordVRRenderScaleTransitionRequested [owns pending-request mutex]
  BuildCurrentVRRenderScaleProfile
    BuildVRRenderScaleRequestProfile
      BuildVRRenderScaleResourceKey
        IsFoveatedVendorDispatchEnabled
          IsNeuralRenderingRequested
            IsNeuralRenderingRenderScaleAvailable [noexcept]
              GetVRRenderScaleModeRequested
                GetPendingVRRenderScaleDesiredProfile [locks same mutex]
```

The recorded return address `CommunityShaders.dll+0x59C5B6` is immediately
after the MSVC deadlock-error throw inside the second lock acquisition.
Both dumps contain that address and error 36, and show that the request
mutex is already owned by the crashing thread. Other retained addresses
are consistent with API/menu transition entry in process 13092 and native
recovery promotion in process 26084; these are context indications, not
fully unwound outer stacks.

The path is reachable with NR enabled in its reduced-resolution route
while an active resource profile is being constructed. Full-resolution
and foveated NR do not take the same render-scale-required query branch.

## Correction and scope

Resource-key construction now resolves dispatch layout from its supplied
profile and NR configuration, without consulting live pending-request or
physical-latch state. The configuration resolver is shared with the
runtime dispatch predicate. Periphery TAA derives from the already
resolved dispatch flag, avoiding another live availability query.

This also lets an incoming NR profile reserve its resources before it has
latched. Runtime execution still uses the existing requested, latched,
active and FOV admission checks. The mutex remains non-recursive, request
serialization remains intact, and exceptions are not swallowed.

No shaders, defaults, render scales, NR image processing, provider reset
policy, runtime hooks, logging, or diagnostic instrumentation change.
The correction is on `main-vr-nr`; the main-VR implementation has no NR
availability callback to remove.

## Regression coverage

`NeuralResourceKey` extracts the actual production resource-key builder,
NR execution gates, dispatch predicates and resource/profile types.
Dependency fixtures record live-state queries while the caller owns its
request mutex; they avoid deliberately terminating or hanging the test
process. The unfixed code fails because it reenters that query.

Coverage includes lock-free profile evaluation, a target not yet latched,
runtime NR rejection when unready, settled dispatch parity across VR/flat,
DLSS/FSR/native methods, all NR routes, enable/load/mask/TAA combinations,
native recovery and FSR host/runtime/FSR4 fallback identity.

Live reproduction of both original transition paths remains necessary
before claiming in-game validation.

## Local validation

Universal Release retains SE, AE and VR support; DevBench, Tracy and
automatic deployment are disabled. CMake 4.4.1 and MSVC were used in the
existing `build/AIO-Release` build directory.

-   The extracted production regression failed before the correction with
    `Resource-key construction reentered the live pending-request query under its lock`.
    It passed after the correction, including 768 settled-mode combinations.
-   DLL, controller and shader targets built with
    `pwsh ./tools/cmake.ps1 --build build/AIO-Release --config Release --target CommunityShaders controller_tests shader_tests -- /m:1`.
-   `ctest --test-dir build/AIO-Release -C Release -N`: 183 registered tests.
-   `ctest --test-dir build/AIO-Release -C Release --output-on-failure --no-tests=error --timeout 300`:
    183 passed, zero failed, skipped or not-built tests, 64.72 seconds.
-   `pwsh ./tests/unified_preset_generator_test.ps1`: passed.
-   `pwsh ./tools/generate-unified-presets.ps1 -Check`: passed after refreshing
    the inventoried source fingerprint. Preset settings and contract revision
    are unchanged; only compatibility metadata changed.
-   Changed-file pre-commit checks and `git diff --check`: passed.

Local logs, the test inventory and JUnit results are retained under
`.tmp/a471-prod/build/nr-lock-fix/`. Initial dependency retrieval failed;
the existing matching local shader-test packages allowed configuration to
complete. An initial build command lost PowerShell's unquoted `--`
separator and returned MSB1011 after compiling the targets; the corrected
invocation preserves the separator. These attempts remain in the logs.

The test AIO reuses the previous NR production FOMOD and all twelve
unchanged SE/AE and VR shader-cache files, verified by size and SHA-256.
It replaces the DLL, PDB and producer manifest only. Its adjacent receipt
records the dirty source identity, build identity and artifact hashes.
No deployment or in-game qualification is implied by the offline results.
