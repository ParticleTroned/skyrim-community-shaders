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

Schema and sequence values must be integers; booleans and floating-point
forms are rejected. UTC dates must be strings in real `YYYY-MM-DD` form.
Invalid persisted fields fail validation before state or workflow outputs
are written.

The state records the base version, UTC date, represented source commit, and
the pull requests associated with the complete first-parent merge range. It
also names the commit that last changed the state. Each allocation is accepted
only when it increments that exact predecessor, uses its own parent as the
source, and changes no file other than the state. Missing Git history or
unavailable pull-request association evidence fails allocation rather than
silently publishing a partial list.

State commits follow the default branch's first-parent history, so an ordinary
PR merge introduces the seed once even when its topic branch revised the seed
several times. Verification walks every allocation back to that introduction;
deleting and later restoring the seed does not reset the sequence.

Merge the initial versioning PR with a merge commit or squash. Rebase merging
that PR would replay its earlier seed-schema revisions onto the default
branch, which the one-time seed check rejects. Subsequent source PRs may use
merge, squash, or rebase merging.

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
Earlier failed runs reduce the remaining dispatch budget. Within one publisher
invocation, uncertain dispatch commands also consume that budget while GitHub's
run inventory catches up. An uncertain final attempt is reconciled before the
publisher reports exhaustion.
The allocation workflow also supports explicit manual dispatch for initial
activation and recovery. Complete Git/GitHub range discovery is the sole source
of authoritative PR provenance; a manual run cannot inject a PR number. It
applies the same lineage, tag, retry, and remote-head checks and never bypasses
the allocator.

The distribution builds only the normal AIO package, with DevBench disabled.
It does not invoke the prebuilt shader-cache workflow or package supplementary
presets or caches. Before upload, the shared build requires `dist/` to contain
exactly the expected AIO archive. It inspects the archive member list and
rejects DevBench, prebuilt shader-cache, preset-supplement, or compiled shader
objects, then uploads that exact validated file rather than the containing
directory.

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
