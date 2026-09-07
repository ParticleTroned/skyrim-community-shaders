#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <string_view>

namespace VRRenderScaleReplacementTelemetryPolicy
{
	inline constexpr std::size_t kPendingCycleCapacity = 8;

	enum class MutationExpectation : std::uint8_t
	{
		Unknown,
		Required,
		NotRequired
	};

	enum class PresentationProofKind : std::uint8_t
	{
		None,
		ExactVendorEvaluation,
		ExactNativePresentation,
		ValidatedCompletedOutputHold
	};

	enum class PreparationAdmission : std::uint8_t
	{
		NotApplicable,
		Eligible,
		Busy,
		Ready,
		Failed,
		ProtectedFallback
	};

	enum class MutationAdmission : std::uint8_t
	{
		NotApplicable,
		Admitted,
		Queued,
		Preparing,
		MemoryDeferred,
		ShaderDeferred,
		ProviderDeferred,
		WorkGateDeferred,
		CleanupOnlyDebt,
		PreMutationRecovery,
		PostMutationRecovery,
		Failed,
		Superseded
	};

	enum class PresentationDisposition : std::uint8_t
	{
		None,
		ExactVendor,
		ExactNative,
		CompletedOutputHold,
		PresentationStretch,
		VendorFailureStretch,
		BoundsMismatchOriginal,
		BlackKeepalive,
		Quarantine,
		Mixed
	};

	[[nodiscard]] constexpr std::string_view GetMutationExpectationName(
		MutationExpectation a_value) noexcept
	{
		switch (a_value) {
		case MutationExpectation::Required:
			return "required";
		case MutationExpectation::NotRequired:
			return "not_required";
		default:
			return "unknown";
		}
	}

	[[nodiscard]] constexpr std::string_view GetProofKindName(
		PresentationProofKind a_value) noexcept
	{
		switch (a_value) {
		case PresentationProofKind::ExactVendorEvaluation:
			return "exact_vendor_evaluation";
		case PresentationProofKind::ExactNativePresentation:
			return "exact_native_presentation";
		case PresentationProofKind::ValidatedCompletedOutputHold:
			return "validated_completed_output_hold";
		default:
			return "none";
		}
	}

	struct PresentationProofFacts
	{
		bool coherentStereoCycle = false;
		bool currentProfileMatches = false;
		bool publicationCurrent = false;
		bool exactDimensions = false;
		bool nativeDimensions = false;
		bool vendorDispatchProven = false;
		bool renderScaleDisabled = false;
		bool foveatedVendorDisabled = false;
		bool staleVendorGenerationAbsent = false;
		bool completedOutputStronglyOwned = false;
		PresentationDisposition disposition = PresentationDisposition::None;
	};

	struct VendorDispatchProofFacts
	{
		bool backendCoherent = false;
		bool dispatchFramesCurrent = false;
		bool runtimeFallbackCoherent = false;
		bool dlssBackend = false;
		bool fsrBackend = false;
		bool runtimeFallback = false;
		std::uint64_t leftDispatchSerial = 0;
		std::uint64_t rightDispatchSerial = 0;
		bool sharedFSRDispatchRequired = false;
	};

	struct PublishedReplacementProofFacts
	{
		bool physicalMutationStarted = false;
		bool differsFromDispatch = false;
		bool observed = false;
		bool profileMatches = false;
		bool mutationBoundaryMatches = false;
		bool presentationPathMatches = false;
		bool resourceContractMatches = false;
		bool providerGenerationMatches = false;
		bool publicationCurrent = false;
	};

	[[nodiscard]] constexpr bool IsPublishedReplacementProven(
		const PublishedReplacementProofFacts& a_facts) noexcept
	{
		return a_facts.physicalMutationStarted &&
		       a_facts.differsFromDispatch && a_facts.observed &&
		       a_facts.profileMatches && a_facts.mutationBoundaryMatches &&
		       a_facts.presentationPathMatches &&
		       a_facts.resourceContractMatches &&
		       a_facts.providerGenerationMatches &&
		       a_facts.publicationCurrent;
	}

	[[nodiscard]] constexpr bool HasCoherentVendorDispatch(
		const VendorDispatchProofFacts& a_facts) noexcept
	{
		if (!a_facts.backendCoherent || !a_facts.dispatchFramesCurrent ||
			!a_facts.runtimeFallbackCoherent) {
			return false;
		}
		if (a_facts.dlssBackend)
			return !a_facts.runtimeFallback;
		if (!a_facts.fsrBackend || a_facts.leftDispatchSerial == 0 ||
			a_facts.rightDispatchSerial == 0) {
			return false;
		}
		return !a_facts.sharedFSRDispatchRequired ||
		       a_facts.leftDispatchSerial == a_facts.rightDispatchSerial;
	}

