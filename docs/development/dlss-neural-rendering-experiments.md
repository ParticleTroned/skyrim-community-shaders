# DLSS Neural Rendering experiments

These branches exercise NVIDIA NGX Feature 18 through an isolated D3D12
interop service. Normal DLSS remains on the existing Streamline D3D11 path.
Feature 18 is not exposed by the public Streamline 2.12 headers, so this code
does not register an invented Streamline feature or tag contract.

This is internal research, not a redistributable or release-ready integration.
The repository does not grant rights to NVIDIA binaries. At the user's explicit
request, these experiment branches can produce an internal AIO that contains a
hash-pinned runtime set from paths supplied through the CMake cache. Do not
commit those DLLs or publish, redistribute, or attach the resulting AIO to a
release.

## Branch contracts

| Branch      | Fixed center-pipeline arrangement              | Failure behavior                                |
| ----------- | ---------------------------------------------- | ----------------------------------------------- |
| `paintball` | normal DLSS, then Feature 18                   | retain the completed DLSS center                |
| `paint`     | Feature 18 at low resolution, then normal DLSS | send the original low-resolution center to DLSS |
| `ball`      | Feature 18 replaces normal DLSS                | run normal DLSS for that center                 |

The arrangement relative to normal DLSS is compiled into each branch; it is not
a saved setting. Gogh's DLSS-then-Neural route additionally exposes a saved
insertion-point setting. `Upscaled Centre` retains the original placement ahead
of feathering and sharpening. Experimental `Final LDR before UI` keeps the same
center-region restriction but evaluates Feature 18 after scene post-processing
and sharpening, immediately before UI composition.

## Runtime trust boundary

Normal DLSS stays separate from Neural Rendering. For this internal test, CMake
copies a matched Streamline 2.13 core/plugin set and NVIDIA-signed 310.8
`nvngx_dlss.dll` from the user-supplied local runtime directory. CSX remains
compiled against the Streamline 2.12 headers, so unchanged normal-DLSS operation
is the first runtime compatibility gate. Feature 18 is loaded directly from
`nvngx_dlssnr.dll`; it does not use or require `sl.dlss_nr.dll` or a Streamline
Neural Rendering plugin.

The direct loader accepts exactly three `nvngx_dlssnr.dll` SHA-256 identities:

-   allowlisted signed 310.8 identity:
    `E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E`
-   allowlisted patched 310.8 identity:
    `8270B350CD82DE5CE89806872CDD6B6A9249B80836B91BBEB3573470744CC206`
-   alternate allowlisted patched 310.8 identity:
    `CEB6432F6FBDF44D886014BCD47241932BF8B67439FEEF9BBDD0961436662650`

The hash is computed before `LoadLibraryExW`. All three identities are accepted
at every CSX log level. The patched identities remain restricted to their exact
pinned hashes; changing the log level does not widen the allowlist, enable
Streamline developer mode, sign a DLL, or authenticate a plugin. “Signed”
describes the inspected allowlisted file; this loader pins its hash and does not
perform a new Authenticode trust decision or certify a binary as malware-free.

The runtime DLL does not necessarily export the NGX parameter allocator. When
it does, its already allowlisted runtime identity is also the parameter-core
identity. Otherwise, before calling NGX initialization, CSX requires exactly
one loaded exporter whose locked file identity resolves below
`System32\\DriverStore\\FileRepository`, whose basename is `nvngx.dll` or the
DriverStore alias `_nvngx.dll`, and whose Authenticode signature passes an
offline, no-UI verification. Current DriverStore binaries can carry a Microsoft
WHCP signature, so CSX checks the file's NVIDIA product metadata separately:
`CompanyName` must be `NVIDIA Corporation`, `ProductName` must be `NGX`, and
`OriginalFilename` must be `nvngx.dll`. Version metadata is an additional
identity check, not proof of the signer's publisher. CSX retains both the module
reference and a file handle that denies writes and deletion across initialization
and for the complete NGX parameter lifetime. Any sibling `nvngx.dll` or
`_nvngx.dll` beside `nvngx_dlssnr.dll` is rejected before the first vendor
initialization call. The selected core path, SHA-256, trust result, and selection
source are exposed by `nr_status`; ambiguity or any failed check is a closed-gate
failure.

During NGX initialization, feature creation, evaluation, release, and shutdown,
the implementation temporarily replaces `nvngx_dlssnr.dll`'s imported
`GetModuleFileNameW` function. A query that passes the CSX module handle is
reported as the sibling `nvngx.dll` path; other queries call the original
function. The import is restored after each scoped NGX call. This is a
caller-path substitution that can affect vendor caller validation, not a
signature or authentication mechanism. It requires explicit legal and license
review before any distribution or use beyond this internal experiment.

