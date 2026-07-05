# RaceMenu / Render Scale Startup Ledger

Working notebook for RaceMenu, startup, Stabilizer, and render-scale log analysis.

Do not commit this file. Keep it updated after each new log. Each log entry should record the RC/build, log timestamp, whether VR FPS Stabilizer was active, observed timings, conclusions, and whether the result is a baseline, regression, or unresolved case.

## Hard Constraints / Do Not Repeat

-   Do not rely on SKSE `kNewGame` / `VRStartupLoadKind::NewGame` as the gate for RaceMenu startup protection. Multiple logs show `loadKind=unknown` during the actual new-game RaceMenu path while RaceSex open/name/close are still detected later.
-   Do not allow Stabilizer or direct VRAPI render-scale profile application immediately after first world frame just because `loadKind` is unknown. This is the repeated failure mode: Stabilizer applies before RaceSex opens, then worldspace disappears.
-   Do not let the startup hold or startup activation block drift into the ordinary no-Stabilizer startup path. It exists only for the VR FPS Stabilizer sync/INI startup path.
-   Do not blame generic render-scale startup when the Stabilizer INI/profile path is inactive. User-confirmed baseline: render scale can be active in CS settings and startup still works when the Stabilizer INI path is inactive.
-   Do not release the Stabilizer startup hold from a fixed first-load/search timer. The user can wait before starting a new game, and startup can contain multiple LoadingMenu phases. Release must be tied to RaceSex close/name signals.
-   RaceMenu detection itself is not the early-enough signal. RaceSex open/name/close are detectable, but RaceSex opens after the first-world-frame Stabilizer apply window in failing runs.
-   The correct startup classifier must be based on observable first-load state: render-scale intent/request, first-load close frame, no completed RaceSex/name-confirmed startup yet, and the first world-frame window, not on `NewGame`.
-   Blocking final relatch/application is not sufficient if VRAPI/Stabilizer writes into the normal pending transition atomics while RaceSex protection is active. Those pending atomics are read by live getters such as `GetPerfModeRequested()`, so they can pollute the active visual state before the relatch actually applies.
-   In the 2026-07-04 10:17 log, the frame 5137 VRAPI/Stabilizer request is quarantined before live mutation. It does not change the runtime resolution; the reduced DLSS dynamic-resolution plan was already active before RaceSex opened.
-   Do not force DLSS/FSR quality to native `0` during the Stabilizer startup hold. The intended "render scale off" startup path preserves the selected upscaler quality and only suppresses render-scale submit/relatch behavior.
-   Current fix direction: while the Stabilizer startup hold is active, render-scale requested/perf/submit path must be suppressed, but normal selected-quality DLSS/FSR dynamic upscaling may continue.
-   Direct VRAPI/Stabilizer profile requests during RaceSex must be quarantined even if the early INI sniff did not arm the startup hold. The 10:46 log shows render scale is already stable, then VRAPI turns it off during RaceSex and causes the visual loss.
-   The 10:46 log's off/native switch is `origin=VRAPI`, not CS `PostLoadSync`. CS's own INI reader did not log an apply; the external Stabilizer plugin issued the active change through the API.
-   `Util::IsInterior()` can classify startup/RaceSex as interior before the eventual exterior world state is stable. In the 10:46 log, `DeferredCompositeCS INTERIOR` appears before RaceSex, matching the wrong Interior/off profile selection.
-   Current A/B test rule: when the CS `VR FPS Stabilizer Sync` toggle is off, ignore external `origin=VRAPI` upscaling requests entirely. This tests whether direct Stabilizer VRAPI calls are the source of the RaceSex off/native switch.
-   Current A/B test rule 2: suppress only the startup RaceSex switch into `menu/full-resolution` after render scale is already latched. The normal latched render-scale path remains active.
-   Do not treat the `menu/full-resolution` log line alone as the failure. The 11:18 no-Stabilizer run works while still entering `menu/full-resolution`; the failure requires Stabilizer profile/API activity or timing around that shared branch.
-   The 11:34 Stabilizer-active A/B run shows suppressing startup RaceSex `menu/full-resolution` is harmful: the suppression log fires while RaceSex is open and the user sees no worldspace until RaceSex closes.

## Aggregate Information Ledger

