# SE/AE 3.19.2 cache compatibility correction

The original 3.19.2 draft's SE/AE cache admitted its top-level metadata but
missed individual optimized records. The user's initial recompilation was
at normal logging level; Trace was enabled afterward to diagnose it.

## Measured baseline

-   Source: `4730e3029fa5e116e8d22741ef31866cedbaa66b`.
-   Build ID:
    `d37ea7ec36546d3dbe631ad74291b80e99ac19fcf6632be3c2aa4f870740a996`.
-   Runtime: SE, Tempus Maeledictum; startup log reports the matching build
    prefix and `csx3.19.2`. The physical MO2 DLL was not independently hashed
    during this diagnostic; provenance is from the log and preserved release.
-   Preserved trace ends at `2026-09-26 22:30:45.898` local time. It contains
    134 distinct compile requests, 117 developer record appends, 13 managed
    cache hits and no error entries. Compilation was still active.
-   Trace snapshot SHA-256 is recorded with eight representative source/stage
    requests in `tests/data/shader_cache_se_runtime_trace.json`.
-   Full local evidence is retained under
    `build/release-csx3192/ae-cache-investigation/`, including the source
    snapshots, per-record proofs and evidence manifest.

## Causes and correction

The SE captured inventory retained `VANILLA_FRESNEL` in 2,927 of 3,429
standard permutations, `WETTERNESS` outside Lighting/Water in 1,642, and
`HDR_OUTPUT` in 81. These counts overlap. Runtime requests use the current
feature scopes, and the record contract includes the complete macro list
even when a define has no bytecode effect.

The builder now removes the obsolete captured defines and reapplies
file-scoped macros only to their declared files. It also includes the
observed `Grass:Vertex:0` permutation in the SE cross-modlist overlay.
The corrected standard SE inventory has 3,430 entries. VR inventories and
resolved macros remain unchanged for both Horizon states.

Separately, the SE builder replaced mandatory non-shipped exclusions with
derived hidden-feature exclusions. That restored legacy Wetness Effects
to cache staging, although the actual AIO omitted it. The source hasher
scans available conditional includes regardless of the active macro list.
Lighting and Water therefore had different source hashes in the cache and
installed AIO. Distribution exclusions now retain the mandatory package,
feature and define exclusions; the escape hatch for legacy defines is
removed.

| Source   | Standard baseline permutations | Builder source hash                | Actual AIO source hash             |
| -------- | -----------------------------: | ---------------------------------- | ---------------------------------- |
| Lighting |                            589 | `3dc4ff158bf460a88743d691d35319e2` | `7eee4e35126a9c3b497517541af24d01` |
| Water    |                            696 | `365378e16e352c998095c4f09ca8f512` | `cfab5169279531b3707fbc7f56221681` |

No runtime C++, HLSL, cache admission checks, cache format or compiler
optimization policy changes. AE shares the SE cache. Both Horizon Fix
variants remain packaged for each runtime, with runtime record selection.

## Exact identity evidence

Using the pinned hash implementation and preserved release pack, all 116
sampled completed records with a release descriptor reproduced both the
original release identity and the exact developer identity logged by the
game. Removing developer mode from both the flags and macros produced
**zero** optimized matches against the original pack. This proves a
normal-mode incompatibility independently of Trace's separate cache.

With corrected generator inputs, all **134/134** captured requests match
the generated macro tasks, including the formerly missing grass request.
All **116/116** reconstructed optimized identities match the runtime inputs.
This is an offline input-contract proof, not an in-game corrected-cache
hit-rate measurement. It does not claim universal permutation coverage.

## Validation before release rebuild

Run with the pinned environment from `tools/shader-cache-requirements.txt`:

-   `python tests/shader_cache_packaging_test.py`: 33 tests passed.
-   `python tests/shader_compile_identity_test.py <shader_compile_identity_test.exe>`:
    9 tests passed, including FXC/runtime and cross-language identity checks.
-   `python tests/fomod_package_test.py`: 31 tests passed.
-   `python tests/release_fomod_workflow_test.py`: 3 tests passed.
-   Complete preserved trace comparison: 134 matching requests and 116
    matching optimized identities; zero changed VR macro tasks for either
    Horizon state.

The release pipeline must rebuild the production DLL, caches and FOMOD
from the corrected commit and validate the resulting archives. An in-game
reuse check of that rebuilt artifact remains outstanding and should run
at Info level: Trace/debug selects the separate developer lane. No active
game cache was cleared, replaced or read as a completed cache snapshot
during this investigation.