	[[nodiscard]] constexpr PresentationProofKind ClassifyPresentationProof(
		const PresentationProofFacts& a_facts) noexcept
	{
		if (!a_facts.coherentStereoCycle || !a_facts.publicationCurrent)
			return PresentationProofKind::None;
		if ((a_facts.disposition == PresentationDisposition::ExactVendor ||
				a_facts.disposition == PresentationDisposition::ExactNative) &&
			a_facts.currentProfileMatches && a_facts.exactDimensions &&
			a_facts.vendorDispatchProven) {
			return PresentationProofKind::ExactVendorEvaluation;
		}
		if (a_facts.disposition == PresentationDisposition::ExactNative &&
			a_facts.currentProfileMatches && a_facts.nativeDimensions &&
			a_facts.renderScaleDisabled &&
			a_facts.foveatedVendorDisabled &&
			a_facts.staleVendorGenerationAbsent) {
			return PresentationProofKind::ExactNativePresentation;
		}
		if (a_facts.disposition == PresentationDisposition::CompletedOutputHold &&
			a_facts.currentProfileMatches &&
			a_facts.completedOutputStronglyOwned) {
			return PresentationProofKind::ValidatedCompletedOutputHold;
		}
		return PresentationProofKind::None;
	}

	[[nodiscard]] constexpr bool IsExactTargetProofKind(
		PresentationProofKind a_kind,
		bool a_requiresVendorEvaluation) noexcept
	{
		return a_requiresVendorEvaluation ?
		           a_kind == PresentationProofKind::ExactVendorEvaluation :
		           a_kind == PresentationProofKind::ExactNativePresentation;
	}

	[[nodiscard]] constexpr std::string_view GetPreparationAdmissionName(
		PreparationAdmission a_value) noexcept
	{
		switch (a_value) {
		case PreparationAdmission::Eligible:
			return "eligible";
		case PreparationAdmission::Busy:
			return "busy";
		case PreparationAdmission::Ready:
			return "ready";
		case PreparationAdmission::Failed:
			return "failed";
		case PreparationAdmission::ProtectedFallback:
			return "protected_fallback";
		default:
			return "not_applicable";
		}
	}

	[[nodiscard]] constexpr std::string_view GetMutationAdmissionName(
		MutationAdmission a_value) noexcept
	{
		switch (a_value) {
		case MutationAdmission::Admitted:
			return "admitted";
		case MutationAdmission::Queued:
			return "queued";
		case MutationAdmission::Preparing:
			return "preparing";
		case MutationAdmission::MemoryDeferred:
			return "memory_deferred";
		case MutationAdmission::ShaderDeferred:
			return "shader_deferred";
		case MutationAdmission::ProviderDeferred:
			return "provider_deferred";
		case MutationAdmission::WorkGateDeferred:
			return "work_gate_deferred";
		case MutationAdmission::CleanupOnlyDebt:
			return "cleanup_only_debt";
		case MutationAdmission::PreMutationRecovery:
			return "pre_mutation_recovery";
		case MutationAdmission::PostMutationRecovery:
			return "post_mutation_recovery";
		case MutationAdmission::Failed:
			return "failed";
		case MutationAdmission::Superseded:
			return "superseded";
		default:
			return "not_applicable";
		}
	}

	[[nodiscard]] constexpr std::string_view GetDispositionName(
		PresentationDisposition a_value) noexcept
	{
		switch (a_value) {
		case PresentationDisposition::ExactVendor:
			return "exact_vendor_evaluation";
		case PresentationDisposition::ExactNative:
			return "exact_native_presentation";
		case PresentationDisposition::CompletedOutputHold:
			return "validated_completed_output_hold";
		case PresentationDisposition::PresentationStretch:
			return "presentation_stretch";
		case PresentationDisposition::VendorFailureStretch:
			return "vendor_failure_stretch";
		case PresentationDisposition::BoundsMismatchOriginal:
			return "bounds_mismatch_original";
		case PresentationDisposition::BlackKeepalive:
			return "black_keepalive";
		case PresentationDisposition::Quarantine:
			return "quarantine";
		case PresentationDisposition::Mixed:
			return "mixed";
		default:
			return "none";
		}
	}

	[[nodiscard]] constexpr bool ShouldUpdateLastPreMutation(
		bool a_hasReplacement,
		bool a_physicalMutationStarted,
		bool a_firstPhysicalMutationRecorded) noexcept
	{
		return a_hasReplacement && !a_physicalMutationStarted &&
		       !a_firstPhysicalMutationRecorded;
	}

	struct ProfileIdentity
	{
		bool valid = false;
		std::uint32_t method = 0;
		std::uint32_t qualityMode = 0;
		bool renderScaleMode = false;
		std::uint32_t backend = 0;
		std::uint32_t renderWidth = 0;
		std::uint32_t renderHeight = 0;
		std::uint32_t displayWidth = 0;
		std::uint32_t displayHeight = 0;
	};