Runtime staging is disabled by default. The source and default package retain
`Shaders/Upscaling/Streamline` with its README and license notices but contain
no NVIDIA DLLs. DLLs copied into that source directory are ignored by Git and
excluded from packages. A user who does not stage at build time can instead
copy a compatible runtime pack into the same directory in the completed mod or
AIO output.

Private local staging is enabled with `CSX_STAGE_LOCAL_DLSS_RUNTIME=ON` plus
the `CSX_LOCAL_DLSS_RUNTIME_DIRECTORY` and
`CSX_LOCAL_DLSSNR_RUNTIME_FILE` CMake cache paths. It stages exactly seven
files: the six normal-DLSS runtime modules and the patched 310.8
`nvngx_dlssnr.dll`. Configuration fails if a source is absent or does not match
its declared SHA-256. Local staging performs no download and never writes a
runtime into the source checkout.

For reproducible internal comparisons, the
`Internal-DLSSNR-AIO` configure and build presets read those two paths from
process environment variables with the same names. Set one shared
`CSX_LOCAL_DLSS_RUNTIME_DIRECTORY` for every experiment worktree and select
the NR binary separately with `CSX_LOCAL_DLSSNR_RUNTIME_FILE`:

```powershell
$env:CSX_LOCAL_DLSS_RUNTIME_DIRECTORY = '<normal-runtime-directory>'
$env:CSX_LOCAL_DLSSNR_RUNTIME_FILE = '<path-to-nvngx_dlssnr.dll>'
cmake --preset Internal-DLSSNR-AIO
cmake --build --preset Internal-DLSSNR-AIO
```

The preset enables DevBench and local staging, disables official fetching and
the package shader tests for this internal iteration build, and retains the
normal fail-closed hash checks. Keep machine-specific paths in the process
environment or an ignored `CMakeUserPresets.json`, never in the tracked preset.
The resulting archive is still internal-only and must not be redistributed.
Run the configure command before every build whose runtime source paths may
have changed; invoking only the build preset does not reread environment
variables. Expanded paths remain in the ignored build cache and staging
manifest. Missing sources, mismatched hashes, or concurrent official
acquisition fail closed.

The verified official acquisition path from `main-VR` is retained behind the
explicit `CSX_FETCH_OFFICIAL_STREAMLINE_RUNTIME=ON` network opt-in. It downloads
the pinned NVIDIA Streamline 2.12 SDK release and stages only the six public
normal-DLSS runtime modules; it does not supply Feature 18. This path can be
advanced when an official Neural Rendering SDK is released. Official fetching
and private local staging are mutually exclusive.

The staged NR identity is the malware-screened but modified `8270...206` file
selected by `CSX_LOCAL_DLSSNR_RUNTIME_FILE`. Its embedded NVIDIA signature
reports `HashMismatch`, so admission relies on its exact pinned SHA-256 and
310.8 version at every CSX log level. The signed `E16B...FC8E` identity and
alternate patched identity remain allowlisted for separately selected tests but
are not staged by this build. Hash pinning and prior screening do not certify a
binary as malware-free. Consult the license accompanying each NVIDIA binary.
Do not publish or redistribute the generated AIO.

The first bridge binds color, depth, motion vectors, and output only. Automatic
masking is therefore fixed on and UI correction is fixed off; manual masks and
UI correction fail closed until their ControlMask/UI resources and subrects are
implemented. A Neural Rendering center is committed only when both eyes succeed
in the same stereo transaction. Otherwise both eyes retain the arrangement's
normal-DLSS fallback.

## Working reference and deliberate divergence

The comparison baseline is YtzyFvra's working `feature/dlssnr-vr` branch at
commit `05e037cad2add33a434c09d7b1260d09d331b6a4`. Its VR route runs Feature 18
over an already assembled LDR stereo image immediately before UI, and commit
`3d6748f16` batches both eyes into one D3D12 command list to reduce queue waits.
Its later fixes establish that before-UI ordering and stereo batching are
separate concerns: the latter is a performance change, not the prerequisite
that made the placement correct.

The default `Upscaled Centre` insertion remains inside CSX's center transaction,
ahead of feathering, sharpening, UI, HMD masking, and submission. Gogh's
experimental `Final LDR before UI` insertion moves Feature 18 to the closest
safe CSX equivalent of the reference boundary while preserving the foveated
center area. It never falls back to the other insertion point: an unavailable
or failed late transaction retains the normal DLSS result for both eyes so an
automated A/B test keeps its requested identity.

