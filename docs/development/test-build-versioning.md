# Test-build versioning

CSX has two deliberately separate identities:

-   `CSX_VERSION` is the release and compatibility version, such as
    `3.19-VR`.
-   `CSX_TEST_BUILD` is an optional test-distribution identity, such as
    `RC204-2026-08-22`.

A stable build therefore remains `CSX 3.19-VR`. An allocated test build is
displayed as `CSX 3.19-VR RC204 (2026-08-22)` and its AIO archive is named
`CSX_AIO-3.19-VR-RC204-2026-08-22.7z`.

## Stored state

`version/test-build.json` is the source of truth. The sequence is global and
monotonically increasing; it does not reset when `CSX_VERSION` changes. The
initial formal allocation is seeded after the existing RC203 work, so the next
allocation is RC204.

The state records the base version, UTC date, represented source commit, and
the pull requests discoverable in that merge batch. Reprocessing the same
source commit does not increment the sequence.

## Merge batching and builds

When a pull request is merged into the repository's default branch, the
allocator waits three minutes. A newer merge restarts that quiet period, so a
closely grouped set of merges normally receives one test-build number. A fixed
workflow concurrency group, state validation, and a final remote-head check
prevent duplicate or stale allocation commits.

The allocation commit then dispatches a separate distribution workflow. The
commit is marked to skip ordinary push CI so PAT-backed and `GITHUB_TOKEN`
pushes cannot produce duplicate or missing builds. The distribution builds only
the normal AIO package, with DevBench disabled. It does not invoke the prebuilt
shader-cache workflow or package supplementary presets or caches.

The test identity is passed explicitly to CMake. It is never derived from the
build machine's clock, so rebuilding an allocation reproduces the same label
and archive name.

## Shader-cache boundary

The test-build identity currently does **not** alter `Plugin::VERSION_LABEL`,
`CSX_PLUGIN_VERSION`, the compatibility marker, Settings version, or any
shader-cache metadata or validation. Those continue to use only `CSX_VERSION`.
This is intentional until shader management owns and defines the relationship.