	[[nodiscard]] constexpr MutationExpectation DetermineMutationExpectation(
		const ProfileIdentity& a_current,
		const ProfileIdentity& a_replacement) noexcept
	{
		if (!a_current.valid || !a_replacement.valid)
			return MutationExpectation::Unknown;
		return a_current.method == a_replacement.method &&
		               a_current.qualityMode == a_replacement.qualityMode &&
		               a_current.renderScaleMode == a_replacement.renderScaleMode &&
		               a_current.backend == a_replacement.backend &&
		               a_current.renderWidth == a_replacement.renderWidth &&
		               a_current.renderHeight == a_replacement.renderHeight &&
		               a_current.displayWidth == a_replacement.displayWidth &&
		               a_current.displayHeight == a_replacement.displayHeight ?
		           MutationExpectation::NotRequired :
		           MutationExpectation::Required;
	}

	struct NativeTargetMutationFacts
	{
		bool targetRenderScaleMode = false;
		bool currentContractKnown = false;
		bool currentPhysicalContractActive = false;
		bool relatchDecisionKnown = false;
		bool relatchRequiresMutation = false;
		bool physicalMutationRecorded = false;
	};

	[[nodiscard]] constexpr MutationExpectation DetermineNativeTargetMutationExpectation(
		const NativeTargetMutationFacts& a_facts) noexcept
	{
		if (a_facts.targetRenderScaleMode)
			return MutationExpectation::Unknown;
		if (a_facts.physicalMutationRecorded ||
			a_facts.currentPhysicalContractActive ||
			(a_facts.relatchDecisionKnown &&
				a_facts.relatchRequiresMutation)) {
			return MutationExpectation::Required;
		}
		return a_facts.currentContractKnown ?
		           MutationExpectation::NotRequired :
		           MutationExpectation::Unknown;
	}

	[[nodiscard]] constexpr MutationExpectation MergeMutationExpectation(
		MutationExpectation a_current,
		MutationExpectation a_observed) noexcept
	{
		if (a_observed == MutationExpectation::Unknown ||
			a_current == MutationExpectation::Required) {
			return a_current;
		}
		if (a_observed == MutationExpectation::Required ||
			a_current == MutationExpectation::Unknown) {
			return a_observed;
		}
		return a_current;
	}

	struct MutationBoundaryOwnershipFacts
	{
		bool ownerActive = false;
		bool auditActive = false;
		bool stressSessionMatches = false;
		std::uint64_t qualificationTransitionID = 0;
		std::uint64_t ownershipToken = 0;
		std::uint64_t dispatchTick = 0;
		std::uint64_t boundaryTick = 0;
		std::uint64_t dispatchTransitionEpoch = 0;
		std::uint64_t controllerTargetEpoch = 0;
		std::uint64_t boundaryTransitionEpoch = 0;
		std::uint64_t replacementRequestID = 0;
		std::uint64_t replacementTransitionEpoch = 0;
		std::uint32_t replacementContractGeneration = 0;
		std::uintptr_t dispatchDeviceIdentity = 0;
		std::uintptr_t currentDeviceIdentity = 0;
	};

	[[nodiscard]] constexpr bool OwnsMutationBoundary(
		const MutationBoundaryOwnershipFacts& a_facts) noexcept
	{
		return a_facts.ownerActive && a_facts.auditActive &&
		       a_facts.stressSessionMatches &&
		       a_facts.qualificationTransitionID != 0 &&
		       a_facts.ownershipToken != 0 &&
		       a_facts.dispatchTick != 0 &&
		       a_facts.boundaryTick >= a_facts.dispatchTick &&
		       a_facts.boundaryTransitionEpoch != 0 &&
		       a_facts.controllerTargetEpoch ==
		           a_facts.boundaryTransitionEpoch &&
		       a_facts.replacementRequestID != 0 &&
		       a_facts.replacementTransitionEpoch ==
		           a_facts.boundaryTransitionEpoch &&
		       a_facts.dispatchTransitionEpoch !=
		           a_facts.boundaryTransitionEpoch &&
		       a_facts.dispatchDeviceIdentity != 0 &&
		       a_facts.dispatchDeviceIdentity ==
		           a_facts.currentDeviceIdentity;
	}

	[[nodiscard]] constexpr bool MatchesTargetContractGeneration(
		bool a_requiresPublishedGeneration,
		std::uint32_t a_observed,
		std::uint32_t a_expected) noexcept
	{
		return a_observed == a_expected &&
		       (!a_requiresPublishedGeneration || a_observed != 0);
	}

