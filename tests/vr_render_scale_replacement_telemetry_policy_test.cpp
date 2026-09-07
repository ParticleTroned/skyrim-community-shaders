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
		bool a_exactReplacement = false,
		bool a_transitionCooldown = false)
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
			.renderWidth = a_exactReplacement ? 120u : 100u,
			.renderHeight = a_exactReplacement ? 120u : 100u,
			.displayWidth = 200,
			.displayHeight = 200,
			.method = 3,
			.qualityMode = a_exactReplacement ? 4u : 3u,
			.renderScaleMode = true,
			.backend = 1,
			.disposition = a_disposition,
			.transitionCooldown = a_transitionCooldown,
			.submitted = true,
			.exactCurrent = a_exactCurrent,
			.exactCurrentPresentationAvailable = a_exactCurrent,
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
			.qualificationTransitionID = 12,
			.ownershipToken = 13,
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
		stale.qualificationTransitionID = 0;
		if (OwnsMutationBoundary(stale))
			return false;
		stale = facts;
		stale.ownershipToken = 0;
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
		if (!OwnsMutationBoundary(stale))
			return false;
		stale = facts;
		stale.currentDeviceIdentity = 0x5678;
		return !OwnsMutationBoundary(stale);
	}

	constexpr bool CoversGenerationCorrelation()
	{
		if (!MatchesTargetContractGeneration(false, 0, 0) ||
			MatchesTargetContractGeneration(true, 0, 0) ||
			!MatchesTargetContractGeneration(true, 8, 8) ||
			MatchesTargetContractGeneration(true, 8, 9) ||
			!MatchesMutationBoundaryGeneration(false, 8, 0) ||
			MatchesMutationBoundaryGeneration(false, 8, 8) ||
			!MatchesMutationBoundaryGeneration(true, 0, 8) ||
			!MatchesMutationBoundaryGeneration(true, 8, 8) ||
			MatchesMutationBoundaryGeneration(true, 8, 0) ||
			MatchesMutationBoundaryGeneration(true, 8, 9)) {
			return false;
		}

		PublishedReplacementProfileFacts facts{
			.profileValid = true,
			.requiresPublishedGeneration = false,
			.observedTransitionEpoch = 17,
			.expectedTransitionEpoch = 17,
			.observedContractGeneration = 0,
			.expectedContractGeneration = 0,
			.observedMethod = 2,
			.expectedMethod = 2,
			.observedRenderWidth = 2016,
			.observedRenderHeight = 2240,
			.observedDisplayWidth = 2016,
			.observedDisplayHeight = 2240,
			.expectedRenderWidth = 2016,
			.expectedRenderHeight = 2240,
			.expectedDisplayWidth = 2016,
			.expectedDisplayHeight = 2240,
			.observedDeviceIdentity = 0x1234,
			.currentDeviceIdentity = 0x1234,
			.observedResourceRevision = 9,
		};
		if (!MatchesPublishedReplacementProfile(facts))
			return false;
		auto stale = facts;
		stale.profileValid = false;
		if (MatchesPublishedReplacementProfile(stale))
			return false;
		stale = facts;
		stale.observedTransitionEpoch = 18;
		if (MatchesPublishedReplacementProfile(stale))
			return false;
		stale = facts;
		stale.observedMethod = 3;
		if (MatchesPublishedReplacementProfile(stale))
			return false;
		stale = facts;
		stale.observedRenderWidth = 2015;
		if (MatchesPublishedReplacementProfile(stale))
			return false;
		stale = facts;
		stale.observedDisplayHeight = 2239;
		if (MatchesPublishedReplacementProfile(stale))
			return false;
		stale = facts;
		stale.observedResourceRevision = 0;
		if (MatchesPublishedReplacementProfile(stale))
			return false;
		facts.requiresPublishedGeneration = true;
		if (MatchesPublishedReplacementProfile(facts))
			return false;
		facts.observedContractGeneration = 8;
		facts.expectedContractGeneration = 8;
		if (!MatchesPublishedReplacementProfile(facts))
			return false;
		facts.observedContractGeneration = 9;
		if (MatchesPublishedReplacementProfile(facts))
			return false;
		facts.observedContractGeneration = 8;
		facts.observedDeviceIdentity = 0x5678;
		return !MatchesPublishedReplacementProfile(facts);
	}

	constexpr bool CoversProofKinds()
	{
		PresentationProofFacts facts{
			.coherentStereoCycle = true,
			.currentProfileMatches = true,
			.publicationCurrent = true,
			.exactDimensions = true,
			.nativeDimensions = true,
			.vendorDispatchProven = true,
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
		if (ClassifyPresentationProof(facts) !=
			PresentationProofKind::ExactVendorEvaluation) {
			return false;
		}
		facts.vendorDispatchProven = false;
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

	constexpr bool CoversVendorDispatchProof()
	{
		VendorDispatchProofFacts facts{
			.backendCoherent = true,
			.dispatchFramesCurrent = true,
			.runtimeFallbackCoherent = true,
			.dlssBackend = true,
		};
		if (!HasCoherentVendorDispatch(facts))
			return false;
		facts.runtimeFallback = true;
		if (HasCoherentVendorDispatch(facts))
			return false;

		facts = {
			.backendCoherent = true,
			.dispatchFramesCurrent = true,
			.runtimeFallbackCoherent = true,
			.fsrBackend = true,
			.leftDispatchSerial = 41,
			.rightDispatchSerial = 42,
		};
		if (!HasCoherentVendorDispatch(facts))
			return false;
		facts.sharedFSRDispatchRequired = true;
		if (HasCoherentVendorDispatch(facts))
			return false;
		facts.rightDispatchSerial = facts.leftDispatchSerial;
		if (!HasCoherentVendorDispatch(facts))
			return false;
		facts.dispatchFramesCurrent = false;
		return !HasCoherentVendorDispatch(facts);
	}

	constexpr bool CoversPublishedReplacementProof()
	{
		constexpr std::uint32_t allFacts = (1u << 9) - 1u;
		for (std::uint32_t bits = 0; bits <= allFacts; ++bits) {
			const PublishedReplacementProofFacts facts{
				.physicalMutationStarted = (bits & (1u << 0)) != 0,
				.differsFromDispatch = (bits & (1u << 1)) != 0,
				.observed = (bits & (1u << 2)) != 0,
				.profileMatches = (bits & (1u << 3)) != 0,
				.mutationBoundaryMatches = (bits & (1u << 4)) != 0,
				.presentationPathMatches = (bits & (1u << 5)) != 0,
				.resourceContractMatches = (bits & (1u << 6)) != 0,
				.providerGenerationMatches = (bits & (1u << 7)) != 0,
				.publicationCurrent = (bits & (1u << 8)) != 0,
			};
			if (IsPublishedReplacementProven(facts) != (bits == allFacts))
				return false;
		}
		return true;
	}

	constexpr bool CoversVendorAuditIdentity()
	{
		auto left = Eye(0, 12, PresentationDisposition::ExactVendor);
		auto right = Eye(1, 12, PresentationDisposition::ExactVendor);
		left.vendorDispatchFrame = left.frame;
		right.vendorDispatchFrame = right.frame;
		left.vendorDispatchSerial = 41;
		right.vendorDispatchSerial = 42;
		left.vendorDispatchProven = true;
		right.vendorDispatchProven = true;
		if (!SameSubmittedIdentity(left, right))
			return false;

		left.sharedVendorDispatchRequired = true;
		right.sharedVendorDispatchRequired = true;
		if (SameSubmittedIdentity(left, right))
			return false;
		right.vendorDispatchSerial = left.vendorDispatchSerial;
		if (!SameSubmittedIdentity(left, right))
			return false;
		right.vendorRuntimeFallback = true;
		return !SameSubmittedIdentity(left, right);
	}

	constexpr bool CoversTargetProofKindRequirements()
	{
		return IsExactTargetProofKind(
				   PresentationProofKind::ExactNativePresentation, false) &&
		       !IsExactTargetProofKind(
				   PresentationProofKind::ExactVendorEvaluation, false) &&
		       IsExactTargetProofKind(
				   PresentationProofKind::ExactVendorEvaluation, true) &&
		       !IsExactTargetProofKind(
				   PresentationProofKind::ExactNativePresentation, true);
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

	constexpr bool CoversExactBoundaryClassification()
	{
		const MutationBoundaryTimestamp boundary{
			.valid = true,
			.frame = 20,
			.qpcTick = 101,
		};
		return !IsAtOrAfterMutationBoundary(19, 102, boundary) &&
		       !IsAtOrAfterMutationBoundary(20, 100, boundary) &&
		       IsAtOrAfterMutationBoundary(20, 101, boundary) &&
		       IsAtOrAfterMutationBoundary(21, 102, boundary) &&
		       !IsAtOrAfterMutationBoundary(21, 102, {});
	}

	constexpr bool CoversBoundarySpanningStereo()
	{
		auto state = StartedAudit();
		CompleteCycle completed{};
		auto left = Eye(
			0, 13, PresentationDisposition::ExactVendor, true, true, false);
		auto right = Eye(
			1, 13, PresentationDisposition::ExactVendor, false, false, true);
		left.qpcTick = 100;
		right.qpcTick = 102;
		(void)ObserveEye(state, 3, 5, left, completed);
		(void)ObserveEye(state, 3, 5, right, completed);
		return completed.boundarySpanning && !completed.beforeMutation &&
		       !completed.afterMutation && !completed.coherent &&
		       completed.leftQpcTick == 100 &&
		       completed.rightQpcTick == 102 &&
		       state.counters.boundarySpanningStereoCycles == 1 &&
		       state.counters.completeStereoCyclesAfterMutation == 0 &&
		       state.counters.postMutationUnprovenStereoSubmitted == 1 &&
		       state.counters.postMutationOldGenerationPresented == 0;
	}

	constexpr bool CoversOutOfOrderPreBoundaryEye()
	{
		auto state = StartedAudit();
		CompleteCycle completed{};
		(void)ObserveEye(
			state, 3, 5,
			Eye(0, 30, PresentationDisposition::ExactVendor, false, false, true),
			completed);
		(void)ObserveEye(
			state, 3, 5,
			Eye(0, 31, PresentationDisposition::ExactVendor, true, true, false),
			completed);
		(void)ObserveEye(
			state, 3, 5,
			Eye(1, 31, PresentationDisposition::ExactVendor, true, true, false),
			completed);
		return completed.beforeMutation && !completed.afterMutation &&
		       completed.exactCurrent &&
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

	constexpr bool CoversProtectedStretchAfterMutation()
	{
		auto state = StartedAudit();
		CompleteCycle completed{};
		(void)ObserveEye(state, 3, 5,
			Eye(0, 17, PresentationDisposition::PresentationStretch, false, false, false, true), completed);
		(void)ObserveEye(state, 3, 5,
			Eye(1, 17, PresentationDisposition::PresentationStretch, false, false, false, true), completed);
		return completed.afterMutation &&
		       state.counters.mixedOrUnprovenStereoPairsSubmitted == 1 &&
		       state.counters.postMutationUnprovenStereoSubmitted == 0 &&
		       !state.counters.firstPostMutationUnprovenStereoSubmitted.valid;
	}

	constexpr bool CoversUnprotectedStretchAfterMutation()
	{
		auto state = StartedAudit();
		CompleteCycle completed{};
		(void)ObserveEye(state, 3, 5,
			Eye(0, 17, PresentationDisposition::PresentationStretch, false, false, false), completed);
		(void)ObserveEye(state, 3, 5,
			Eye(1, 17, PresentationDisposition::PresentationStretch, false, false, false), completed);
		return completed.afterMutation &&
		       state.counters.mixedOrUnprovenStereoPairsSubmitted == 1 &&
		       state.counters.postMutationUnprovenStereoSubmitted == 1 &&
		       state.counters.firstPostMutationUnprovenStereoSubmitted.valid;
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

	constexpr bool CoversUnprovenPreMutationStretch()
	{
		auto state = StartedAudit();
		CompleteCycle completed{};
		auto left = Eye(0, 19, PresentationDisposition::PresentationStretch);
		auto right = Eye(1, 19, PresentationDisposition::PresentationStretch);
		left.exactCurrentPresentationAvailable = false;
		right.exactCurrentPresentationAvailable = false;
		(void)ObserveEye(state, 3, 5, left, completed);
		(void)ObserveEye(state, 3, 5, right, completed);
		return state.counters.presentationStretchCyclesBeforeMutation == 1 &&
		       state.counters.preMutationStretchWithoutMutation == 0 &&
		       !state.counters.firstPreMutationStretchWithoutMutation.valid;
	}

	constexpr bool CoversNewGenerationProof()
	{
		auto state = StartedAudit();
		CompleteCycle completed{};
		(void)ObserveEye(state, 3, 5,
			Eye(0, 20, PresentationDisposition::ExactVendor, false, false, true), completed);
		(void)ObserveEye(state, 3, 5,
			Eye(1, 20, PresentationDisposition::ExactVendor, false, false, true), completed);
		(void)ObserveEye(state, 3, 5,
			Eye(0, 21, PresentationDisposition::ExactVendor, false, false, true), completed);
		(void)ObserveEye(state, 3, 5,
			Eye(1, 21, PresentationDisposition::ExactVendor, false, false, true), completed);
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
	static_assert(CoversGenerationCorrelation());
	static_assert(CoversProofKinds());
	static_assert(CoversVendorDispatchProof());
	static_assert(CoversPublishedReplacementProof());
	static_assert(CoversVendorAuditIdentity());
	static_assert(CoversTargetProofKindRequirements());
	static_assert(CoversPartialAndCompleteCycles());
	static_assert(CoversExactBoundaryClassification());
	static_assert(CoversBoundarySpanningStereo());
	static_assert(CoversOutOfOrderPreBoundaryEye());
	static_assert(CoversMixedSubmittedViolation());
	static_assert(CoversOldGenerationAfterMutation());
	static_assert(CoversProtectedStretchAfterMutation());
	static_assert(CoversUnprotectedStretchAfterMutation());
	static_assert(CoversPreMutationViolations());
	static_assert(CoversUnprovenPreMutationStretch());
	static_assert(CoversNewGenerationProof());
	static_assert(CoversStaleOwnership());
	static_assert(CoversBoundedOverflow());
}

int main()
{
	return 0;
}