| Date       | RC / Build                                                                     | Stabilizer                                                                                             | Scenario                                                                                    | Result                                          | Key conclusion                                                                                                                                                                                                                                                                                                                      |
| ---------- | ------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------- | ----------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 2026-07-04 | Not specified; current logging build                                           | Inactive / no Stabilizer INI observed                                                                  | New game startup with render scale requested/on                                             | Good baseline                                   | RaceSex open, character-name hook, and close are detectable; render scale latches and stabilizes without Stabilizer involvement.                                                                                                                                                                                                    |
| 2026-07-04 | Not specified; current uncommitted logging build after release-frame fix       | Active / VRFpsStabilizerPlugin present                                                                 | New game startup with render scale requested/on and Stabilizer sync active                  | Failing                                         | RaceSex gate releases correctly after name/close, but it arms too late: render scale has already boot-latched before RaceSex opens, and Stabilizer mutates a pending transition while RaceSex is still open.                                                                                                                        |
| 2026-07-04 | RC36                                                                           | Inactive / no Stabilizer INI observed                                                                  | New game startup with render scale requested/on after startup watch commit                  | Mixed                                           | RaceMenu stays visible and render scale waits until after name/close, but the first post-release render-scale frame regresses to the old single-eye submit-bounds failure pattern (`eye=0`).                                                                                                                                        |
| 2026-07-04 | RC36                                                                           | Active / VRFpsStabilizerPlugin present                                                                 | New game startup with render scale requested/on and Stabilizer sync active                  | Failing                                         | RaceSex gating and release timing work, but Stabilizer flips the desired profile to native/off during RaceMenu; the log ends with a clean OFF/native VRAPI relatch and no render-scale boot latch, so black worldspace is coming from a separate startup presentation path.                                                         |
| 2026-07-04 | RC37                                                                           | No active Stabilizer profile; sync toggle still enabled and INI present without unconditional profiles | New game startup with render scale requested/on                                             | CTD                                             | Render scale latches and stabilizes before RaceSex opens; then RaceSex/native presentation inherits reduced render-scale dimensions (`4194x2329`) instead of true HMD dimensions (`4936x2740`) immediately before a face overlay crash.                                                                                             |
| 2026-07-04 | RC37 plus HMD-size RaceSex bypass experiment                                   | No active Stabilizer profile; sync toggle still enabled and INI present without unconditional profiles | New game startup with render scale requested/on                                             | All black                                       | Forcing RaceSex/native bypass to true HMD size creates a destination/output mismatch: runtime output becomes `4936x2740`, but the active destination remains reduced `4194x2329`, so VR per-eye finalize is skipped.                                                                                                                |
| 2026-07-04 | RC37 plus latched-contract RaceSex bypass fix                                  | No Stabilizer INI                                                                                      | New game startup with render scale requested/on                                             | Good baseline                                   | User reports no CTD and both world menu and RaceMenu are shown properly; the fix keeps RaceSex on the already-latched render-scale contract instead of switching to native bypass after latch.                                                                                                                                      |
| 2026-07-04 | RC37 plus latched-contract RaceSex bypass fix                                  | Stabilizer INI present and sync active                                                                 | New game startup with render scale requested/on, then Stabilizer Interior profile applies   | Baseline with transient black/worldspace return | Stabilizer applies Interior profile before RaceSex opens, changing render scale on/requested to native/off; native/off stabilizes before RaceSex opens, and user reports worldspace comes back after a short while.                                                                                                                 |
| 2026-07-04 | Current uncommitted early-intent / late-apply test                             | Stabilizer INI present and sync active                                                                 | New game startup with render scale requested/on                                             | Failing                                         | Stabilizer still applies before RaceSex opens because `loadKind` remains `unknown`; the new late-apply block is scoped to `NewGame` and therefore never activates in this run.                                                                                                                                                      |
| 2026-07-04 | Current uncommitted startup Stabilizer hold test                               | Stabilizer INI present and sync active                                                                 | New game startup with render scale requested/on                                             | Failing, later black                            | Startup hold arms at frame 1 and prevents first-world-frame Stabilizer apply, but a VRAPI/Stabilizer render-scale-off request is still staged into normal pending state during RaceSex; live `perfRequested` flips to `no` while render scale is still latched.                                                                     |
| 2026-07-04 | Previous implementation before latest hold-tightening changes; log 09:56:15    | Stabilizer INI inactive / no unconditional profile found                                               | New game startup with render scale active in CS settings                                    | Good baseline                                   | Log confirms startup works when the Stabilizer INI/profile path is inactive. This isolates the regression trigger to Stabilizer acting on the INI, not render scale being enabled in settings.                                                                                                                                      |
| 2026-07-04 | Current uncommitted startup Stabilizer hold test; log 10:03:11                 | Stabilizer INI active                                                                                  | New game startup with render scale requested/on and multi-stage loading before RaceSex      | Failing                                         | Hold releases without RaceSex at frame 7030 using the first load close frame, but RaceSex opens only after a later LoadingMenu close at frame 8216. Stabilizer applies Interior profile at frame 8225 before RaceSex opens at frame 8290.                                                                                           |
| 2026-07-04 | Current uncommitted startup hold/quarantine test; log 10:17:28                 | Stabilizer INI active                                                                                  | New game startup with render scale requested/on; user reports black before character naming | Failing                                         | Frame 5137 only quarantines the Stabilizer/VRAPI off/native profile. No pending relatch, pending transition, or runtime-plan change occurs there; the visual issue must come from the already-active RaceSex/presentation path, not that request applying.                                                                          |
| 2026-07-04 | Current uncommitted native-quality hold test; log 10:36:32                     | Stabilizer INI active                                                                                  | New game startup with render scale requested/on                                             | Failing faster                                  | Forcing the hold to native quality `0` makes worldspace disappear faster. This is the wrong interpretation of "render scale off"; the path should keep selected quality and suppress only render-scale submit/relatch behavior.                                                                                                     |
| 2026-07-04 | Current uncommitted selected-quality visual hold implementation                | Stabilizer INI active                                                                                  | Implementation target after 10:36 log                                                       | Pending test                                    | Startup Stabilizer hold now suppresses render-scale requested/perf/submit path while preserving selected DLSS/FSR quality and normal vendor dynamic resolution.                                                                                                                                                                     |
| 2026-07-04 | Current uncommitted selected-quality hold test; log 10:46:29                   | Stabilizer direct VRAPI active                                                                         | New game startup with render scale requested/on                                             | Failing after a few seconds                     | Render scale boot-latches and stabilizes before RaceSex, RaceSex opens, then VRAPI queues off/native at frame 5183 while RaceSex is open. The off/native relatch at frame 5189 is the likely black transition.                                                                                                                      |
| 2026-07-04 | Current uncommitted selected-quality hold test; log 10:46:29                   | Stabilizer direct VRAPI active                                                                         | INI/profile classification during RaceSex startup                                           | Wrong temporary profile                         | Startup/RaceSex is classified as interior, so the plugin applies the Interior profile (`quality=0 renderScale=no`) even though the final gameplay area later behaves like exterior/Hoshipa + render scale.                                                                                                                          |
| 2026-07-04 | Current uncommitted A/B gate                                                   | Stabilizer direct VRAPI present, sync toggle off                                                       | New game startup with render scale requested/on                                             | Pending test                                    | External `origin=VRAPI` upscaling requests are ignored while the CS sync toggle is off; CS menu changes and normal non-VRAPI render-scale state remain untouched.                                                                                                                                                                   |
| 2026-07-04 | Current uncommitted A/B gate; log 11:06:20                                     | Stabilizer plugin present, CS sync toggle off                                                          | New game startup with render scale requested/on                                             | Failing                                         | A/B gate works mechanically: the direct VRAPI request is ignored at frame ~4943. Black still happens with render scale already latched and RaceSex entering `menu/full-resolution`, so the remaining culprit is the render-scale RaceSex/menu presentation path, not the ignored VRAPI transition.                                  |
| 2026-07-04 | Current uncommitted RaceSex menu/full-resolution suppression A/B               | Stabilizer plugin/INI path under test                                                                  | New game startup with render scale requested/on                                             | Pending test                                    | During startup RaceSex protection, a latched render-scale contract no longer enters `menu/full-resolution`; a debug line should report suppression if the branch is blocked.                                                                                                                                                        |
| 2026-07-04 | Current uncommitted A/B gate; log 11:18:38                                     | Stabilizer inactive / INI absent                                                                       | New game startup with render scale requested/on                                             | Good baseline                                   | Render scale latches and stabilizes before RaceSex, RaceSex still enters `menu/full-resolution`, and the run is good. This confirms `menu/full-resolution` is shared with the working path.                                                                                                                                         |
| 2026-07-04 | Current uncommitted RaceSex menu/full-resolution suppression A/B; log 11:34:26 | Stabilizer plugin active, sync toggle off                                                              | New game startup with render scale requested/on                                             | Failing worse                                   | VRAPI request is ignored, RaceSex opens, suppression fires, and user sees no worldspace until RaceSex close. The full-resolution menu branch is required for RaceSex/worldspace presentation and should not be blocked this way.                                                                                                    |
| 2026-07-04 | Post sync-toggle revert build; log 11:52:05                                    | Stabilizer plugin active                                                                               | New game startup with render scale requested/on                                             | Failing                                         | Removing the CS-side sync-toggle path does not stop external Stabilizer API activity. RaceSex opens after render scale is stable, then an `origin=VRAPI` request switches to native/off while RaceSex is still open.                                                                                                                |
| 2026-07-04 | Modified Stabilizer test; log 12:59:43                                         | Stabilizer plugin present, CS sync path active                                                         | New game startup with render scale requested/on                                             | Failing immediately black                       | This run is not a direct RaceSex-open VRAPI failure. CS itself applies `VR FPS Stabilizer Sync` from the INI before RaceSex opens, changing quality `1 -> 0` and render scale `yes -> no`; RaceSex opens only after native/off is already stable.                                                                                   |
| 2026-07-04 | Modified Stabilizer test with CS sync toggle off; log 13:06:30                 | Stabilizer plugin active, CS sync disabled                                                             | New game startup with render scale requested/on                                             | Failing                                         | CS-side sync is absent, but direct `origin=VRAPI` still switches render scale off/native while RaceSex is open. The plugin-side RaceSex guard is either not active for this API path or not covering the startup classification event.                                                                                              |
| 2026-07-04 | RC40 with revised Stabilizer API user; log 14:35:22                            | Stabilizer plugin active and appears API-guard aware                                                   | New game startup with render scale requested/on, then DLSS/FSR in-game transitions          | Working baseline                                | RaceSex open/name/close are detected, no CS-side or direct API render-scale apply happens during RaceSex, and the first render-scale relatch happens much later after startup. Later transitions all stabilize; rough early transitions correlate with one-frame vendor-resource relatch deferrals and rapid-transition protection. |

## Worked Well Ledger

