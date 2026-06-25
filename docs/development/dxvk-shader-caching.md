# DXVK shader pipeline & caching

How shaders get from HLSL to pixels under the DXVK backend, and where every layer
of caching lives. Read this before touching `ShaderCache`, the DXVK submodule, or
anything that worries about shader-compile stutter.

## The pipeline: HLSL → DXBC → SPIR-V → VkPipeline

Community Shaders does **not** compile HLSL to SPIR-V. It compiles to **DXBC**
(the normal FXC / `D3DCompileFromFile` path, exactly as on native D3D11), and
hands that DXBC to the D3D11 device. Under this branch that device is **DXVK**
(loaded as `csd3d11.dll` / `csgi.dll` from the mod's `dxvk` subfolder — see
`src/DxvkLoader.cpp`). **DXVK converts the DXBC to SPIR-V itself** (its built-in
`dxbc-spirv` IR backend) and then builds the Vulkan pipelines.

```
 .hlsl  --FXC-->  DXBC  --DXVK dxbc-spirv-->  internal IR  -->  SPIR-V  -->  VkPipeline
        (CS)            (DXVK)                       (DXVK)         (DXVK)      (driver)
```

> **There is no native HLSL→SPIR-V (DXC) path.** An earlier iteration compiled
> shaders straight to SPIR-V with DXC; it was removed (`f62f6120`, "DXBC-only
> shaders"). Because DXVK re-derives SPIR-V from the DXBC, shaders are authored
> and compiled **exactly as for native D3D11** — no DXC/SPIR-V-specific HLSL
> adaptations are needed (no `bool`→`uint` cbuffer juggling, no namespace→struct
> rewrites, no vector-`&&` splitting). If you find such adaptations, they are
> vestigial and should be reverted to match `dev`.

The only DXVK-specific shader handling left in `ShaderCache` is **routing**, not
authoring: a couple of constructs are forced down the DXBC path explicitly
because DXVK's older descriptor model rejected the SPIR-V form (e.g. descriptor
samplers, groupshared compute). That lives in C++, not in the HLSL.

## The three caching layers

A shader crosses three compiles, each cached independently. Only the first is
CS's; the other two are DXVK's / the driver's.

| # | Stage | Owner | On-disk cache | Portable? | When it runs |
|---|-------|-------|---------------|-----------|--------------|
| 1 | HLSL → DXBC | CS `ShaderCache` | `Data/ShaderCache/<fxp>/<descriptor><suffix>.{pso,vso,cso}` + `Info.ini` | yes (DXBC is GPU-independent) | first launch (or after a cache wipe) |
| 2 | DXBC → IR | DXVK `DxvkShaderCache` | `%LOCALAPPDATA%\dxvk\<exehash>.dxvk.{lut,bin}` | yes (IR is GPU-independent) | first time each shader is *created* |
| 3 | IR → VkPipeline | DXVK + GPU driver | driver's own shader cache (not a DXVK file) | **no** (per-GPU, per-driver) | first time each pipeline state is *drawn* |

### 1. CS DXBC cache (`Data/ShaderCache`)

- Written via `D3DWriteBlobToFile`, read via `D3DReadFileToBlob`
  (`src/ShaderCache.cpp`). One blob per shader permutation.
- `Info.ini` stamps the plugin version + feature-validation data. On a
  version/feature mismatch the **whole** `Data/ShaderCache` directory is deleted
  and everything recompiles (`ValidateDiskCache` → `DeleteDiskCache`).
- **Dev builds wipe it every launch** ("Disk cache outdated: no plugin version
  found") because they have no release version stamp — this is normal for
  iteration. Release builds keep it warm.
- This is the big, multi-minute "compiling shaders" cost. It is identical to
  native D3D11; DXVK changes nothing here.

### 2. DXVK IR cache (`%LOCALAPPDATA%\dxvk`)

- DXVK's post-rewrite builds replaced the old `dxvk_state_cache` with
  `dxvk_shader_cache.cpp`, which caches the **decompiled IR** (DXBC→IR), *not*
  compiled pipelines.
- Two files: `<hash>.dxvk.lut` (lookup table) + `<hash>.dxvk.bin` (IR blob).
  Observed for Skyrim: ~8300 shaders, ~50 MB.
- `<hash>` is `FNV-1a(64)` of the exe path from the last directory separator —
  which reduces to just `\SkyrimSE.exe`, so it is the **same hash
  (`a61130db3799372a`) on every SE/AE install** regardless of directory.
- Directory override: `DXVK_SHADER_CACHE_PATH` (else `%LOCALAPPDATA%\dxvk`). Note
  DXVK opens the cache files **read-write**, so pointing it at a read-only path
  (e.g. an MO2-VFS mod folder) *disables* the cache entirely — keep it on a
  writable location.
- Hard-keyed to the exact `DXVK_VERSION` string; bumping the DXVK build discards
  and rebuilds the file.

### 3. Pipeline compilation (GPL + driver cache)

- DXVK uses **`VK_EXT_graphics_pipeline_library` (GPL)**, on by default whenever
  the driver supports it (effectively all modern AMD/NVIDIA/Intel). Confirm in
  DXVK's `SkyrimSE_d3d11.log`: *"Graphics pipeline libraries supported"*.
- On the first draw of a new pipeline state, DXVK **fast-links** a base pipeline
  from per-shader libraries and renders **immediately**, compiling the optimized
  pipeline asynchronously on low-priority workers and hot-swapping it in. So a
  cold pipeline does **not** hard-stall — there is no classic "shader compile
  freeze," only minor first-session hitching as libraries build in the
  background.
- DXVK keeps **no persisted `VkPipelineCache`**. The compiled pipeline binaries
  live only in the **GPU driver's** own cache (per-GPU, per-driver). This cannot
  be shipped, but it warms after the first run and is clean thereafter.

## Why we don't ship a pre-warmed cache

- Layer 1 (DXBC) and layer 2 (IR) are both portable and *could* be shipped, but
  the cost they save is a **startup / first-run** cost, not in-game stutter — GPL
  already prevents pipeline-compile freezes during play (layer 3).
- Layer 3 — the only cost that would actually show up as in-game hitching — is
  per-GPU and **not shippable** by any mechanism in this DXVK (no state cache, no
  persisted `VkPipelineCache`).
- A shipped IR seed (~50 MB) would only shave the one-time DXBC→IR pass, is
  DXVK-version-locked (needs CI regeneration per DXVK bump), and the seeding
  machinery added maintenance for a benefit users feel once, at first launch.
  Not worth it. (Investigated and dropped deliberately.)

If you ever do want to seed layer 2: the universal filename is
`a61130db3799372a.dxvk.{lut,bin}`, the content is GPU-portable, and it must land
in a writable cache dir before the D3D11 device is created.

## Inspecting / clearing the caches

```powershell
# CS DXBC cache (per game install)
ls "<Skyrim>\Data\ShaderCache"            # blobs + Info.ini
Remove-Item -Recurse "<Skyrim>\Data\ShaderCache"   # force full HLSL->DXBC recompile

# DXVK IR cache (per user)
ls "$env:LOCALAPPDATA\dxvk"               # <hash>.dxvk.lut / .bin
Remove-Item -Recurse "$env:LOCALAPPDATA\dxvk"      # force full DXBC->IR rebuild

# DXVK's own log (GPL status, "Found cache file", shader count)
"<Skyrim>\SkyrimSE_d3d11.log"
```

The GPU driver's pipeline cache is managed by the driver and cleared via driver
tools (or a driver update), not by anything here.
