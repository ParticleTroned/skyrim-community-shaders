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

## Validate packaging

Run the focused release suites before publishing a fork's AIO:

```text
python tests/csx_distribution_test.py
python tests/csx_release_test.py
python tests/release_fomod_workflow_test.py
```

The workflow suite requires PyYAML, CMake, PowerShell, and Bash. It uses
synthetic release assets without uploading them. Keep measured outcomes
and publication receipts in the ignored local records.
