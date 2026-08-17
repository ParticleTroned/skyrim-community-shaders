# AI Development Instructions

This file provides guidance for AI assistants working with the Skyrim Community Shaders codebase.

## Primary Documentation

**For comprehensive development guidance, see `.claude/CLAUDE.md`** which provides detailed information on:

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

-   **Build**: `./BuildRelease.bat [PRESET]` (WSL: use `powershell.exe -Command`)
-   **Shader Test**: `hlslkit-compile --shader-dir [target]` (install via pip first)
-   **Feature Access**: `globals::features::*` namespace

### Build Options

**Runtime Presets**: `ALL` (universal), `SE`, `AE`, `ALL-TRACY`

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

For detailed explanations, examples, and comprehensive guidance, refer to `.claude/CLAUDE.md`.

## Commit Messages

All commits on the `cs-1.7-PL-SE` branch must use a scoped subject in this format:

```text
scope(target): concise imperative summary
```

Every commit message must also include a body with both of these sections:

```text
Rationale:
- Explain why the change is needed.

Implementation:
- Summarize how the change was made.
```

Keep the subject concise and make the rationale and implementation specific to the committed change.

### Acknowledging Work from Other Contributors

When any implementation is copied, ported, adapted, or materially based on another contributor's commit or pull request, the commit message must include an `Acknowledgements:` section. No acknowledgement is required when the source is exclusively the current user's own work.

Each acknowledgement must include:

-   The original contributor's GitHub-associated email address.
-   The source repository in `owner/repository` form.
-   The source commit SHA, pull request number, or a direct link to the source.
-   A precise description of the specific code, behavior, or approach that was copied, ported, or adapted.

Use this format:

```text
Acknowledgements:
- contributor@users.noreply.github.com — owner/repository, PR #123 or commit abcdef1: adapted the named function or specific behavior.
```

Do not imply that an entire commit or pull request was ported when only part of it was used. Clearly distinguish the externally sourced portion from original implementation work in the commit.
