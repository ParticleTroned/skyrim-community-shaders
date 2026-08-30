# VR render-scale replacement telemetry

DevBench render-scale qualification receipts at schema revision 11 separate
render success from evidence completeness. This diagnostic contract does not
change render-scale preparation, admission, presentation, cleanup, or failure
policy.

## Immutable transition facets

Each owner-bound transition can retain these observations exactly once:

-   `dispatch`
-   `blockedPreMutation`
-   `lastPreMutation`
-   `firstPhysicalMutation`
-   `firstPostMutation`
-   `firstNewGenerationProven`
-   `terminal`

Every presentation facet contains a generic `presentationProof` rather than a
provider-specific proxy. Its kind is one of:

-   `exact_vendor_evaluation`
-   `exact_native_presentation`
-   `validated_completed_output_hold`
-   `none`

The proof records both-eye identity, method and backend values, dimensions,
request and transition identity, provider and publication generations,
resource revision, D3D device identity, compositor-cycle token, frame, and QPC
tick. DLSS and FSR use the same proof shape. Native None, TAA, DLAA, and FSR
Native AA use the native proof without pretending render-scale submission is
active.

## Admission and physical mutation

`preparationAdmission` describes only preparation work. Ineligible or
wrong-origin preparation is `not_applicable`; it is not evidence that a
replacement mutation was blocked.

`replacementMutationAdmission` is derived from controller state and separately
classifies queued, preparing, memory-deferred, shader-deferred,
provider-deferred, work-gate-deferred, admitted, recovery, failed, superseded,
and cleanup-only states. `mutationExpectation` states whether the transition
requires a physical mutation, does not require one, or could not be determined.
When both the source and target use the native physical contract, the
expectation is explicitly `not_required`; native vendor evaluation remains
proven independently. Moving from a scaled contract to a native target remains
`required` because the scaled resources must be retired.

## Authoritative presentation-cycle audit

The audit is compiled only into DevBench builds and records decisions at the
actual presentation boundary. Left and right eyes are paired by compositor
cycle and must agree on the complete identity tuple before the cycle is
coherent. Quarantine and black-keepalive decisions are recorded explicitly.
Bounded storage, saturating counters, owner validation, and an overflow flag
make incomplete evidence visible without affecting rendering.

The decisive counters are:

-   `preMutationExactPresentationSuppressed`
-   `preMutationStretchWithoutMutation`
-   `postMutationOldGenerationPresented`
-   `postMutationUnprovenStereoSubmitted`

The receipt also retains the first offending cycle for each nonzero counter,
disposition counts before and after physical mutation, partial-eye observation
counts, and incomplete stereo cycles still pending at receipt time.

## Verdicts

The tuning runners report render qualification and Task 2 evidence separately.
Task 2 is:

-   `PASS` when all required immutable facets exist, the audit is complete, and
    every decisive invariant counter is zero;
-   `FAIL` when authoritative evidence proves an invariant violation; or
-   `INCONCLUSIVE` when evidence is missing, ambiguous, overflowed, or cannot
    establish whether physical mutation was required.

An `INCONCLUSIVE` evidence verdict does not relabel a successful render
transition as a render failure. Full receipts are stored during the measured
pass and materialized and hashed at pass finalization so evidence handling does
not extend transition pacing.