| Date       | RC / Build                                                                     | Evidence                                                                                                                                                                                                                              | Notes                                                                                                                                                                                   |
| ---------- | ------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 2026-07-04 | Not specified; current logging build                                           | `source=menu-event-open` at frame 4895, `source=change-name-hook` at frame 6251, `source=menu-event-close` at frame 6253                                                                                                              | RaceSex/RaceMenu signals are available in the good no-Stabilizer path.                                                                                                                  |
| 2026-07-04 | Not specified; current logging build                                           | Boot latch generation 1 at frame 4849, generation 2 at frame 4861, stable after 18 frames                                                                                                                                             | Render-scale startup works when Stabilizer does not apply a profile during RaceMenu.                                                                                                    |
| 2026-07-04 | Not specified; current logging build                                           | No `VR FPS Stabilizer` or `origin=VRAPI` transition found in the selected log lines                                                                                                                                                   | Confirms the good baseline is not exercising the problematic Stabilizer startup path.                                                                                                   |
| 2026-07-04 | Not specified; current uncommitted logging build after release-frame fix       | Name hook at frame 5927, `releaseFrame=5997`, gate released at frame 5997                                                                                                                                                             | The fixed release-frame accounting works; the gate no longer stays stuck in `post-name-tail`.                                                                                           |
| 2026-07-04 | RC36                                                                           | `gate=awaiting-racesex` from frame 1, RaceSex open at frame 4849, name at 5584, close at 5586, gate release at 5654, first boot latch only after release                                                                              | The early startup watch does the intended RaceMenu job in the no-Stabilizer path: RaceMenu remains visible and render scale stays visually inactive until after character naming/close. |
| 2026-07-04 | RC36                                                                           | With Stabilizer active: RaceSex open at frame 4734, name at 6108, close at 6110, gate release at 6178                                                                                                                                 | Startup RaceSex detection and fixed post-name release timing still hold even when Stabilizer is active; the gate is not the part failing in this log.                                   |
| 2026-07-04 | RC37                                                                           | Passive diagnostics show RaceSex open at frame 4900 after render scale had already stabilized at frame 4900                                                                                                                           | The passive baseline no longer delays render-scale startup, which avoids the RC36 gate behavior but exposes a native-plan sizing bug during RaceSex.                                    |
| 2026-07-04 | RC37 plus HMD-size RaceSex bypass experiment                                   | Runtime plan during RaceSex became true native `4936x2740`, proving the override was active                                                                                                                                           | The all-black result proves that size override alone is not safe after render targets have already been relatched to reduced render-scale size.                                         |
| 2026-07-04 | RC37 plus latched-contract RaceSex bypass fix                                  | User-reported test result: no CTD, world menu/RaceMenu visible with no Stabilizer INI                                                                                                                                                 | Clean no-Stabilizer-INI startup baseline restored while keeping render scale allowed to latch before RaceSex opens.                                                                     |
| 2026-07-04 | RC37 plus latched-contract RaceSex bypass fix                                  | Stabilizer applies Interior profile at frame 4902: `renderScale yes -> no`; native/off stable after 12 frames at frame 4920; RaceSex opens later at frame 4950                                                                        | Stabilizer startup baseline is a fast render-scale-off transition before RaceSex, not a render-scale-on RaceSex presentation path.                                                      |
| 2026-07-04 | Current uncommitted early-intent / late-apply test                             | RaceSex open at frame 5000, name hook at frame 5917, close at frame 5919                                                                                                                                                              | RaceSex/menu/name signals are still detected correctly; the failure is earlier, before RaceSex opens.                                                                                   |
| 2026-07-04 | Current uncommitted startup Stabilizer hold test                               | Hold arms at frame 1, RaceSex opens at frame 4813, name at frame 6243, release at frame 6305                                                                                                                                          | The startup hold release timing works and is independent of `NewGame`; it successfully keeps the Stabilizer apply from happening at first world frame.                                  |
| 2026-07-04 | Previous implementation before latest hold-tightening changes; log 09:56:15    | `VR FPS Stabilizer Sync enabled, but no unconditional Interior/Exterior upscaling profile was found`; then `Boot-latched kDLSS quality 1` and `Stable after 18 frame(s): state=on` before RaceSex opens                               | Confirms the clean path is ordinary render-scale startup without Stabilizer profile application. Future fixes must preserve this path.                                                  |
| 2026-07-04 | Current uncommitted A/B gate; log 11:18:38                                     | Stable ON at frame 5244, RaceSex opens at frame 5291, `menu/full-resolution` logs at frame 5292, no `VRFpsStabilizerPlugin` interface or `origin=VRAPI` request                                                                       | Confirms the no-Stabilizer path can tolerate the same RaceSex/full-resolution menu branch.                                                                                              |
| 2026-07-04 | Current uncommitted RaceSex menu/full-resolution suppression A/B; log 11:34:26 | Stable ON at frame 5215, VRAPI request ignored at frame ~5215, RaceSex opens at frame 5263, `Suppressing startup RaceSex menu/full-resolution` logs at frame 5264, `menu/full-resolution` only returns after close/tail at frame 6368 | Blocking the branch removes the visible RaceSex/worldspace presentation. This A/B should be reverted or abandoned.                                                                      |
| 2026-07-04 | RC40 with revised Stabilizer API user; log 14:35:22                            | RaceSex opens frame 4866, name hook frame 5669, RaceSex closes frame 5671, and the first render-scale-on relatch is only at frame 10846                                                                                               | Confirms the external Stabilizer no longer applies a profile during RaceSex startup. Startup RaceMenu/worldspace works with the plugin present.                                         |
| 2026-07-04 | RC40 with revised Stabilizer API user; log 14:35:22                            | DLSS on/off Stabilizer transitions stabilize in 6 frames for OFF/native and 18 frames for ON/submit-stage resume; FSR CS-menu transitions also stabilize in 18 frames                                                                 | Normal in-game transition state machine is healthy after startup.                                                                                                                       |

## Does Not Work / Risk Ledger

