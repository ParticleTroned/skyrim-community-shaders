# Ghidra MCP integration

The repository provisions
[GhidrAssistMCP](https://github.com/alandtse/GhidrAssistMCP) as an optional
reverse-engineering tool. It is not linked into Community Shaders and does not
participate in normal CMake, shader, or release builds.

The installer pins upstream revision
`2f6b10410081ea3691a4c2a73f8e8e7f24b72fcd`. Source is cached under the
repository's shared Git tooling directory, so worktrees reuse one verified
checkout without adding a submodule or untracked source tree.

## Install

Requirements:

-   Ghidra 11.4 or newer;
-   Java 25 or newer;
-   Ghidra closed while the extension is replaced; and
-   network access for the first source and Gradle dependency download.

Run the installer with explicit Ghidra and Java locations:

```powershell
pwsh ./tools/setup-ghidra-mcp.ps1 `
  -GhidraInstallDir 'D:\Tools\ghidra_12.1.2_PUBLIC' `
  -JavaHome 'C:\Program Files\Eclipse Adoptium\jdk-25.0.3.9-hotspot'
```

Pass `-GhidraUserExtensionsDir` when the local Ghidra profile redirects its
extension directory. Pass `-RefreshSource` only to replace a damaged cached
checkout with the same pinned revision.

The installer validates the Ghidra layout and Java major version, fetches the
exact pinned commit over HTTPS, runs the upstream `installExtension` Gradle
task, and restores the caller's Java environment afterward.

## Managed headless server

The normal automation path does not require a Ghidra window, a manually
created project, or manual plugin activation. Managed sessions use Ghidra's
native `analyzeHeadless` launcher and do not require PyGhidra activation. Start
one with the exact binary to import:

```powershell
pwsh ./tools/ghidra-mcp-control.ps1 start `
  -GhidraInstallDir 'D:\Tools\ghidra_12.1.2_PUBLIC' `
  -JavaHome 'C:\Program Files\Eclipse Adoptium\jdk-25.0.3.9-hotspot' `
  -ProgramPath 'C:\Program Files (x86)\Steam\steamapps\common\SkyrimVR\SkyrimVR.exe'
```

The first start creates the `SkyrimVRMcp` project in the controller's local
application-data state directory, imports the binary, and runs Ghidra analysis.
The MCP endpoint becomes ready after that analysis completes. A bounded start
wait can therefore return `state: starting`; it does not interrupt analysis.
Inspect progress and readiness with:

```powershell
pwsh ./tools/ghidra-mcp-control.ps1 status `
  -ProgramPath 'D:\Artifacts\intended-build\CommunityShaders.dll'
```

The status receipt is ready only when the harmless `list_binaries` MCP probe
succeeds and reports the active executable path matching `-ProgramPath`. The
controller records the exact artifact SHA-256 when it imports the program and
rechecks that hash for every status request. Always pass the artifact intended
for the current investigation. A listening endpoint or an old project program
is not sufficient.

Later starts reuse the saved paths and imported program without rerunning
auto-analysis:

```powershell
pwsh ./tools/ghidra-mcp-control.ps1 start
```

Passing a different `-ProgramPath` after stopping the managed session imports
and analyzes that artifact with overwrite semantics, then binds the saved
session to its path and SHA-256. This prevents a project left on an RC build
from being accepted for a PR artifact.

Stop the managed session cleanly with:

```powershell
pwsh ./tools/ghidra-mcp-control.ps1 stop
```

The stop action creates only the session's controller-owned completion file.
Ghidra then closes the MCP server, saves the project, and exits normally. The
controller never terminates a process when clean shutdown times out, and it
refuses to claim an unmanaged server already using port `8080`.

The controller uses `tools/ghidra_scripts/CSXGAMCPStartServerScript.java` to
normalize the alternating key/value arguments emitted by Ghidra's Windows
headless launcher. The adapter then delegates server startup, wait mode, and
completion-file handling to the pinned extension implementation.

Use `-ProjectDirectory`, `-ProjectName`, and `-ProgramName` to select a
different persistent analysis project. Do not open the same project in the
Ghidra GUI while its managed headless session is running.

## GUI server fallback

For an interactive Ghidra session, restart Ghidra after installation, then:

1. Open **File > Configure > Configure Plugins**.
2. Find and enable **GhidrAssistMCP**.
3. Open **Window > GhidrAssistMCP**.
4. Bind the server to `127.0.0.1` on port `8080` and enable it.

Keep the server on loopback. Do not expose an unauthenticated analysis server
to the LAN. Leave `import_file`, `scripts`, and `export_program` disabled unless
the current analysis explicitly requires their host-filesystem or code-execution
access.

## Connect Codex

The installer can register the streamable HTTP endpoint in the current user's
Codex configuration:

```powershell
pwsh ./tools/setup-ghidra-mcp.ps1 `
  -GhidraInstallDir 'D:\Tools\ghidra_12.1.2_PUBLIC' `
  -JavaHome 'C:\Program Files\Eclipse Adoptium\jdk-25.0.3.9-hotspot' `
  -RegisterCodex
```

Registration is idempotent when the existing `ghidra` server already targets
the same endpoint. It fails instead of overwriting a different server. To
register an already-installed extension without rerunning the installer, use:

```powershell
codex mcp add ghidra --url http://127.0.0.1:8080/mcp
codex mcp get ghidra
```

Start a new Codex session after changing the MCP configuration so the Ghidra
tools are discovered. Either the managed headless session or a GUI session
must be serving the endpoint before those tools can answer requests.

## DevBench pairing

Ghidra MCP and DevBench remain separate loopback services with separate
ownership:

-   `ghidra` on port `8080` owns persistent static analysis and symbols;
-   `devbench_vr` on port `8921` owns the exact live Skyrim VR process and
    runtime operations; and
-   Codex orchestrates both MCP services during an investigation.

Keep both servers registered and enabled in Codex. The DevBench SKSE plugin
does not launch Java or own Ghidra's project lifecycle. This keeps process
launching out of the game and lets the Ghidra controller enforce bounded waits,
loopback identity, persistent project reuse, and clean shutdown independently.

Before correlating a Ghidra address with runtime evidence, verify DevBench's
health response identifies `SkyrimVR.exe`, VR mode, the intended PID, and port
`8921`. Then use Ghidra MCP for decompilation and cross-references against the
matching imported image or live dump.

## Skyrim VR analysis

The extension does not bypass Skyrim VR's encrypted on-disk `.text` section.
Continue using the live-dump workflow in
[VR render-scale iteration records](vr-render-scale-iteration.md) to capture
decrypted process bytes. Import the saved dump into Ghidra, establish its image
base from the matching metadata file, and use MCP for decompilation, cross
references, symbols, types, and comments.

Treat Ghidra mutations as persistent project changes. Review renames, types,
patches, and comments before calling `save_program`. Binary patching and
project deletion tools should remain outside ordinary read-only analysis.

## Updating the pin

Update `$pinnedRevision` in `tools/setup-ghidra-mcp.ps1` and the revision above
in the same change. Review upstream build scripts, bundled dependencies, MCP
tool permissions, and loopback defaults before advancing the pin. Reinstall
with `-RefreshSource`, then verify extension startup and MCP tool discovery.