	struct PublishedReplacementProfileFacts
	{
		bool profileValid = false;
		bool requiresPublishedGeneration = false;
		std::uint64_t observedTransitionEpoch = 0;
		std::uint64_t expectedTransitionEpoch = 0;
		std::uint32_t observedContractGeneration = 0;
		std::uint32_t expectedContractGeneration = 0;
		std::uint32_t observedMethod = 0;
		std::uint32_t expectedMethod = 0;
		std::uint32_t observedRenderWidth = 0;
		std::uint32_t observedRenderHeight = 0;
		std::uint32_t observedDisplayWidth = 0;
		std::uint32_t observedDisplayHeight = 0;
		std::uint32_t expectedRenderWidth = 0;
		std::uint32_t expectedRenderHeight = 0;
		std::uint32_t expectedDisplayWidth = 0;
		std::uint32_t expectedDisplayHeight = 0;
		std::uintptr_t observedDeviceIdentity = 0;
		std::uintptr_t currentDeviceIdentity = 0;
		std::uint64_t observedResourceRevision = 0;
	};

	[[nodiscard]] constexpr bool MatchesPublishedReplacementProfile(
		const PublishedReplacementProfileFacts& a_facts) noexcept
	{
		return a_facts.profileValid &&
		       a_facts.observedTransitionEpoch ==
		           a_facts.expectedTransitionEpoch &&
		       MatchesTargetContractGeneration(
				   a_facts.requiresPublishedGeneration,
				   a_facts.observedContractGeneration,
				   a_facts.expectedContractGeneration) &&
		       a_facts.observedMethod == a_facts.expectedMethod &&
		       a_facts.observedRenderWidth == a_facts.expectedRenderWidth &&
		       a_facts.observedRenderHeight == a_facts.expectedRenderHeight &&
		       a_facts.observedDisplayWidth == a_facts.expectedDisplayWidth &&
		       a_facts.observedDisplayHeight == a_facts.expectedDisplayHeight &&
		       a_facts.observedDeviceIdentity != 0 &&
		       a_facts.observedDeviceIdentity ==
		           a_facts.currentDeviceIdentity &&
		       a_facts.observedResourceRevision != 0;
	}

	[[nodiscard]] constexpr bool MatchesMutationBoundaryGeneration(
		bool a_requiresPublishedGeneration,
		std::uint32_t a_boundary,
		std::uint32_t a_published) noexcept
	{
		if (!a_requiresPublishedGeneration)
			return a_published == 0;
		return a_published != 0 &&
		       (a_boundary == 0 || a_boundary == a_published);
	}

	inline constexpr std::uint64_t kPreparationNotApplicableReasonMask =
		(1ull << 1) | (1ull << 2) | (1ull << 3) | (1ull << 4);

	[[nodiscard]] constexpr bool IsPreparationNotApplicable(
		std::uint64_t a_reasonMask) noexcept
	{
		return (a_reasonMask & kPreparationNotApplicableReasonMask) != 0;
	}

	struct MutationAdmissionFacts
	{
		bool hasReplacement = false;
		bool superseded = false;
		bool failed = false;
		bool physicalMutationStarted = false;
		bool recoveryActive = false;
		bool memoryDeferred = false;
		bool shaderDeferred = false;
		bool providerDeferred = false;
		bool workGateDeferred = false;
		bool cleanupDebt = false;
		bool preparing = false;
		bool queued = false;
	};

	[[nodiscard]] constexpr MutationAdmission ClassifyMutationAdmission(
		const MutationAdmissionFacts& a_facts) noexcept
	{
		if (!a_facts.hasReplacement)
			return a_facts.cleanupDebt ? MutationAdmission::CleanupOnlyDebt :
			                             MutationAdmission::NotApplicable;
		if (a_facts.superseded)
			return MutationAdmission::Superseded;
		if (a_facts.failed)
			return MutationAdmission::Failed;
		if (a_facts.recoveryActive)
			return a_facts.physicalMutationStarted ?
			           MutationAdmission::PostMutationRecovery :
			           MutationAdmission::PreMutationRecovery;
		if (a_facts.memoryDeferred)
			return MutationAdmission::MemoryDeferred;
		if (a_facts.shaderDeferred)
			return MutationAdmission::ShaderDeferred;
		if (a_facts.providerDeferred)
			return MutationAdmission::ProviderDeferred;
		if (a_facts.workGateDeferred)
			return MutationAdmission::WorkGateDeferred;
		if (a_facts.physicalMutationStarted)
			return MutationAdmission::Admitted;
		if (a_facts.preparing)
			return MutationAdmission::Preparing;
		if (a_facts.queued)
			return MutationAdmission::Queued;
		return MutationAdmission::Admitted;
	}

