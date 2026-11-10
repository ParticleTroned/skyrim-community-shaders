# Optional DLL loader diagnostic

The DLL loader diagnostic loads a built CSX DLL repeatedly in fresh
surrogate processes. Use it when investigating failures before SKSE plugin
initialization, particularly after compiler or CommonLib changes. It is
an optional developer command and is not part of normal builds, packaging,
or automated release qualification.

The host uses a version resource and an executable region filled with
return instructions in place of engine code. It cannot model engine
objects, return values, or real startup ordering. A failure can be specific
to this surrogate; a pass establishes only repeated DLL loading in it.
The host never calls `SKSEPlugin_Load` or validates rendering, unloading,
or shutdown. Follow up with authorized real-game validation.

## Inputs and invocation

Run with PowerShell 7.4 or newer on Windows. Python, Visual Studio C++ x64
tools, and the Windows SDK resource compiler are required. The runner uses
the repository's existing Visual Studio discovery; an inherited developer
environment must already target x64.

Supply an existing DLL and its finalized build manifest. The default
manifest is `CSX.BuildManifest.json` beside the DLL; `-Manifest` accepts a
different location. The existing provenance verifier must accept both the
Build ID and artifact hash, and the artifact size must also match.
Keep the DLL's runtime dependencies beside it or in their normal installed
locations; the surrogate's DLL search environment is not the game process.

For an installed VR game:

```powershell
pwsh ./tools/dll-load-smoke/run.ps1 -Dll path/to/CommunityShaders.dll -GameRoot path/to/SkyrimVR -Runtime VR
```

Use `-Runtime SE` for the 1.5 runtime family and `-Runtime AE` for 1.6 or
later. VR requires 1.4.15.0. The runner reads the four-part version from
`SkyrimSE.exe` or `SkyrimVR.exe` in the supplied game root. An explicit
`-RuntimeVersion` must match that executable when present.

A staged game root without the executable requires `-RuntimeVersion`, for
example `1.5.97.0`, `1.6.1170.0`, or `1.4.15.0`. It must contain the matching
Address Library under `Data/SKSE/Plugins`:

| Runtime | Address Library filename                         |
| ------- | ------------------------------------------------ |
| SE      | `version-<four-part-version-with-dashes>.bin`    |
| AE      | `versionlib-<four-part-version-with-dashes>.bin` |
| VR      | `version-<four-part-version-with-dashes>.csv`    |

These must be physical files visible to the diagnostic, rather than files
available only through a mod manager's virtual filesystem. The runner holds
read locks on the DLL and Address Library throughout the diagnostic.

## Results and limits

Defaults are 30 loads, an 80 MiB surrogate image region, and a 60-second
timeout per child process. `-Runs` accepts 1 through 1000, `-PadMB` accepts
16 through 256, and `-TimeoutSeconds` accepts 1 through 60. Changing the
surrogate region changes the diagnostic conditions and can affect failures.

Each invocation creates a unique ignored directory under
`.local-docs/dll-load-smoke/`. It builds a new host there and checks both
compiler exit codes before any load attempts. A successful attempt requires
a timely zero process exit, complete output collection, and exactly one
full-line success marker. A timeout is a failure even if a marker was
printed. The runner terminates timed-out process trees; supervision errors
stop further attempts and remain failures.

`summary.json` records the requested and completed counts, individual stage
results, DLL SHA-256 and size, verified Build ID and producer source
identity, Address Library identity, tool source hashes, and host/tool
identities. Each stage also retains stdout, stderr, elapsed time, timeout
status, process exit code, and its JSON receipt. Early failures retain the
available evidence and fail the command; zero attempted loads never pass.

The command returns zero only if every requested load passed. This result
is not a game startup or rendering verdict. Keep generated binaries, logs,
and reports local; summarize outcomes and limitations when publishing.
