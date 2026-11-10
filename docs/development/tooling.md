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
-   shares pre-commit hook environments across worktrees for each Windows account;
-   enables the tracked `.githooks` directory;
-   validates the pre-commit configuration and pre-installs hook environments;
-   configures this repository to use OpenSSL for HTTPS and explicit SSH URLs for GitHub remotes.

Set `CSX_GITHUB_SSH_COMMAND` before setup when an isolated environment needs
an explicit repository-local SSH command. Keep credentials and authentication
locations local.

The initial hook-environment installation can take several minutes. Let it finish; later commits reuse the same environments.

On Windows, hook caches are separated by account SID because Python creates
private environment directories. This lets desktop and sandbox accounts use
the same checkout without reusing inaccessible environments. Existing caches
are preserved. `CSX_PRE_COMMIT_HOME` still overrides the hook cache location.
Virtualenv's default metadata cache lives under the common Git tool directory;
an explicit `VIRTUALENV_OVERRIDE_APP_DATA` is preserved.
The Prettier hook pins its Node runtime so a new cache does not select an
unsupported prerelease from Node's download index.

## Daily commands

```powershell
# Run Git with this worktree as a command-scoped safe directory
pwsh ./tools/git.ps1 status --short

# Run hooks on staged files, as Git does before a commit
pwsh ./tools/pre-commit.ps1 run

# Validate a branch diff
pwsh ./tools/pre-commit.ps1 run --from-ref origin/main-VR --to-ref HEAD

# Configure or build with a sandbox-safe temporary directory
pwsh ./tools/cmake.ps1 --preset ALL
pwsh ./tools/cmake.ps1 --build build/ALL --config Release --target controller_tests

# Record the complete DLL, controller, shader and preset validation
pwsh ./tools/validate-local.ps1

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
the launcher process. On Windows, the launcher also imports the x64 MSVC
environment from the installed Visual Studio instance when it is not already
active. This keeps Ninja builds independent of the shell used to launch them.
Set `CSX_VSDEVCMD` to select a specific `VsDevCmd.bat`. Without that override,
a valid `VSINSTALLDIR` takes precedence over installation discovery so that
repairing an incomplete MSVC environment retains the active toolchain and
SDK state. Active installations with an unavailable path, missing
`VsDevCmd.bat`, or missing MSVC tools fall back to discovery.
`tools/dev-doctor.ps1` validates both `vcpkg.exe` and the toolchain file
instead of treating CMake alone as a complete build toolchain.

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

## Skyrim VR control routing

Use the installed `skyrim-vr-automation` skills and read the selected protocol
before operating the game. Paths below are relative to that automation bundle
or its maintained source checkout. They identify existing controls, not new
protocols to combine into a replacement runner.

| Request                                                   | Existing entry point                                                                                      |
| --------------------------------------------------------- | --------------------------------------------------------------------------------------------------------- |
| DevBench discovery and calls                              | `skills/devbench-control/SKILL.md`                                                                        |
| CSX pass profiling                                        | `tools/profiler-control/Measure-CSXProfiler.ps1` and `Compare-CSXProfiler.ps1`                            |
| Save-load frame times with CPU stacks/waits (`gameft-sw`) | `tools/gameft-sw/Invoke-GameFtStackWait.ps1`                                                              |
| Reload-paired frustum OFF/ON (`frustrum`)                 | `tools/frustrum/Invoke-Frustrum.ps1`                                                                      |
| Settled depth-control matrix (`depthc`)                   | `tools/depthc/Invoke-DepthC.ps1`                                                                          |
| User-guided hotspot walk                                  | `tools/hotspot-sw/Invoke-HotspotSw.ps1`                                                                   |
| COC, menu-transition, static COC, and fidelity assays     | The exact `simple-coc`, `simple-coc-5`, `simple-csm`, `simple-csm-rs`, `static-coc`, or `fidelstab` skill |
| Recorded routes with Tracy and fpsVR                      | `tools/devbench-control/tracy-replay.md`                                                                  |
| Render-scale qualification or tuning                      | The explicitly requested qualification or vendor tuning skill                                             |

For a custom scene comparison, reuse the applicable existing collectors and
logger helpers without invoking an unrelated save-load or transition assay.
Preserve the requested order and scene. Before separate matched windows,
reset noon, verify `gameHour` in `[12, 12.05]`, and settle at least five seconds
unless the selected protocol specifies another rule. Check the required live
capabilities before starting any recorder. A production DLL can expose core
DevBench controls without registering the CSX profiling bridge; a process or
generic scenario dispatcher does not prove profiler availability.

### fpsVR and CSX measurements

fpsVR records whole-frame CPU/GPU timings. VR FPS Stabilizer owns adaptive
settings; it is a different tool. CSX's internal profiler measures its
instrumented passes and does not replace fpsVR whole-frame timings.

The maintained fpsVR logging helpers are in
`tools/gameft-sw/protocol/Invoke-SaveLoadTimingV2.ps1`. They use the installed
`fpsVRcmd.exe logging_startstop` command, inspect raw CSV growth, and preserve
recording ownership. Reuse them with the selected protocol's rules. Do not
restart fpsVR or SteamVR to repair a measured run. An exit code of zero with
`Can't connect to fpsVR` is a failure. The command and recording hotkey both
toggle state: never issue both to repair an uncertain acknowledgement.

When the user requests the configured recording shortcut, read its actual
fpsVR binding and observe the resulting logger state. Skyrim's DevBench
`input` tool injects game input, not a Windows shortcut for another process.
Use the established desktop control when available; do not substitute mouse
automation or invent a key injector. Verify advancing samples in the pinned
raw CSV before measurement, and stop only a recording owned by this run.

### MCP configuration, readiness, and visibility

Check these separately: the configured server name and endpoint, a successful
live handshake/health result, and tool definitions visible to the current
client. Search the complete callable catalog, including deferred tools.
Missing definitions alone do not establish that a server is unconfigured.
An unsupported `resources/list` method is not a failed MCP connection.

DevBench's endpoint runs inside Skyrim and is unavailable while the game is
closed. Follow `devbench-control` when choosing a transport: callable direct
tools are mandatory; the existing controller is permitted only when the skill
and selected protocol allow it before the first live call. Never improvise
HTTP requests or mix live transports. Runtime metadata's `exe` expectation
matches the health executable name; retain the full physical executable path
in the process identity rather than substituting it for that name.

For managed Ghidra, use this repository's
`tools/ghidra-mcp-control.ps1 status -Compact`. An enabled HTTP configuration
connects to a service; it does not launch Ghidra. When starting Ghidra is in
scope, the existing controller's `start` action can reuse its saved project.
Require endpoint readiness, binary-list readiness, and the expected program.

A client can retain a failed startup after the server becomes healthy. Prove
server readiness before requesting a client refresh. Refresh that client's
MCP connection after its required services are ready; do not reinstall or
repeatedly restart a healthy server. The supported client configuration and
restart controls are described in the
[official MCP guide](https://learn.chatgpt.com/docs/extend/mcp?surface=cli).

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