	struct EyeObservation
	{
		bool valid = false;
		std::uint32_t eyeIndex = 0;
		std::uint32_t frame = 0;
		std::uint64_t qpcTick = 0;
		std::uint64_t compositorCycleToken = 0;
		std::uint64_t requestID = 0;
		std::uint64_t transitionEpoch = 0;
		std::uint32_t contractGeneration = 0;
		std::uint32_t providerGeneration = 0;
		std::uint64_t publicationGeneration = 0;
		std::uint64_t resourceRevision = 0;
		std::uintptr_t deviceIdentity = 0;
		std::uint32_t renderWidth = 0;
		std::uint32_t renderHeight = 0;
		std::uint32_t displayWidth = 0;
		std::uint32_t displayHeight = 0;
		std::uint32_t method = 0;
		std::uint32_t qualityMode = 0;
		bool renderScaleMode = false;
		std::uint32_t backend = 0;
		std::uint32_t vendorDispatchFrame = 0;
		std::uint64_t vendorDispatchSerial = 0;
		bool vendorRuntimeFallback = false;
		bool vendorDispatchProven = false;
		bool sharedVendorDispatchRequired = false;
		PresentationDisposition disposition = PresentationDisposition::None;
		bool loadingOrMenuContext = false;
		bool transitionCooldown = false;
		bool submitted = false;
		bool exactCurrent = false;
		bool exactCurrentPresentationAvailable = false;
		bool exactReplacement = false;
		bool blockedPreMutation = false;
		bool physicalMutationStarted = false;
	};

	struct CompleteCycle
	{
		bool valid = false;
		bool coherent = false;
		bool submitted = false;
		bool beforeMutation = true;
		bool afterMutation = false;
		bool boundarySpanning = false;
		bool exactCurrent = false;
		bool exactCurrentPresentationAvailable = false;
		bool exactReplacement = false;
		bool blockedPreMutation = false;
		std::uint32_t frame = 0;
		std::uint64_t qpcTick = 0;
		std::uint32_t leftFrame = 0;
		std::uint64_t leftQpcTick = 0;
		std::uint32_t rightFrame = 0;
		std::uint64_t rightQpcTick = 0;
		std::uint64_t compositorCycleToken = 0;
		std::uint64_t requestID = 0;
		std::uint64_t transitionEpoch = 0;
		std::uint32_t contractGeneration = 0;
		std::uint32_t providerGeneration = 0;
		std::uint64_t publicationGeneration = 0;
		std::uint64_t resourceRevision = 0;
		std::uintptr_t deviceIdentity = 0;
		std::uint32_t renderWidth = 0;
		std::uint32_t renderHeight = 0;
		std::uint32_t displayWidth = 0;
		std::uint32_t displayHeight = 0;
		std::uint32_t method = 0;
		std::uint32_t qualityMode = 0;
		bool renderScaleMode = false;
		std::uint32_t backend = 0;
		std::uint32_t leftVendorDispatchFrame = 0;
		std::uint64_t leftVendorDispatchSerial = 0;
		std::uint32_t rightVendorDispatchFrame = 0;
		std::uint64_t rightVendorDispatchSerial = 0;
		bool vendorRuntimeFallback = false;
		bool vendorDispatchProven = false;
		bool sharedVendorDispatchRequired = false;
		PresentationDisposition disposition = PresentationDisposition::None;
		bool loadingOrMenuContext = false;
		bool transitionCooldown = false;
	};

	struct FirstOffender
	{
		bool valid = false;
		CompleteCycle cycle{};
	};

	struct AuditCounters
	{
		std::uint64_t eyeObservations = 0;
		std::uint64_t completeStereoCyclesBeforeMutation = 0;
		std::uint64_t blockedPreMutationCycles = 0;
		std::uint64_t exactPreviousGenerationCycles = 0;
		std::uint64_t suppressedExactPreviousGenerationCycles = 0;
		std::uint64_t presentationStretchCyclesBeforeMutation = 0;
		std::uint64_t blackKeepaliveCyclesBeforeMutation = 0;
		std::uint64_t quarantineCyclesBeforeMutation = 0;
		std::uint64_t completeStereoCyclesAfterMutation = 0;
		std::uint64_t boundarySpanningStereoCycles = 0;
		std::uint64_t oldGenerationEvaluationsAfterMutation = 0;
		std::uint64_t oldGenerationCompletedOutputReuseAfterMutation = 0;
		std::uint64_t mixedOrUnprovenStereoPairsSubmitted = 0;
		std::uint64_t firstExactNewGenerationCycles = 0;
		std::array<std::uint64_t, 10> dispositionsBeforeMutation{};
		std::array<std::uint64_t, 10> dispositionsAfterMutation{};
		std::uint64_t preMutationExactPresentationSuppressed = 0;
		std::uint64_t preMutationStretchWithoutMutation = 0;
		std::uint64_t postMutationOldGenerationPresented = 0;
		std::uint64_t postMutationUnprovenStereoSubmitted = 0;
		FirstOffender firstPreMutationExactPresentationSuppressed{};
		FirstOffender firstPreMutationStretchWithoutMutation{};
		FirstOffender firstPostMutationOldGenerationPresented{};
		FirstOffender firstPostMutationUnprovenStereoSubmitted{};
	};

