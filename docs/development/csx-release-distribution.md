# CSX release distribution

The public CSX installer is one `CSX_AIO-*.7z` archive. It contains the
production SE/AE/VR plugin, all shipped feature packages and the shader-cache
FOMOD. DevBench and Tracy are disabled. The installer offers VR, SE/AE, or
no prebuilt shader cache; both runtime caches contain standard and Horizon
Fix Water variants. Bundling an integration does not bundle its companion
plugin or remove hardware/runtime requirements.

`release-build.yaml` assembles and validates this final archive before
attestation and publication. Its upload and attestation patterns select
only `CSX_AIO-*.7z`. Core-only, individual-feature and standalone-cache
archives remain internal workflow artifacts. GitHub adds source ZIP and
tar.gz links automatically; those are not install packages.

## Feature inventory and release notes

`tools/feature_version_audit.py` uses the same distribution profile as the
shader-cache builder. The inventory follows packaged feature INIs, excludes
hidden/unshipped packages, resolves runtime display names and includes
modules outside `src/Features`, such as True PBR. Every listed module is
part of the CSX core AIO, independent of its inherited standalone metadata.
The obsolete Wetness Effects package is excluded; Wetterness is included.

Module counts are not a count of all CSX capabilities. Adaptive Balance
and Performance Tuning are versioned modules; shader-cache management is
a built-in system and must also appear in the release description.
The main feature table lists modules and built-in systems together in
alphabetical display-name order. Performance Tuning has its own row
directly after Performance Overlay. Keep the public feature tables above
the detailed commit history.
Performance Tuning is separate from Performance Overlay and does not
require DevBench. Preserve attribution to Community Shaders, Open Shaders
and other contributors where applicable; AIO inclusion is not an authorship
claim. Do not relabel every inherited technique as exclusive to CSX.

Invisible section markers let reruns replace the feature tables without
removing the later commit history. Legacy audit headings are also handled.
The complete user-facing feature list precedes the technical component
version audit. Release publication requires a successful audit and its
downloaded artifact; missing or empty feature notes cannot be skipped.

Audit defaults select reachable stable CSX tags, including the historical
two-component `csx3.18` tag, and use `origin/main-VR` for PRs. Commit links
point to this CSX repository. Reruns remove the previous generated audit
appendix without replacing the human release notes or their selected
changelog baseline.

## Nexus destination

`nexus-upload.yaml` plans exactly one AIO from a published stable CSX
release. It rejects missing or ambiguous AIO assets and never generates
uploads from individual feature INI metadata. The upload version strips
the `csx` prefix.

Set the explicit `nexus_mod_id` and `nexus_file_group_id` inputs, or the
repository variables `CSX_NEXUS_MOD_ID` and `CSX_NEXUS_FILE_GROUP_ID`.
Both must identify the intended CSX destination; the inherited upstream
Community Shaders mod ID is rejected. With no mod ID, a dry run inspects
the AIO without scheduling an upload; a real upload requires both IDs.
Publishing the GitHub
release triggers only the existing dry run, never a Nexus upload.

## 3.19.2 correction

The published `csx3.19.2` release remains at
`d1980b8e151a814f1e6de5b7036da1b9ef21686e`. Its human changelog retains all
536 commits (487 non-merge commits and 49 merges) since `csx3.18`; the corrected feature appendix
describes 39 bundled modules plus the built-in CSX systems. The eight
redundant downloads were hash-verified and backed up before removal.
The AIO was neither rebuilt nor replaced:

| Field           | Preserved value                                                    |
| --------------- | ------------------------------------------------------------------ |
| Asset           | `CSX_AIO-3.19.2-VR.7z`                                             |
| GitHub asset ID | `591631760`                                                        |
| Bytes           | `94565747`                                                         |
| SHA-256         | `8b2ac7e2f60de574d6ba59bdd816ee57f79769c64214c75dac8b74e992fa4ddb` |

These automation changes apply to subsequent releases. Do not move the
published tag to include them.

