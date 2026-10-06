# Build identities and local provenance

Each DLL build has three related identities:

-   **Artifact SHA-256** identifies the linked DLL bytes.
-   **Build ID** identifies canonical source, dirty-content digest,
    submodules, dependencies, toolchain, runtime, configuration, and
    behavior-affecting build options.
-   **Shader cache ABI ID** identifies the compiled-shader compatibility
    contract. SE/AE and VR permutations retain separate compile-state
    identities within the universal build.

`refresh_build_provenance` runs before DLL compilation. The generated
header embeds the Build ID; a post-link step writes `CSX.BuildManifest.json`
beside the DLL and binds it to the artifact hash. Deployment and packaging
copy that sidecar with the DLL.

CI uses `CSX_REQUIRE_CLEAN_PROVENANCE=ON` to reject dirty source, dirty
submodules, and checkouts that do not match their gitlinks. Local builds
can retain edits; the dirty-content digest still distinguishes their source.

## Verify an artifact

```powershell
python tools/build_provenance.py verify `
  --manifest path/to/CSX.BuildManifest.json `
  --artifact path/to/CommunityShaders.dll
```

The verifier recalculates the canonical Build ID and artifact SHA-256 and
fails if either identity is inconsistent. Compare the deployed DLL with
its manifest and build receipt before attributing a runtime result.

## Shader cache identities

`Info.ini` records build, artifact, shader ABI, and compiler identities.
Managed packs use content identities that include global and enabled-feature
ABIs. A matching pack header alone does not prove every permutation matches.
Use the [cache runbook](prebuilt-shader-cache.md) for generation and checks.

## Publication policy

Keep detailed validation provenance, investigation notes, run reports,
ledgers, captures, and exact physical locations in ignored local records.
They are not committed documentation or PR attachments.

Public summaries report concise checks and outcomes, coverage limitations,
and source commits, Build IDs, or artifact hashes when useful. Omit personal
usernames, machine-specific paths, and local evidence locations. Reusable
build instructions use repository-relative paths or neutral placeholders.
