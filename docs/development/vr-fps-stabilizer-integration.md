# VR FPS Stabilizer controls and live reload

The **VR > VR Stabilizer** tab edits the installed Stabilizer INIs. The
page selector separates Profiles, Performance, LOD & Grass, Quality Levels,
Locations, and Commands. Normal options use named controls with units and
tooltips; custom commands remain in explicit advanced editors.

Game-unit distance controls display whole numbers and use 500-unit +/-
steps. Type a whole-number distance directly for finer adjustments. Actor,
object and item fade multipliers and city modifiers use steps of 1; direct
entry also supports fractional values.

The main settings catalog covers all 42 additional `[Settings]` options in
the supplied VR FPS Stabilizer 1.4.13 beta1 configuration. The existing
Profiles page owns `CSVRFadeToBlackDuration` and unconditional
Interior/Exterior CSX profiles. The new pages also expose all ten quality
levels, five location tiers plus Interior, grass world lists, and the
DataLoaded, PostLoadGame, AfterLoadGame, NewGame and Conditional sections.

## Editing and applying

-   **Save & Apply** writes the current INI and queues its reload on the
    SKSE game thread. Performance, LOD, levels and commands share one draft;
    locations use their separate INI. Save a draft before editing the same
    file through Profiles or an external editor.
-   **Read INI / Reload INI** reads disk into the editor. It does not execute
    the external reload interface. Discarding a draft is explicit.
-   **Apply saved INI** reloads disk settings without saving an editor draft.
-   Missing main settings show **Configure**, which explicitly adds the
    supplied beta template value. Opening the editor never invents settings.
-   Completion means the external void API returned and, for the main INI,
    CSX refreshed its own profile snapshot. Commands still wait for their
    configured events and conditions; reloading does not replay startup
    events. Visual effects require runtime verification.
-   When the INI has no Interior/Exterior CSX profile rows, switching starts
    off. Select a Method in both columns before saving. The other fields
    initially show the current CSX values so creating profiles does not
    silently turn existing features off. For DLAA interiors and Hoshipa
    exteriors, select NVIDIA DLSS in both columns, then DLAA and Hoshipa
    respectively. Render Scale is a separate choice for the exterior.

The status area reports whether Stabilizer is loaded, its detected build,
reload availability, pending work, completion and failures. When the DLL
is absent, every Stabilizer control is disabled, including page navigation,
profile switching, fades and save/reload actions. Leftover INIs cannot
activate profile sync or enable saves. The status explains how to enable
the companion mod. A loaded older version without the optional interface
retains **Save INI**, which clearly requires restarting Skyrim VR.
`ResetIniSettings()` is not invoked: reloading must not unexpectedly reset
the player's game INI settings.

## Interface and ownership

`src/VRAPI/VRFpsStabilizerInterface001.h` preserves the exact four-slot
revision 1 virtual interface supplied by the mod author, without a virtual
destructor. During SKSE PostPostLoad, VR sends message `0xF43A9D7C` to
`VRFpsStabilizerPlugin` and requests revision 1. CommonLib's messaging
wrapper supplies CSX's plugin handle. SE and AE do not dispatch the request.

Companion presence is detected once at PostPostLoad using the loaded
`VRFpsStabilizer.dll` module, following the existing companion-plugin
detection pattern. A successful interface handshake also proves presence.
The cached flag is separate from live API availability and defaults to
false before detection. UI, persistence, reload and profile reconciliation
share this gate; an INI on disk never proves that the plugin is loaded.

Main INI reloads call `loadConfig()`; location reloads call
`loadLocationConfig()`. CSX's save-load profile reconciliation takes a
synchronized snapshot of the most recently reloaded main INI. Existing
upscaling transition admission, runtime blocks and render-scale safety
policies remain in force.

Profile snapshots carry a revision. Pending-load Render Scale intent is
cached per thread and revision, so a live reload cannot reuse a decision
from an older profile with the same load frame, method and quality.

Only one save/reload may be pending. Main and location reload failures keep
their own outstanding reload state so an unrelated successful reload cannot
clear a restart requirement. The author interface has no success/error
return for these methods; CSX cannot certify the external parser's outcome.

