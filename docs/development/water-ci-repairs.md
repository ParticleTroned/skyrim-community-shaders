# Water integration CI repairs

This document records the fork-only follow-ups to integration run `41299604824` and the subsequent run `41302393633`. Source merge: `0b1e8857667a5692737859704873d2102c0a3590`.

## Changes made

| Group | Failure addressed | Follow-up |
|---|---|---|
| 4 | Native D3D enums/descriptors passed to REX wrapper interfaces | Convert interfaces with `Util::AsReal` before calling the native SDK APIs. |
| 3 | Smart node pointers passed to raw-pointer culling helpers | Pass `.get()` explicitly; do not change ownership. |
| 9 | Signed/unsigned target-count comparison | Compare the validated target and target count in the same unsigned type. |
| 2 | Refraction fallback initialization warning | Initialize bounds and output fields and retain the validated fallback path. |
| 2, 8, 10a, 1, 11b | FXC optimizes away `isfinite` checks on computed values | Use shared bit-exponent classification with scalar/vector overloads; retain strict production flags. |
| 11b and shader tests | DXC legacy signedness diagnostics for high-bit literals, even with a suffix | Use unsigned bit expressions (`~0u`, `1u << 31`) rather than high-valued literals. |
| 11b | FXC candidate-loop variable scope warning | Give initialization, ranking and packing loops distinct indices. |

Corrections are committed on their originating work branches and merged forward into dependent branches and the integration. Follow-up commit messages identify the preceding commit. Published history is not rewritten.

## Evidence and remaining gates

The SE and VR water-only permutation compiles in run `41302393633` each reported **zero new warnings and zero errors in both standard and reverse-Z configurations**. That run still failed its preflight on the separate candidate-compute loop warning and failed shader unit tests on legacy literals. Those remaining diagnostics are addressed by this source merge.

C++ and Python unit-test jobs passed in that run. This does not establish a successful full plugin build or full shader validation for the final source tree.

The push containing this document triggers the complete integration workflow again: MSVC/clang-cl plugin builds, SE/VR water preflight, full-config/release-parity shader compilation, shader/C++/Python unit tests and guarded AIO packaging. Use the resulting run, not an earlier commit's status, as the installation gate.

The shader suite now discovers six additional regression functions covering scalar/vector finite values, non-finite refraction bounds/fallback, the parallax budget and empty packed candidates. No tests or compiler warning gates are disabled.

The 64-step parallax ceiling, the existing debug depth toggle, upstream shadow ownership and NR runtime-DLL exclusion remain unchanged. No upstream PR, main-VR update, release or version tag is created. In-game SE/AE/VR correctness and performance still require the existing test matrix.
