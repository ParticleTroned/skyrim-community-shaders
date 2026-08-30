#include "Features/Upscaling/VRRenderScaleReplacementTelemetryPolicy.h"

#include <cstdint>

namespace
{
	using namespace VRRenderScaleReplacementTelemetryPolicy;

	constexpr EyeObservation Eye(
		std::uint32_t a_eye,
		std::uint64_t a_cycle,
		PresentationDisposition a_disposition,
		bool a_beforeMutation = true,
		bool a_exactCurrent = true,
		bool a_exactReplacement = false)
	{
		return {
			.valid = true,
			.eyeIndex = a_eye,
			.frame = 20,
			.qpcTick = 100 + a_eye,
			.compositorCycleToken = a_cycle,
			.requestID = 7,
			.transitionEpoch = 9,
			.contractGeneration = a_exactReplacement ? 9u : 8u,
			.providerGeneration = a_exactReplacement ? 11u : 10u,
			.publicationGeneration = a_exactReplacement ? 101u : 100u,
			.resourceRevision = a_exactReplacement ? 41u : 40u,
			.deviceIdentity = 0x1234,
			.method = 3,
			.backend = 1,
			.disposition = a_disposition,
			.submitted = true,
			.exactCurrent = a_exactCurrent,
			.exactReplacement = a_exactReplacement,
			.physicalMutationStarted = !a_beforeMutation,
		};
	}

	constexpr AuditState StartedAudit()
	{
		AuditState state{};
		state.active = true;
		state.ownerTransitionID = 3;
		state.ownerToken = 5;
		return state;
	}

	constexpr bool CoversImmutableTimeline()
	{
		return ShouldUpdateLastPreMutation(true, false, false) &&
		       !ShouldUpdateLastPreMutation(true, false, true) &&
		       !ShouldUpdateLastPreMutation(true, true, false) &&
		       !ShouldUpdateLastPreMutation(false, false, false);
	}

	constexpr bool CoversMutationExpectation()
	{
		const ProfileIdentity oldProfile{
			.valid = true,
			.method = 3,
			.qualityMode = 3,
			.renderScaleMode = true,
			.backend = 1,
			.renderWidth = 100,
			.renderHeight = 100,
			.displayWidth = 200,
			.displayHeight = 200,
		};
		auto same = oldProfile;
		auto changed = oldProfile;
		changed.qualityMode = 4;
		return DetermineMutationExpectation({}, changed) == MutationExpectation::Unknown &&
		       DetermineMutationExpectation(oldProfile, same) == MutationExpectation::NotRequired &&
		       DetermineMutationExpectation(oldProfile, changed) == MutationExpectation::Required &&
		       DetermineNativeTargetMutationExpectation({}) ==
		           MutationExpectation::Unknown &&
		       DetermineNativeTargetMutationExpectation({
				   .targetRenderScaleMode = true,
				   .currentContractKnown = true,
			   }) ==
		           MutationExpectation::Unknown &&
		       DetermineNativeTargetMutationExpectation({
				   .currentContractKnown = true,
			   }) ==
		           MutationExpectation::NotRequired &&
		       DetermineNativeTargetMutationExpectation({
				   .currentContractKnown = true,
				   .currentPhysicalContractActive = true,
			   }) ==
		           MutationExpectation::Required &&
		       DetermineNativeTargetMutationExpectation({
				   .currentContractKnown = true,
				   .relatchDecisionKnown = true,
				   .relatchRequiresMutation = true,
			   }) ==
		           MutationExpectation::Required &&
		       DetermineNativeTargetMutationExpectation({
				   .currentContractKnown = true,
				   .physicalMutationRecorded = true,
			   }) ==
		           MutationExpectation::Required &&
		       MergeMutationExpectation(
				   MutationExpectation::Required,
				   MutationExpectation::Unknown) ==
		           MutationExpectation::Required &&
		       MergeMutationExpectation(
				   MutationExpectation::NotRequired,
				   MutationExpectation::Unknown) ==
		           MutationExpectation::NotRequired &&
		       MergeMutationExpectation(
				   MutationExpectation::NotRequired,
				   MutationExpectation::Required) ==
		           MutationExpectation::Required;
	}

