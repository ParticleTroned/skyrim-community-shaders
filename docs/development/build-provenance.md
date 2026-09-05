# Build provenance

CSX identifies tested behavior with immutable build evidence rather than a
branch name or display version. Every DLL build has three related identities:

-   **Artifact SHA-256** is the authoritative identity of the linked DLL.
-   **Build ID** is SHA-256 over canonical JSON describing the source commit and
    dirty-content digest, exact submodule checkouts, vcpkg baseline and overlay,
    compiler/toolchain, runtime, configuration, and behavior-affecting options.
-   **Shader cache ABI ID** is an explicit identity over
    `config/shader-cache-abi.json`. It is runtime-neutral so one universal DLL
    accepts the matching SE/AE and VR cache packs; runtime shader permutations
    remain separated by each record's compile-state digest. The ABI invalidates
    globally incompatible cache blobs without changing merely because
    cache-controller or unrelated C++ code was edited. Feature-scoped non-HLSL
    incompatibilities use
    `Feature::GetShaderCacheAbiVersion()` instead.

`refresh_build_provenance` runs before every DLL compilation. It intentionally
does not rely on CMake configure time, because an existing build tree can
survive branch switches and dependency changes. The generated header embeds
the Build ID in the DLL. A post-link step writes `CSX.BuildManifest.json` beside
the DLL and binds it to the actual artifact SHA-256. Install, deployment, and
archive paths copy that sidecar with the DLL.

## Runtime and automation contract

CSX verifies the sidecar against the loaded DLL once per process and logs the
result. DevBench menu, profiler, and render-scale responses contain a
`producer` object with the full Build ID, source identity, artifact SHA-256,
manifest-verification state, shader ABI, and compiler identity.

Automation may pass `expectedBuildId` to those tools. The operation fails with
`code: producer_mismatch` before changing state when the loaded DLL is not the
requested producer. Captures and comparisons should preserve the returned
`producer` object, not infer provenance from the checked-out branch.

## Shader caches

Runtime-generated `Info.ini` files record `BuildId`, `ArtifactSHA256`,
`ShaderCacheABI`, and `ShaderCompilerIdentity`; feature sections may also record
an explicit `ShaderCacheABI`. Build ID, artifact hash, plugin version, and
ordinary feature versions are evidence. Global/scoped shader ABI and a changed
runtime compiler invalidate the corresponding cache scope.

The prebuilt-cache generator calculates `ShaderCacheABI` using the same Python
module and canonical contract file list as the DLL build. Precompiled caches do
not compare their build-host `fxc.exe` with the player's runtime compiler.

## Clean release builds and verification

CI configures with `CSX_REQUIRE_CLEAN_PROVENANCE=ON`, which rejects dirty source,
dirty submodules, or submodule checkouts that do not match their gitlinks.
Local development leaves this off; a dirty-content digest still makes each
distinct local build unambiguous.

Verify a delivered pair with:

```powershell
python tools/build_provenance.py verify `
  --manifest path/to/CSX.BuildManifest.json `
  --artifact path/to/CommunityShaders.dll
```

The verifier recalculates both the canonical Build ID and artifact SHA-256 and
fails if either identity is inconsistent.
