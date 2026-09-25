# Ordinary-save VR rendering recovery

Ordinary saves retain the existing 120-rendered-frame save/load safety
grace for shader-cache persistence and other guarded mutations. The
separate 60-frame persistent-mutation extension also remains unchanged.
VR render-scale presentation can recover earlier by reusing an unchanged,
validated resource contract after the engine finishes saving.

## Eligibility and resource ownership

`State::NotifyOrdinarySave` records a new save generation for every SKSE
save notification. A rendered engine-saving rising edge also advances that
generation. The token becomes eligible only when the engine save/load
singleton is available and its saving, loading, form initialization,
deferred initialization and player-positioning flags are all idle.

Preload, postload, new-game and serialized-load completion notifications
retain load provenance. Loading/main menus, initialization work, a pending
postload reset or an unavailable engine singleton revoke ordinary-save
eligibility. That disqualification remains until the existing broad guard
ends; another save during that window cannot downgrade it. Guard writers,
frame updates and render-token reads share one mutex, so a notification
overlapping guard expiry retains its deadline, persistence protection and
source. Token reads see a complete guard update. Source and generation
publication prevents overlapping saves from erasing load provenance or
retaining an earlier save's stereo proof.

The early path applies only to latched VR render-scale mode. It reuses
already matching vendor and foveated resources. A missing or mismatched
resource rejects reuse without allocating a replacement or destroying the
retained resource. Pending transitions, vendor resets, loading/menu
presentation, device loss or an unconverged physical contract prevent
presentation qualification. SE and AE keep their existing behavior.

## Stereo qualification

Both eyes must supply valid, current temporal inputs for completed world
frames under the same save token, method, resource key and contract
generation, including common and intermediate resource generations.
Each pair must also share the validated outer submit scope and flags,
including when the engine supplies separate color resources for each eye.
All six frames must be consecutive and complete; duplicate eye submissions
cannot advance the count. Missing eyes, skipped frames, changed identity,
unsafe observations and rejected output preparation reset it.

Until the proof qualifies, both eyes use the existing stretch path. A frame
counts only after its output is prepared successfully and its original
inputs and resource contract remain current. This avoids depending on mask
repair that is itself waiting for qualification. The count describes
prepared output, not a receipt of native compositor acceptance.

Presentation resumes in the compositor cycle after qualification so both
eyes receive the same decision. The proof counts the temporal snapshot's
completed world producer frame; a desktop Present advancing the engine
frame within that compositor cycle cannot qualify either eye early.
Qualification has no timeout that can authorize unsafe reuse. Rendering
eligibility does not release persistence protection.

A new save or failed output can revoke proof after the first eye has
cached its admission decision. The current cycle then retains stretch
presentation for every remaining submission; cached vendor admission
cannot override that downgrade, even if the save grace expires mid-cycle.

## DevBench inspection

Call `communityshaders.renderscale` with `{"action":"status"}` and inspect
`status.vendorWorkGate.ordinarySaveRecovery`:

| Field                                                | Meaning                                                                                         |
| ---------------------------------------------------- | ----------------------------------------------------------------------------------------------- |
| `saveToken`                                          | Current eligible ordinary-save token; zero when ineligible.                                     |
| `presentationReady`                                  | Whether the current proof permits early presentation.                                           |
| `persistenceBlocked`                                 | Independent persistent-mutation protection.                                                     |
| `proofSaveToken`                                     | Save generation associated with the retained proof.                                             |
| `contractGeneration`, `resourceKey`                  | Resource contract associated with the proof.                                                    |
| `method`                                             | Numeric upscale method associated with the proof.                                               |
| `commonResourceGeneration`, `intermediateGeneration` | Resource ownership generations rechecked before presentation.                                   |
| `firstFrame`, `lastCompleteFrame`                    | First candidate and most recent completed stereo frame.                                         |
| `stableFrames`, `requiredStereoFrames`               | Completed consecutive stereo frames and the six-frame threshold.                                |
| `producerScope`, `submitFlags`                       | Shared outer submit scope and flags for the latest stereo pair.                                 |
| `qualifiedFrame`                                     | Completed six-frame qualification, or zero while unqualified.                                   |
| `qualifiedCycle`, `lastObservedCycle`                | Qualification and latest observation compositor cycles; release requires a later current cycle. |

`presentationReady: true` with `persistenceBlocked: true` is expected during
the remainder of an ordinary save's broad safety grace. These are live
diagnostics, not a retained timing or visual-validation receipt.

## Validation scope

Controller tests exercise production State provenance, concurrent guard
publication, resource guards, cropped FSR dispatch and the ordinary-save
presentation admission and completion paths, plus stereo proof sequencing.
The admission harness includes the production compositor-cycle cache and
checks proof revocation, repeated saves and grace expiry between eyes.
The universal Release DLL builds with SE, AE, VR and DevBench enabled;
18 focused CTest cases pass, including both DevBench variants of relatch
promotion. The save-publication race and cropped-FSR regression tests also
fail against the corresponding pre-review code.

Local build and test receipts are retained under
`build/save-render-recovery-review/` and `build/save-render-recovery-all/`.
Reproduce the focused policy checks with the `OrdinarySaveState`,
`VROrdinarySaveRecovery`, `OrdinarySavePresentation`, `FoveatedSaveReuse`,
`FSRSaveReuse` and `FSREyeDispatch` CTest names after enabling
`BUILD_CONTROLLER_TESTS`.

Runtime quicksave, manual-save and autosave
timings and visual behavior have not yet been measured for this change.
The six-frame interval is a qualification policy, not a demonstrated save
latency or stability result. No runtime measurement ledger is generated by
these controller tests.