## Persistence and validation

The editor preserves unedited bytes, including custom commands, comments,
duplicate key formatting, UTF-8 BOMs and line endings. Profile saves retain
the existing managed-profile normalization behavior. Duplicate value edits
update every matching key consistently. Advanced whole-section editing
rejects duplicate section headers and embedded new section headers.

Files are bounded to 4 MiB. UTF-16, embedded NULs, non-finite edited numeric
values, invalid integer syntax, reversed paired limits, invalid world lists
and out-of-range controls are rejected. Untouched custom settings are not
silently normalized. The original loaded contents must still match disk
before saving. Atomic replacement failures retain the original file and
report an error; this integration disables the shared writer's direct-write
fallback. No changes are made to the downloaded beta package.

Advanced command editors retain Stabilizer's native syntax. They are for
users who understand those commands; CSX does not reinterpret arbitrary
console commands or replay them on its own.

CSX's serialized settings keys, defaults, loading, saving and migrations
are unchanged. The unified-preset source fingerprint is refreshed because
it includes the complete Upscaling and VR source files. Contract revision
5 and the base template are retained; generated preset settings change
only their compatibility fingerprint.

## DevBench

DevBench builds register `communityshaders.stabilizer`; production builds
compile out the adapter. All responses carry CSX build provenance.

| Action   | Behavior                                                                          |
| -------- | --------------------------------------------------------------------------------- |
| `status` | Plugin `loaded`, interface build/availability, pending state, revision and result |
| `read`   | Installed INI contents, path and main control catalog                             |
| `save`   | Validated `settings` and/or complete `sections` patches; save and queue reload    |
| `reload` | Queue a reload of the saved main or location INI                                  |

`file` is `main` or `locations`; no arbitrary path is accepted. `save`
requires `expectedContents` from `read`. Setting and section values are
strings. `expectedBuildId` rejects requests to an unintended CSX binary.
The response distinguishes saving from reload completion; inspect `status`
after pending work completes. Advanced section patches can exercise the
same conditional profiles exposed by the Profiles UI.
`save` and `reload` reject requests when the companion is absent. `read`
remains available for diagnosing leftover INIs without changing them.

## Validation

The adversarial review covered scope, ABI and thread ownership, INI
preservation, failure behavior, UI navigation, and reuse of existing
infrastructure. It found and corrected the following issues:

| Finding                                                                        | Correction and evidence                                                                                                                                                              |
| ------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| A section header without a final newline joined the next edit onto the header. | Insert the missing separator; regression covers plain and commented headers and preserves CRLF. The test failed before the fix.                                                      |
| A cached pending-load decision survived a live profile revision.               | Include the profile revision and keep the cache local to each thread. The extracted production function failed the same-frame reload regression before the fix and passes afterward. |
| A changed limit could be accepted alongside an invalid unchanged partner.      | Validate both members of related limits; the test covers an unsupported CPU sample window.                                                                                           |
| A failed discard/read retained a dirty draft and blocked navigation.           | Explicit reload clears the discarded draft before reading; errors remain visible without trapping navigation.                                                                        |
| Duplicate-row enumeration repeatedly rescanned the entire file.                | Enumerate in one pass while retaining first-key spelling and last-value semantics; the regression covers case-insensitive duplicates.                                                |
| A queued reload could execute after its INI disappeared.                       | Recheck bounded, readable text on the game thread before invoking the provider; the test verifies no external call and a recoverable failure.                                        |
| Optional interface exceptions escaped initialization.                          | Degrade to unavailable with the error recorded; a throwing test provider verifies the fallback.                                                                                      |
| First-time profiles replaced current CSX values with disabled defaults.        | Keep the resolved runtime values when no managed profile rows exist; switching still starts off until both methods are chosen.                                                       |

Successful edits also clear earlier edit errors, and range errors include
their actual limits. UI and DevBench share the section catalog. Profile
initialization is shared, and the frequent sync-availability query reads
the protected state without copying a full configuration.

