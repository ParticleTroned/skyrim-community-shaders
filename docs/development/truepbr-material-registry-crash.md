# TruePBR material registry crash investigation

## Evidence and diagnosis

The supplied `5pQ5swC8.txt` records a Skyrim VR 1.4.15 crash on
2026-09-01 at 12:29:14, with CommunityShaders 3.18.0.0 loaded. The log's
SHA-256 is
`88B4E1723968EBC83B057FF148DB1A622CD37075190777207ABDE00DDCB4C133`.

The fault at `CommunityShaders.dll+01A64F2` is inside the MSVC
`std::unordered_map<BSLightingShaderMaterialPBR*, MaterialExtensions>`
lookup. `cmp rcx, [rax+0x10]` reads address `0x10` because `RAX` is zero;
the material key in `RCX` is non-null. An absent key should return `end()`.
It does not require dereferencing the material, so a material null check
does not address this failure.

The source line numbers match tag `CSX3.18`, commit
`2051e2aead1b2bb2b03faa421201376e8bc84fe0`:

-   `TruePBR.cpp:708` looks up `BSLightingShaderMaterialPBR::All` inside
    `TryGetRegisteredPBRMaterial`.
-   `TruePBR.cpp:882` calls that helper from the `GetRenderPasses` hook.

The logger's landscape-function label at line 708 is inconsistent with
that source location. The stack also contains geometry culling and
`BSJobs::JobThread` context. Its scanned frames are supporting evidence,
not a complete thread trace or a verified binary/PDB identity.

Both PBR registries exposed raw shared `std::unordered_map` instances.
Material creation, copying, texture loading, MATO assignment and
destruction modified those maps without a common lock. Culling queried
them and the material editors traversed them without synchronization.
Concurrent insertion/rehashing or erasure can therefore invalidate the
map state observed by lookup. This is the leading explanation for the
reported failure and an actionable CSX synchronization defect. The log
alone cannot identify the competing writer or exclude earlier corruption.

`Clutter\Hay\HayScatter03.nif`, its PBR hay textures and Enderal cell
`BauernkuesteBrueckenkopfhof` identify reproduction context. They do not
establish a defective asset or responsibility for the corruption.

## Fix and ownership contract

`TruePBR/MaterialRegistry.h` centralizes both registries. Membership checks
take a shared lock. Registration, copying, updates, visits and removal
take an exclusive lock. The map is private; callers cannot retain map
iterators or bypass synchronization through ordinary map operations.

Copying reads source metadata into a value before publishing the
destination. It preserves default metadata for unregistered sources and
self-copy behavior. Registration preserves existing metadata. Updating
can register temporary loading materials, preserving the previous
`operator[]` behavior. `TryUpdate` instead requires an existing entry, so
MATO assignment cannot recreate metadata removed after a membership check.

Editor visits retain the lock while applying scalar parameters, excluding
unregistration and subsequent member destruction. This does not give a
membership result ownership of a material: property/scene ownership still
governs native material lifetimes outside a visit. Registry synchronization
does not make all native material payload reads and writes thread-safe.

Callbacks must not retain entry references, reenter the registry or call
engine operations. Landscape texture loading builds a local six-slot
array and publishes it after engine texture calls return. MATO ownership
checks and in-place updates share one transaction; conflicting owners
clone outside the lock, then publish the clone's metadata before replacing
the property material. Allocation and base material copying can therefore
reenter registration without deadlocking.

Landscape reload skips null entries for unused or non-PBR slots. The
culling-path one-time diagnostics use atomic flags. No material instance
layout, vtable, relocation, shader code or render-scale code is changed.
The same registry implementation serves SE, AE and VR.

## Adversarial review

The review covered every registry access, callback lock ordering, metadata
copy/default behavior, destruction, runtime compatibility and existing
utilities. The repository's other registries and shared-mutex users have
domain-specific contracts; none provides this material metadata lifecycle.
Both PBR material types share one helper without changing instance layouts.

The review corrected the MATO check-then-update path: an entry removed
after the initial lookup now causes the assignment to be skipped, without
invoking its callback or inserting metadata. Loading and fresh-clone paths
retain their existing insertion behavior. Engine calls remain outside
registry locks to allow registration during allocation and copying.

