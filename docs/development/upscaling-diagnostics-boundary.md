# Upscaling diagnostics boundary

Production upscaling must not depend on retained diagnostic history or on
DevBench being compiled or connected. `DEVBENCH_BRIDGE=OFF` excludes hot-path
reporting; `DEVBENCH_BRIDGE=ON` retains the qualification evidence. A DevBench
AIO still collects that evidence, so this separation is not a performance
claim for diagnostic builds.

| Surface                   | Production                                                         | DevBench build                                                                           |
| ------------------------- | ------------------------------------------------------------------ | ---------------------------------------------------------------------------------------- |
| Presentation observations | Exact eye, cycle, generation, backend and hold-release proof       | Also counts paths and times lifetime/session stretch episodes                            |
| Controller snapshots      | Operational state under its existing mutex                         | Also derives diagnostic owners/phases and stretch reporting                              |
| Transition metrics        | Current request, retry/failure and recovery state                  | Also retains the 50-entry completed/superseded history                                   |
| Stress capture            | No capture state, mutex, event recording or menu controls          | Existing controls, bounded events, schema and health gates                               |
| Memory queries            | System commit and DXGI budget/pressure/admission checks            | Also samples private-process memory and retains its peaks                                |
| Motion sharpening         | Same validation, dispatch, resource ownership and fallback         | Also records the last dispatch status for the menu bridge                                |
| DLSS dispatch             | Same frame-token/constants coherence and bounded failure reporting | Also collects verbose option/resource descriptions and trace payloads                    |
| Menu tracing              | Semantic tracking and accepted indexed-draw hooks                  | Diagnostic D3D hook installation is available only here; its forensic switch remains off |
| Startup environment       | Explicit Developer Mode diagnostics, with text at debug level      | Same startup facility plus bridge status access                                          |

Useful production debug messages describe transitions and state changes
from information already required by rendering. Optional structured startup
diagnostics run only when Developer Mode and their setting are enabled.
They do not collect per-frame samples. Diagnostic status messages must not
be promoted to info level. Device loss, allocation failure and other real
failures retain warning/error reporting.

Do not gate the complete presentation observer, memory sampler, current
metrics record, or DLSS dispatch context merely because it contains
diagnostic fields. They also carry authoritative state. In particular:

-   Preserve both-eye coherence, stable-frame requirements, relatch/hold
    release and native presentation decisions.
-   Preserve request/epoch/generation identity, current retry history,
    retirement and post-load recovery, including current-metric clearing.
-   Preserve system-commit reserves, DXGI budgets, pressure hysteresis and
    sampling cadence used by admission and recovery.
-   Preserve the native renderer critical section, accepted-draw tracking,
    frame-token/constants cache, per-eye evaluations and prepared colour input.
-   Preserve all DevBench evidence and schema fields, including diagnostic
    stretch results and rejecting terminal/stereo/active-tail health gates.

Changing compiler guards requires compiling the actual production sources
with the bridge both off and on, including their external callers. A policy
test alone cannot detect a missing guard in a caller or struct declaration.
Run the controller suite, check retained functional symbols and excluded
diagnostic symbols, and scope formatting to the changed files. Runtime
visual and performance qualification remains separate; source inspection
and successful compilation do not establish a frame-time improvement.

## Review and validation, 2026-09-16

Reviewed against parent `f70f29103e282e79d63b216ab2b43ad6bbd8ba33` on
`perf/cpu-dlss-regression-20260916`. The cleanup changes diagnostic collection
and log visibility, not rendering policy. No presets, defaults, shaders,
provider selection, native context ownership or scheduling were changed.

The review followed each gated field to its consumers. Current metrics and
their clearing remain operational; completed history is optional. Both-eye
observations and release decisions remain shared. The two external trace-hook
callers in `Globals.cpp` and `State.cpp` are gated along with the installer;
accepted indexed-draw hooks and menu semantic epochs remain available.
Production retains bounded provider error reporting and debug state changes.
Private-memory figures are omitted from ordinary memory messages rather than
reported as misleading zeros. DevBench JSON retains the real figures.

No alternative controller or duplicated policy implementation was introduced.
Dormant menu-trace helper declarations still share the translation unit with
production menu code; their existence is not evidence of runtime cost. The
production trace-active result is compile-time disabled, diagnostic detours
cannot be installed, and symbol inspection excludes the diagnostic resource
hook while retaining accepted indexed draws.

Validation used CMake 4.4.1 and MSVC 14.51.36231, with Universal SE/AE/VR
Release flags:

-   `pwsh ./tools/cmake.ps1 --build --preset CSmain -- /m:1` passed with
    `DEVBENCH_BRIDGE=ON`.
-   `pwsh ./tools/cmake.ps1 --build build/ALL --config Release --target controller_tests shader_tests -- /m:1`
    built both test groups.
-   `ctest --test-dir build/ALL -C Release -N` saved 124 discovered tests.
-   `ctest --test-dir build/ALL -C Release --output-on-failure --no-tests=error --timeout 300`
    passed all 124 tests in 119.06 seconds, including shader execution.
-   Fourteen actual production translation units, including the API and hook
    callers, compiled separately with DevBench off. This was a compile check,
    not a separately linked production DLL.
-   Three principal translation units also compiled separately with DevBench
    on. All 42 diagnostic/functional symbol-presence assertions passed.
-   A supplementary source comparison found identical C++ tokens in 14 key
    function bodies after selecting DevBench branches, including the complete
    qualification record emitter, frame-token/constants handling, presentation
    observer, promotion and stable publication. This supports the source review;
    it does not substitute for the compiler or runtime tests.
-   Changed-line clang-format checks, other applicable scoped pre-commit hooks,
    and `git diff --check` passed. Whole-file clang-format was replaced by the
    changed-line check to preserve unrelated legacy formatting.

Local commands, compiler logs, symbol assertions, inventory and CTest output
are retained under `build/validation/upscaling-telemetry-audit-20260916/`.
Existing deprecation warnings for Streamline sharpness and Windows string
conversion remain; they were not suppressed. No in-game assay or measured
performance improvement is claimed by this cleanup.

## Submit trace compiler boundary, 2026-09-19

The OpenVR submit trace declaration, implementation and all four external
call sites are compiled only with `DEVBENCH_BRIDGE_ENABLED`. The production
submit path does not depend on link-time elimination of a trace helper
whose tracing-active predicate is constant false. Release enables LTCG;
pre-link references alone do not prove an overhead in the shipped DLL.
This is explicit diagnostic exclusion, not a measured performance fix.

Submit results, packet retention, quarantine and recovery decisions remain
unchanged. Accumulator semantic epochs and accepted-draw processing remain
available in production even where their helper names contain `Trace`.
These helpers carry operational state and cannot be gated as diagnostics.

The targeted object-symbol assertion failed before the correction because
both the definition and the caller reference existed with DevBench off.
Local source, compile logs and symbol checks are preserved under
`build/validation/release-upscaling-audit-20260919/`.

After the correction, both affected translation units compiled with
DevBench off and on, and all 61 selected symbol assertions passed.
Twenty focused current-source controller tests passed, including native
boundary, relatch, stereo and freshness coverage. These are separate
translation-unit checks and a controller subset, not a new full DLL build
or physical-HMD performance result.
