# Test-build versioning

CSX has two deliberately separate identities:

-   `CSX_VERSION` is the release and compatibility version, such as
    `3.19-VR`.
-   `CSX_TEST_BUILD` is an optional test-distribution identity, such as
    `RC218-2026-09-07`.

A stable build therefore remains `CSX 3.19-VR`. An allocated test build is
displayed as `CSX 3.19-VR RC218 (2026-09-07)` and its AIO archive is named
`CSX_AIO-3.19-VR-RC218-2026-09-07.7z`.

## Stored state

`version/test-build.json` is the source of truth. The sequence is global and
monotonically increasing; it does not reset when `CSX_VERSION` changes. The
root state is bound to the existing RC217 tag and its UTC date, so the next
allocation is RC218.

The state records the base version, UTC date, represented source commit, and
the pull requests associated with the complete first-parent merge range. It
also names the commit that last changed the state. Each allocation is accepted
only when it increments that exact predecessor, uses its own parent as the
source, and changes no file other than the state. Missing Git history or
unavailable pull-request association evidence fails allocation rather than
silently publishing a partial list.

## Merge batching and builds

When a pull request is merged into the repository's default branch, a
cancellable job waits three minutes. A newer eligible merge restarts only that
quiet job, so an unmerged or differently based close cannot cancel publication.
The publisher is separately serialized and cannot be cancelled by another
merge event.

The publisher verifies the complete state lineage and reconciles the previous
allocation before advancing it. It atomically pushes the state-only commit and
a unique `csx-test-build-RCn-YYYY-MM-DD` tag. The tag, commit parent, state
lineage, source version, and package identity must agree. Rollback, an
unrelated descendant, a copied state, or a moved tag fails closed.

Distribution is dispatched for that immutable tag and exact allocation SHA.
Existing queued, running, or successful runs are reused. Failed or uncertain
dispatches are reconciled before retry, with no more than three attributable
attempts; exhaustion requires manual diagnosis and does not allocate another
RC. Re-running the publisher at an unchanged allocation therefore resumes the
same identity instead of treating its state commit as new product source.
The allocation workflow also supports explicit manual dispatch for initial
activation and recovery. It applies the same lineage, tag, retry, and remote
head checks and never bypasses the allocator.

The distribution builds only the normal AIO package, with DevBench disabled.
It does not invoke the prebuilt shader-cache workflow or package supplementary
presets or caches. Before upload, the shared build requires `dist/` to contain
exactly the expected AIO archive.

The test identity is passed explicitly to CMake. CMake admits only an empty
stable identity or a positive RC with a real Gregorian date. Rebuilding an
allocation from its immutable tag reproduces the same label and archive name.

For a manual rebuild, dispatch `test-build-distribution.yaml` from the
allocation tag and supply the tag's exact commit as `allocation-sha`. A
branch, descendant, or arbitrary state-bearing commit is not a valid rebuild
source.

## Shader-cache boundary

The test-build identity currently does **not** alter `Plugin::VERSION_LABEL`,
`CSX_PLUGIN_VERSION`, the compatibility marker, Settings version, or any
shader-cache metadata or validation. Those continue to use only `CSX_VERSION`.
This is intentional until shader management owns and defines the relationship.