Batched evaluation records both Feature 18 slots into one D3D12 command list
and fence transaction; per-eye evaluation uses one transaction per slot. Staged
output preserves the original intermediate `NeuralOut` pair before resolving it
into the selected insertion point's outputs. Direct output targets those private
outputs and avoids the extra center-sized copy. A failed direct pair restores or
retains the complete normal-DLSS pair, so a partial NR result cannot remain
visible.

The Neural Rendering controls live in the `NVIDIA DLSS Neural Rendering`
dropdown in Upscaling, between Frame Generation and NVIDIA Reflex. `Insertion
Point` selects `Upscaled Centre` (the default) or experimental `Final LDR
(Pre-UI)`. Stereo submission and output commit remain independent controls:

| Stereo submission | Output commit | Comparison role                  |
| ----------------- | ------------- | -------------------------------- |
| Per-eye           | Staged        | Original baseline                |
| Batched           | Staged        | Isolates stereo batching benefit |
| Per-eye           | Direct        | Isolates direct-commit benefit   |
| Batched           | Direct        | Fully optimized path             |

Changing the insertion point or either implementation axis invalidates the NR
settings key and requests a synchronized temporal-history reset without
changing Feature 18 tuning values. An insertion-point transition retires the
backend and suppresses NR for the transition frame so both eyes switch together.

## First runtime validation

Use `communityshaders.renderscale` Neural Rendering status and reset actions to
preserve the exact runtime and parameter-core paths, versions, hashes and trust
decisions; load/init/create/evaluate/rollback stages; NGX and HRESULT results;
proxy hits; D3D formats; per-slot dimensions; stereo eye masks; current-frame
and current-cycle freshness; submit admission reason; and fallback counters.
The stereo route reports prepared and attempted masks separately. Prepared
means the complete renderer arguments exist for that eye; attempted means the
renderer actually entered NVIDIA Feature 18 evaluation. A prepared eye with no
attempt reached a validation, trust, initialization, or latched-failure gate,
which can be diagnosed from the renderer and runtime failure stages.
Performance telemetry labels D3D11 preparation and output commit as CPU enqueue
time, while Feature 18 duration uses D3D12 GPU timestamps and separately reports
readback failures, command submissions, and bounded backpressure waits. API v5
also stamps the actual insertion point at the evaluation boundary and keeps
cumulative GPU samples and time for each insertion point, independent of the
existing Main/Submit route attribution.

`nr_status.settings.implementation` reports one of
`per_eye_staged_commit`, `stereo_batched_staged_commit`,
`per_eye_direct_commit`, or `stereo_batched_direct_commit`. The same status
includes the independent booleans, display modes, and comparison purpose.

`nr_configure` can select `insertionPoint` as `upscaled_center` or
`final_ldr_pre_ui`, select a lane atomically with `implementation`, or control
the implementation axes independently with `batchedStereo` and `directCommit`.
It also exposes the menu's `enabled`, `preset`, `intensity`,
`localToneStrength`, `localStructureStrength`, `skinStructureStrength`, and
`style` controls.
Preset selection applies the same values as the in-game UI; subsequent tuning
overrides select Custom. `nr_cycle_modes` advances through the four lanes, or
selects an exact lane with `matrixIndex` 0 through 3. `nr_reset` operates the
runtime-reset button. The
deprecated `optimizedStereoPath` alias remains accepted only by itself and sets
both axes for compatibility. Invalid preset, style, and tuning ranges are
rejected rather than silently clamped so automated comparisons retain their
requested identity.

Validate in this order:

1. Confirm unchanged normal foveated DLSS with Neural Rendering disabled.
2. Enable Neural Rendering and prove both eye/role slots reach runtime probe.
3. Prove D3D11-to-D3D12 shared copies and Feature 18 evaluation without a
   fallback or device-removal signal.
4. Confirm both centers complete in the same compositor cycle and at the
   selected insertion point: before feathering for `upscaled_center`, or after
   scene processing and sharpening but before UI for `final_ldr_pre_ui`.
5. Exercise camera cuts, loads, menus, render-scale transitions, quality/preset
   changes, enable/disable, and backend reset.

Feature 18 and its `DLSSNR.*` parameter names are private, version-specific
contracts. A successful build proves only API compatibility; a controlled VR
run is required before calling any arrangement working.
