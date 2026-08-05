#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace VRVendorRelatchPolicy
{
	using WorkGateMask = std::uint32_t;
	using WorkGateState = std::uint64_t;

	enum class WorkGateSource : WorkGateMask
	{
		None = 0,
		ProcessStartup = 1u << 0,
		MainMenu = 1u << 1,
		LoadingMenu = 1u << 2,
		PreLoadGame = 1u << 3,
		GameLoadNotification = 1u << 4
	};

	inline constexpr WorkGateMask kNoWorkGateSources = 0;
	inline constexpr WorkGateMask kGameEntryWorkGateSources =
		static_cast<WorkGateMask>(WorkGateSource::ProcessStartup) |
		static_cast<WorkGateMask>(WorkGateSource::MainMenu) |
		static_cast<WorkGateMask>(WorkGateSource::PreLoadGame) |
		static_cast<WorkGateMask>(WorkGateSource::GameLoadNotification);
	inline constexpr WorkGateMask kAllWorkGateSources =
		kGameEntryWorkGateSources |
		static_cast<WorkGateMask>(WorkGateSource::LoadingMenu);
	inline constexpr std::array kWorkGateSources{
		WorkGateSource::ProcessStartup,
		WorkGateSource::MainMenu,
		WorkGateSource::LoadingMenu,
		WorkGateSource::PreLoadGame,
		WorkGateSource::GameLoadNotification,
	};
	inline constexpr std::uint32_t kWorkGateStateMaskBits = 32u;

	[[nodiscard]] constexpr WorkGateMask ToMask(WorkGateSource a_source) noexcept
	{
		return static_cast<WorkGateMask>(a_source);
	}

	[[nodiscard]] constexpr std::string_view GetWorkGateSourceName(
		WorkGateSource a_source) noexcept
	{
		switch (a_source) {
		case WorkGateSource::ProcessStartup:
			return "process_startup";
		case WorkGateSource::MainMenu:
			return "main_menu";
		case WorkGateSource::LoadingMenu:
			return "loading_menu";
		case WorkGateSource::PreLoadGame:
			return "pre_load_game";
		case WorkGateSource::GameLoadNotification:
			return "game_load_notification";
		default:
			return "none";
		}
	}

	[[nodiscard]] constexpr WorkGateMask GetStateMask(WorkGateState a_state) noexcept
	{
		return static_cast<WorkGateMask>(a_state);
	}

	[[nodiscard]] constexpr std::uint32_t GetStateEpoch(WorkGateState a_state) noexcept
	{
		return static_cast<std::uint32_t>(a_state >> kWorkGateStateMaskBits);
	}

	[[nodiscard]] constexpr WorkGateState AdvanceState(
		WorkGateState a_previous,
		WorkGateMask a_nextMask) noexcept
	{
		const auto nextEpoch = static_cast<std::uint32_t>(GetStateEpoch(a_previous) + 1u);
		return (static_cast<WorkGateState>(nextEpoch) << kWorkGateStateMaskBits) |
		       static_cast<WorkGateState>(a_nextMask);
	}

	[[nodiscard]] constexpr bool HasAny(WorkGateMask a_sources) noexcept
	{
		return a_sources != kNoWorkGateSources;
	}

	[[nodiscard]] constexpr bool HasAny(
		WorkGateMask a_sources,
		WorkGateMask a_candidates) noexcept
	{
		return (a_sources & a_candidates) != kNoWorkGateSources;
	}

	[[nodiscard]] constexpr bool HasSource(
		WorkGateMask a_sources,
		WorkGateSource a_source) noexcept
	{
		return HasAny(a_sources, ToMask(a_source));
	}

	[[nodiscard]] constexpr WorkGateMask AcquireSource(
		WorkGateMask a_sources,
		WorkGateSource a_source) noexcept
	{
		return a_sources | ToMask(a_source);
	}

	[[nodiscard]] constexpr WorkGateMask ReleaseSource(
		WorkGateMask a_sources,
		WorkGateSource a_source) noexcept
	{
		return a_sources & ~ToMask(a_source);
	}

	struct GameEntryConvergence
	{
		bool hasGateOwner = false;
		bool mainMenuActive = false;
		bool loadingPresentationActive = false;
		bool raceSexPresentationActive = false;
		bool saveLoadProtectionActive = false;
		bool completedWorldFrame = false;
		bool recoveryPending = false;
		bool relatchPending = false;
		bool profileTransitionPending = false;
	};

	[[nodiscard]] constexpr bool CanReleaseGameEntryVendorGate(
		const GameEntryConvergence& a_state) noexcept
	{
		return a_state.hasGateOwner &&
		       !a_state.mainMenuActive &&
		       !a_state.loadingPresentationActive &&
		       !a_state.raceSexPresentationActive &&
		       !a_state.saveLoadProtectionActive &&
		       a_state.completedWorldFrame &&
		       !a_state.recoveryPending &&
		       !a_state.relatchPending &&
		       !a_state.profileTransitionPending;
	}

	struct StabilizerDestinationSyncReadiness
	{
		bool completedWorldFrameAfterClose = false;
		bool sourceCellKnown = false;
		bool currentCellKnown = false;
		bool destinationCellChanged = false;
		bool completedWorldFrameAfterDestinationObservation = false;
		bool identityFallbackElapsed = false;
	};

	[[nodiscard]] constexpr uint32_t SelectStabilizerSourceCell(
		uint32_t a_lastResolvedCell,
		uint32_t a_currentPlayerCell) noexcept
	{
		return a_lastResolvedCell != 0 ? a_lastResolvedCell : a_currentPlayerCell;
	}

	[[nodiscard]] constexpr bool ShouldPublishStabilizerDestinationProfile(
		bool a_settingsMatch,
		bool a_controllerCurrentTargetMatches) noexcept
	{
		return !a_settingsMatch || !a_controllerCurrentTargetMatches;
	}

	struct StabilizerControllerTargetAdmission
	{
		bool profileValid = false;
		bool targetEpochKnown = false;
		bool profileOwnsTargetEpoch = false;
		bool methodMatches = false;
		bool qualityMatches = false;
		bool renderScaleModeMatches = false;
		bool dlssPresetMatchesOrIrrelevant = false;
	};

	[[nodiscard]] constexpr bool MatchesStabilizerControllerTarget(
		const StabilizerControllerTargetAdmission& a_admission) noexcept
	{
		return a_admission.profileValid &&
		       a_admission.targetEpochKnown &&
		       a_admission.profileOwnsTargetEpoch &&
		       a_admission.methodMatches &&
		       a_admission.qualityMatches &&
		       a_admission.renderScaleModeMatches &&
		       a_admission.dlssPresetMatchesOrIrrelevant;
	}

	[[nodiscard]] constexpr bool IsStabilizerDestinationSyncReady(
		const StabilizerDestinationSyncReadiness& a_state) noexcept
	{
		if (!a_state.completedWorldFrameAfterClose)
			return false;
		// Startup and early save-load paths may not have a source cell to capture.
		// Retain their existing completed-world-frame readiness contract.
		if (!a_state.sourceCellKnown)
			return true;

		if (!a_state.currentCellKnown)
			return a_state.identityFallbackElapsed;

		if (a_state.destinationCellChanged)
			return a_state.completedWorldFrameAfterDestinationObservation;

		// A real same-cell load cannot prove a destination identity change. It may
		// fail open only after the bounded transition fallback has elapsed.
		return a_state.identityFallbackElapsed;
	}

	struct LifecycleMutationAdmission
	{
		bool isVR = false;
		WorkGateMask gateSources = kNoWorkGateSources;
		bool postLoadResetPending = false;
		bool relatchPending = false;
		bool relatchInProgress = false;
	};

	[[nodiscard]] constexpr bool CanMutateVendorLifecycle(
		const LifecycleMutationAdmission& a_state) noexcept
	{
		return !a_state.isVR ||
		       (!HasAny(a_state.gateSources) &&
				   !a_state.postLoadResetPending &&
				   !a_state.relatchPending &&
				   !a_state.relatchInProgress);
	}

	struct DispatchAdmission
	{
		bool isVR = false;
		bool vendorEvaluationSelected = false;
		bool resourcesReady = false;
		bool relatchInProgress = false;
	};

	[[nodiscard]] constexpr bool CanDispatchVendorEvaluation(
		const DispatchAdmission& a_state) noexcept
	{
		return a_state.vendorEvaluationSelected &&
		       a_state.resourcesReady &&
		       (!a_state.isVR || !a_state.relatchInProgress);
	}

	struct NativeRestorePresentationAdmission
	{
		bool targetUsesVendorEvaluation = false;
		bool exactPhysicalNativeContinuity = false;
		bool exactRuntimeContract = false;
	};

	[[nodiscard]] constexpr bool CanAcceptNativeRestorePresentation(
		const NativeRestorePresentationAdmission& a_admission) noexcept
	{
		// A vendor-evaluated 1:1 target still needs the exact runtime provider
		// contract. A non-vendor target does not: once the controller owns a
		// publishable native target generation and OpenVR is receiving that exact
		// engine texture, lagging resolution-plan metadata cannot make the physical
		// frame stale again.
		return a_admission.exactPhysicalNativeContinuity &&
		       (a_admission.exactRuntimeContract ||
				   !a_admission.targetUsesVendorEvaluation);
	}

	inline constexpr std::uint32_t
		kNativeRestoreMaximumPhysicalRecoveryAttempts = 1u;
	inline constexpr std::uint32_t
		kNativeRestoreMaximumRecoveryAttempts = 2u;

	[[nodiscard]] constexpr bool
	CanScheduleNativeRestorePresentationRecovery(
		std::uint32_t a_completedAttempts,
		bool a_candidateAlreadyPending,
		bool a_duplicateCompositorCycle) noexcept
	{
		return a_completedAttempts < kNativeRestoreMaximumRecoveryAttempts &&
		       !a_candidateAlreadyPending &&
		       !a_duplicateCompositorCycle;
	}

	enum class NativeRestorePresentationRecoveryAction : std::uint8_t
	{
		PhysicalRetry,
		LogicalFallback,
		Exhausted
	};

	[[nodiscard]] constexpr NativeRestorePresentationRecoveryAction
	SelectNativeRestorePresentationRecoveryAction(
		std::uint32_t a_attempt) noexcept
	{
		if (a_attempt == 0 ||
			a_attempt > kNativeRestoreMaximumRecoveryAttempts) {
			return NativeRestorePresentationRecoveryAction::Exhausted;
		}
		return a_attempt <= kNativeRestoreMaximumPhysicalRecoveryAttempts ?
		           NativeRestorePresentationRecoveryAction::PhysicalRetry :
		           NativeRestorePresentationRecoveryAction::LogicalFallback;
	}

	[[nodiscard]] constexpr bool ShouldDeferPhysicalRelatchForStereo(
		std::uint32_t a_currentFrame,
		std::uint32_t a_admissionFrame,
		std::uint32_t a_submittedEyeMask,
		std::uint64_t a_relatchEpoch,
		std::uint64_t a_deferredRelatchEpoch) noexcept
	{
		const std::uint32_t stereoMask = a_submittedEyeMask & 0x3u;
		return a_relatchEpoch != 0 &&
		       a_relatchEpoch != a_deferredRelatchEpoch &&
		       a_admissionFrame == a_currentFrame &&
		       (stereoMask == 0x1u || stereoMask == 0x2u);
	}

	struct BufferedDoorRequestCoalescingAdmission
	{
		bool existingRequestValid = false;
		bool incomingRequestValid = false;
		bool existingBufferedDoorHandoff = false;
		bool incomingBufferedDoorHandoff = false;
		bool sameOrigin = false;
		bool sameCompleteTarget = false;
		std::uint64_t existingLoadingSerial = 0;
		std::uint64_t incomingLoadingSerial = 0;
	};

	[[nodiscard]] constexpr bool CanCoalesceBufferedDoorRequest(
		const BufferedDoorRequestCoalescingAdmission& a_admission) noexcept
	{
		return a_admission.existingRequestValid &&
		       a_admission.incomingRequestValid &&
		       a_admission.existingBufferedDoorHandoff &&
		       a_admission.incomingBufferedDoorHandoff &&
		       a_admission.sameOrigin &&
		       a_admission.sameCompleteTarget &&
		       a_admission.existingLoadingSerial != 0 &&
		       a_admission.existingLoadingSerial ==
		           a_admission.incomingLoadingSerial;
	}

	enum class NativeRestorePhase : std::uint8_t
	{
		Idle,
		VendorTeardownPending,
		SharedRetirementPending,
		Complete
	};

	enum class NativeRestoreAction : std::uint8_t
	{
		BeginVendorTeardown,
		PollVendorTeardown,
		DrainPreviousRetirement,
		DrainCurrentRetirement,
		RecreatePhysicalTargets
	};

	enum class NativeRestoreTeardownOutcome : std::uint8_t
	{
		Ready,
		Pending,
		FailedBeforeRelease,
		FailedAfterMutation
	};

	enum class NativeRestoreTeardownDisposition : std::uint8_t
	{
		Complete,
		Retry,
		Abort
	};

	[[nodiscard]] constexpr NativeRestoreTeardownDisposition SelectNativeRestoreTeardownDisposition(
		NativeRestoreTeardownOutcome a_outcome) noexcept
	{
		switch (a_outcome) {
		case NativeRestoreTeardownOutcome::Ready:
			return NativeRestoreTeardownDisposition::Complete;
		case NativeRestoreTeardownOutcome::Pending:
			return NativeRestoreTeardownDisposition::Retry;
		case NativeRestoreTeardownOutcome::FailedBeforeRelease:
		case NativeRestoreTeardownOutcome::FailedAfterMutation:
			// A terminal failure must preserve the still-live engine targets. The
			// caller can rebuild presentation intermediates and stretch that source,
			// or degrade to the original submit if allocation also fails, but it must
			// not advance to physical target recreation.
			return NativeRestoreTeardownDisposition::Abort;
		default:
			return NativeRestoreTeardownDisposition::Abort;
		}
	}

	struct NativeRestoreOperation
	{
		bool valid = false;
		bool destroyDLSSResources = false;
		bool destroyPeripheryTAAResources = false;
		bool destroyFSRResources = false;
		bool waitForFSRIdleTeardown = false;
		bool destroySharedResources = false;
		bool preserveVRIntermediateTextures = false;

		[[nodiscard]] constexpr bool Covers(
			const NativeRestoreOperation& a_required) const noexcept
		{
			return valid && a_required.valid &&
			       (!a_required.destroyDLSSResources || destroyDLSSResources) &&
			       (!a_required.destroyPeripheryTAAResources || destroyPeripheryTAAResources) &&
			       (!a_required.destroyFSRResources || destroyFSRResources) &&
			       (!a_required.waitForFSRIdleTeardown || waitForFSRIdleTeardown) &&
			       (!a_required.destroySharedResources ||
					   (destroySharedResources &&
						   (a_required.preserveVRIntermediateTextures ||
							   !preserveVRIntermediateTextures)));
		}
	};

	struct NativeRestoreProgress
	{
		std::uint64_t ownerEpoch = 0;
		std::uint64_t retirementSerial = 0;
		NativeRestorePhase phase = NativeRestorePhase::Idle;
	};

	struct NativeRestoreOwnershipAdmission
	{
		bool lowPeakNativeRestore = false;
		bool previousVendorWasDLSS = false;
		bool previousVendorWasFSR = false;
		std::uint64_t targetEpoch = 0;
		std::uint64_t progressOwnerEpoch = 0;
	};

	[[nodiscard]] constexpr bool UsesEpochOwnedNativeRestore(
		const NativeRestoreOwnershipAdmission& a_admission) noexcept
	{
		if (a_admission.targetEpoch == 0)
			return false;

		const bool vendorNativeRestore =
			a_admission.lowPeakNativeRestore &&
			(a_admission.previousVendorWasDLSS ||
				a_admission.previousVendorWasFSR);
		const bool continuingOwnedRestore =
			a_admission.progressOwnerEpoch == a_admission.targetEpoch;
		return vendorNativeRestore || continuingOwnedRestore;
	}

	[[nodiscard]] constexpr bool ShouldApplyGenericMemoryReliefCleanup(
		bool a_memoryReliefActive,
		bool a_epochOwnedNativeRestore,
		bool a_lowPeakNativeRestore,
		bool a_previousVendorWasFSR) noexcept
	{
		const bool fsrNativeRestoreOwnsSharedRetirement =
			a_epochOwnedNativeRestore &&
			a_lowPeakNativeRestore &&
			a_previousVendorWasFSR;
		return a_memoryReliefActive &&
		       !fsrNativeRestoreOwnsSharedRetirement;
	}

	struct NativeRestoreFenceReadyResumeAdmission
	{
		bool relatchPending = false;
		std::uint64_t queuedEpoch = 0;
		std::uint64_t controllerTargetEpoch = 0;
		NativeRestoreProgress progress{};
		NativeRestoreOperation operation{};
		std::uint64_t completedRetirementSerial = 0;
		std::uint32_t queuedFrame = 0;
		std::uint32_t currentFrame = 0;
		std::uint32_t queuedDelayFrames = 0;
		std::uint32_t ordinaryRetryFrames = 0;
		std::uint64_t queuedNativeRestoreRetirementSerial = 0;
		bool loadingMenuOpen = false;
		bool loadingSerialMatches = false;
	};

	[[nodiscard]] constexpr bool CanResumeNativeRestoreAfterProvenRetirement(
		const NativeRestoreFenceReadyResumeAdmission& a_admission) noexcept
	{
		return a_admission.relatchPending &&
		       a_admission.queuedEpoch != 0 &&
		       a_admission.queuedEpoch == a_admission.controllerTargetEpoch &&
		       a_admission.queuedEpoch == a_admission.progress.ownerEpoch &&
		       a_admission.progress.phase == NativeRestorePhase::Complete &&
		       a_admission.progress.retirementSerial != 0 &&
		       a_admission.completedRetirementSerial >=
		           a_admission.progress.retirementSerial &&
		       a_admission.operation.valid &&
		       a_admission.operation.destroyDLSSResources &&
		       a_admission.operation.destroySharedResources &&
		       !a_admission.operation.preserveVRIntermediateTextures &&
		       a_admission.ordinaryRetryFrames != 0 &&
		       a_admission.queuedDelayFrames ==
		           a_admission.ordinaryRetryFrames &&
		       a_admission.queuedNativeRestoreRetirementSerial ==
		           a_admission.progress.retirementSerial &&
		       a_admission.queuedFrame != 0 &&
		       a_admission.currentFrame > a_admission.queuedFrame &&
		       a_admission.currentFrame - a_admission.queuedFrame <
		           a_admission.queuedDelayFrames &&
		       !a_admission.loadingMenuOpen &&
		       a_admission.loadingSerialMatches;
	}

	[[nodiscard]] constexpr bool HasNativeRestoreTransaction(
		const NativeRestoreProgress& a_progress) noexcept
	{
		return a_progress.ownerEpoch != 0 &&
		       a_progress.phase != NativeRestorePhase::Idle;
	}

	[[nodiscard]] constexpr bool CanArmNativeRestoreGuard(
		std::uint64_t a_operationEpoch,
		std::uint64_t a_targetEpoch) noexcept
	{
		return a_operationEpoch != 0 &&
		       a_operationEpoch == a_targetEpoch;
	}

	[[nodiscard]] constexpr bool CanBeginNativeRestore(
		std::uint64_t a_requestedEpoch,
		std::uint64_t a_ownerEpoch,
		NativeRestorePhase a_phase) noexcept
	{
		if (a_requestedEpoch == 0)
			return false;
		if (a_ownerEpoch == 0)
			return a_phase == NativeRestorePhase::Idle;
		return a_ownerEpoch == a_requestedEpoch &&
		       a_phase == NativeRestorePhase::VendorTeardownPending;
	}

	[[nodiscard]] constexpr bool CanAttachNativeRestoreRetirement(
		std::uint64_t a_requestedEpoch,
		std::uint64_t a_ownerEpoch,
		NativeRestorePhase a_phase) noexcept
	{
		return a_requestedEpoch != 0 &&
		       a_ownerEpoch == a_requestedEpoch &&
		       a_phase == NativeRestorePhase::VendorTeardownPending;
	}

	[[nodiscard]] constexpr NativeRestoreAction SelectNativeRestoreAction(
		std::uint64_t a_requestedEpoch,
		std::uint64_t a_ownerEpoch,
		NativeRestorePhase a_phase) noexcept
	{
		if (a_requestedEpoch == 0)
			return NativeRestoreAction::BeginVendorTeardown;

		if (a_ownerEpoch != a_requestedEpoch) {
			if (a_ownerEpoch != 0) {
				if (a_phase == NativeRestorePhase::VendorTeardownPending)
					return NativeRestoreAction::PollVendorTeardown;
				if (a_phase == NativeRestorePhase::SharedRetirementPending)
					return NativeRestoreAction::DrainPreviousRetirement;
			}
			return NativeRestoreAction::BeginVendorTeardown;
		}

		switch (a_phase) {
		case NativeRestorePhase::VendorTeardownPending:
			return NativeRestoreAction::PollVendorTeardown;
		case NativeRestorePhase::SharedRetirementPending:
			return NativeRestoreAction::DrainCurrentRetirement;
		case NativeRestorePhase::Complete:
			return NativeRestoreAction::RecreatePhysicalTargets;
		default:
			return NativeRestoreAction::BeginVendorTeardown;
		}
	}

	[[nodiscard]] constexpr bool CanCompleteNativeRestoreRetirement(
		std::uint64_t a_ownerEpoch,
		NativeRestorePhase a_phase,
		std::uint64_t a_expectedRetirementSerial,
		std::uint64_t a_completedRetirementSerial) noexcept
	{
		return a_ownerEpoch != 0 &&
		       a_phase == NativeRestorePhase::SharedRetirementPending &&
		       a_expectedRetirementSerial != 0 &&
		       a_completedRetirementSerial >= a_expectedRetirementSerial;
	}

	[[nodiscard]] constexpr bool BeginNativeRestore(
		NativeRestoreProgress& a_progress,
		std::uint64_t a_epoch) noexcept
	{
		if (!CanBeginNativeRestore(
				a_epoch,
				a_progress.ownerEpoch,
				a_progress.phase)) {
			return false;
		}
		if (a_progress.ownerEpoch == a_epoch)
			return true;

		a_progress = {
			.ownerEpoch = a_epoch,
			.retirementSerial = 0,
			.phase = NativeRestorePhase::VendorTeardownPending,
		};
		return true;
	}

	[[nodiscard]] constexpr bool RecordNativeRestoreRetirement(
		NativeRestoreProgress& a_progress,
		std::uint64_t a_epoch,
		std::uint64_t a_retirementSerial) noexcept
	{
		if (a_retirementSerial == 0 ||
			!CanAttachNativeRestoreRetirement(
				a_epoch,
				a_progress.ownerEpoch,
				a_progress.phase)) {
			return false;
		}
		if (a_progress.retirementSerial != 0 &&
			a_progress.retirementSerial != a_retirementSerial) {
			return false;
		}

		a_progress.retirementSerial = a_retirementSerial;
		return true;
	}

	[[nodiscard]] constexpr bool CompleteNativeRestoreVendorTeardown(
		NativeRestoreProgress& a_progress,
		std::uint64_t a_epoch) noexcept
	{
		if (!CanAttachNativeRestoreRetirement(
				a_epoch,
				a_progress.ownerEpoch,
				a_progress.phase)) {
			return false;
		}

		a_progress.phase = a_progress.retirementSerial != 0 ?
		                       NativeRestorePhase::SharedRetirementPending :
		                       NativeRestorePhase::Complete;
		return true;
	}

	[[nodiscard]] constexpr bool CompleteNativeRestoreRetirement(
		NativeRestoreProgress& a_progress,
		std::uint64_t a_epoch,
		std::uint64_t a_completedRetirementSerial) noexcept
	{
		if (a_progress.ownerEpoch != a_epoch ||
			!CanCompleteNativeRestoreRetirement(
				a_progress.ownerEpoch,
				a_progress.phase,
				a_progress.retirementSerial,
				a_completedRetirementSerial)) {
			return false;
		}

		a_progress.phase = NativeRestorePhase::Complete;
		return true;
	}

	[[nodiscard]] constexpr bool AbortNativeRestore(
		NativeRestoreProgress& a_progress,
		std::uint64_t a_expectedOwnerEpoch) noexcept
	{
		if (a_expectedOwnerEpoch == 0 ||
			a_progress.ownerEpoch != a_expectedOwnerEpoch ||
			a_progress.phase == NativeRestorePhase::Idle) {
			return false;
		}

		a_progress = {};
		return true;
	}

	enum class DeferredDispatchAction : std::uint8_t
	{
		EvaluateExisting,
		PresentationStretch
	};

	struct DeferredDispatchAdmission
	{
		bool mutationDeferred = false;
		bool exactProviderReady = false;
	};

	[[nodiscard]] constexpr DeferredDispatchAction SelectDeferredDispatchAction(
		const DeferredDispatchAdmission& a_state) noexcept
	{
		if (!a_state.mutationDeferred || a_state.exactProviderReady)
			return DeferredDispatchAction::EvaluateExisting;

		return DeferredDispatchAction::PresentationStretch;
	}

	[[nodiscard]] constexpr bool UsesVendorEvaluation(bool a_vendorMethod) noexcept
	{
		return a_vendorMethod;
	}

	[[nodiscard]] constexpr bool RequiresFSRCompatibility(bool a_fsrEvaluation) noexcept
	{
		return a_fsrEvaluation;
	}

	[[nodiscard]] constexpr bool NeedsDeferredFSRReset(
		bool a_fsrEvaluation,
		bool a_preservedResources,
		bool a_recreatedResources) noexcept
	{
		return a_fsrEvaluation &&
		       !a_preservedResources &&
		       !a_recreatedResources;
	}
}
