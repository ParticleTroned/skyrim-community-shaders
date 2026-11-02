# Water dev rebase: publication and validation

## Published source

All fourteen water code branches, the two dependency bases, and the combined integration were rebased onto Open Shaders `dev` commit `34b4b82881d2507ebba26a958f471a7e5f8a7742` on 2 November 2026. The previous upstream base was `d64dc05b60d38774f43797b9d4e6c1f357b8b1f8`.

The rebase preserves all 41 original water/repair/integration commits with mapped parent relationships, including the manually resolved integration merges. Updated branch heads and dependencies are recorded in `water-integration-manifest.json` and `water-port-branches.md`.

Published integration before this validation-note commit: `4d8859aa4de9a8ac9d3b568f620d7551a5ec41d5`.

The fork-only publication run [41507558052](https://github.com/ParticleTroned/skyrim-community-shaders/actions/runs/41507558052) succeeded. It atomically updated all 17 refs with explicit expected-old-head leases and created 17 backup refs under `backup/os-water-pre-dev-34b4b82881-20261102/`. Remote verification matched every intended head. The `main-VR` and `main-SE` heads were unchanged across publication. No upstream PR, upstream push, version bump, or release was made.

## Completed checks

- Every rebased commit has the new upstream base in its ancestry; all required dependency relationships are preserved.
- Production water source outside the upstream changes is byte-identical to the corresponding previous branch snapshot. The only overlapping path is the English translation catalog, for which key/value preservation from both sides was checked explicitly.
- All ten water shader/helper payload hashes checked against the previous successful AIO manifest matched. This is a source-equivalence check, not validation of a new binary.
- Whitespace/error checks passed for each rebased branch.
- Five Python suites ran successfully before publication: shader-cache build (25 tests, 2 skipped), shader-cache verification (29 tests, 11 skipped), FOMOD packaging (23 tests), scene-settings catalog generation (29 tests), and CMake shader staging (7 tests). Total: 113 tests, 100 passed and 13 skipped.

The preview's initial missing `hlslkit` and `pyfomod` dependencies were corrected in the isolated preparation workflow; no production tests or warning gates were disabled.

## Scope retained

The hard water-parallax ceiling remains 64 steps, VR water foveation remains included, and the debug depth toggle is unchanged. There is no additional 256-step policy, blanket Shadow/PortalStrict exclusion, extra scene-depth-copy proposal, or external NR runtime DLL introduced by this rebase. Existing conservative settings and testing requirements remain in effect.

## Full integration validation

This documentation-only push triggers the existing `Water integration test build` workflow on the rebased integration. It retains MSVC/clang-cl plugin builds, SE/VR water preflight in both depth conventions, full-config/release-parity shader compilation, shader/C++/Python unit tests, and guarded complete-AIO packaging. The package gate still rejects `nvngx_dlssnr*.dll`.

The publication checks above are complete. The full Windows build and in-game SE/AE/VR correctness/performance are NOT established by this note. Use the new run's actual result and its `water-integration-test-build` artifact as the installation gate. The successful archive from commit `362160e22b` is the previous build, not a build of this rebased source.

## Local worktrees

This operation changed fork remote refs only; it did not access or update the Windows worktree `D:/Coding/GitHub/os-water-integration` or switch any local branch. Because the remote water history was rebased, an old `--ff-only` update will not cross the rebase. Preserve any local work before reconciling it, or use a new dedicated worktree. Do not hard-reset a different working branch.