	struct PendingCycle
	{
		bool active = false;
		std::uint64_t compositorCycleToken = 0;
		std::uint8_t eyeMask = 0;
		std::array<EyeObservation, 2> eyes{};
	};

	struct AuditState
	{
		bool active = false;
		bool evidenceComplete = true;
		bool retentionOverflow = false;
		bool physicalMutationObserved = false;
		std::uint64_t ownerTransitionID = 0;
		std::uint64_t ownerToken = 0;
		AuditCounters counters{};
		std::array<PendingCycle, kPendingCycleCapacity> pending{};
	};

	struct MutationBoundaryTimestamp
	{
		bool valid = false;
		std::uint32_t frame = 0;
		std::uint64_t qpcTick = 0;
	};

	[[nodiscard]] constexpr bool IsAtOrAfterMutationBoundary(
		std::uint32_t a_frame,
		std::uint64_t a_qpcTick,
		const MutationBoundaryTimestamp& a_boundary) noexcept
	{
		return a_boundary.valid && a_boundary.frame != 0 &&
		       a_boundary.qpcTick != 0 && a_frame >= a_boundary.frame &&
		       a_qpcTick >= a_boundary.qpcTick;
	}

	constexpr void SaturatingIncrement(std::uint64_t& a_value) noexcept
	{
		if (a_value != std::numeric_limits<std::uint64_t>::max())
			++a_value;
	}

	[[nodiscard]] constexpr std::uint64_t CountIncompleteStereoCycles(
		const AuditState& a_state) noexcept
	{
		std::uint64_t count = 0;
		for (const auto& pending : a_state.pending) {
			if (pending.active && pending.eyeMask != 0x3)
				SaturatingIncrement(count);
		}
		return count;
	}

	[[nodiscard]] constexpr bool SameSubmittedIdentity(
		const EyeObservation& a_left,
		const EyeObservation& a_right) noexcept
	{
		return a_left.valid && a_right.valid &&
		       a_left.compositorCycleToken != 0 &&
		       a_left.compositorCycleToken == a_right.compositorCycleToken &&
		       a_left.frame == a_right.frame &&
		       a_left.requestID == a_right.requestID &&
		       a_left.transitionEpoch == a_right.transitionEpoch &&
		       a_left.contractGeneration == a_right.contractGeneration &&
		       a_left.providerGeneration == a_right.providerGeneration &&
		       a_left.publicationGeneration == a_right.publicationGeneration &&
		       a_left.resourceRevision == a_right.resourceRevision &&
		       a_left.deviceIdentity != 0 &&
		       a_left.deviceIdentity == a_right.deviceIdentity &&
		       a_left.renderWidth != 0 &&
		       a_left.renderWidth == a_right.renderWidth &&
		       a_left.renderHeight != 0 &&
		       a_left.renderHeight == a_right.renderHeight &&
		       a_left.displayWidth != 0 &&
		       a_left.displayWidth == a_right.displayWidth &&
		       a_left.displayHeight != 0 &&
		       a_left.displayHeight == a_right.displayHeight &&
		       a_left.method == a_right.method &&
		       a_left.qualityMode == a_right.qualityMode &&
		       a_left.renderScaleMode == a_right.renderScaleMode &&
		       a_left.backend == a_right.backend &&
		       a_left.vendorRuntimeFallback == a_right.vendorRuntimeFallback &&
		       a_left.vendorDispatchProven == a_right.vendorDispatchProven &&
		       a_left.sharedVendorDispatchRequired ==
		           a_right.sharedVendorDispatchRequired &&
		       (!a_left.sharedVendorDispatchRequired ||
				   a_left.vendorDispatchSerial == a_right.vendorDispatchSerial) &&
		       a_left.disposition == a_right.disposition &&
		       a_left.loadingOrMenuContext == a_right.loadingOrMenuContext &&
		       a_left.transitionCooldown == a_right.transitionCooldown;
	}