The focused post-review checks pass: 104 parser/validation assertions,
the extracted interface/save/reload and profile-cache integration test,
and the real atomic-write persistence test. The complete local validation
had passed all 172 CTest cases plus preset tests before this review; rerun
the command below for the final committed build. Archive receipts retain
the final validation directory, build identity, inventory and hashes.

The clean-INI regression extracts the production profile-editor initializer
and preset mapping. It checks 50,176 Interior/Exterior combinations across
all four methods, all seven quality presets, both Render Scale states and
feature switches, including FSR and DLSS. Its 200,728 assertions pass.
This verifies editor state and preset translation; applying the profile in
Skyrim VR still requires runtime acceptance. The correction does not
rewrite profiles already saved by an older build or establish the cause of
an NVIDIA driver crash while DLSS Render Scale is active.

CSX API build 12 also accepts a configured current-cell profile after a live
reload or a delayed retry. Previously, the admission predicate accepted
only the opposite profile outside LoadingMenu. An exterior
`(UpscaleMethod=3, RenderScaleMode=true, UpscalePreset=5, DLSSProfile=1)`
request could therefore return `Blocked` forever while standing outside,
even when the renderer safety mask was clear. The preflight now checks a
settled no-op before profile admission, returning `NoChange` when no
renderer work is needed. A changed current-cell profile returns `Apply`.
The atomic setter uses the same admission predicate, and Render Scale
eligibility is resolved with the existing method/quality policy.

The extracted production preflight, setter, target resolver, admission
predicate and no-op check pass 7,838 assertions. They cover both cell types,
pre-move and current-cell requests, handoff admission, all four methods,
seven presets, five DLSS profiles and both Render Scale inputs. FSR ignores
the DLSS profile field. Active safety masks still block preflight and the
setter, and unsettled physical state cannot report a no-op. The original
implementation fails the regression for the author's exact exterior call.
These tests stub the renderer; beta2 in-game acceptance and the separate
render-scale qualification have not run. Callers must retry on a later
game task/frame so CSX and Skyrim can complete pending work.

Build and run the focused parser/validation checks with:

```powershell
pwsh ./tools/cmake.ps1 --build build/ALL --config Release --target vr_fps_stabilizer_config_test vr_fps_stabilizer_integration_test vr_fps_stabilizer_profile_editor_test vr_fps_stabilizer_api_test game_setting_persistence_test
./build/ALL/Release/vr_fps_stabilizer_config_test.exe
./build/ALL/Release/vr_fps_stabilizer_integration_test.exe
./build/ALL/Release/vr_fps_stabilizer_profile_editor_test.exe
./build/ALL/Release/vr_fps_stabilizer_api_test.exe
./build/ALL/Release/game_setting_persistence_test.exe
```

These cover byte preservation, numeric constraints, the supplied message
and vtable ABI, game-thread queuing, stale edits, atomic replacement
failure, per-file reload failures and missing-interface fallback. Use
`pwsh ./tools/validate-local.ps1` for the full DLL, controller, shader,
preset and build-provenance validation record.

The missing-plugin regression runs with both INIs present. It verifies
that main/location saves and reloads fail without writing or queuing work,
and that the extracted production sync and render-scale-intent functions
remain inactive. Loaded older plugins retain save/restart and profile
sync; the existing runtime, OpenComposite, RenderDoc and profile validity
gates remain covered. The menu contract checks the shared disabled scope
around all Stabilizer pages. Full in-game UI acceptance remains pending
installation by the user; no DLL deployment is part of this work.

Each integration-test run exclusively creates a directory with a clock
suffix and retries name collisions. Successful runs remove only their own
fixture; failed runs retain theirs and report its path. A retained legacy
fixture reproduced the old startup failure. After this correction, two
sequential and four concurrent runs passed in that same working directory,
preserved the retained fixture byte-for-byte, and removed their own fixtures.

The universal DLL build checks SE/AE/VR compilation. Runtime acceptance
requires the beta interface, live edits to both INIs, UI inspection, an
unavailable-interface case, and transitions using reloaded profiles.
Compilation and configuration tests do not substitute for those checks or
the repository's separate render-scale qualification protocol.
