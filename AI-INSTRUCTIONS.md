# AI Development Instructions

This file provides guidance for AI assistants working with the Community Shaders Expanded (CSX) codebase.

## Primary Documentation

**Read `AGENTS.md` first.** It is the canonical repository-wide policy for pull
requests, commits, code quality, runtime safety, validation, and Git operations.
GPT/Codex and Codex Code Review load the root `AGENTS.md` directly; do not create
a second Codex-only copy of these rules.

Use `.claude/CLAUDE.md` as the detailed architecture and build reference. It
provides information on:

-   Build commands and development setup
-   Architecture overview and critical dependencies (CommonLibSSE-NG)
-   Runtime targeting system for SE/AE compatibility
-   Core architecture including Globals system and feature registry
-   Shader architecture (base shaders in `package/Shaders/`, feature shaders, compute shader patterns)
-   Development workflows and best practices
-   Common pitfalls and testing requirements

## Quick Reference

### Project Type

SKSE plugin providing advanced DirectX 11 graphics modifications for Skyrim SE/AE.

### Essential Commands

-   **One-time setup**: `pwsh ./tools/setup-dev.ps1`
-   **Git in Codex/Windows worktrees**: `pwsh ./tools/git.ps1 <arguments>`
-   **Build**: `./BuildRelease.bat [PRESET]` or `pwsh ./tools/cmake.ps1 <cmake arguments>`
-   **Pre-commit**: `pwsh ./tools/pre-commit.ps1 run <arguments>`
-   **Tooling diagnostics**: `pwsh ./tools/dev-doctor.ps1 -Network`
-   **Shader Test**: `hlslkit-compile --shader-dir [target]` (install via pip first)
-   **Feature Access**: `globals::features::*` namespace
-   **PR and commit format**: `type(scope): description`; target `cs-1.7-PL-SE` and follow the release-aware type and acknowledgement rules in `AGENTS.md`

### Build Options

**Configure Presets**: `ALL`, `ALL-VS2022`, `ALL-DEBUG`, `AIO-Release`

**Build Presets**: `Dev`, `ALL`, `ALL-VS2022`, `Package`, `Shaders`, `Debug`, `AIO-Release`

**CMake Options** (set in user preset):

-   `AUTO_PLUGIN_DEPLOYMENT=ON` - Auto-copy to `CommunityShadersOutputDir`
-   `ZIP_TO_DIST=ON` (default) - Create individual feature 7z packages
-   `AIO_ZIP_TO_DIST=ON` (default) - Create all-in-one 7z package
-   `TRACY_SUPPORT=ON` - Enable Tracy profiler integration

### Custom CMake Targets

**Quick targets** (common):

-   `PREPARE_AIO`, `prepare_shaders`, `COPY_SHADERS`, `AIO_ZIP_PACKAGE`
-   `FORMAT_CODE`, `generate_shader_configs`

For full details about manual packaging targets (Package-Core, Package-AIO-Manual, Package-<Feature>, AIO) and example workflows, see the "Manual packaging targets (detailed)" section in `.claude/CLAUDE.md` to avoid duplication.

### AI Assistant Role

**Act as an experienced graphics programming and Skyrim modding expert.**

**Key Focus**: Performance impact awareness, runtime compatibility (SE/AE), complete working solutions, DirectX/HLSL best practices.

For behavioral rules, refer to `AGENTS.md`. For detailed technical explanations
and examples, refer to `.claude/CLAUDE.md`.
