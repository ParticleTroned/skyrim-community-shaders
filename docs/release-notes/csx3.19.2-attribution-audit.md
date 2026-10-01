# CSX 3.19.2 Nexus attribution audit

The [Nexus changelog](csx3.19.2-VR-nexus.txt) covers exactly:

-   `CSX3.18`: `2051e2aead1b2bb2b03faa421201376e8bc84fe0`,
    committed 9 August 2026.
-   `csx3.19.2`: `d1980b8e151a814f1e6de5b7036da1b9ef21686e`,
    committed 26 September 2026.

The range contains **536 commits**, including **49 merge commits** and
**five reverts**. The later sky-composition and whole-scene colour-control
commits are excluded. `RC166` is not the baseline.

The earlier draft ended at `4730e3029`, before the final release rebuild.
This record includes `dd6ca74d3` (SE/AE cache input compatibility) and
`d1980b8e1` (water-profile persistence). Their effects and supporting
commits are included in the existing cache and water entries, retaining
the original 66-entry structure. The published tag and AIO are unchanged.

## Credit convention

Only `CS`, `OS` and `CSX` appear in Nexus credit labels. Their order records
the relationship between the implementations:

-   A port or adaptation lists its originating project first, followed by
    the project implementing the adaptation.
-   A new CSX implementation informed by CS or OS lists `CSX` first,
    followed by the relevant source project.
-   `CSX` alone describes an implementation originating in this fork,
    without a CS/OS implementation or design contribution to that change.
    A direct contribution to CSX does not become an OS port merely because
    its author also contributes to OS.
-   A CS commit appearing unchanged in OS history does not earn OS an
    additional implementation credit. Distinct OS changes do.
-   Grouped entries retain the credits for all the changes they describe.
    Existing features before the baseline do not automatically determine
    attribution for a later, independent correction.

Individual authors, co-authors and external design references remain in
the evidence records. No individual names were added to the Nexus text.
The independent FSR and motion-sharpening implementations retain their
OptiScaler acknowledgments in the existing development documentation and
audit evidence; they are not misrepresented as CS or OS ports.

## Complete commit and entry records

-   [Commit inventory](csx3.19.2-attribution-commits.csv): one row for every
    commit in the range, retaining its complete hash, parents, author,
    co-author trailers, subject, message, changed paths, attribution
    assessment, available upstream source candidates and matching merged
    PR description.
-   [Changelog entry map](csx3.19.2-attribution-entries.csv): all **66**
    release-note entries and the exact implementation commits supporting each.

Changed paths use ordinary commit diffs and combined merge diffs. The
September 28 completeness check filled 23 missing combined-merge path
inventories; constituent commits retain their own paths and attribution.
The original project-credit assessments and recorded PR evidence remain.

This is an attribution audit. All commit metadata and changed-path
inventories were reviewed, together with available PR provenance and
upstream source references. Ambiguous cases received targeted source/diff
checks. It is not a claim that every line of all 536 patches received a
new correctness review or runtime test.

The source search indexed 5,803 commit objects from the available
`open-shaders/main`, `open-shaders/dev` and `upstream-review/main`
histories. GitHub supplied all 75 merged CSX PR descriptions available
during the audit. Subject matches are discovery candidates, not proof of
code authorship; explicit source descriptions, co-author evidence and
targeted comparisons resolve the attribution decisions. Merge authorship
does not replace the constituent commits' provenance. Dependency changes
remain identified as integrations of external dependencies in the CSV.

## Corrections and important distinctions