| Date       | RC / Build                                                               | Evidence                                                                                                                                                                                                                                                                                                                                                             | Current conclusion / next check                                                                                                                                                                                                                                                                  |
| ---------- | ------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| 2026-07-04 | Not specified; current logging build                                     | Trace remains in `gate=post-name-tail` after name/close in later polling                                                                                                                                                                                                                                                                                             | Fixed in current uncommitted code by using a fixed release frame instead of a moving `lastCompletedWorldRenderFrame` delta.                                                                                                                                                                      |
| 2026-07-04 | Not specified; current logging build                                     | `configure-poll` trace can emit every frame while debug logging is enabled                                                                                                                                                                                                                                                                                           | Debug dedupe likely includes moving frame-derived state such as `lastWorldFrame`; keep this out of info mode and reduce debug churn before broader testing.                                                                                                                                      |
| 2026-07-04 | Not specified; current logging build                                     | User reports bad path only when Stabilizer INI/sync is present                                                                                                                                                                                                                                                                                                       | Current baseline does not yet explain the Stabilizer-induced worldspace loss; next failing log should compare first Stabilizer profile application against RaceSex open/name/close timing.                                                                                                       |
| 2026-07-04 | Not specified; current uncommitted logging build after release-frame fix | Gate is `inactive eligible=no` at frame 4674, boot latch happens at frame 4682, RaceSex opens later at frame 4766                                                                                                                                                                                                                                                    | Startup RaceSex protection arms too late. We need a pre-RaceSex startup watch window that blocks render-scale activation before RaceSex is detected.                                                                                                                                             |
| 2026-07-04 | Not specified; current uncommitted logging build after release-frame fix | At frame 5022 RaceSex is still open, `perfRequested=no`, `pendingTransition=yes`; final VRAPI relatch off is queued at frame 6002 after gate release                                                                                                                                                                                                                 | Stabilizer can queue/mutate the desired profile during RaceSex. The live active contract should stay frozen until RaceSex protection releases.                                                                                                                                                   |
| 2026-07-04 | RC36                                                                     | First post-release boot latch at frame 5654 is immediately followed by `Submit-stage kDLSS bypassed render-scale presentation ... eye=0` and a recovery relatch                                                                                                                                                                                                      | RaceMenu visibility is fixed in the no-Stabilizer path, but the old single-eye startup submit-bounds/stale-eye issue is back. The next failing Stabilizer log should be compared against this no-Stabilizer pattern to see whether both failures share the same first-post-release eye mismatch. |
| 2026-07-04 | RC36                                                                     | Stabilizer-on run has `renderScaleIntent=no`, `perfRequested=no`, `pendingTransition=yes` while RaceSex is still open; after release it queues `origin=VRAPI` to `renderScaleMode=no`, `quality=0`, then reports `Stable after 12 frame(s): state=off`                                                                                                               | This black-worldspace case is not a logged `eye=0` / submit-bounds failure. The startup path is ending in native/off without any render-scale boot latch, so the remaining visual takeover is likely in the separate VR transition/full-resolution presentation path.                            |
| 2026-07-04 | RC37                                                                     | After RaceSex opens, runtime plan changes to `owner=Native method=kDLSS quality=1 display=4194x2329 render=4194x2329 final=4194x2329`; crash follows with `Face [Ovl2]` in the crash log                                                                                                                                                                             | The RaceSex visual bypass is not truly native after render scale is already latched. It must use the stored true HMD display size, not the current reduced `state->screenSize`.                                                                                                                  |
| 2026-07-04 | RC37 plus HMD-size RaceSex bypass experiment                             | `Runtime plan: owner=Native ... display=4936x2740 render=4936x2740 final=4936x2740`, followed by `Skipping VR per-eye finalize because destination 4194x2329 is smaller than runtime output 4936x2740`                                                                                                                                                               | Corrected conclusion: after render scale is latched, RaceSex must not enter native bypass at all. It should stay on the active render-scale contract and normal render-scale menu path.                                                                                                          |
| 2026-07-04 | RC37 plus latched-contract RaceSex bypass fix                            | User reports worldspace comes back after a short while with Stabilizer INI present                                                                                                                                                                                                                                                                                   | There is still a transient startup visual loss/recovery in the Stabilizer path even though it no longer CTDs; this should be treated as the current Stabilizer-INI baseline, not as clean final behavior.                                                                                        |
| 2026-07-04 | Current uncommitted early-intent / late-apply test                       | `loadKind=unknown` from frame 1 through RaceSex close; Stabilizer applies at frame 4963; RaceSex opens at frame 5000                                                                                                                                                                                                                                                 | The new late-apply policy is correct in shape but not reached. It cannot depend on SKSE `NewGame` classification for this path; it needs a fallback startup-load classifier using first-load-close + render-scale intent + RaceSex/name signals.                                                 |
| 2026-07-04 | Current uncommitted startup Stabilizer hold test                         | At frame 5100 RaceSex is open, render scale is latched, but `perfRequested=no` and `pendingTransition=yes`; log then says `Pending VRAPI render-scale profile waiting for startup RaceSex protection to clear`                                                                                                                                                       | The remaining failure is not early Stabilizer application. It is pending-state leakage: the VRAPI/Stabilizer request is blocked from applying, but its pending off/native state already affects live runtime decisions while RaceSex is still open.                                              |
| 2026-07-04 | Current uncommitted startup Stabilizer hold test; log 10:03:11           | Hold releases at frame 7030 with `firstLoadCloseFrame=6850`, then a later load closes at frame 8216 and Stabilizer applies Interior profile at frame 8225; RaceSex opens at frame 8290                                                                                                                                                                               | The hold used only the first startup load close. It must also use the latest observed LoadingMenu close while waiting for RaceSex/name, otherwise multi-stage new-game loading can still apply Stabilizer just before RaceSex.                                                                   |
| 2026-07-04 | Current uncommitted startup hold/quarantine test; log 10:17:28           | Runtime plan changes to `VendorDynamicResolution method=kDLSS quality=1 render=4195x2329` at frame 4766, RaceSex opens at frame 4885, and the frame 5137 request is only `Quarantined VRAPI ... quality=0 renderScale=no`; subsequent traces through frame 5839 still show `renderScaleIntent=yes`, `perfRequested=yes`, `pendingTransition=no`, `pendingRelatch=no` | The frame 5137 request did not change resolution. If the screen goes black before name entry, the culprit is a presentation/runtime path already active during RaceSex open, not an applied Stabilizer/off-native transition.                                                                    |
| 2026-07-04 | Current uncommitted native-quality hold test; log 10:36:32               | Hold arms at frame 1, runtime becomes `Native method=kDLSS quality=0` immediately, RaceSex opens at frame 6579, quarantine happens at frame 6588, user reports black much faster                                                                                                                                                                                     | Forcing native quality is harmful. The selected-quality dynamic path lasted longer; the missing piece is suppressing raw render-scale request/submit state, not changing upscaler quality.                                                                                                       |
| 2026-07-04 | Current uncommitted selected-quality hold test; log 10:46:29             | Boot latch generation 1 at frame 4886, generation 2 at frame 4898, stable at frame 4899, RaceSex opens at frame 4954, VRAPI request changes `perfRequested=no pendingTransition=yes` at frame 5178, relatch off/native applies at frame 5189 while RaceSex is still open                                                                                             | This is a direct VRAPI-during-RaceSex failure. It must be buffered/quarantined even when the early Stabilizer hold was not armed.                                                                                                                                                                |
| 2026-07-04 | Current uncommitted A/B gate; log 11:06:20                               | RaceSex opens at frame 4913 after render scale is already stable ON. At frame 4914, presentation switches to `context=menu/full-resolution method=kDLSS display=4936x2740 render=4194x2329`. At frame ~4943, the later `CS API VR method-specific upscaling transition profile` is ignored because sync is disabled.                                                 | The strong VRAPI disable test removes the direct API transition but does not prevent black. The failure now points to RaceSex/menu presentation while render scale is already latched, especially the full-resolution/menu path using reduced render-scale dimensions.                           |
| 2026-07-04 | Post sync-toggle revert build; log 11:52:05                              | Stable ON at frame 4717/4718, RaceSex opens at frame 4769, frame 5023 queues `origin=VRAPI renderScaleMode=no quality=0`, and frame 5029 applies native/off while RaceSex is still open                                                                                                                                                                              | Confirms the remaining failure is not the removed CS sync toggle. The external Stabilizer plugin still calls the VRAPI path during RaceSex/startup and changes live resolution state.                                                                                                            |
| 2026-07-04 | Modified Stabilizer test; log 12:59:43                                   | Frame 5726 logs `VR FPS Stabilizer Sync applying Interior profile ... quality 1 -> 0 ... renderScale yes -> no`; frame 5732 queues `origin=PostLoadSync`; frame 5744 is stable native/off; RaceSex opens later at frame 5778                                                                                                                                         | The modified plugin-side RaceSex guard cannot prevent this path because the live change is made by CS's sync reader before RaceSex opens. To test plugin-side blocking, CS-side sync must be disabled/removed.                                                                                   |
| 2026-07-04 | Modified Stabilizer test with CS sync toggle off; log 13:06:30           | No `VR FPS Stabilizer Sync` lines. Render scale stable ON at frame 5326/5327, RaceSex opens at frame 5382, frame 5742 queues `origin=VRAPI renderScaleMode=no quality=0`, and frame 5748 applies native/off while RaceSex remains open                                                                                                                               | This is the clean proof that the plugin still calls CS VRAPI during RaceSex despite CS sync being off. The guard must cover direct API calls and should buffer until RaceSex close plus tail.                                                                                                    |
| 2026-07-04 | RC40 with revised Stabilizer API user; log 14:35:22                      | Several later relatches log `Render-target relatch deferred because vendor resources are still in use`; the roughest later transition is FSR/Hoshipa to DLSS/native OFF at frame 46058, stable after 24 frames                                                                                                                                                       | The remaining non-smoothness is transition-cost/resource-teardown timing, not RaceSex/startup black-worldspace failure. The FSR->DLSS + render-scale-on->off transition is the main candidate for further smoothing.                                                                             |

## Per-Log Observation Ledger

### 2026-07-04 07:28 Good Baseline

| Field               | Value                                                                          |
| ------------------- | ------------------------------------------------------------------------------ |
| RC / build          | Not specified; current logging build                                           |
| Log file            | `C:\Users\Win10\Documents\My Games\Skyrim VR\SKSE\CommunityShaders.log`        |
| Log last write      | 2026-07-04 07:28:54                                                            |
| Stabilizer active   | No evidence of active Stabilizer INI/profile application in selected lines     |
| User result         | Render scale on, no Stabilizer INI: startup worked, no CTD, no worldspace loss |
| Upscaler            | DLSS                                                                           |
| Render-scale intent | Yes                                                                            |
| Render-scale result | Latched and stable                                                             |

Timings observed:

| Time         | Frame | Event                                                                                              |
| ------------ | ----: | -------------------------------------------------------------------------------------------------- |
| 07:28:23.750 |  4841 | Loading has closed recently; trace reports `loadingCloseFrame=4840`.                               |
| 07:28:24.252 |  4849 | Boot-latched DLSS quality 1, display 2468x2740 per eye, render 2097x2329 per eye, generation 1.    |
| 07:28:24.288 |  4849 | Recovery relatch queued for persistent VR render-scale scene submit-bounds mismatch.               |
| 07:28:24.417 |  4855 | Recovery relatch applied from `ConfigureUpscaling`.                                                |
| 07:28:24.567 |  4861 | Recovery relatch applied again; boot-latched generation 2.                                         |
| 07:28:24.721 |  4867 | Render scale reports stable after 18 frames, submit-stage vendor resume.                           |
| 07:28:25.239 |  4895 | RaceSex menu open event detected: `directOpen=yes`, `eventOpen=yes`, gate becomes `awaiting-name`. |
| 07:28:36.693 |  6251 | Character-name hook detected: `hasName=yes`, gate becomes `post-name-tail`.                        |
| 07:28:36.732 |  6253 | RaceSex menu close event detected: `directOpen=no`, `eventOpen=no`.                                |

Conclusions:

