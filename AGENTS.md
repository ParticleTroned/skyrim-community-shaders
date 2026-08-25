# Repository tooling instructions

These instructions apply to the complete repository.

## One-time setup

-   Run `pwsh ./tools/setup-dev.ps1` after cloning or when the developer-tool environment changes.
-   The setup script installs a repository-owned pre-commit runner under the common Git directory, shares hook environments across worktrees, and configures the tracked `.githooks` directory.

## Required command entry points

-   In Codex on Windows, invoke repository Git commands through `pwsh ./tools/git.ps1 <git arguments>` so linked-worktree ownership is scoped safely without changing global `safe.directory`.
-   On Windows and in Codex, invoke CMake through `pwsh ./tools/cmake.ps1 <cmake arguments>`.
-   Invoke pre-commit through `pwsh ./tools/pre-commit.ps1 run <pre-commit arguments>`.
-   Run `pwsh ./tools/dev-doctor.ps1 -Network` when Git, hooks, authentication, caches, or the Windows sandbox behave unexpectedly.
-   Do not set user-level `TEMP` or `TMP`, and do not inject them with Codex `shell_environment_policy`; both approaches can prevent the Windows sandbox helper from starting.

## Git and GitHub

-   Use explicit SSH URLs for GitHub repository remotes.
-   Keep public dependency URLs as HTTPS. Do not add a global `https://github.com/` to `git@github.com:` rewrite because it leaks repository authentication policy into pre-commit and dependency downloads.
-   To make SSH pushes work across repositories without rewriting fetches, run `pwsh ./tools/setup-git-user.ps1` once from a normal user terminal.
-   Git pushes and GitHub CLI API authentication are separate. A working SSH push does not imply that `gh auth status` is valid.

## Validation and long-running work

-   Scope pre-commit to the files or revision range being changed. Do not run `--all-files` merely to validate a focused change; the repository contains legacy third-party files with preserved formatting.
-   Never interrupt shader compilation or shader-cache generation because output is temporarily silent. Check process/cache activity first and allow the documented build window.
-   Preserve user-owned build and shader caches unless the user explicitly requests cache removal.
