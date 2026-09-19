# September 19 main-VR integration into main-vr-nr

The requested source snapshot is local `main-VR` at
`63e40d610d7a9fdbb93f9c915c39f6fa1c67f157`. The target started clean at
`4b037c7a9a19c54361720465d4afdbd34b181421`. Origin was fetched before
comparison. Work continued across midnight; "today" remains September 19,
the date of the request and all twelve incoming commits.

## Incoming history

| Commit      | Change                                 |
| ----------- | -------------------------------------- |
| `3afd74640` | Protected material admission           |
| `e27d65c26` | Maintained gameft automation tools     |
| `a456b558c` | Relatch lock ownership                 |
| `c50bc986a` | Compile-time submit tracing gate       |
| `30c9db3eb` | Depth-culling controls                 |
| `606be2099` | Capture settings and service discovery |
| `e2bd8c77b` | Managed shader-cache reuse             |
| `012e5139e` | Scene-owned sun shadows                |
| `8ee45559a` | CommonLib 8.3.0 and engine/SDK bridge  |
| `c03809d69` | CommonLib integration ancestry         |
| `c80be3834` | Sun-shadow visual confirmation         |
| `63e40d610` | Production-disabled submit tracing     |

Both sun-shadow changes were already patch-equivalent on NR. A merge
preserves the original authorship and history without replaying their
behavior. The source branch and other worktrees are not modified.

## Integration decisions

CommonLib is pinned and checked out at
`abe9ca7b7318dbc04bccfcd59fc3fb670244c2d7` (`v8.3.0`). NR's additional
engine-resource boundaries use the same upstream `REX::W32::AsReal`
bridge: character capture, full-resolution guides, final-LDR target/depth
checks, retained stereo resource ownership, and flat pre-DLSS inputs.
The bridge reinterprets compatible pointer types; it adds no image copy,
allocation, dispatch, or ownership change.

The flat pre-DLSS route keeps its private NR candidate, DLSS input-history
handling and route receipts. NR source freshness and depth proofs remain
mandatory. Updated source contracts require the equivalent SDK pointer
identities through the CommonLib bridge.

Capture combines upstream settings expansion with NR's immutable retained
measurement/exposure companions and descriptor fields. Menu preparation
retains the NR/FOV+TAA rejection. NR profiling query guards and the
subsurface implementation remain; engine-resource adaptations are applied
without changing their behavior. A/B/C routes, colour/Lighting preservation,
character defaults and the Render Scale dependency guard are retained.

Generated presets keep NR settings revision 8 and its full source inventory.
The incoming screenshot preset change sets legacy
`Screenshot/Sequence/Outputs/SeparateEyes` to false, matching main-VR's
capture defaults. Apart from that upstream value, preset changes are source
fingerprints only. Historical gameft wrappers remain as ignored local files
while their maintained versions live in the automation repository.

## Validation

Passed without a DLL or shader build:

-   Seven CMake source contracts: NR DevBench, submit-pair, Multi-ROI,
    main-depth presentation, screenshot API, VR submit-input freshness and
    menu DevBench preflight.
-   NR colour source contracts: 8 tests; transaction evidence: 27 tests.
-   FOV mask visualization source checks: 4 tests.
-   Shader-cache packaging: 27 tests; managed-pack corpus: 29 cases
    (6 accepted and 23 correctly rejected).
-   Both merged DevBench descriptors parse and retain their combined fields.
-   Generated preset verification and whitespace checks.

Logs are retained in `.tmp/main-vr-sept19-merge/validation/`. Two initial
NR source-contract failures expected the old unbridged pointer spelling;
the corrected assertions retain the original ownership and ordering tests.
The initial screenshot contract invocation lacked `PROJECT_ROOT`; the
complete run passed with that required argument supplied.

Compilation against the upgraded CommonLib, shader execution and in-game
validation have not run under the standing no-unrequested-build instruction.
No production archive was rebuilt or deployed. The previous `4b037c7a9`
AIO remains the pre-merge build. No automatic push is performed.
