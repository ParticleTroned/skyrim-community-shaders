# Shader Development Workflow

## Quick Reference

```bash
# Fast shader-only deployment (recommended for dev iteration)
cmake --build build/ALL-WITH-AUTO-DEPLOYMENT --target COPY_SHADERS

# Full deployment (DLL + tests + shaders)
cmake --build build/ALL-WITH-AUTO-DEPLOYMENT --target DEPLOY_ALL

# Prove an HLSL refactor changed no compiled bytecode
pwsh tools/verify-shader-refactor.ps1 package/Shaders/Foo.hlsl

# Runtime A/B for expected bytecode divergence
# See docs/development/shader-runtime-ab.md
exec(open(r"<repo>/tools/taa-renderdoc-ab.py").read())

# Compile only shaders affected by working-tree changes
cmake --build ./build/ALL --target validate_changed

# Build raw SE and VR shader-cache archives for release assembly
python tools/build-shader-cache.py --runtime both --package
```

## Prebuilt shader cache

Use `tools/build-shader-cache.py` for release caches. The authoritative
prerequisites, exact local and CI commands, version/update rules, validation
gates, recovery procedures, scope boundary, and AI-agent instructions are in
the [Prebuilt Shader Cache Runbook](./prebuilt-shader-cache.md).

## Startup compilation estimates

During initial startup, the compilation overlay shows elapsed time, a central
total-time estimate and a labelled range immediately. Compatible saved costs
are reused; a first run starts with broad defaults and refines them as shaders
finish. The range is a heuristic, not a guaranteed completion window; its
upper end is not the predicted total.

ETA uses separate shader family, stage and major Lighting technique costs,
worker capacity, and remaining work in active jobs. The estimate uses the
configured dispatch limit capped by remaining tasks, so the first dispatched
worker cannot inflate the prediction. Active jobs retain individual tail
estimates. Successful source compiles refine their own category; disk hits
and shared bytecode have separate costs. For categories without measurements,
defaults adapt to the machine using the median measured-to-default cost ratio
from at least three categories
in the same shader stage, each with at least three source samples. Each
category has equal weight, so a common expensive shader cannot dominate.
Direct category measurements take precedence. Compatible history can supply
these measurements on subsequent startups.

Calibration requires direct measurements or a calibrated stage model to
support at least half the remaining cost, rather than a fixed number of
completed shaders. Calibration is diagnostic and never hides the initial
prediction. Inferred stage costs retain the wider range; cache hits,
failures and stale compiler contexts cannot teach source speed. Scheduling
priority and compiler options are unchanged. This is a heuristic adaptation,
not a fixed speedup factor; unusually different category costs can still
affect accuracy.

Startup timing priors are stored in `CSX-CompileTimeHistory.json` beside
`CSX.log`, using the runtime's discovered SKSE log directory. This optional
file survives restarts, shader-cache clears and mod replacement. The file is
read on startup without deleting or truncating it. When absent, timing
history starts with the new compilation measurements.

Changed timings are checkpointed as startup tasks complete, at most once
every five seconds, with a final flush at DataLoaded. Writes use atomic
replacement outside the compilation queue lock. A failed write retains the
previous file and remains pending for retry while startup is active. A
cache-only run leaves existing history unchanged. Compatible categories not
compiled in the current run are retained when other categories are updated.
Invalid or stale completions do not schedule writes. Checkpoint deadlines
are checked under the existing completion lock using its existing timestamp.

The compatibility key covers runtime, compiler identity, shader ABI/global
defines, compiler options, worker count and startup priority mode. Invalid
or incompatible history falls back to
initial estimates without deleting the file. The model and loaded priors
are released at DataLoaded before waiting for an active history writer.
The final flush drains before startup returns; a failed in-flight checkpoint
is retried during this flush. This also applies when background compilation
allows remaining startup tasks to continue.
There is no in-game timing learning, history I/O or ETA calculation;
gameplay keeps basic progress and elapsed-time reporting. Different CPU load
and changed shader sources can still affect startup ETA.

DevBench's `communityshaders.shader_api` snapshot includes
`snapshot.compilation.estimate`: elapsed and remaining milliseconds, heuristic
bounds, enabled/calibration status, learned fraction, source sample count and
worker capacity. The learned fraction counts directly measured costs;
inferred stage costs can enable calibration but do not increase that fraction.
Remaining values are available from the start, including while calibration
is pending. They are null when disabled, and zero when enabled but idle.
Observing an estimate never schedules compilation or writes timing history.

## Verifying refactors

`tools/verify-shader-refactor.ps1` (bash wrapper: `tools/verify-shader-refactor.sh`)
compiles a shader from a base git ref and from the working tree across the `VR`
x `HDR_OUTPUT` permutations, then compares the compiled DXBC bytecode. The base
ref's full include tree is materialized with `git archive`, so shared `.hlsli`
changes are compared against their base-ref versions instead of being masked by
working-tree headers.

-   **IDENTICAL** SHA-256 of the `.cso` means the compiled GPU program is
    byte-for-byte identical.
-   **DIFFERS** dumps `/Fc` assembly differences for review.