	constexpr CompleteCycle BuildCompleteCycle(
		const EyeObservation& a_left,
		const EyeObservation& a_right) noexcept
	{
		CompleteCycle cycle{};
		cycle.valid = a_left.valid && a_right.valid;
		cycle.boundarySpanning =
			a_left.physicalMutationStarted != a_right.physicalMutationStarted;
		cycle.coherent = SameSubmittedIdentity(a_left, a_right) &&
		                 !cycle.boundarySpanning;
		cycle.submitted = a_left.submitted && a_right.submitted;
		cycle.beforeMutation = !a_left.physicalMutationStarted &&
		                       !a_right.physicalMutationStarted;
		cycle.afterMutation = a_left.physicalMutationStarted &&
		                      a_right.physicalMutationStarted;
		cycle.exactCurrent = a_left.exactCurrent && a_right.exactCurrent;
		cycle.exactCurrentPresentationAvailable =
			a_left.exactCurrentPresentationAvailable &&
			a_right.exactCurrentPresentationAvailable;
		cycle.exactReplacement = a_left.exactReplacement &&
		                         a_right.exactReplacement;
		cycle.blockedPreMutation = a_left.blockedPreMutation ||
		                           a_right.blockedPreMutation;
		cycle.leftFrame = a_left.frame;
		cycle.leftQpcTick = a_left.qpcTick;
		cycle.rightFrame = a_right.frame;
		cycle.rightQpcTick = a_right.qpcTick;
		cycle.frame = a_left.frame > a_right.frame ? a_left.frame : a_right.frame;
		cycle.qpcTick = a_left.qpcTick > a_right.qpcTick ?
		                    a_left.qpcTick :
		                    a_right.qpcTick;
		cycle.compositorCycleToken = a_right.compositorCycleToken;
		cycle.requestID = a_right.requestID;
		cycle.transitionEpoch = a_right.transitionEpoch;
		cycle.contractGeneration = a_right.contractGeneration;
		cycle.providerGeneration = a_right.providerGeneration;
		cycle.publicationGeneration = a_right.publicationGeneration;
		cycle.resourceRevision = a_right.resourceRevision;
		cycle.deviceIdentity = a_right.deviceIdentity;
		cycle.renderWidth = a_right.renderWidth;
		cycle.renderHeight = a_right.renderHeight;
		cycle.displayWidth = a_right.displayWidth;
		cycle.displayHeight = a_right.displayHeight;
		cycle.method = a_right.method;
		cycle.qualityMode = a_right.qualityMode;
		cycle.renderScaleMode = a_right.renderScaleMode;
		cycle.backend = a_right.backend;
		cycle.leftVendorDispatchFrame = a_left.vendorDispatchFrame;
		cycle.leftVendorDispatchSerial = a_left.vendorDispatchSerial;
		cycle.rightVendorDispatchFrame = a_right.vendorDispatchFrame;
		cycle.rightVendorDispatchSerial = a_right.vendorDispatchSerial;
		cycle.vendorRuntimeFallback = a_right.vendorRuntimeFallback;
		cycle.vendorDispatchProven = a_left.vendorDispatchProven &&
		                             a_right.vendorDispatchProven;
		cycle.sharedVendorDispatchRequired =
			a_right.sharedVendorDispatchRequired;
		cycle.disposition = cycle.coherent ? a_right.disposition :
		                                     PresentationDisposition::Mixed;
		cycle.loadingOrMenuContext = a_left.loadingOrMenuContext ||
		                             a_right.loadingOrMenuContext;
		cycle.transitionCooldown = a_left.transitionCooldown ||
		                           a_right.transitionCooldown;
		return cycle;
	}

	constexpr void RecordFirstOffender(
		FirstOffender& a_offender,
		const CompleteCycle& a_cycle) noexcept
	{
		if (!a_offender.valid) {
			a_offender.valid = true;
			a_offender.cycle = a_cycle;
		}
	}