| Changelog area                                    | Final credit  | Evidence and reason                                                                                                                                                                                                                                       |
| ------------------------------------------------- | ------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Manual AIO cache installer                        | CSX + OS      | `d9ba7c458` identifies OS #444's design concept, then describes a new manual four-cache implementation which retains none of the upstream automatic detection or presentation.                                                                            |
| Grass rain darkening                              | CSX + OS      | `15b3face5` adds the local darkening implementation using the wetness-phase data exposed in the OS #470 adaptation `930e362c0`. `72c1c8ce8` later removes obsolete PBR roughness; the entry now describes the surviving ordinary/complex-grass darkening. |
| Puddle-mask modes                                 | CSX           | `730cce00e`, CSX PR #63, is a direct contribution to CSX. Its author identity is preserved in the commit record; no OS port is asserted.                                                                                                                  |
| Dynamic cubemap quality selector                  | CSX + CS      | CS #2645 supplies the higher-resolution capture in `cbc0e6a98`. The selector is the new local implementation `664ef303b`, carried from CSX SE `1cd2bd5b9`.                                                                                                |
| Skylighting                                       | CS + OS + CSX | CS per-probe shadows and history updates combine with local history/loading fixes. CSX PR #94 explicitly identifies OS #596 as the source of capture-state and hemisphere guards in `2fd637c36`. That provenance was not explicit in its commit title.    |
| Mesh Terrain Variation                            | CS + OS + CSX | `96dc8192e` combines CS mesh support carried by OS commit `b0e78b952`, OS tree filtering `36fcbd649`, and local integration. `5dcbf163e` additionally adapts the record-availability correction from CS #2717.                                            |
| Fog, reflections and skin                         | CS + OS + CSX | CS supplies additive fog and skin corrections; `0621d7d9a` restores OS reflection normalization; `8e581f2a5` explicitly ports OS #593 and #464 stereo corrections.                                                                                        |
| Settings-save fallback and compatibility messages | OS + CSX      | `52636aa7b` and `c5fef5326` substantially adapt OS #434 and #520. The source and local adaptation both require credit.                                                                                                                                    |
| OCU integration                                   | CSX           | `54e99dde3`, CSX PR #82, is a direct CSX integration with preserved original authorship. A contributor name is not substituted for project credit.                                                                                                        |
| RenderDoc menu selection and loading holds        | CSX           | `a07f1f680` and `42577999b` add local conditions to CSX's overlay, Stabilizer and post-load-hold policies. The Nexus sentence is restricted to those changes.                                                                                             |

## RenderDoc boundary

The earlier NVIDIA passthrough correction is from
[Community Shaders #2621](https://github.com/community-shaders/skyrim-community-shaders/pull/2621).
Local adaptation `03493e035` records its original source and notes that OS
also carried the same upstream implementation. An ancestry check confirms
that `03493e035` is already contained in `CSX3.18`, so it is outside this
release range.

The in-range patches were checked directly:

-   `a07f1f680` adds the existing RenderDoc blocker to
    `VR::ShouldUseInSceneOverlay()` and explains the SteamVR session override.
-   `42577999b` applies that blocker to Stabilizer synchronization,
    post-load-hold admission and submit suppression.

They do not add or rewrite the NvAPI passthrough implementation. The final
entry therefore claims only these CSX-specific changes, not authorship of
RenderDoc integration as a whole or a fully validated capture fix.

## Validation

-   Exact tag ancestry and endpoint hashes resolved from Git.
-   Commit CSV membership matches all 536 commits in the range, with no
    duplicates. Parents, authors, subjects, message contents and changed paths
    match local Git; message comparison ignores trailing line whitespace.
-   Every changelog entry has supporting commits within the selected range.
-   All Nexus credit labels use only `CS`, `OS` and `CSX`.
-   No `CSX`-only entry is backed by an implementation classified as a
    CS/OS port or adaptation in the audited entry map.
-   All 66 entries are unique and retain the requested category/credit form.
-   The terrain source head `6bd4004e703f674fb702a219daabc759305af158`
    resolves through GitHub to
    [CS #2717](https://github.com/community-shaders/skyrim-community-shaders/pull/2717).
-   This documentation update changes no runtime code, existing authorship,
    published tag, release asset or remote history. No build or runtime tests
    were required for this documentation audit.
