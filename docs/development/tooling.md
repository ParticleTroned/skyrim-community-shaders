# Developer tooling

The repository owns its Git-hook, pre-commit, and Windows build launchers. This keeps development independent of a particular Python installation, user `PATH`, global temporary directory, or Git Credential Manager state.

## One-time setup

Run this from any worktree:

```powershell
pwsh ./tools/setup-dev.ps1
```

From a normal (non-Codex-sandbox) PowerShell terminal, also run this once for
the current Windows user:

```powershell
pwsh ./tools/setup-git-user.ps1
```

That migration keeps fetches and public dependencies on HTTPS while routing
GitHub pushes through SSH for every repository.

The setup is idempotent. It:

-   creates a pinned developer-tool virtual environment under the common Git directory;
-   shares pre-commit hook environments across worktrees;
-   enables the tracked `.githooks` directory;
-   validates the pre-commit configuration and pre-installs hook environments;
-   configures this repository to use OpenSSL for HTTPS and explicit SSH URLs for GitHub remotes.

Set `CSX_GITHUB_SSH_COMMAND` before setup when an isolated account must use a
specific SSH executable, private key, or known-hosts file without reading the
interactive user's `.gitconfig` or credential vault.

The initial hook-environment installation can take several minutes. Let it finish; later commits reuse the same environments.

## Daily commands

```powershell
# Run Git with this worktree as a command-scoped safe directory
pwsh ./tools/git.ps1 status --short

# Run hooks on staged files, as Git does before a commit
pwsh ./tools/pre-commit.ps1 run

# Validate a branch diff
pwsh ./tools/pre-commit.ps1 run --from-ref origin/cs-1.7-PL-SE --to-ref HEAD

# Configure or build with a sandbox-safe temporary directory
pwsh ./tools/cmake.ps1 --preset ALL
pwsh ./tools/cmake.ps1 --build build/ALL --config Release --target CommunityShaders shader_tests

# Diagnose the complete local toolchain and remote connectivity
pwsh ./tools/dev-doctor.ps1 -Network
```

Do not set Windows user-level `TEMP` or `TMP` to a repository path. Do not inject those variables through Codex `shell_environment_policy`. The Windows sandbox helper initializes before spawned commands and can fail when its own temporary directory is inside a managed writable root. The CMake and pre-commit launchers set temporary paths only after the command process has started.

Codex read-only and mutating commands can run under different isolated Windows
identities. The Git launcher adds only the current worktree as a command-scoped
`safe.directory`; it never adds `safe.directory=*` or changes the user's global
safe-directory list.

## GitHub transport

Repository remotes use SSH for authenticated Git operations. Public pre-commit and dependency sources remain HTTPS. Avoid a global `url.git@github.com:.insteadof=https://github.com/` rewrite because it silently converts public tool downloads into authenticated SSH operations.

The user setup replaces that rule with `pushInsteadOf`, which applies only to
pushes. Repository launchers also isolate public tool downloads from a legacy
broad rewrite, so pre-commit remains usable before the user migration is run.

GitHub CLI authentication is separate from Git SSH authentication. `git fetch` or `git push` can work while `gh auth status` reports an invalid API token. Reauthenticate `gh` only when CLI/API features are needed; Git commits and pushes do not depend on it.

On Windows, this repository selects Git's OpenSSL backend to avoid Schannel-specific failures. The setup is repository-local and does not replace credential settings for unrelated hosts.

The launchers propagate the repository's OpenSSL and explicit SSH settings to
recursive child Git processes. This keeps public and nested submodule fetches
on HTTPS while allowing authenticated child operations to use the same strict
SSH identity as the superproject.

`tools/cmake.ps1` honors an explicit `VCPKG_ROOT`, then checks `vcpkg.exe` on
`PATH` and installed Visual Studio instances. The discovered root is scoped to
the launcher process. `tools/dev-doctor.ps1` validates both `vcpkg.exe` and the
toolchain file instead of treating CMake alone as a complete build toolchain.

The launchers keep vcpkg downloads, registries, and binary archives in the
shared `csx-tools/vcpkg` directory under Git's common directory. This avoids
depending on a writable user-profile cache in isolated environments and lets
worktrees share immutable package artifacts. Explicit `VCPKG_DOWNLOADS`,
`X_VCPKG_REGISTRIES_CACHE`, and `VCPKG_DEFAULT_BINARY_CACHE` values are
preserved. The doctor verifies that every active cache path is writable.

In a Codex sandbox, the launcher also routes vcpkg assets and the pinned
FidelityFX runtime downloads through the managed Python TLS stack because the
native Windows downloaders cannot access the interactive user's Schannel
credentials. This fallback is not enabled for normal developer sessions and
never replaces an explicit `X_VCPKG_ASSET_SOURCES` setting. Every FidelityFX
download remains SHA-256 verified, and matching cached files are reused.

## Recovery

Run the doctor before changing global configuration or deleting caches:

```powershell
pwsh ./tools/dev-doctor.ps1 -Network
```

The doctor reports stale worktree registrations and temporary Git objects but does not delete them. `git worktree prune --verbose` removes registrations whose worktree paths no longer exist; it does not delete branches. Preserve shader caches and build outputs unless a task explicitly requires rebuilding them.

After confirming that no Git operation is running, remove only abandoned Git
`tmp_obj_*` files with:

```powershell
pwsh ./tools/clean-git-temporary-objects.ps1 -Apply
```

The cleanup refuses to run while Git-related processes exist and validates that
every target remains inside the common Git object directory. It does not run
`git gc`, remove branches, or touch build and shader caches.