	constexpr void RecordCompleteCycle(
		AuditState& a_state,
		const CompleteCycle& a_cycle) noexcept
	{
		if (!a_cycle.valid)
			return;
		auto& counters = a_state.counters;
		const auto dispositionIndex = static_cast<std::size_t>(a_cycle.disposition);
		if (a_cycle.beforeMutation) {
			SaturatingIncrement(counters.completeStereoCyclesBeforeMutation);
			if (a_cycle.blockedPreMutation)
				SaturatingIncrement(counters.blockedPreMutationCycles);
			if (dispositionIndex < counters.dispositionsBeforeMutation.size())
				SaturatingIncrement(counters.dispositionsBeforeMutation[dispositionIndex]);
			if (a_cycle.exactCurrent && a_cycle.coherent && a_cycle.submitted)
				SaturatingIncrement(counters.exactPreviousGenerationCycles);
			if (a_cycle.exactCurrentPresentationAvailable &&
				(!a_cycle.submitted || a_cycle.disposition == PresentationDisposition::BlackKeepalive ||
					a_cycle.disposition == PresentationDisposition::Quarantine)) {
				SaturatingIncrement(counters.suppressedExactPreviousGenerationCycles);
				SaturatingIncrement(counters.preMutationExactPresentationSuppressed);
				RecordFirstOffender(
					counters.firstPreMutationExactPresentationSuppressed, a_cycle);
			}
			if (a_cycle.disposition == PresentationDisposition::PresentationStretch) {
				SaturatingIncrement(counters.presentationStretchCyclesBeforeMutation);
				if (a_cycle.exactCurrentPresentationAvailable) {
					SaturatingIncrement(counters.preMutationStretchWithoutMutation);
					RecordFirstOffender(
						counters.firstPreMutationStretchWithoutMutation, a_cycle);
				}
			}
			if (a_cycle.disposition == PresentationDisposition::BlackKeepalive)
				SaturatingIncrement(counters.blackKeepaliveCyclesBeforeMutation);
			if (a_cycle.disposition == PresentationDisposition::Quarantine)
				SaturatingIncrement(counters.quarantineCyclesBeforeMutation);
		} else {
			if (a_cycle.boundarySpanning) {
				SaturatingIncrement(counters.boundarySpanningStereoCycles);
			} else if (a_cycle.afterMutation) {
				SaturatingIncrement(counters.completeStereoCyclesAfterMutation);
				if (dispositionIndex < counters.dispositionsAfterMutation.size())
					SaturatingIncrement(counters.dispositionsAfterMutation[dispositionIndex]);
			}
			if (a_cycle.afterMutation && a_cycle.exactCurrent) {
				if (a_cycle.disposition == PresentationDisposition::CompletedOutputHold)
					SaturatingIncrement(counters.oldGenerationCompletedOutputReuseAfterMutation);
				else
					SaturatingIncrement(counters.oldGenerationEvaluationsAfterMutation);
				SaturatingIncrement(counters.postMutationOldGenerationPresented);
				RecordFirstOffender(
					counters.firstPostMutationOldGenerationPresented, a_cycle);
			}
			const bool protectedCooldownStretch =
				a_cycle.afterMutation && a_cycle.coherent && a_cycle.transitionCooldown &&
				a_cycle.disposition == PresentationDisposition::PresentationStretch;
			if (a_cycle.submitted && (!a_cycle.coherent ||
										 (!a_cycle.exactReplacement && !a_cycle.exactCurrent))) {
				SaturatingIncrement(counters.mixedOrUnprovenStereoPairsSubmitted);
				if (!protectedCooldownStretch) {
					SaturatingIncrement(counters.postMutationUnprovenStereoSubmitted);
					RecordFirstOffender(
						counters.firstPostMutationUnprovenStereoSubmitted, a_cycle);
				}
			}
			if (a_cycle.afterMutation && a_cycle.exactReplacement &&
				a_cycle.coherent && a_cycle.submitted &&
				counters.firstExactNewGenerationCycles == 0) {
				SaturatingIncrement(counters.firstExactNewGenerationCycles);
			}
		}
	}

	[[nodiscard]] constexpr bool ObserveEye(
		AuditState& a_state,
		std::uint64_t a_transitionID,
		std::uint64_t a_ownerToken,
		const EyeObservation& a_observation,
		CompleteCycle& a_completed) noexcept
	{
		a_completed = {};
		if (!a_state.active || a_transitionID == 0 || a_ownerToken == 0 ||
			a_state.ownerTransitionID != a_transitionID ||
			a_state.ownerToken != a_ownerToken || !a_observation.valid ||
			a_observation.eyeIndex >= 2 ||
			a_observation.compositorCycleToken == 0) {
			return false;
		}
		const EyeObservation observation = a_observation;
		if (observation.physicalMutationStarted)
			a_state.physicalMutationObserved = true;

		PendingCycle* slot = nullptr;
		for (auto& pending : a_state.pending) {
			if (pending.active && pending.compositorCycleToken ==
									  observation.compositorCycleToken) {
				slot = &pending;
				break;
			}
		}
		if (!slot) {
			for (auto& pending : a_state.pending) {
				if (!pending.active) {
					pending = {};
					pending.active = true;
					pending.compositorCycleToken =
						observation.compositorCycleToken;
					slot = &pending;
					break;
				}
			}
		}
		if (!slot) {
			a_state.retentionOverflow = true;
			a_state.evidenceComplete = false;
			return false;
		}

		const std::uint8_t eyeBit = static_cast<std::uint8_t>(1u << observation.eyeIndex);
		if ((slot->eyeMask & eyeBit) == 0)
			SaturatingIncrement(a_state.counters.eyeObservations);
		slot->eyes[observation.eyeIndex] = observation;
		slot->eyeMask = static_cast<std::uint8_t>(slot->eyeMask | eyeBit);
		if (slot->eyeMask != 0x3)
			return false;

		a_completed = BuildCompleteCycle(slot->eyes[0], slot->eyes[1]);
		*slot = {};
		RecordCompleteCycle(a_state, a_completed);
		return true;
	}

	[[nodiscard]] constexpr bool HasInvariantViolation(
		const AuditCounters& a_counters) noexcept
	{
		return a_counters.preMutationExactPresentationSuppressed != 0 ||
		       a_counters.preMutationStretchWithoutMutation != 0 ||
		       a_counters.postMutationOldGenerationPresented != 0 ||
		       a_counters.postMutationUnprovenStereoSubmitted != 0;
	}
}
