# VR Door-Transition Hang Status

## Current status

Repeated interior/exterior door transitions can still leave Skyrim VR permanently
outside the world view in the SteamVR waiting room. The failure normally appears
on the third transition. Skyrim does not recover without being closed.

The failure has now been reproduced on RC164 with DevBench capture and the HAM
probe inactive. It is therefore a production-path regression, not merely an
effect of the diagnostic probe.

Current branch and candidate:

-   Branch: `cs-1.7-PL-VR`
-   RC164: `744183a0756cb10540b740fab266f2b3d876f62b`
-   Result: failed; the third-transition hang remains

## Confirmed baseline

RC145 is the latest build confirmed to complete ordinary door transitions:

-   RC145: `459d029acc39ef0b836f7ea9352cd8fae2c5c79c`
-   HAM probe inactive: eight transitions, or four complete
    exterior/interior/exterior cycles, passed
-   Presentation remained correctly vendor-evaluated for both eyes
-   No permanent stretch, bounds fallback, vendor failure, device loss, or OOM
    was observed
-   Visual inspection was reported as perfect

The same RC145 build did hang on the third transition when the HAM probe was
explicitly active. That is a separate diagnostic-path problem and must not be
used to classify RC145's normal production behavior.

Earlier reference points:

-   RC141 (`eae01bb5934631438a5e064de4b2f54472d3a60a`) was stable during repeated
    door transitions, although an occasional second black fade remained.
-   RC144b (`339585ed6f4d47a91ab3a87dbec85c0984d5c242`) is the last
    non-diagnostic commit before RC145.

The first suspect after the confirmed baseline is RC146
(`71cc4c76e`), which begins the later post-load presentation changes.

## What RC164 attempted

RC164 attempted to retain the newer first-load white/HAM protection while
returning ordinary door and travel transitions to the RC141 behavior:

-   Limited startup presentation protection to the first save loaded in a process
-   Cancelled that startup ownership before subsequent loading transitions
-   Restored the earlier mask-repair policy for ordinary doors and travel
-   Added bounded recovery instead of allowing startup waits to persist forever
-   Kept paired-eye presentation ownership intact while a startup transition ended

Because RC164 still hangs, startup ownership leaking directly into later doors
is not the complete root cause. The remaining regression must be isolated
against RC145 rather than extended with another speculative fix.

## Reproduction protocol

Use the same protocol for every A/B build:

1. Start a fresh Skyrim VR process.
2. Keep the DevBench HAM probe inactive. Prefer a production build with the
   DevBench bridge disabled for the decisive check.
3. Load a stable exterior save and wait for the normal baseline to settle.
4. Enter and exit the same building at normal gameplay speed.
5. Continue for at least ten complete exterior/interior/exterior cycles.
6. Record whether each destination profile latches, whether both eyes continue
   presenting, and whether SteamVR shows “Waiting for Skyrim”.
7. Repeat the complete test separately with DLSS and FSR.

Do not combine this isolation run with rapid stress transitions, menu changes,
fast travel, or the HAM probe. Those remain follow-up tests after the ordinary
door regression is removed.

## Next investigation

Use RC145 as the known-good side of a narrow A/B comparison:

1. Reconfirm RC145 with the HAM probe inactive.
2. Test RC146 unchanged.
3. If RC146 fails, compare only the RC145-to-RC146 presentation and submit
   changes.
4. If RC146 passes, continue one release-candidate commit at a time until the
   first failing build is identified.
5. Restore the RC145 ordinary door path at that exact boundary.
6. Reintroduce only the minimum initial-load protection needed to prevent the
   confirmed white/HAM startup artefacts.

Retain the existing untracked DevBench, Ghidra, WPR, and ETL evidence for later
comparison. Do not commit or delete it.

## Acceptance criteria

A replacement is ready only when all of the following pass:

-   At least ten normal door cycles with DLSS
-   At least ten normal door cycles with FSR
-   No SteamVR waiting-room hang or loss of world view
-   Correct destination profile and Render Scale state after every transition
-   No permanent stretch, flicker, white flash, or black HAM artefact
-   No second Community Shaders-generated black fade
-   Stable memory behavior through the complete session
-   The same first-load visual checks that previously passed with the newer
    startup protection
