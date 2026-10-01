# Open Shaders 2.17 port review

Review the eight selective ports from
`d53ba287d69dec693a107fbd834e36df1ef95b72` through
`80193fb46f5eab15a9d0986d642c21cb85aecccc` for scope, correctness,
robustness, reuse of existing code and avoidable performance regressions.
Follow-up fixes retain separate commits and identify their original
implementation in the commit body.

The user stopped end-of-sync validation before this review. Configure
completed and compilation/linking began, but the task-owned process tree
was terminated. Logs remain under the local
`build/analysis/open-shaders-217-review-20260929/final-validation` directory.
That interrupted run is not a successful build or validation result.
No further builds, shader compilation or game measurements are authorized
until the user resumes them.

## Tracy tool revision headers

Original commit: `e6fb4ff9d658d126ec4121744da9e3052da9d632` (#806).

Upstream's GitRef helper creates one global generation target in the first
tool's binary directory, but adds each subsequent tool's own directory to
its header search path. The combined CLI/viewer build therefore leaves
later tools without their generated `GitRef.hpp`. Looking up HEAD from a
downloaded source archive can also report the surrounding vcpkg checkout
instead of Tracy's source revision.

Pass the overlay's existing source pin as `TRACY_GIT_REF`. The patched
helper configures a revision header in each consuming directory directly
from that pin, updating it only when contents change. This bypasses the
shared custom-target path and archive Git discovery for the packaged tools.
The source pin is declared once in the port; standalone upstream behavior
remains available when the override is absent.

Validation without compilation:

-   Applied the complete overlay patch to files read from the pinned Tracy
    archive; `git apply --check` passed.
-   A CMake script audit exercised the actual patched helper with its old
    path selected and reproduced the csvexport/capture header-directory
    mismatch. This failure was expected.
-   The same audit with the source-pin override passed for all seven tools,
    including repeated configuration, and checked every generated revision.
-   Full tool builds and live protocol/capture validation remain unrun.

## Ambient-effect work without a visible contribution

Original commit: `c20bcd836558851971d43ba8d44b0ba4272ac833` (#803).

The new lighting mode evaluated ambient/IBL and filtered shadows even
when effect brightness, the Effects multiplier or lighting influence was
zero. Sky statics likewise traced world shadows before their full-hide
discard. Directional shadow work was also unnecessary when the directional
light contribution was exactly zero.

Keep uniform branches around the optional mode and its zero-contribution
cases. Preserve the unlit fraction when lighting influence is partial and
retain point-light accumulation. Discard fully hidden sky statics before
their new lighting work; return ambient lighting directly when there is
no directional contribution. Nonzero lighting calculations, shadow sample
counts, shader layouts and settings remain unchanged.

Validation without shader compilation:

-   A source audit checked the branch guards and early-discard ordering.
-   An algebra audit checked 1,296 scalar combinations of influence,
    brightness, Effects multiplier, shadow, ambient, directional, base colour
    and point light; original and guarded compositions agreed. This checks
    the zero-case reasoning, not compiled GPU execution.
-   Changed-line clang-format 22.1.4 and `git diff --check` passed. Full-file
    shader formatting is skipped to preserve unrelated legacy formatting.
-   DXBC comparison, runtime A/B and GPU timing remain deferred. The change
    removes avoidable work by inspection; no measured speedup is claimed.

## Periphery dispatch failure propagation

Original commit: `4a5d6f0cd9f8244c3a2f51e34c3f9ed7b420ad2a` (#791).

The committed-history policy relies on a successful eye composite meaning
that the periphery history was written. Its existing low-level dispatch
returned void on nine missing-resource or invalid-region guards, while the
adapter always returned true. That leaves the new continuity record unable
to distinguish an aborted dispatch from a history write.

Return false from those guards and true only after issuing the dispatch
and unbinding its compute resources. Forward that result through the
existing tile-list and fallback-rectangle failure handling, which prevents
the composite from reaching the history commit on failure. Mark the helper
nodiscard. Preserve all guards, dimensions, stereo ownership and normal
render scheduling; no new resources or passes are introduced.

Validation without compilation: a source audit checked all nine failure
returns before binding, the sole success return after dispatch/unbind,
and propagation through both existing failure routes. It also verified
that guard and dispatch calculations are otherwise identical. Production
FoveatedSaveReuse and Adaptive Balance fixtures extracted successfully;
this is not a compiled-test pass. Changed-line formatting and diff checks
passed. Fault-injection, compiled controllers and runtime qualification
remain pending; no performance measurement was made.

## Coverage and remaining limits

| Original port    | Scope and review result                                                                                                                                                                        |
| ---------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| #792 `9b7818e97` | Existing utility binds the same b5/b6 buffers with null checks; no allocation or extra pass. No issue found.                                                                                   |
| #791 `4a5d6f0cd` | Reviewed both dispatch routes, four reset sites, resource contracts, frame/cycle continuity and stereo commit ownership. Dispatch-result gap corrected above.                                  |
| #785 `eedb801d0` | Startup conflict entry uses the established checker and explains LLF hook ownership. No steady-frame work or new runtime-specific hook. No issue found.                                        |
| #794 `cf67b03b3` | All three grass motion-vector writes preserve XY and provide explicit Z/alpha in existing render targets. No extra texture samples or passes. No issue found.                                  |
| #803 `c20bcd836` | Global persistence/presets, default-off runtime gates, DevBench schema, 80-byte CPU/HLSL layout, point lights and sky resource availability reviewed. Avoidable lighting work corrected above. |
| #810 `724adb74c` | Four guards reuse the shared Interior Sun availability flag and existing masks. Extra interior shadow work is the requested correction; no duplicate pass. No issue found.                     |
| #806 `e6fb4ff9d` | Pin/hash/protocol, core versus tool features, dependency scope and Release tool paths reviewed. Revision header integration corrected above.                                                   |
| #819 `80193fb46` | Capture follows time-jump handling and precedes current-frame caster handoff. Reset/invalid state, world rotation, fallback and selected-direction refresh reviewed. No issue found.           |

The Tracy client delta moves instrumented unlock bookkeeping before the
native unlock and renames the corresponding C entry points. No local
consumer of those lock-wrapper or renamed C APIs was found. Keep that
upstream correctness change; the tool-header fix does not alter client
instrumentation. Protocol 83 still needs matched rebuilt binaries.

No excluded wind, GO, Scene Manager, E11/EHF/SLF, translation, upstream UI
or upstream NR implementation was introduced. Existing shader/serialization
and D3D helpers are reused, and no new graphics resource requires naming.
The existing tests cover policy and layout portions, but rendering and
performance are not qualified by source review. Shader compilation/DXBC,
DLL/controller/tool builds, SE/AE/VR runtime checks and matched GPU/CPU
measurements remain outstanding. The three follow-up changes are source
review fixes, not evidence of a measured frame-time improvement.
