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