-   This is the clean baseline: render scale can start successfully when the Stabilizer path is not applying a profile during RaceMenu.
-   The key RaceSex signals are detectable in this path: menu open, character-name confirmation, and menu close.
-   The Stabilizer-induced failure is not caused by an inability to detect RaceSex in general; it is likely a timing/priority issue when Stabilizer applies a render-scale profile while RaceSex protection should still own startup.
-   The current gate-release logic is suspicious because it can remain in `post-name-tail` after name and close. That should be reviewed before the next Stabilizer-focused test.
-   Debug trace output is too chatty because the polling dedupe appears to include moving per-frame data.

### 2026-07-04 07:44 Stabilizer-On Failure

| Field               | Value                                                                                                                     |
| ------------------- | ------------------------------------------------------------------------------------------------------------------------- |
| RC / build          | Not specified; current uncommitted logging build after release-frame fix                                                  |
| Log file            | `C:\Users\Win10\Documents\My Games\Skyrim VR\SKSE\CommunityShaders.log`                                                   |
| Log last write      | 2026-07-04 07:44:03                                                                                                       |
| Stabilizer active   | Yes. `VRFpsStabilizerPlugin` interface is provided, and a later transition is `origin=VRAPI`.                             |
| User result         | With Stabilizer on, worldspace is visible briefly and then disappears/goes black during RaceMenu.                         |
| Upscaler            | DLSS                                                                                                                      |
| Render-scale intent | Yes at startup                                                                                                            |
| Render-scale result | Render scale latches before RaceSex opens; later Stabilizer requests render-scale off/native while RaceSex is still open. |

Timings observed:

| Time         |     Frame | Event                                                                                                                             |
| ------------ | --------: | --------------------------------------------------------------------------------------------------------------------------------- |
| 07:42:43.550 |       n/a | Community Shaders provides plugin interface to `VRFpsStabilizerPlugin`.                                                           |
| 07:43:42.371 |      4674 | Loading has closed recently; trace still shows `gate=inactive eligible=no`, `renderScaleIntent=yes`.                              |
| 07:43:42.862 |      4682 | Boot-latched DLSS quality 1, render scale ON, generation 1.                                                                       |
| 07:43:42.881 |      4682 | Recovery relatch queued for submit-bounds mismatch.                                                                               |
| 07:43:42.922 |      4688 | Recovery relatch applied.                                                                                                         |
| 07:43:42.972 |      4694 | Recovery relatch applied again; boot-latched generation 2.                                                                        |
| 07:43:43.112 | 4695-4697 | Render scale reports stable after 18 frames.                                                                                      |
| 07:43:43.938 |      4766 | RaceSex opens; startup gate finally arms from RaceSex menu open. This is too late because render scale is already latched.        |
| 07:43:46.216 |      5022 | RaceSex still open; `perfRequested=no`, `pendingTransition=yes`. Pending VR transition waits for protected presentation to clear. |
| 07:43:53.756 |      5927 | Character-name hook fires; release target becomes frame 5997.                                                                     |
| 07:43:53.795 |      5929 | RaceSex menu close event fires.                                                                                                   |
| 07:43:54.452 |      5997 | Startup RaceSex gate releases exactly at `releaseFrame=5997`.                                                                     |
| 07:43:54.585 |      6002 | Deferred VRAPI transition queues relatch to render scale OFF/native DLSS quality 0.                                               |
| 07:43:54.647 |      6008 | VRAPI relatch applies render scale OFF/native.                                                                                    |
| 07:43:54.667 |      6008 | Render scale reports stable OFF after 6 frames.                                                                                   |

Conclusions:

-   The release-frame fix worked. The gate no longer gets stuck after the name hook and RaceSex close.
-   The startup gate is still not early enough. It only arms when RaceSex opens at frame 4766, but render scale already boot-latched at frame 4682.
-   No `Handled kNewGame` or startup gate initialization log is present in this log. Relying on the SKSE new-game message alone is not sufficient for this path.
-   The visual failure is consistent with render scale and/or Stabilizer state becoming active during the RaceSex overlay window.
-   The next fix target is to add a pre-RaceSex startup watch window: when render scale is requested during first load/startup, keep visual behavior native/render-scale-off until either RaceSex opens and later closes/name-confirms, or the watch window expires without RaceSex.
-   The second fix target is to freeze the live active contract during this protected window. Stabilizer may record the desired profile, but it must not change live `perfRequested`/render-scale state or queue relatch effects while RaceSex protection is active.

### 2026-07-04 08:05 RC36 No-Stabilizer Regression

| Field               | Value                                                                                                                                                                                     |
| ------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| RC / build          | RC36                                                                                                                                                                                      |
| Log file            | `C:\Users\Win10\Documents\My Games\Skyrim VR\SKSE\CommunityShaders.log`                                                                                                                   |
| Log last write      | 2026-07-04 08:05:19                                                                                                                                                                       |
| Stabilizer active   | No Stabilizer INI / no VRAPI transition observed in selected lines                                                                                                                        |
| User result         | RaceMenu visible and usable, but startup regressed to one stale eye after RaceMenu; user identifies this as the old issue previously fixed in `3e18869d4a4f194cf0acd0b717abfde9b2b6dd95`. |
| Upscaler            | DLSS                                                                                                                                                                                      |
| Render-scale intent | Yes at startup                                                                                                                                                                            |
| Render-scale result | Render scale remains inactive through RaceMenu and only latches after gate release, then immediately hits a single-eye submit-bounds failure and recovery relatch.                        |

Timings observed:

| Time         |     Frame | Event                                                                                                                                                       |
| ------------ | --------: | ----------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 08:04:20.257 |       n/a | Startup RaceSex render-scale watch arms before the first frame.                                                                                             |
| 08:04:21.196 |         1 | Trace already shows `gate=awaiting-racesex`, `latched=no`, `perfRequested=yes`.                                                                             |
| 08:05:01.716 |      4753 | First-load close is latched; release target shows `releaseFrame=4932` while still awaiting RaceSex.                                                         |
| 08:05:02.528 |      4761 | World rendering has started, but render scale is still not latched.                                                                                         |
| 08:05:03.539 |      4849 | RaceSex opens; gate promotes to `awaiting-name`.                                                                                                            |
| 08:05:10.676 |      5584 | Character-name hook fires; release target becomes frame 5654.                                                                                               |
| 08:05:10.723 |      5586 | RaceSex menu close event fires.                                                                                                                             |
| 08:05:11.499 |      5654 | Startup RaceSex gate releases and the first render-scale boot latch happens immediately after.                                                              |
| 08:05:11.509 |      5654 | First post-release submit-stage failure appears: `Submit-stage kDLSS bypassed render-scale presentation ... eye=0 ... actual=2468x2740 expected=2097x2329`. |
| 08:05:11.626 |      5660 | Recovery relatch applies.                                                                                                                                   |
| 08:05:11.766 |      5666 | Second boot latch (generation 2) completes after recovery.                                                                                                  |
| 08:05:11.879 | 5666-5672 | Render scale stabilizes after 18 frames.                                                                                                                    |

Conclusions:

-   The early startup watch is doing its intended job for RaceMenu visibility: render scale does not boot-latch during RaceMenu, and RaceMenu remains visible.
-   The no-Stabilizer normal path is not clean, though. The first render-scale activation after RaceMenu close regresses to the old single-eye startup failure pattern.
-   The concrete log marker for that regression is the first post-release `eye=0` submit-bounds bypass at frame 5654, immediately after generation-1 boot latch and before recovery generation 2.
-   `loadKind` is still `unknown` in this run, so the fallback startup watch path is the one actually protecting RaceMenu here.

### 2026-07-04 08:09 RC36 Stabilizer-On Native-Off Takeover

| Field               | Value                                                                                                                                                              |
| ------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| RC / build          | RC36                                                                                                                                                               |
| Log file            | `C:\Users\Win10\Documents\My Games\Skyrim VR\SKSE\CommunityShaders.log`                                                                                            |
| Log last write      | 2026-07-04 08:09:34                                                                                                                                                |
| Stabilizer active   | Yes. `VRFpsStabilizerPlugin` interface is provided at startup, and the deferred relatch is `origin=VRAPI`.                                                         |
| User result         | User reports the stale-eye symptom already from the beginning, matching the normal RC36 path, and then worldspace goes black when Stabilizer is on.                |
| Upscaler            | DLSS                                                                                                                                                               |
| Render-scale intent | Yes at startup, then mutated to OFF/native while RaceSex is still open                                                                                             |
| Render-scale result | No render-scale boot latch occurs in this log. After RaceSex release, the runtime performs a clean deferred VRAPI relatch to native/off DLSS and stabilizes there. |