Regression coverage now includes missing/removed-entry rejection, a real
destructor blocked by a registry visit, lookup blocked until metadata
publication completes, and lock release when any callback operation throws.
Concurrent ownership claims exercise the same `TryUpdate` operation used
by MATO assignment. No further in-scope defect was identified; native
runtime behavior and lock contention still require in-game validation.

## Saved live snapshot

The saved live mapped image was located and its SHA-256 verified:

-   Directory:
    `C:\src\skyrim-vr-upscaler-standalone\live-ghidra-snapshots\20260822T111246Z`
-   File: `SkyrimVR-live-pid-39940-base-00007FF6F2B30000.bin`
-   Capture base: `0x7FF6F2B30000`; captured bytes: `60133376`.
-   SHA-256:
    `1A6FBB7E726491929EA6EAA1DBFD866C48404EDAEFD63BD712F41FB13FFA2CE8`.

Its provenance describes a different live process, not this crash's CSX
heap. The configured Ghidra endpoint at `127.0.0.1:8081` refused the
connection; port 8080 served the FOMOD tracker. No Ghidra disassembly or
static Ghidra analysis was performed. The fix follows the symbolized CSX
lookup and the source access audit; no native instruction patch is needed.

## Validation

Validation used an isolated `build/pbr-registry-fix` directory, leaving
existing DLLs and shader caches intact.

```powershell
pwsh ./tools/cmake.ps1 --preset ALL -B build/pbr-registry-fix -DAIO_ZIP_TO_DIST=OFF -DZIP_TO_DIST=OFF -DAUTO_PLUGIN_DEPLOYMENT=OFF -DBUILD_CONTROLLER_TESTS=ON -DBUILD_SHADER_TESTS=OFF
pwsh ./tools/cmake.ps1 --build build/pbr-registry-fix --config Release --target pbr_material_registry_test CommunityShaders --parallel 8
ctest --test-dir build/pbr-registry-fix -C Release -R '^PBRMaterialRegistry$' --output-on-failure
```

-   Configure and universal Release DLL build: passed, with SE/AE/VR enabled.
-   `PBRMaterialRegistry`: passed in 0.36 seconds. Eight scenarios cover
    metadata lifecycle/self-copy, landscape slots, missing/removed-entry
    rejection, concurrent ownership/80,000 updates, lookup alongside
    growth/copy/removal of 8,192 materials, exclusion of destruction during
    visits, lookup waiting for publication, and callback exception safety.
-   Focused whitespace, line-ending and C++ formatting hooks: passed.
-   New CMake registration fragment: passed the pinned Gersemi formatter.
    Whole-file Gersemi also reformatted unrelated existing CMake code; that
    churn was removed and the final focused hook run skipped whole-file
    Gersemi. CMake configuration validated the final registration.
-   Scoped `git diff --check`: passed.
-   Initial full-build dependency warning: `hde64.c:59`, MSVC C4701 for local
    variable `c`.
-   Runtime reproduction on Enderal VR, in-game editor/reload testing, SE/AE
    runtime testing and frame-time measurements: not run. This is a prepared
    candidate, not a runtime-confirmed resolution of the supplied crash.

The generated DLL's size and hash match its adjacent build manifest:

-   DLL: `build/pbr-registry-fix/Release/CommunityShaders.dll`.
-   Size: `23606272` bytes.
-   SHA-256:
    `D234C635DDA8A9563B307F1EBDA65183FE11B0F367EA18D6F683A1A40A5B5771`.
-   Build ID:
    `04b788c1e91397339ef77c88fd21541d3a16e2db9fd51342582a897060acb472`.
-   Source base: `ef7c366dd73989b2b87751c0ef975db7c6fd310f`, with local changes.
-   Compile-time dirty digest:
    `54aad221a16cd3cf4f7c5eed99efb69d458011b243b5b796bd5c66ea45d683a8`.
-   DevBench bridge and Tracy: off. Auto-deployment: off.

Configuration/build logs and the CTest receipt remain in the isolated build
directory. The reviewed build log is `review-build.log`; the source hashes
and manifest are recorded in `validation-record.json`. Earlier candidate
artifacts are preserved under `before-review`. No game installation was
changed.

Before commit preparation on 2026-09-11, the seven PBR source/header/test
files still matched their saved validation hashes. The branch had advanced
to `cf3834e182c719dfd4c0539d746b65d2a287ab78`; the pending CMake change
remained the same seven-line test registration. The full DLL build was not
repeated on that newer base; the build evidence above retains its original
source identity.
