# VR render-scale replacement telemetry

DevBench render-scale qualification receipts at schema revision 15 separate
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
-   `mutationNotRequiredTerminalProof`
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
tick. Vendor eye records also retain dispatch frame, dispatch serial, and
runtime-fallback state. DLSS and FSR use the same proof shape. None and TAA use
exact native presentation. DLAA and FSR Native AA retain exact
target-correlated provider evaluation even though render-scale mode is
disabled.

Vendor proof requires coherent backends and a dispatch frame matching each
eye's presentation frame. DLSS uses backend and same-frame identity because
Streamline does not publish a dispatch serial. FSR requires a nonzero serial
for each eye. Render-scale FSR may use separate per-eye dispatches, while the
fixed native FSR path requires both eyes to identify the shared stereo batch.
When a status read lands between FSR eye publications, the diagnostic snapshot
uses the last coherent vendor pair only while its stable profile, device,
resource revision, and current publication still match.

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
proven independently. None and TAA use generation zero. Vendor-backed targets,
including DLAA and FSR Native AA, require a nonzero provider generation. Moving
from a scaled contract to a native target remains `required` because the scaled
resources must be retired.

`RecordPhysicalMutationBoundary` is the sole mutation-boundary authority. It
retains the first exact qualification session, transition, owner token,
replacement request and epoch, contract generation, device, source, frame, and
QPC tick. The boundary covers engine-target creation, provider invalidation,
and activation of an exact new FSR provider. It may precede contract
publication, so generation zero is retained there and later correlated through
the same request, epoch, owner, and device. Vendor-backed targets require the
later nonzero published generation to match exactly. None and TAA instead
require generation zero and remain correlated by request, transition epoch,
device, dimensions, resource revision, publication, and coherent stereo proof.
Provider lifecycle phases remain diagnostic and cannot move an observation
across this boundary.

## Authoritative presentation-cycle audit

The audit is compiled only into DevBench builds and records decisions at the
actual presentation boundary. Left and right eyes are paired by compositor
cycle and must agree on the complete identity tuple before the cycle is
coherent. Each eye retains its own frame and QPC tick. A pair that spans the
destructive boundary is reported separately and fails closed when submitted.
Quarantine and black-keepalive decisions are recorded explicitly.
Bounded storage, saturating counters, owner validation, and an overflow flag
make incomplete evidence visible without affecting rendering.

The decisive counters are:

-   `preMutationExactPresentationSuppressed`
-   `preMutationStretchWithoutMutation`
-   `postMutationOldGenerationPresented`
-   `postMutationUnprovenStereoSubmitted`

Coherent `PresentationStretch` stereo pairs submitted after mutation during
the transition cooldown remain visible in disposition counts and
`mixedOrUnprovenStereoPairsSubmitted`, but do not increment the decisive
`postMutationUnprovenStereoSubmitted` counter. That stretch is the protected
deferred-relatch presentation path. Pre-mutation stretch, boundary-spanning or
mixed stereo, and post-mutation stretch outside the cooldown remain decisive
violations.

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
    establish whether physical mutation was required. A claimed post-mutation
    offender whose frame or QPC tick precedes the canonical boundary is also
    producer-invalid and `INCONCLUSIVE`, never silently discarded.

An `INCONCLUSIVE` evidence verdict does not relabel a successful render
transition as a render failure. Full receipts are stored during the measured
pass and materialized and hashed at pass finalization so evidence handling does
not extend transition pacing.