Timings observed:

| Time         | Frame | Event                                                                                                                                 |
| ------------ | ----: | ------------------------------------------------------------------------------------------------------------------------------------- |
| 08:08:17.003 |   n/a | Community Shaders provides plugin interface to `VRFpsStabilizerPlugin`.                                                               |
| 08:08:34.622 |   n/a | Startup RaceSex render-scale watch arms from the startup render-scale request.                                                        |
| 08:08:35.581 |     1 | Trace already shows `gate=awaiting-racesex`, `renderScaleIntent=yes`, `latched=no`, `perfRequested=yes`.                              |
| 08:09:15.946 |  4654 | World rendering has completed, but render scale is still inactive due to the startup watch.                                           |
| 08:09:16.971 |  4734 | RaceSex opens and the gate promotes to `awaiting-name`.                                                                               |
| 08:09:19.237 |  4956 | While RaceSex is still open, Stabilizer flips the desired state: `renderScaleIntent=no`, `perfRequested=no`, `pendingTransition=yes`. |
| 08:09:30.205 |  6108 | Character-name hook fires; release target becomes frame 6178.                                                                         |
| 08:09:30.250 |  6110 | RaceSex close event fires.                                                                                                            |
| 08:09:31.070 |  6178 | Startup RaceSex gate releases.                                                                                                        |
| 08:09:31.129 |  6183 | Deferred VRAPI relatch is queued for native/off DLSS: `renderScaleMode=no`, `perfMode=no`, `quality=0`.                               |
| 08:09:31.361 |  6189 | First VRAPI relatch apply starts in native/off mode.                                                                                  |
| 08:09:31.440 |  6195 | Second VRAPI relatch apply runs with pending DLSS reset.                                                                              |
| 08:09:31.501 |  6195 | Native/off state reports stable after 12 frames.                                                                                      |

Conclusions:

-   RaceSex open, character-name confirmation, close, and fixed release timing all work in this RC36 Stabilizer-on run.
-   This log does not show the no-Stabilizer RC36 stale-eye signature. `Boot-latched` count is `0`, and `Submit-stage` / `eye=0` count is also `0`.
-   User clarification: the stale-eye symptom was visually present from the beginning in this run as well, but it is not represented by the current `eye=0` / submit-bounds logging.
-   The desired profile is changed to native/off during RaceSex, held pending until the gate releases, and then applied cleanly as a VRAPI relatch to DLSS native/off.
-   Because the log ends in a stable native/off state without any render-scale boot latch, the black-worldspace takeover is very likely not coming from the same first-post-release render-scale `eye=0` mismatch as the no-Stabilizer regression.
-   The remaining suspect is the separate VR transition/full-resolution startup presentation behavior that can still take over visually even when the active upscaling contract stays native/off.

### 2026-07-04 08:35 RC37 No-Stabilizer CTD

| Field               | Value                                                                                                                                               |
| ------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------- |
| RC / build          | RC37                                                                                                                                                |
| Log file            | `C:\Users\Win10\Documents\My Games\Skyrim VR\SKSE\CommunityShaders.log`                                                                             |
| Crash log           | `C:\Users\Win10\Documents\My Games\Skyrim VR\SKSE\crash-2026-07-04-07-35-15.log`                                                                    |
| Log last write      | 2026-07-04 08:35:14                                                                                                                                 |
| Stabilizer active   | No active profile application. Sync toggle is enabled and an INI is present, but the log says no unconditional Interior/Exterior profile was found. |
| User result         | CTD during startup/RaceMenu baseline test without Stabilizer profile use.                                                                           |
| Upscaler            | DLSS                                                                                                                                                |
| Render-scale intent | Yes at startup                                                                                                                                      |
| Render-scale result | Render scale latches and reports stable before RaceSex opens.                                                                                       |

Timings observed:

| Time         | Frame | Event                                                                                                            |
| ------------ | ----: | ---------------------------------------------------------------------------------------------------------------- |
| 08:34:31.315 |     1 | Startup trace shows `renderScaleIntent=yes`, `latched=no`, and passive `gate=inactive`.                          |
| 08:35:12.770 |  4868 | Runtime plan is normal vendor dynamic resolution: display `4936x2740`, render `4195x2329`, final `4936x2740`.    |
| 08:35:13.283 |  4877 | Post-load render-scale activation queues as `origin=PostLoadSync`.                                               |
| 08:35:13.576 |  4883 | First relatch attempt defers DLSS resource free because the D3D11 queue is not idle.                             |
| 08:35:13.849 |  4889 | Render scale boot-latches: display `4936x2740`, render `4194x2329`, generation 1.                                |
| 08:35:14.018 |  4900 | Render scale reports stable after 18 frames.                                                                     |
| 08:35:14.328 |  4900 | RaceSex opens after render scale is already latched and stable.                                                  |
| 08:35:14.406 |  4901 | RaceSex protection is active, but runtime plan becomes `owner=Native` with display/render/final all `4194x2329`. |
| 08:35:14.415 |  4901 | VR intermediates recreate as per-eye in/out `2097x2329`. Crash follows immediately.                              |

Conclusions:

-   This is not the same as the RC36 stale-eye regression and not an OOM/SSS failure. GPU memory is not exhausted, and the crash stack points into Skyrim around `Face [Ovl2]`.
-   The crash occurs after RaceSex opens over an already-latched render-scale state.
-   The immediate bad state is that the RaceSex/native visual bypass uses the reduced render-scale `state->screenSize` as native display/final size.
-   Targeted fix: keep RC37 passive startup behavior, but make RaceSex visual bypass derive native display/final sizing from stored true HMD size when available.

### 2026-07-04 08:56 RC37 Stabilizer-INI Baseline

| Field               | Value                                                                                                           |
| ------------------- | --------------------------------------------------------------------------------------------------------------- |
| RC / build          | RC37 plus latched-contract RaceSex bypass fix                                                                   |
| Log file            | `C:\Users\Win10\Documents\My Games\Skyrim VR\SKSE\CommunityShaders.log`                                         |
| Log last write      | 2026-07-04 08:56:22                                                                                             |
| Stabilizer active   | Yes. `VRFpsStabilizerPlugin` interface is provided and the Stabilizer INI applies an Interior profile.          |
| User result         | Stabilizer INI present: worldspace comes back after a short while. This is the current Stabilizer-INI baseline. |
| Upscaler            | DLSS                                                                                                            |
| Render-scale intent | Yes at startup, then Stabilizer changes it to OFF/native before RaceSex opens                                   |
| Render-scale result | Render scale does not boot-latch. Native/off DLSS stabilizes before RaceSex opens.                              |

Timings observed:

| Time         | Frame | Event                                                                                                   |
| ------------ | ----: | ------------------------------------------------------------------------------------------------------- |
| 08:55:08.936 |   n/a | Community Shaders provides plugin interface to `VRFpsStabilizerPlugin`.                                 |
| 08:55:28.146 |     1 | Startup trace shows `renderScaleIntent=yes`, `latched=no`, `perfRequested=yes`, `gate=inactive`.        |
| 08:56:09.810 |  4894 | Loading has closed recently; trace shows `loadingCloseFrame=4893`, no completed world frame yet.        |
| 08:56:09.844 |  4894 | Runtime plan is normal vendor dynamic DLSS: display `4936x2740`, render `4195x2329`, final `4936x2740`. |
| 08:56:10.282 |  4902 | First world frame is complete; Stabilizer sync queues after save-load menu close.                       |
| 08:56:10.282 |  4902 | Stabilizer Interior profile applies: method stays DLSS, quality `1 -> 0`, render scale `yes -> no`.     |
| 08:56:10.619 |  4908 | Deferred relatch is queued as `origin=PostLoadSync`, target render scale off/native, quality 0.         |
| 08:56:10.826 |  4914 | First relatch attempt starts but defers because vendor resources are still in use.                      |
| 08:56:10.886 |  4920 | Second relatch attempt runs with pending DLSS reset and target native/off.                              |
| 08:56:10.927 |  4920 | Native/off DLSS reports stable after 12 frames: render/display `4936x2740`, quality 0.                  |
| 08:56:11.391 |  4950 | RaceSex opens after native/off is already stable.                                                       |
| 08:56:19.059 |  5644 | Character-name hook fires.                                                                              |
| 08:56:19.107 |  5646 | RaceSex menu close event fires; tail extends to frame 5706.                                             |
| 08:56:19.971 |  5706 | RaceSex tail ends; trace remains in protected context with render scale still off/native.               |