The versioned [3.19.2 release notes and attribution audit](../release-notes/csx3.19.2-attribution-audit.md)
retain the Nexus text, the complete 536-commit inventory and the supporting
commit map for all 66 entries, including the final cache and water-profile
corrections in the published tag.

## Performance Tuning version registration

After 3.19.2, Performance Tuning has its own initial version, `1-0-0`, in
`features/Performance Tuning/Shaders/Features/PerformanceTuning.ini`. Its
`CORE` marker keeps it in the core AIO without a separate feature archive.
CMake's existing feature discovery generates the `PerformanceTuning`
version and core entries in the build's `cmake/FeatureVersions.h`; the
checked-in `include/FeatureVersions.h` contains the same registration.
The INI supplies the audit description and capability list, so release
notes contain one versioned Performance Tuning entry rather than an
additional synthetic row.

This registers metadata for the existing built-in menu. It does not add
a render feature, shader permutation, toggle or additional per-frame work.
The existing DevBench measurement API is unchanged. The published 3.19.2
AIO predates this registration and still has 39 versioned modules; future
packages include the new Performance Tuning INI as the fortieth module.

The orphaned-INI scanner also checks the version registry, so built-in
menus with metadata but no runtime `Feature` object are recognized.
Obsolete-feature warnings take precedence, and unknown INIs and failed
runtime features retain their existing diagnostics on SE, AE and VR.

Registration validation passed all eight inventory/policy checks and 33
shader-cache packaging tests. The inventory suite executes the production
CMake version-discovery block against the packaged INI and CORE marker,
then compares its generated header with the checked-in registry. The
generated release audit includes one Performance Tuning feature row and
reports its new `1-0-0` version. No production DLL rebuild or in-game test
was needed for this metadata registration.

## 3.20.0 version labels

The main-VR defaults use the `3.20-VR` series. Production labels, the
numeric plugin version, DLL resources, settings metadata and shader-cache
producer labels include the patch component: `3.20.0-VR`. Stable builds
continue to use `CSX_RELEASE_VERSION`; release automation accepts the next
patch or the first version of the next minor line and retains the previous
reachable stable tag as its changelog baseline. Existing tags are never
replaced.

The production AIO includes the matching `CommunityShaders.pdb`, the
universal DLL and both complete shader-cache FOMOD choices. A label-only
rebuild can reuse existing shader packs after verifying identical shader
sources, feature state and cache ABI. Only the copied cache's producer
label is updated; the original cache and compiled bytecode are preserved.

The bundled revision-5 unified presets from the 3.19 line remain accepted
by 3.20. New generated presets target 3.20 and retain the same graphics
settings. Historical release records and test-build allocation seeds keep
their original identities.

Validation passed six release tests, 33 shader-cache packaging tests, 32
FOMOD tests, eight distribution tests, the compiled preset-compatibility regression, and the
complete preset-generator regression and generated-output check. Scoped
hooks passed with the existing whole-file CMake formatter excluded to avoid
unrelated formatting changes. No in-game validation is claimed.

## Validation

Run the focused release suites:

```text
python tests/csx_distribution_test.py
python tests/csx_release_test.py
python tests/release_fomod_workflow_test.py
```

The workflow suite requires PyYAML, CMake and PowerShell, plus Bash for the
Nexus planning checks (Git for Windows supplies Bash locally). It executes
the embedded archive assembler in both runtime-selection modes and the
Nexus planning step with synthetic release assets, without an upload.

For the distribution correction, all 19 tests passed (7 inventory/policy,
5 release identity and 7 workflow tests). Scoped pre-commit checks passed.
The generated audit was inspected against the current checkout: 39 shipped
modules, CSX display names and links, plus the built-in systems. It also
retained the existing Extended Materials version-bump recommendation for
the newer unreleased parallax change; that is not a 3.19.2 release defect.
The live GitHub asset digest matched the preserved value above. No DLL,
shader-cache rebuild, in-game test or Nexus upload was performed for this
tooling-only correction.