Exit codes: `0` all identical, `2` some differ, `1` compile error. By default,
the base is `merge-base(HEAD, <current branch upstream>)`. Pass
`-BaseRef <ref>` to compare against another ref. Requires `fxc.exe` from the
Windows SDK. The default sweep is
useful targeted coverage, not the full `.github/configs/shader-validation*.yaml`
matrix; pass `-Permutations` for feature-specific define combinations.

When a refactor intentionally changes compiled bytecode, validate the behavior
with the RenderDoc runtime A/B harness in `tools/taa-renderdoc-ab.py`. Capture
one frame, swap in baseline and candidate DXBC on that same frame, and judge the
candidate against the baseline noise floor. See
`docs/development/shader-runtime-ab.md` for the operator runbook and the
RenderDoc/upscaling caveats on this branch.

## Incremental Shader Validation

Validating the full shader suite recompiles thousands of variants per config,
which is slow for a small HLSL change. Incremental validation compiles only the
entry-point shaders that transitively `#include` a changed file, derived from an
include dependency graph. A leaf shader used by one entry point validates in
seconds; a shared `Common/*.hlsli` fans out to every shader that includes it.

### Local

```bash
# Compile only shaders affected by working-tree changes vs HEAD
cmake --build ./build/ALL --target validate_changed

# Override the config (defaults to Flatrim / shader-validation.yaml)
cmake -DVALIDATE_CHANGED_CONFIG=.github/configs/shader-validation-vr.yaml -S . -B build/ALL
cmake --build ./build/ALL --target validate_changed
```

The target depends on `prepare_shaders`, so the AIO shader tree is assembled
first. Both targets are available even with `AUTO_PLUGIN_DEPLOYMENT` and
`AIO_ZIP_TO_DIST` disabled. Staging does not build the game DLL; when
`BUILD_SHADER_TESTS` is enabled, it runs the shader unit tests first.
Under the hood it runs `tools/validate_changed_shaders.py`, which maps
changed `package/Shaders/**` and `features/**/Shaders/**` paths into the AIO
layout and passes them to `hlslkit-compile --changed-files`. Local use requires
Python and an `hlslkit-compile` version with `--changed-files` support on PATH.

### CI

The PR shader-validation job feeds the PR changed-file list from
`tj-actions/changed-files` into the same wrapper. Validation is narrowed only
when it is provably safe. These cases force a full run:

-   a validation config (`.github/configs/**`), CMake, or submodule change,
    because these can redefine the entry-point/define set;
-   a changed shader path outside the known shader roots;
-   push/release builds, which do not provide a PR change set.

`hlslkit` applies the same safety net independently: any changed path it cannot
find in the shader tree falls back to full validation. Preprocessor guards are
ignored when scanning `#include`s, so the affected set is always a conservative
superset.

## Overview

Two deployment targets for different workflows:

-   **`COPY_SHADERS`** - Fast shader-only deployment (seconds)
-   **`DEPLOY_ALL`** - Full build + tests + deployment (minutes)

### Requirements

-   Must have `AUTO_PLUGIN_DEPLOYMENT=ON` in your CMake preset
-   Must have `CommunityShadersOutputDir` environment variable set to your Skyrim directory

### Usage

#### Manual

```bash
# Fast iteration: Only copy changed shaders to game directory
cmake --build build/ALL-WITH-AUTO-DEPLOYMENT --target COPY_SHADERS

# Or in Visual Studio: Right-click "COPY_SHADERS" target -> Build

# Full deployment (same as running cmake --build with no target):
cmake --build build/ALL-WITH-AUTO-DEPLOYMENT --target DEPLOY_ALL
```

#### Automatic (VSCode)

You can configure VSCode to automatically deploy shaders when you save `.hlsl` or `.hlsli` files using the [RunOnSave](https://marketplace.visualstudio.com/items?itemName=emeraldwalk.RunOnSave) extension.

**See [VSCode Setup](../development/vscode-setup.md) for complete configuration instructions.**

### Prerequisites

1. Run `cmake --preset ALL-WITH-AUTO-DEPLOYMENT` at least once to create build directory
2. Set `CommunityShadersOutputDir` environment variable to your Skyrim `Data` directory
3. Ensure `AUTO_PLUGIN_DEPLOYMENT=ON` in your CMake preset

### What COPY_SHADERS does now

1. ✅ Transforms shaders from source layout → game layout (via AIO staging)
2. ✅ Copies only changed shader files (incremental robocopy)
3. ✅ Deploys to `$CommunityShadersOutputDir/Shaders`
4. ❌ Does NOT build the DLL
5. ❌ Does NOT run shader tests
6. ❌ Does NOT deploy non-shader files

### Target Comparison

| Target            | Builds DLL | Runs Tests | Copies Shaders | Use Case              |
| ----------------- | ---------- | ---------- | -------------- | --------------------- |
| `COPY_SHADERS`    | ❌         | ❌         | ✅             | Fast shader iteration |
| `DEPLOY_ALL`      | ✅         | ✅         | ✅             | Full deployment       |
| `prepare_shaders` | ❌         | ✅         | ✅ (AIO only)  | CI validation         |