Conclusions:

-   This baseline is not exercising render-scale-on RaceSex presentation. Stabilizer turns render scale off before RaceSex opens.
-   The visible transient is consistent with the early Stabilizer native/off relatch around frames 4908-4920, before RaceSex opens at frame 4950.
-   There is no logged render-scale boot latch, no logged submit-stage `eye=0` mismatch, and no logged VR per-eye finalize skip in this run.
-   RaceSex open, character-name hook, and close are detected correctly while render scale remains off/native.
-   Future Stabilizer fixes should compare against this baseline and avoid breaking the already-clean no-Stabilizer-INI startup baseline.

### 2026-07-04 09:15 Early-Intent / Late-Apply Failed Test

| Field               | Value                                                                                                  |
| ------------------- | ------------------------------------------------------------------------------------------------------ |
| RC / build          | Current uncommitted early-intent / late-apply test                                                     |
| Log file            | `C:\Users\Win10\Documents\My Games\Skyrim VR\SKSE\CommunityShaders.log`                                |
| Log last write      | 2026-07-04 09:15:09                                                                                    |
| Stabilizer active   | Yes. `VRFpsStabilizerPlugin` interface is provided and the Stabilizer INI applies an Interior profile. |
| User result         | Worldspace still disappears after a short time.                                                        |
| Upscaler            | DLSS                                                                                                   |
| Render-scale intent | Yes at startup, then Stabilizer changes it to OFF/native before RaceSex opens.                         |
| Render-scale result | Render scale does not boot-latch; native/off DLSS stabilizes before RaceSex opens.                     |

Timings observed:

| Time         | Frame | Event                                                                                                                                            |
| ------------ | ----: | ------------------------------------------------------------------------------------------------------------------------------------------------ |
| 09:13:47.739 |   n/a | Community Shaders provides plugin interface to `VRFpsStabilizerPlugin`.                                                                          |
| 09:14:06.815 |     1 | Startup trace shows `renderScaleIntent=yes`, `perfRequested=yes`, `loading=yes`, but `loadKind=unknown`.                                         |
| 09:14:33.248 |  3125 | Menu event handler registers while still loading; `loadKind` remains `unknown`.                                                                  |
| 09:14:48.986 |  4955 | Final loading close has occurred; `firstLoadCloseFrame=4954`, `worldComplete=no`, `loadKind=unknown`.                                            |
| 09:14:49.524 |  4963 | First completed world frame exists; Stabilizer sync queues and immediately applies Interior profile: quality `1 -> 0`, render scale `yes -> no`. |
| 09:14:49.917 |  4969 | Deferred relatch is queued as `origin=PostLoadSync`, target native/off DLSS quality 0.                                                           |
| 09:14:50.109 |  4975 | First relatch attempt defers because vendor resources are still in use.                                                                          |
| 09:14:50.218 |  4981 | Second relatch applies native/off.                                                                                                               |
| 09:14:50.238 |  4981 | Native/off reports stable after 12 frames.                                                                                                       |
| 09:14:50.699 |  5000 | RaceSex opens after the Stabilizer profile has already applied and stabilized native/off.                                                        |
| 09:15:00.752 |  5917 | Character-name hook fires.                                                                                                                       |
| 09:15:00.799 |  5919 | RaceSex menu close event fires; tail extends to frame 5979.                                                                                      |

Conclusions:

-   The early-intent sniff works: frame 1 already reports `renderScaleIntent=yes`.
-   The late-apply block did not run because every RaceSex trace has `loadKind=unknown`, not `NewGame`.
-   The bad ordering is unchanged from the previous Stabilizer-INI baseline: Stabilizer applies at frame 4963, RaceSex opens later at frame 5000.
-   RaceSex detection is not the missing signal. RaceSex open/name/close are detected correctly, but too late to prevent the live Stabilizer startup apply.
-   Next fix target: do not rely on `VRStartupLoadKind::NewGame` for this protection. Use a fallback startup classifier: first load has closed, render-scale intent exists, first world frame has just appeared, and RaceSex/name has not completed yet. That should block live Stabilizer/VRAPI application even when `loadKind=unknown`.

### 2026-07-04 09:56 INI-Inactive Render-Scale Baseline

| Field               | Value                                                                                                                                       |
| ------------------- | ------------------------------------------------------------------------------------------------------------------------------------------- |
| RC / build          | Previous implementation before latest hold-tightening changes                                                                               |
| Log file            | `C:\Users\Win10\Documents\My Games\Skyrim VR\SKSE\CommunityShaders.log`                                                                     |
| Log last write      | 2026-07-04 09:56:15                                                                                                                         |
| Stabilizer active   | Sync path is checked, but INI profile application is inactive. The log says no unconditional Interior/Exterior upscaling profile was found. |
| User result         | INI inactive: all works, even though render scale is active in CS settings.                                                                 |
| Upscaler            | DLSS                                                                                                                                        |
| Render-scale intent | Yes at startup                                                                                                                              |
| Render-scale result | Render scale boot-latches and stabilizes before RaceSex opens.                                                                              |

Timings observed:

| Time         | Frame | Event                                                                                                              |
| ------------ | ----: | ------------------------------------------------------------------------------------------------------------------ |
| 09:55:21.159 |     1 | Startup trace shows `renderScaleIntent=yes`, `perfRequested=yes`, `latched=no`, `loading=yes`, `loadKind=unknown`. |
| 09:56:02.455 |  4850 | Loading has closed; `loadingCloseFrame=4849`, `firstLoadCloseFrame=4849`, no completed world frame yet.            |
| 09:56:02.976 |  4858 | First completed world frame exists; Stabilizer sync is queued after save-load menu close.                          |
| 09:56:02.976 |  4858 | Log reports `VR FPS Stabilizer Sync enabled, but no unconditional Interior/Exterior upscaling profile was found`.  |
| 09:56:02.976 |  4858 | Normal deferred render-scale activation queues as `origin=PostLoadSync`, method DLSS, quality 1, render scale on.  |
| 09:56:03.414 |  4870 | Render scale boot-latches DLSS quality 1, display `2468x2740` per eye to render `2097x2329` per eye.               |
| 09:56:03.570 |  4870 | Render scale reports stable after 18 frames: state on, render `4194x2329`, display `4936x2740`, quality 1.         |
| 09:56:03.959 |  4885 | RaceSex opens after render scale is already stable. No pending transition or relatch is present.                   |
| 09:56:10.241 |  5629 | Character-name hook fires while render scale remains stable and no pending transition is present.                  |
| 09:56:10.281 |  5631 | RaceSex closes; render scale remains stable and no pending transition is present.                                  |

Conclusions:

-   This log confirms the working baseline: render scale active in CS settings is not sufficient to reproduce the RaceMenu/worldspace failure.
-   The Stabilizer sync check runs, but it does not apply a profile because the INI has no unconditional Interior/Exterior upscaling profile.
-   There is no logged `origin=VRAPI`, no quarantined profile, and no startup Stabilizer profile hold in this baseline.
-   The RaceSex open/name/close sequence happens after render scale has already stabilized, and `pendingTransition` stays `no` throughout RaceSex.
-   Future fixes must target Stabilizer profile application timing and pending-state mutation, not generic render-scale startup.

### 2026-07-04 10:03 Latest-Close Startup Hold Failure

| Field               | Value                                                                                       |
| ------------------- | ------------------------------------------------------------------------------------------- |
| RC / build          | Current uncommitted startup Stabilizer hold test after latest hold-tightening changes       |
| Log file            | `C:\Users\Win10\Documents\My Games\Skyrim VR\SKSE\CommunityShaders.log`                     |
| Log last write      | 2026-07-04 10:03:11                                                                         |
| Stabilizer active   | Yes. `VRFpsStabilizerPlugin` interface is provided and the INI applies an Interior profile. |
| User result         | Did not work; RaceMenu/worldspace still fails with Stabilizer INI active.                   |
| Upscaler            | DLSS                                                                                        |
| Render-scale intent | Yes at startup                                                                              |
| Render-scale result | Render scale eventually boot-latches on, then Stabilizer flips it off before RaceSex opens. |

Timings observed:

| Time         | Frame | Event                                                                                                             |
| ------------ | ----: | ----------------------------------------------------------------------------------------------------------------- |
| 10:01:18.388 |   n/a | Community Shaders provides plugin interface to `VRFpsStabilizerPlugin`.                                           |
| 10:01:36.589 |     1 | Startup Stabilizer profile hold arms immediately, target render scale off, requested render scale on.             |
| 10:02:36.745 |  6763 | Direct CS API / VRAPI method-specific profile queues a render-scale-on relatch while LoadingMenu is still active. |
| 10:02:37.389 |  6840 | `notify-post-load` fires with load kind `post-load-game`; hold arms again.                                        |
| 10:02:37.477 |  6850 | Loading closes and Stabilizer sync is queued for this load.                                                       |
| 10:02:40.932 |  7030 | Hold releases without RaceSex using the first close frame `6850`.                                                 |
| 10:02:41.113 |  7041 | VRAPI relatch applies and boot-latches DLSS render scale on.                                                      |
| 10:02:41.618 |  7041 | Render scale reports stable after 284 frames.                                                                     |
| 10:02:52.775 |  8216 | A later LoadingMenu close is observed; this is the close that actually precedes RaceSex.                          |
| 10:02:53.136 |  8225 | First completed world frame after the later load exists.                                                          |
| 10:02:53.137 |  8225 | Stabilizer Interior profile applies: quality `1 -> 0`, render scale `yes -> no`.                                  |
| 10:02:53.294 |  8237 | Native/off DLSS reports stable after 6 frames.                                                                    |
| 10:02:54.109 |  8290 | RaceSex opens after Stabilizer has already changed the live profile to native/off.                                |
| 10:03:08.308 |  9614 | Character-name hook fires.                                                                                        |
| 10:03:08.356 |  9616 | RaceSex menu close event fires.                                                                                   |

Conclusions:

-   The latest hold tightening still missed the real RaceSex handoff because it used only the first load close frame.
-   New-game startup can contain multiple LoadingMenu phases before RaceSex opens. The relevant blocker must use the latest observed LoadingMenu close while the startup hold is still waiting for RaceSex/name.
-   The failure is again Stabilizer profile timing: the Interior profile applies at frame 8225, before RaceSex opens at frame 8290.
-   The patch target is to keep the Stabilizer startup hold event-based: sniff the INI/profile path early, block live Stabilizer/VRAPI profile application, and release only after RaceSex close/name plus the existing close tail.

### 2026-07-04 11:52 Post Sync-Toggle Revert Stabilizer Failure

| Field               | Value                                                                                                                                            |
| ------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------ |
| RC / build          | Post sync-toggle revert build                                                                                                                    |
| Log file            | `C:\Users\Win10\Documents\My Games\Skyrim VR\SKSE\CommunityShaders.log`                                                                          |
| Log last write      | 2026-07-04 11:52:05                                                                                                                              |
| Stabilizer active   | Yes. Community Shaders provides the plugin interface to `VRFpsStabilizerPlugin`.                                                                 |
| User result         | Worldspace appears briefly, then disappears/goes black after a couple of seconds.                                                                |
| Upscaler            | DLSS                                                                                                                                             |
| Render-scale intent | Yes at startup                                                                                                                                   |
| Render-scale result | Render scale boots and stabilizes ON before RaceSex; then an external `origin=VRAPI` request switches to native/off while RaceSex is still open. |

Timings observed:

| Time         |     Frame | Event                                                                                           |
| ------------ | --------: | ----------------------------------------------------------------------------------------------- |
| 11:50:47.624 |       n/a | Community Shaders provides plugin interface to `VRFpsStabilizerPlugin`.                         |
| 11:51:47.134 |      4705 | Boot-latched DLSS quality 1, render scale ON, generation 1.                                     |
| 11:51:47.288 |      4717 | Recovery relatch completes and boot-latches generation 2.                                       |
| 11:51:47.449 | 4717-4718 | Render scale reports stable ON after 18 frames.                                                 |
| 11:51:48.146 |      4769 | RaceSex opens while render scale is stable ON.                                                  |
| 11:51:48.213 |      4770 | RaceSex uses `menu/full-resolution` presentation with render-scale render size `4194x2329`.     |
| 11:51:50.285 |      5018 | While RaceSex is still open, `perfRequested=no` and `pendingTransition=yes` appear.             |
| 11:51:50.327 |      5023 | Queued render-target relatch: `origin=VRAPI`, `renderScaleMode=no`, `perfMode=no`, `quality=0`. |
| 11:51:50.377 |      5029 | VRAPI relatch applies to native/off with target render/display `4936x2740`.                     |
| 11:51:50.398 |      5029 | Render scale reports stable OFF/native after 6 frames.                                          |
| 11:52:00.202 |      5949 | Character-name hook fires much later.                                                           |
| 11:52:00.250 |      5951 | RaceSex menu close event fires.                                                                 |

Conclusions:

-   The black transition happens around frame 5023-5029, about 2.2 seconds after RaceSex opens.
-   The immediate cause is not the removed CS-side sync toggle. The log contains no `VR FPS Stabilizer Sync` line, but it does contain an `origin=VRAPI` relatch.
-   The external Stabilizer plugin still has the CS plugin interface and can still issue direct VRAPI profile changes during RaceSex/startup.
-   This reproduces the earlier direct-VRAPI-during-RaceSex failure: live state changes from render-scale ON to native/off before name confirmation and before RaceSex closes.

### 2026-07-04 12:59 Modified Stabilizer With CS Sync Active

| Field               | Value                                                                                                                |
| ------------------- | -------------------------------------------------------------------------------------------------------------------- |
| RC / build          | Current branch with CS-side VR FPS Stabilizer Sync active                                                            |
| Log file            | `C:\Users\Win10\Documents\My Games\Skyrim VR\SKSE\CommunityShaders.log`                                              |
| Log last write      | 2026-07-04 12:59:43                                                                                                  |
| Stabilizer active   | Yes. Community Shaders provides the plugin interface to `VRFpsStabilizerPlugin`, and CS reads `VRFpsStabilizer.ini`. |
| User result         | Modified Stabilizer build: worldspace immediately black.                                                             |
| Upscaler            | DLSS                                                                                                                 |
| Render-scale intent | Yes at startup, then CS sync changes it to off before RaceSex opens                                                  |
| Render-scale result | No render-scale boot latch in the selected lines; CS sync applies native/off and stabilizes before RaceSex opens.    |

Timings observed:

| Time         | Frame | Event                                                                                               |
| ------------ | ----: | --------------------------------------------------------------------------------------------------- |
| 12:58:17.261 |   n/a | Community Shaders provides plugin interface to `VRFpsStabilizerPlugin`.                             |
| 12:59:24.821 |  5718 | Loading closes; render-scale intent still yes and no RaceSex is open.                               |
| 12:59:24.854 |  5718 | Runtime plan is still DLSS quality 1 dynamic resolution.                                            |
| 12:59:25.321 |  5726 | CS logs `VR FPS Stabilizer Sync queued after save-load menu close`.                                 |
| 12:59:25.321 |  5726 | CS applies Interior profile from `VRFpsStabilizer.ini`: quality `1 -> 0`, render scale `yes -> no`. |
| 12:59:25.667 |  5732 | Relatch queued as `origin=PostLoadSync`, render scale off, quality 0.                               |
| 12:59:25.811 |  5738 | PostLoadSync relatch applies.                                                                       |
| 12:59:25.920 |  5744 | Native/off DLSS reports stable after 12 frames.                                                     |
| 12:59:26.462 |  5778 | RaceSex opens after native/off is already stable.                                                   |
| 12:59:34.673 |  6544 | Character-name hook fires.                                                                          |
| 12:59:34.719 |  6546 | RaceSex closes.                                                                                     |

Conclusions:

-   This run does not prove the modified Stabilizer RaceSex-open guard failed.
-   The visible failure happens because CS's own sync path reads the Stabilizer INI and applies the Interior/off profile before RaceSex opens.
-   The relatch origin is `PostLoadSync`, not direct `VRAPI`.
-   Any plugin-side "do not call CS while RaceSex is open" guard cannot prevent this specific path, because the call is not coming from the plugin during RaceSex; CS applies it from the INI before RaceSex opens.
-   To test the Stabilizer author's RaceSex-open guard cleanly, CS-side `VR FPS Stabilizer Sync` must be disabled/removed, or CS must stop reading/applying the INI during startup.