	constexpr bool CoversAdmissions()
	{
		if (!IsPreparationNotApplicable(1ull << 1) ||
			!IsPreparationNotApplicable(1ull << 2) ||
			IsPreparationNotApplicable(1ull << 10)) {
			return false;
		}
		MutationAdmissionFacts facts{ .hasReplacement = true, .queued = true };
		if (ClassifyMutationAdmission(facts) != MutationAdmission::Queued)
			return false;
		facts.memoryDeferred = true;
		if (ClassifyMutationAdmission(facts) != MutationAdmission::MemoryDeferred)
			return false;
		facts.memoryDeferred = false;
		facts.shaderDeferred = true;
		if (ClassifyMutationAdmission(facts) != MutationAdmission::ShaderDeferred)
			return false;
		facts.shaderDeferred = false;
		facts.providerDeferred = true;
		if (ClassifyMutationAdmission(facts) != MutationAdmission::ProviderDeferred)
			return false;
		facts.providerDeferred = false;
		facts.workGateDeferred = true;
		return ClassifyMutationAdmission(facts) == MutationAdmission::WorkGateDeferred;
	}

	constexpr bool CoversMutationBoundaryOwnership()
	{
		MutationBoundaryOwnershipFacts facts{
			.ownerActive = true,
			.auditActive = true,
			.stressSessionMatches = true,
			.dispatchTick = 100,
			.boundaryTick = 101,
			.dispatchTransitionEpoch = 8,
			.controllerTargetEpoch = 9,
			.boundaryTransitionEpoch = 9,
			.replacementRequestID = 7,
			.replacementTransitionEpoch = 9,
			.replacementContractGeneration = 10,
			.dispatchDeviceIdentity = 0x1234,
			.currentDeviceIdentity = 0x1234,
		};
		if (!OwnsMutationBoundary(facts))
			return false;
		auto stale = facts;
		stale.stressSessionMatches = false;
		if (OwnsMutationBoundary(stale))
			return false;
		stale = facts;
		stale.replacementRequestID = 0;
		if (OwnsMutationBoundary(stale))
			return false;
		stale = facts;
		stale.replacementTransitionEpoch = 10;
		if (OwnsMutationBoundary(stale))
			return false;
		stale = facts;
		stale.replacementContractGeneration = 0;
		if (OwnsMutationBoundary(stale))
			return false;
		stale = facts;
		stale.currentDeviceIdentity = 0x5678;
		return !OwnsMutationBoundary(stale);
	}

	constexpr bool CoversProofKinds()
	{
		PresentationProofFacts facts{
			.coherentStereoCycle = true,
			.currentProfileMatches = true,
			.publicationCurrent = true,
			.exactDimensions = true,
			.nativeDimensions = true,
			.vendorBackendPresent = true,
			.renderScaleDisabled = true,
			.foveatedVendorDisabled = true,
			.staleVendorGenerationAbsent = true,
			.completedOutputStronglyOwned = true,
			.disposition = PresentationDisposition::ExactVendor,
		};
		if (ClassifyPresentationProof(facts) !=
			PresentationProofKind::ExactVendorEvaluation) {
			return false;
		}
		facts.disposition = PresentationDisposition::ExactNative;
		facts.vendorBackendPresent = false;
		if (ClassifyPresentationProof(facts) !=
			PresentationProofKind::ExactNativePresentation) {
			return false;
		}
		facts.disposition = PresentationDisposition::CompletedOutputHold;
		if (ClassifyPresentationProof(facts) !=
			PresentationProofKind::ValidatedCompletedOutputHold) {
			return false;
		}
		facts.completedOutputStronglyOwned = false;
		return ClassifyPresentationProof(facts) ==
		       PresentationProofKind::None;
	}

	constexpr bool CoversPartialAndCompleteCycles()
	{
		auto state = StartedAudit();
		CompleteCycle completed{};
		if (ObserveEye(state, 3, 5, Eye(0, 12, PresentationDisposition::ExactVendor), completed))
			return false;
		if (state.counters.mixedOrUnprovenStereoPairsSubmitted != 0 ||
			CountIncompleteStereoCycles(state) != 1)
			return false;
		if (!ObserveEye(state, 3, 5, Eye(1, 12, PresentationDisposition::ExactVendor), completed))
			return false;
		return completed.coherent &&
		       state.counters.eyeObservations == 2 &&
		       CountIncompleteStereoCycles(state) == 0 &&
		       state.counters.completeStereoCyclesBeforeMutation == 1 &&
		       state.counters.exactPreviousGenerationCycles == 1;
	}

