# Deferred FSR eye dispatch

## Failure mechanism

The NVIDIA tuning sequence's transition 26 (DLSS Hoshipa to FSR3 Hoshipa)
and transition 28 (None to FSR3 Ultra Performance) enter scaled FSR from
another method. Cold runtime-provider resources can require an asynchronous
readiness wait before the first evaluation.

Submit-input freshness admission can reject stereo batching and select
single-eye dispatch instead. The stereo FSR API already distinguishes
`Deferred` from `Failed`; the single-eye API previously collapsed readiness
waits into its Boolean failure result when no compatible host fallback was
available. This can produce one failed evaluation for each eye followed by
successful evaluation on a later frame. Keeping the producer-proof checks
requires preserving this distinction in the single-eye path too.

This mechanism is consistent with the recovered cold-entry failures
reported for PR65 and the PR66 candidate that includes it. The exact
internal readiness wait responsible for those historical frames was not
retained, so source inspection does not prove that a particular fence was
the original trigger.

## Repair behavior

- Return `Deferred` for deferred dispatch admission, deferred provider
  setup without a compatible host fallback, and pending runtime dispatch
  without a compatible host fallback. Returning `Deferred` does not arm host
  fallback or quarantine the provider.
- Propagate the result through full-eye, foveated-center, main-pass, and
  submit replay callers before publishing vendor output or committing
  temporal history. Deferred submits request a history reset because
  changing eye sources can invalidate the cached output that would prove
  an earlier eye already advanced history. This does not reset provider
  resources or report an evaluation failure.
- Main-pass deferral also preserves reset intent, including flat FSR and
  a foveated first-eye wait. A warm provider that missed a frame must not
  reuse history with motion vectors describing only the latest frame.
- Present ordinary `PresentationStretch` and hold the current compositor
  cycle on presentation-only output, including when intermediate texture
  replacement cleared its admission record. Preserve a conflicting
  nonzero cycle rather than replacing its identity.
- Keep compatible host fallback available. Single-eye compatibility uses
  the planned context count, including both contexts in VR, and admits
  foveated subregions within the allocated context's render/output bounds.
  Lifecycle compatibility still requires the exact display contract,
  including when a gate changes after foveated admission.
- Latch a frame's host fallback when dispatch selects that path. A pure
  provider-readiness wait does not prevent a later compositor cycle in
  the same engine frame from using the now-ready runtime provider.
- Preserve failed evaluations, device-loss handling, and provider
  quarantine for actual failures. DLSS maps its existing success/failure
  result to the shared dispatch result. Flat SE/AE FSR callers also carry
  readiness deferral to the existing main-pass lifecycle handling.

No new DevBench setting or schema is introduced. Existing presentation,
fidelity, lifecycle, and producer-proof diagnostics expose the behavior.

## Validation status

The implementation was reviewed as source only. Focused controller tests
use the production single-eye/stereo dispatch functions, resource
compatibility checks, resolver gate-selection block, and deferred
presentation helper with provider and graphics doubles. They cover host
subregions, same-frame gate recovery, failure/quarantine behavior, invalid
resources, and reset protection after output-cache invalidation. They have
not been built or run and do not execute the complete resolver, submit, or
main-pass integration. They cannot establish graphics or hook correctness
in Skyrim.

The operator explicitly deferred all builds and tests while another
workload is running. Runtime qualification and the generated
`csx-render-scale-pr-v1` summary are therefore absent; the PR remains a
draft. The existing [comparison ledger](vr-render-scale-comparison-ledger.csv)
has no new candidate measurements from this repair. After authorization,
validation must cover cold entry and warm reuse, both eyes, repeated
cycles, compatible-host and runtime-only paths, and the required
[render-scale qualification](render-scale-pr-qualification.md).