	constexpr bool CoversMixedSubmittedViolation()
	{
		auto state = StartedAudit();
		CompleteCycle completed{};
		auto left = Eye(0, 14, PresentationDisposition::ExactVendor, false, false, false);
		auto right = Eye(1, 14, PresentationDisposition::ExactNative, false, false, false);
		(void)ObserveEye(state, 3, 5, left, completed);
		(void)ObserveEye(state, 3, 5, right, completed);
		return !completed.coherent && completed.disposition == PresentationDisposition::Mixed &&
		       state.counters.postMutationUnprovenStereoSubmitted == 1 &&
		       state.counters.firstPostMutationUnprovenStereoSubmitted.valid;
	}

	constexpr bool CoversOldGenerationAfterMutation()
	{
		auto state = StartedAudit();
		CompleteCycle completed{};
		(void)ObserveEye(state, 3, 5,
			Eye(0, 16, PresentationDisposition::ExactVendor, false, true, false), completed);
		(void)ObserveEye(state, 3, 5,
			Eye(1, 16, PresentationDisposition::ExactVendor, false, true, false), completed);
		return state.counters.postMutationOldGenerationPresented == 1 &&
		       state.counters.oldGenerationEvaluationsAfterMutation == 1 &&
		       state.counters.firstPostMutationOldGenerationPresented.valid;
	}

	constexpr bool CoversPreMutationViolations()
	{
		auto state = StartedAudit();
		CompleteCycle completed{};
		(void)ObserveEye(state, 3, 5,
			Eye(0, 18, PresentationDisposition::PresentationStretch), completed);
		(void)ObserveEye(state, 3, 5,
			Eye(1, 18, PresentationDisposition::PresentationStretch), completed);
		return state.counters.preMutationStretchWithoutMutation == 1 &&
		       state.counters.firstPreMutationStretchWithoutMutation.valid;
	}

	constexpr bool CoversNewGenerationProof()
	{
		auto state = StartedAudit();
		CompleteCycle completed{};
		(void)ObserveEye(state, 3, 5,
			Eye(0, 20, PresentationDisposition::ExactVendor, false, false, true), completed);
		(void)ObserveEye(state, 3, 5,
			Eye(1, 20, PresentationDisposition::ExactVendor, false, false, true), completed);
		return state.counters.firstExactNewGenerationCycles == 1 &&
		       state.counters.postMutationUnprovenStereoSubmitted == 0;
	}

	constexpr bool CoversStaleOwnership()
	{
		auto state = StartedAudit();
		CompleteCycle completed{};
		return !ObserveEye(state, 4, 5,
				   Eye(0, 22, PresentationDisposition::ExactVendor), completed) &&
		       !ObserveEye(state, 3, 6,
				   Eye(0, 22, PresentationDisposition::ExactVendor), completed) &&
		       state.counters.eyeObservations == 0;
	}

	constexpr bool CoversBoundedOverflow()
	{
		auto state = StartedAudit();
		CompleteCycle completed{};
		for (std::uint64_t cycle = 1; cycle <= kPendingCycleCapacity; ++cycle) {
			(void)ObserveEye(state, 3, 5,
				Eye(0, cycle, PresentationDisposition::ExactVendor), completed);
		}
		(void)ObserveEye(state, 3, 5,
			Eye(0, kPendingCycleCapacity + 1, PresentationDisposition::ExactVendor), completed);
		return state.retentionOverflow && !state.evidenceComplete;
	}

	static_assert(CoversImmutableTimeline());
	static_assert(CoversMutationExpectation());
	static_assert(CoversAdmissions());
	static_assert(CoversMutationBoundaryOwnership());
	static_assert(CoversProofKinds());
	static_assert(CoversPartialAndCompleteCycles());
	static_assert(CoversMixedSubmittedViolation());
	static_assert(CoversOldGenerationAfterMutation());
	static_assert(CoversPreMutationViolations());
	static_assert(CoversNewGenerationProof());
	static_assert(CoversStaleOwnership());
	static_assert(CoversBoundedOverflow());
}

int main()
{
	return 0;
}
