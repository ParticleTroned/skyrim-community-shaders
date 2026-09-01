#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <string_view>

namespace VRVendorRelatchPolicy
{
	using WorkGateMask = std::uint32_t;
	using WorkGateState = std::uint64_t;

	inline constexpr std::uint64_t kPostMutationEmergencyMinimumProjectionBytes =
		4ull * 1024ull * 1024ull * 1024ull;
	inline constexpr std::uint64_t kPostMutationEmergencyCommitReserveBytes =
		2ull * 1024ull * 1024ull * 1024ull;

	[[nodiscard]] constexpr std::uint64_t GetPostMutationEmergencyProjectionMultiplier(
		bool a_targetRenderScaleActive) noexcept
	{
		return a_targetRenderScaleActive ? 4u : 8u;
	}

	[[nodiscard]] constexpr bool CanAttemptPhysicalRelatchThisFrame(
		std::uint32_t a_lastAttemptFrame,
		std::uint64_t a_candidateEpoch,
		std::uint32_t a_currentFrame) noexcept
	{
		return a_candidateEpoch != 0 &&
		       a_currentFrame != 0 &&
		       a_lastAttemptFrame != a_currentFrame;
	}

	[[nodiscard]] constexpr bool CanServiceQueuedPostMutationRecovery(
		bool a_emergencyRecoveryRequested,
		bool a_emergencyAttemptConsumed) noexcept
	{
		return !a_emergencyRecoveryRequested || !a_emergencyAttemptConsumed;
	}

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
		// Queued relatch/profile work consumes this release; treating it as a
		// prerequisite would leave both the work and its owning gate blocked.
		return a_state.hasGateOwner &&
		       !a_state.mainMenuActive &&
		       !a_state.loadingPresentationActive &&
		       !a_state.raceSexPresentationActive &&
		       !a_state.saveLoadProtectionActive &&
		       a_state.completedWorldFrame &&
		       !a_state.recoveryPending;
	}

	enum class MissedLoadingMenuCloseAction : std::uint8_t
	{
		Wait,
		Disarm,
		ResetClosedWorldFrame,
		RecordFirstClosedWorldFrame,
		PublishClose
	};

	struct MissedLoadingMenuCloseAdmission
	{
		bool armed = false;
		bool eventOpen = false;
		bool serialOpen = false;
		bool stateMirrorAvailable = false;
		bool stateMirrorClosed = false;
		bool uiMirrorAvailable = false;
		bool uiMirrorClosed = false;
		bool openGenerationAuthorized = false;
		bool completedWorldFrameAvailable = false;
		bool firstClosedWorldFrameRecorded = false;
		std::uint64_t eventGeneration = 0;
		std::uint64_t expectedEventGeneration = 0;
		std::uint64_t loadingSerial = 0;
		std::uint64_t expectedLoadingSerial = 0;
		std::uint32_t completedWorldFrame = 0;
		std::uint32_t armWorldFrame = 0;
		std::uint32_t firstClosedWorldFrame = 0;
	};

	// A missing UI close may be synthesized only for the exact observed open edge,
	// after both independent physical mirrors remain closed across two newly
	// completed world frames. A newer open/close event invalidates the candidate.
	[[nodiscard]] constexpr MissedLoadingMenuCloseAction SelectMissedLoadingMenuCloseAction(
		const MissedLoadingMenuCloseAdmission& a_state) noexcept
	{
		if (!a_state.armed)
			return MissedLoadingMenuCloseAction::Wait;
		if (!a_state.eventOpen ||
			!a_state.serialOpen ||
			a_state.eventGeneration != a_state.expectedEventGeneration ||
			a_state.expectedLoadingSerial == 0 ||
			a_state.loadingSerial != a_state.expectedLoadingSerial) {
			return MissedLoadingMenuCloseAction::Disarm;
		}
		if (!a_state.stateMirrorAvailable ||
			!a_state.stateMirrorClosed ||
			!a_state.uiMirrorAvailable ||
			!a_state.uiMirrorClosed ||
			!a_state.openGenerationAuthorized ||
			!a_state.completedWorldFrameAvailable ||
			a_state.completedWorldFrame == a_state.armWorldFrame) {
			return a_state.firstClosedWorldFrameRecorded ?
			           MissedLoadingMenuCloseAction::ResetClosedWorldFrame :
			           MissedLoadingMenuCloseAction::Wait;
		}
		if (!a_state.firstClosedWorldFrameRecorded)
			return MissedLoadingMenuCloseAction::RecordFirstClosedWorldFrame;
		if (a_state.completedWorldFrame == a_state.firstClosedWorldFrame)
			return MissedLoadingMenuCloseAction::Wait;
		return MissedLoadingMenuCloseAction::PublishClose;
	}

	struct StartupMainMenuStateDefinition
	{
		bool isVR = false;
		bool startupMainMenuObserved = false;
		bool shaderCompilationComplete = false;
		bool completedWorldFrame = false;
		bool alreadyDefined = false;
	};

	[[nodiscard]] constexpr bool ShouldDefineStartupMainMenuState(
		const StartupMainMenuStateDefinition& a_state) noexcept
	{
		return a_state.isVR &&
		       a_state.startupMainMenuObserved &&
		       a_state.shaderCompilationComplete &&
		       !a_state.completedWorldFrame &&
		       !a_state.alreadyDefined;
	}

	// Boot sizing proves the requested dimensions, but it does not prove that
	// Skyrim's scene targets have been physically recreated at those dimensions.
	// Schedule that handoff explicitly so the bounds watchdog remains emergency
	// recovery rather than the ordinary startup scheduler.
	enum class StartupRenderScaleDirectHandoffAction : std::uint8_t
	{
		Inactive,
		WaitForBootSizing,
		QueuePhysicalContract,
		WaitForPhysicalContract,
		Complete,
		Cancel,
	};

	struct StartupRenderScaleDirectHandoff
	{
		bool active = false;
		bool targetActive = false;
		bool bootSizingContractExact = false;
		bool physicalContractConverged = false;
		bool physicalRelatchPending = false;
		bool physicalRelatchInProgress = false;
	};

	// Select the next handoff action without coupling the lifecycle policy to
	// renderer state, logging, or atomic publication details.
	[[nodiscard]] constexpr StartupRenderScaleDirectHandoffAction
	SelectStartupRenderScaleDirectHandoffAction(
		const StartupRenderScaleDirectHandoff& a_state) noexcept
	{
		if (!a_state.active)
			return StartupRenderScaleDirectHandoffAction::Inactive;
		if (!a_state.targetActive)
			return StartupRenderScaleDirectHandoffAction::Cancel;
		if (!a_state.bootSizingContractExact)
			return StartupRenderScaleDirectHandoffAction::WaitForBootSizing;
		if (a_state.physicalContractConverged)
			return StartupRenderScaleDirectHandoffAction::Complete;
		if (a_state.physicalRelatchPending ||
			a_state.physicalRelatchInProgress) {
			return StartupRenderScaleDirectHandoffAction::WaitForPhysicalContract;
		}
		return StartupRenderScaleDirectHandoffAction::QueuePhysicalContract;
	}

	struct SubmitBoundsRecoveryAdmission
	{
		bool displaySizedSubmitDuringPressure = false;
		bool startupDirectHandoffActive = false;
	};

	// Decide whether a display-sized submit is an immediate contract failure or
	// remains eligible for the ordinary bounded fallback watchdog.
	[[nodiscard]] constexpr bool ShouldForceSubmitBoundsRecovery(
		const SubmitBoundsRecoveryAdmission& a_state) noexcept
	{
		// A direct startup handoff owns the normal physical relatch. Preserve the
		// ordinary watchdog grace instead of allocating a duplicate recovery while
		// that planned transaction is pending or entering its creator.
		return !a_state.displaySizedSubmitDuringPressure &&
		       !a_state.startupDirectHandoffActive;
	}

	struct RenderScaleRuntimeActivation
	{
		bool loaded = false;
		bool isVR = false;
		bool establishedPhysicalContract = false;
		bool unresolvedProfileSync = false;
		bool completedWorldFrame = false;
		bool postLoadResetPending = false;
		bool loadingPresentationActive = false;
	};

	[[nodiscard]] constexpr bool CanPresentRenderScaleRuntime(
		const RenderScaleRuntimeActivation& a_state) noexcept
	{
		if (!a_state.loaded)
			return false;
		if (!a_state.isVR)
			return true;

		// Loading and save-load ownership may block lifecycle mutation, but an
		// already-proven physical contract remains the safest presentation state.
		// Preserve it until a replacement can be applied at a safe point.
		if (a_state.establishedPhysicalContract)
			return true;

		return !a_state.unresolvedProfileSync &&
		       a_state.completedWorldFrame &&
		       !a_state.postLoadResetPending &&
		       !a_state.loadingPresentationActive;
	}

	struct RenderTransitionCoverAdmission
	{
		bool isVR = false;
		bool renderChangePublished = false;
		bool loadingSerialMatches = false;
		bool loadingTransitionOpenOrTail = false;
		bool presentationCoverActive = false;
	};

	[[nodiscard]] constexpr bool ShouldArmRenderTransitionCover(
		const RenderTransitionCoverAdmission& a_state) noexcept
	{
		return a_state.isVR &&
		       a_state.renderChangePublished &&
		       a_state.loadingSerialMatches &&
		       a_state.loadingTransitionOpenOrTail &&
		       !a_state.presentationCoverActive;
	}

	struct LoadingFadeHoldAdmission
	{
		bool isVR = false;
		bool blackFade = false;
		bool fadingIn = false;
		bool presentationCoverActive = false;
		bool loadingMenuClosed = false;
		bool releaseAlreadyScheduled = false;
	};

	[[nodiscard]] constexpr bool ShouldHoldLoadingFadeIn(
		const LoadingFadeHoldAdmission& a_state) noexcept
	{
		return a_state.isVR &&
		       a_state.blackFade &&
		       a_state.fadingIn &&
		       a_state.presentationCoverActive &&
		       a_state.loadingMenuClosed &&
		       !a_state.releaseAlreadyScheduled;
	}

	struct LoadingPresentationReleaseAdmission
	{
		bool engineSaveLoadActivityActive = false;
		bool statePostLoadResetPending = false;
		bool hmdClearMaskDeferred = false;
		bool upscalingPostLoadResetPending = false;
		bool renderTargetRecreatePending = false;
		bool renderTargetRecreateInProgress = false;
		bool upscalingTransitionPending = false;
		bool stabilizerSyncScheduled = false;
		bool stabilizerSyncUnresolved = false;
	};

	// Presentation follows concrete renderer/engine activity. The broader
	// save-load safe-mode grace intentionally remains outside this policy so it
	// can protect mutation and disk persistence without extending black.
	[[nodiscard]] constexpr bool IsLoadingPresentationReleaseReady(
		const LoadingPresentationReleaseAdmission& a_state) noexcept
	{
		return !a_state.engineSaveLoadActivityActive &&
		       !a_state.statePostLoadResetPending &&
		       !a_state.hmdClearMaskDeferred &&
		       !a_state.upscalingPostLoadResetPending &&
		       !a_state.renderTargetRecreatePending &&
		       !a_state.renderTargetRecreateInProgress &&
		       !a_state.upscalingTransitionPending &&
		       !a_state.stabilizerSyncScheduled &&
		       !a_state.stabilizerSyncUnresolved;
	}

	struct SerializedPresentationReadiness
	{
		bool compositorHoldActive = false;
		bool unresolvedPhysicalMutation = false;
		bool serializationOwnsAppliedContract = false;
		bool exactAppliedContract = false;
		bool completedDestinationWorldFrame = false;
		bool engineSaveLoadActivityActive = false;
		bool statePostLoadResetPending = false;
		bool upscalingPostLoadResetPending = false;
		bool renderTargetRecreatePending = false;
		bool renderTargetRecreateInProgress = false;
		bool vendorRuntimeResetPending = false;
		bool physicalContractConverged = false;
		std::uint64_t serializationLoadingSerial = 0;
		std::uint64_t compositorHoldLoadingSerial = 0;
		std::uint64_t currentLoadingSerial = 0;
	};

	[[nodiscard]] constexpr bool HasSerializedPresentationReadiness(
		const SerializedPresentationReadiness& a_state) noexcept
	{
		return a_state.compositorHoldActive &&
		       !a_state.unresolvedPhysicalMutation &&
		       a_state.serializationOwnsAppliedContract &&
		       a_state.exactAppliedContract &&
		       a_state.completedDestinationWorldFrame &&
		       !a_state.engineSaveLoadActivityActive &&
		       !a_state.statePostLoadResetPending &&
		       !a_state.upscalingPostLoadResetPending &&
		       !a_state.renderTargetRecreatePending &&
		       !a_state.renderTargetRecreateInProgress &&
		       !a_state.vendorRuntimeResetPending &&
		       a_state.physicalContractConverged &&
		       a_state.serializationLoadingSerial != 0 &&
		       a_state.serializationLoadingSerial ==
		           a_state.compositorHoldLoadingSerial &&
		       a_state.serializationLoadingSerial ==
		           a_state.currentLoadingSerial;
	}

	// A console COC does not necessarily publish the ordinary post-load completion
	// event. Exact destination/contract evidence may arm the normal safety grace;
	// it may not bypass that grace or the vendor lifecycle mutation gate.
	[[nodiscard]] constexpr bool CanArmSerializedSaveLoadCompletionGrace(
		const SerializedPresentationReadiness& a_state,
		bool a_saveLoadSafeModeActive,
		bool a_completionGraceAlreadyArmed) noexcept
	{
		return a_saveLoadSafeModeActive &&
		       !a_completionGraceAlreadyArmed &&
		       HasSerializedPresentationReadiness(a_state);
	}

	// Once the ordinary grace and lifecycle gate have retired, the same exact
	// serialized contract may break the compositor-hold/promotion proof cycle.
	// The compositor still requires a validated vendor stereo pair before release.
	[[nodiscard]] constexpr bool CanQualifySerializedPresentationPromotion(
		const SerializedPresentationReadiness& a_state,
		bool a_saveLoadSafeModeActive,
		bool a_lifecycleMutationDeferred) noexcept
	{
		return !a_saveLoadSafeModeActive &&
		       !a_lifecycleMutationDeferred &&
		       HasSerializedPresentationReadiness(a_state);
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

	struct QueuedMenuRuntimeOptionsAdmission
	{
		bool stableValid = false;
		bool stableActive = false;
		bool targetValid = false;
		bool targetActive = false;
		bool sameMethod = false;
		bool directMenuRequest = false;
		bool exactRequestPrepared = false;
	};

	[[nodiscard]] constexpr bool CanQueuedMenuProfileOwnRuntimeOptions(
		const QueuedMenuRuntimeOptionsAdmission& a_state) noexcept
	{
		return a_state.stableValid && !a_state.stableActive &&
		       a_state.targetValid && a_state.targetActive &&
		       a_state.sameMethod && a_state.directMenuRequest &&
		       a_state.exactRequestPrepared;
	}

	[[nodiscard]] constexpr bool CanBypassPreparedMenuRequestDelay(
		bool a_directMenuRequest,
		std::uint64_t a_requestID,
		std::uint64_t a_preparedRequestID) noexcept
	{
		return a_directMenuRequest && a_requestID != 0 &&
		       a_requestID == a_preparedRequestID;
	}

	[[nodiscard]] constexpr bool HasDirectMenuRequestAuthority(
		bool a_directMenuEdit,
		std::uint64_t a_requestID) noexcept
	{
		// Origin alone is insufficient because reload and measurement share
		// CSMenu. Only the immutable committed-edit bit grants direct privileges.
		return a_directMenuEdit && a_requestID != 0;
	}

	[[nodiscard]] constexpr bool CanUseDirectMenuRequestPacing(
		bool a_directMenuEdit,
		std::uint64_t a_requestID) noexcept
	{
		return HasDirectMenuRequestAuthority(a_directMenuEdit, a_requestID);
	}

	struct MenuEditDispatch
	{
		bool publishRequest = false;
		bool directMenuEdit = false;
	};

	[[nodiscard]] constexpr MenuEditDispatch SelectMenuEditDispatch(
		bool a_valueChanged,
		bool a_editCommitted) noexcept
	{
		// Continuous controls remain on the ordinary latest-wins coalescing path
		// until ImGui reports the final committed value.
		return {
			.publishRequest = a_valueChanged || a_editCommitted,
			.directMenuEdit = a_editCommitted,
		};
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

	struct InactiveContractNativeReleaseAdmission
	{
		bool controllerOwnsPublishedInactiveContract = false;
		bool exactNativePresentationObservation = false;
		bool releaseLifecycleReady = false;
	};

	// An inactive Render Scale contract still owns a concrete full-resolution
	// presentation. Admit that exact presentation to the compositor handoff even
	// though it is neither an active Render Scale vendor output nor an unrelated
	// fixed/native runtime. Without this fourth class, the hold and native
	// stabilization guard wait on one another indefinitely.
	[[nodiscard]] constexpr bool CanReleasePublishedInactiveContract(
		const InactiveContractNativeReleaseAdmission& a_admission) noexcept
	{
		return a_admission.controllerOwnsPublishedInactiveContract &&
		       a_admission.exactNativePresentationObservation &&
		       a_admission.releaseLifecycleReady;
	}

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
		RetryPresentationValidation,
		PublishInactiveContractFailOpen,
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
		return a_attempt == 1u ?
		           NativeRestorePresentationRecoveryAction::RetryPresentationValidation :
		           NativeRestorePresentationRecoveryAction::PublishInactiveContractFailOpen;
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

	// Menu open/close events can be delivered from a UI worker while OpenVR is
	// between the two eye submissions for one rendered frame. Once the first eye
	// has fixed its presentation decision, the render owner must keep that
	// transaction immutable until a later frame; the event thread may only
	// publish the pending context change.
	[[nodiscard]] constexpr bool ShouldDeferMenuContextInvalidation(
		std::uint32_t a_transactionFrame,
		std::uint32_t a_currentFrame,
		bool a_presentationDecisionLatched) noexcept
	{
		return a_presentationDecisionLatched &&
		       a_transactionFrame == a_currentFrame;
	}

	// The first eye's decision is authoritative for its peer. In particular, a
	// close must not drop the second eye out of the menu path, and an open must
	// not pull only the second eye into it.
	[[nodiscard]] constexpr bool ResolveMenuPresentationAttempt(
		bool a_decisionLatched,
		bool a_latchedAttempt,
		bool a_transactionSealed,
		bool a_transactionOwnsPresentationWork,
		bool a_committedLayerValid,
		bool a_menuTextProtectionContext) noexcept
	{
		return a_decisionLatched ?
		           a_latchedAttempt :
		           (a_transactionSealed ||
					   a_transactionOwnsPresentationWork ||
					   (a_committedLayerValid && a_menuTextProtectionContext));
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

	struct StableNativeRestoreAdmission
	{
		bool physicalResizeNeeded = false;
		bool previousBootActiveVendor = false;
		bool targetRenderScaleActive = false;
		bool stableValid = false;
		bool stableActiveVendor = false;
		bool stableMatchesBootContract = false;
	};

	[[nodiscard]] constexpr bool PreservesStableVendorContractDuringNativeRestore(
		const StableNativeRestoreAdmission& a_admission) noexcept
	{
		return a_admission.physicalResizeNeeded &&
		       a_admission.previousBootActiveVendor &&
		       !a_admission.targetRenderScaleActive &&
		       a_admission.stableValid &&
		       a_admission.stableActiveVendor &&
		       a_admission.stableMatchesBootContract;
	}

	[[nodiscard]] constexpr bool UsesLowPeakNativeRestore(
		const StableNativeRestoreAdmission& a_admission) noexcept
	{
		return a_admission.physicalResizeNeeded &&
		       a_admission.previousBootActiveVendor &&
		       !a_admission.targetRenderScaleActive &&
		       !PreservesStableVendorContractDuringNativeRestore(a_admission);
	}

	struct SystemCommitGuardAdmission
	{
		bool residencyOverlapAllocation = false;
		bool systemCommitValid = false;
		bool systemCommitLimitKnown = false;
	};

	[[nodiscard]] constexpr bool UsesSystemCommitProjectionGuard(
		const SystemCommitGuardAdmission& a_admission) noexcept
	{
		return a_admission.residencyOverlapAllocation &&
		       a_admission.systemCommitValid &&
		       a_admission.systemCommitLimitKnown;
	}

	[[nodiscard]] constexpr bool ShouldRetainStableDoorContract(
		bool a_stabilizerDoorHandoff,
		bool a_hardSafetyDeferred,
		bool a_physicalMutationOccurred) noexcept
	{
		// A door handoff is presentation-critical: it must not wait behind a
		// resource request which admission has already rejected. Before any physical
		// mutation, the last truthful stable contract (or startup None) is terminally
		// safe and lets the owned loading fade complete. After mutation, recovery must
		// instead follow the truthful mutated contract.
		return a_stabilizerDoorHandoff &&
		       a_hardSafetyDeferred &&
		       !a_physicalMutationOccurred;
	}

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

	struct RelatchRetryPacingAdmission
	{
		std::uint32_t queuedFrame = 0;
		std::uint32_t currentFrame = 0;
		std::uint32_t delayFrames = 0;
		bool bypassMultiFrameDelay = false;
	};

	// A render-frame retry floor is a hard serialization boundary. Emergency and
	// presentation-deadline recovery may waive the remaining ordinary backoff,
	// but must not consume a request requeued by an earlier SetDirtyStates call in
	// the same frame.
	[[nodiscard]] constexpr bool ShouldDeferRelatchForRetryPacing(
		const RelatchRetryPacingAdmission& a_state) noexcept
	{
		if (a_state.queuedFrame == 0)
			return false;
		if (a_state.currentFrame <= a_state.queuedFrame) {
			// Equality is the hard same-frame floor. A lower current frame means
			// Skyrim reset or wrapped its frame counter after this tuple was queued;
			// the old timestamp cannot impose a meaningful multi-frame backoff.
			return a_state.currentFrame == a_state.queuedFrame;
		}
		return !a_state.bypassMultiFrameDelay &&
		       a_state.delayFrames != 0 &&
		       a_state.currentFrame - a_state.queuedFrame <
		           a_state.delayFrames;
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
		ReuseCompletedOutput,
		PresentationStretch,
		FailClosed
	};

	struct DeferredDispatchAdmission
	{
		bool mutationDeferred = false;
		bool physicalMutationStarted = false;
		bool hardFailure = false;
		bool exactProviderReady = false;
		bool completedOutputReady = false;
	};

	[[nodiscard]] constexpr DeferredDispatchAction SelectDeferredDispatchAction(
		const DeferredDispatchAdmission& a_state) noexcept
	{
		if (a_state.hardFailure || a_state.physicalMutationStarted)
			return DeferredDispatchAction::FailClosed;
		if (a_state.exactProviderReady)
			return DeferredDispatchAction::EvaluateExisting;
		if (a_state.completedOutputReady)
			return DeferredDispatchAction::ReuseCompletedOutput;
		if (!a_state.mutationDeferred)
			return DeferredDispatchAction::EvaluateExisting;

		return DeferredDispatchAction::PresentationStretch;
	}

	[[nodiscard]] constexpr bool IsSameStereoDispatchContract(
		std::uint32_t a_admittedGeneration,
		std::uint32_t a_currentGeneration,
		std::uint32_t a_admittedMethod,
		std::uint32_t a_currentMethod) noexcept
	{
		return a_admittedGeneration == a_currentGeneration &&
		       a_admittedMethod == a_currentMethod;
	}

	[[nodiscard]] constexpr bool DoesPendingVendorResetInvalidateProvider(
		bool a_resetPending,
		std::uint32_t a_resetGeneration,
		std::uint32_t a_providerGeneration) noexcept
	{
		if (!a_resetPending)
			return false;

		return a_resetGeneration == 0 || a_providerGeneration == 0 ||
		       a_resetGeneration == a_providerGeneration;
	}

	struct NativeRestoreSuccessorAdmission
	{
		bool recoveryOrigin = false;
		std::uint64_t incomingEpoch = 0;
		std::uint64_t presentationGuardEpoch = 0;
		NativeRestoreProgress progress{};
	};

	// Ordinary successors cannot replace an incomplete native restore owner. The
	// recovery path may transfer that ownership explicitly under its stronger locks.
	[[nodiscard]] constexpr bool ShouldDeferNativeRestoreSuccessor(
		const NativeRestoreSuccessorAdmission& a_state) noexcept
	{
		if (a_state.recoveryOrigin)
			return false;

		const bool foreignPresentationGuard =
			a_state.presentationGuardEpoch != 0 &&
			(a_state.incomingEpoch == 0 ||
				a_state.presentationGuardEpoch != a_state.incomingEpoch);
		const bool foreignRestoreTransaction =
			HasNativeRestoreTransaction(a_state.progress) &&
			(a_state.incomingEpoch == 0 ||
				a_state.progress.ownerEpoch != a_state.incomingEpoch);
		return foreignPresentationGuard || foreignRestoreTransaction;
	}

	struct ProviderRetirementSuccessorAdmission
	{
		bool recoveryOrigin = false;
		bool incomingUsesProvider = false;
		bool stableProfileValid = false;
		bool stableUsesProvider = false;
		bool resetPending = false;
		bool lifecycleRetiring = false;
	};

	// A provider cannot be re-entered while its last non-owning stable contract is
	// still retiring it. Let the inactive contract keep presenting until teardown.
	[[nodiscard]] constexpr bool ShouldDeferProviderRetirementSuccessor(
		const ProviderRetirementSuccessorAdmission& a_state) noexcept
	{
		return !a_state.recoveryOrigin &&
		       a_state.incomingUsesProvider &&
		       a_state.stableProfileValid &&
		       !a_state.stableUsesProvider &&
		       (a_state.resetPending || a_state.lifecycleRetiring);
	}

	struct AppliedContractGenerationSelection
	{
		bool bootContractActive = false;
		bool vendorEvaluationSelected = false;
		bool relatchPlanOwnsTransition = false;
		std::uint32_t bootGeneration = 0;
		std::uint32_t sourceGeneration = 0;
		std::uint32_t relatchGeneration = 0;
	};

	// Render Scale-off vendor evaluation owns a real provider contract even though
	// the physical boot latch is inactive. Preserve only an exact relatch owner.
	[[nodiscard]] constexpr std::uint32_t SelectAppliedContractGeneration(
		const AppliedContractGenerationSelection& a_state) noexcept
	{
		if (a_state.bootContractActive)
			return a_state.bootGeneration;
		if (!a_state.vendorEvaluationSelected)
			return a_state.bootGeneration;
		if (a_state.relatchPlanOwnsTransition && a_state.relatchGeneration != 0)
			return a_state.relatchGeneration;
		return a_state.sourceGeneration;
	}

	struct VendorEvaluationRelatchSelection
	{
		bool isVR = false;
		bool methodChanged = false;
		bool previousMethodUsesVendor = false;
		bool targetMethodUsesVendor = false;
		bool targetMethodIsFSR = false;
		bool fsrRuntimeSelectionChanged = false;
	};

	// A fixed-resolution vendor selection still owns provider resources. Method
	// and FSR runtime-path changes must therefore cross the relatch boundary.
	[[nodiscard]] constexpr bool NeedsVendorEvaluationRelatch(
		const VendorEvaluationRelatchSelection& a_state) noexcept
	{
		if (!a_state.isVR)
			return false;

		const bool vendorMethodBoundary =
			a_state.methodChanged &&
			(a_state.previousMethodUsesVendor ||
				a_state.targetMethodUsesVendor);
		const bool fsrRuntimeBoundary =
			a_state.targetMethodIsFSR &&
			a_state.fsrRuntimeSelectionChanged;
		return vendorMethodBoundary || fsrRuntimeBoundary;
	}

	struct PhysicalRelatchAdmission
	{
		bool renderScaleActive = false;
		bool renderScaleEligible = false;
		bool restartRequired = false;
		bool postLoadRecovery = false;
		bool nativeRestoreRecovery = false;
		bool providerNeutralRecovery = false;
		bool vendorEvaluationSelected = false;
	};

	// Fixed-resolution vendor evaluation owns a physical provider contract even
	// though the reduced render-target latch is inactive.
	[[nodiscard]] constexpr bool AllowsPhysicalRelatch(
		const PhysicalRelatchAdmission& a_state) noexcept
	{
		return a_state.renderScaleActive ||
		       a_state.renderScaleEligible ||
		       a_state.restartRequired ||
		       a_state.postLoadRecovery ||
		       a_state.nativeRestoreRecovery ||
		       a_state.providerNeutralRecovery ||
		       a_state.vendorEvaluationSelected;
	}

	struct VendorEvaluationGenerationSelection
	{
		bool renderScaleActive = false;
		bool publishedProviderMatches = false;
		std::uint32_t renderScaleGeneration = 0;
		std::uint32_t publishedProviderGeneration = 0;
	};

	// The encode intermediates must carry the same generation as the provider
	// they feed, including the native-sized Render Scale-off path.
	[[nodiscard]] constexpr std::uint32_t SelectVendorEvaluationGeneration(
		const VendorEvaluationGenerationSelection& a_state) noexcept
	{
		if (a_state.renderScaleActive)
			return a_state.renderScaleGeneration;
		if (a_state.publishedProviderMatches)
			return a_state.publishedProviderGeneration;
		return 0;
	}

	struct RelatchResourceTrackingSyncAdmission
	{
		bool syncPending = false;
		bool physicalRelatchPending = false;
		bool physicalRelatchInProgress = false;
		bool appliedProfileValid = false;
		bool appliedOwnsCurrentTarget = false;
		bool appliedStatePublishable = false;
		bool frameGenerationChanged = false;
		bool foveatedChanged = false;
		bool peripheryTAAChanged = false;
	};

	// The relatch owns method, quality, Render Scale, and FSR runtime selection.
	// Only resource changes outside that transaction require a second teardown.
	[[nodiscard]] constexpr bool CanSyncRelatchResourceTracking(
		const RelatchResourceTrackingSyncAdmission& a_state) noexcept
	{
		return a_state.syncPending &&
		       !a_state.physicalRelatchPending &&
		       !a_state.physicalRelatchInProgress &&
		       a_state.appliedProfileValid &&
		       a_state.appliedOwnsCurrentTarget &&
		       a_state.appliedStatePublishable &&
		       !a_state.frameGenerationChanged &&
		       !a_state.foveatedChanged &&
		       !a_state.peripheryTAAChanged;
	}

	struct VendorPresentationContractAdmission
	{
		bool appliedProfileValid = false;
		bool vendorMethodSelected = false;
		bool appliedProfileActive = false;
		bool bootContractActive = false;
		bool activeBootMatchesApplied = false;
		bool nativeDimensionsMatch = false;
	};

	// Both reduced-resolution and native-sized vendor providers require stereo
	// presentation proof. The inactive boot latch is valid only for the latter.
	[[nodiscard]] constexpr bool CanStabilizeVendorPresentationContract(
		const VendorPresentationContractAdmission& a_state) noexcept
	{
		if (!a_state.appliedProfileValid || !a_state.vendorMethodSelected)
			return false;

		if (a_state.appliedProfileActive) {
			return a_state.bootContractActive &&
			       a_state.activeBootMatchesApplied;
		}

		return !a_state.bootContractActive &&
		       a_state.nativeDimensionsMatch;
	}

	enum class PostLoadRecoverySettleAction : std::uint8_t
	{
		WaitForCleanup,
		WaitForSettledSamples,
		UseSettledSamples,
		EvaluateDeadlineOnce
	};

	struct PostLoadRecoverySettleAdmission
	{
		bool cleanupDrained = false;
		std::uint32_t currentFrame = 0;
		std::uint32_t admissionWaitStartFrame = 0;
		std::uint32_t settledSamples = 0;
		std::uint32_t requiredSettledSamples = 0;
		std::uint32_t timeoutFrames = 0;
	};

	[[nodiscard]] constexpr PostLoadRecoverySettleAction SelectPostLoadRecoverySettleAction(
		const PostLoadRecoverySettleAdmission& a_state) noexcept
	{
		if (a_state.admissionWaitStartFrame == 0)
			return PostLoadRecoverySettleAction::WaitForCleanup;
		if (a_state.currentFrame - a_state.admissionWaitStartFrame >= a_state.timeoutFrames)
			return PostLoadRecoverySettleAction::EvaluateDeadlineOnce;
		if (!a_state.cleanupDrained)
			return PostLoadRecoverySettleAction::WaitForCleanup;
		if (a_state.settledSamples >= a_state.requiredSettledSamples)
			return PostLoadRecoverySettleAction::UseSettledSamples;
		return PostLoadRecoverySettleAction::WaitForSettledSamples;
	}

	enum class PostLoadRecoveryDeadlineAction : std::uint8_t
	{
		NotExpired,
		AttemptOnce,
		WaitForClaimedAttempt,
		ContinueClaimedAttempt,
		FallbackClaimedAttempt,
		ContinueMutatedRecovery,
		RetainStableContract
	};

	struct PostLoadRecoveryDeadlineAdmission
	{
		bool deadlineExpired = false;
		bool attemptConsumed = false;
		bool attemptInProgress = false;
		bool attemptBudgetExpired = false;
		bool physicalMutationStarted = false;
		bool recoveryOwned = false;
		bool loadingSerialOwned = false;
		bool cleanupAndTrimComplete = false;
		bool retirementReady = false;
		bool memorySampleFresh = false;
		bool pressureAcceptable = false;
		bool gpuHeadroomSufficient = false;
		bool projectedSystemCommitSafe = false;
		bool deviceHealthy = false;
		bool noRecentOutOfMemory = false;
	};

	[[nodiscard]] constexpr PostLoadRecoveryDeadlineAction SelectPostLoadRecoveryDeadlineAction(
		const PostLoadRecoveryDeadlineAdmission& a_state) noexcept
	{
		if (!a_state.deadlineExpired)
			return PostLoadRecoveryDeadlineAction::NotExpired;
		const bool exactRecoveryOwner =
			a_state.recoveryOwned && a_state.loadingSerialOwned;
		if (a_state.physicalMutationStarted &&
			exactRecoveryOwner) {
			return PostLoadRecoveryDeadlineAction::ContinueMutatedRecovery;
		}
		const bool admissionReady =
			a_state.cleanupAndTrimComplete &&
			a_state.retirementReady &&
			a_state.memorySampleFresh &&
			a_state.pressureAcceptable &&
			a_state.gpuHeadroomSufficient &&
			a_state.projectedSystemCommitSafe &&
			a_state.deviceHealthy &&
			a_state.noRecentOutOfMemory;
		// AttemptOnce claims one logical recovery operation, not one call into a
		// vendor teardown API. Pending teardown/drain results must keep servicing
		// that exact owner until it can cross the physical mutation boundary.
		if (a_state.attemptConsumed &&
			a_state.attemptInProgress &&
			exactRecoveryOwner) {
			if (a_state.attemptBudgetExpired)
				return PostLoadRecoveryDeadlineAction::FallbackClaimedAttempt;
			return admissionReady ?
			           PostLoadRecoveryDeadlineAction::ContinueClaimedAttempt :
			           PostLoadRecoveryDeadlineAction::WaitForClaimedAttempt;
		}
		if (!a_state.attemptConsumed &&
			exactRecoveryOwner &&
			admissionReady) {
			return PostLoadRecoveryDeadlineAction::AttemptOnce;
		}
		return PostLoadRecoveryDeadlineAction::RetainStableContract;
	}

	enum class PostLoadVendorTeardownPhase : std::uint8_t
	{
		Idle,
		Draining,
		Released
	};

	enum class PostLoadVendorTeardownAction : std::uint8_t
	{
		Inactive,
		BeginTeardown,
		ContinueTeardown,
		WaitForCreatorAdmission,
		AdmitCreator,
		AbortForDeviceLoss,
		FallbackToNative
	};

	struct PostLoadVendorTeardownAdmission
	{
		PostLoadVendorTeardownPhase phase =
			PostLoadVendorTeardownPhase::Idle;
		bool deadlineExpired = false;
		bool attemptConsumed = false;
		bool physicalMutationStarted = false;
		bool recoveryOwned = false;
		bool loadingSerialOwned = false;
		bool recoveryRelatch = false;
		bool reducedDLSSContract = false;
		bool stableDLSSContractExisted = false;
		bool stableVendorResourcesTruthful = false;
		bool destroysDLSSResources = false;
		bool destroysOtherVendorResources = false;
		bool preservesStablePresentationResources = false;
		bool cleanupAndTrimComplete = false;
		bool retirementReady = false;
		bool memorySampleFresh = false;
		bool highGPUPressure = false;
		bool pressureAcceptable = false;
		bool gpuHeadroomSufficient = false;
		bool projectedSystemCommitSafe = false;
		bool deviceHealthy = false;
		bool noRecentOutOfMemory = false;
		bool retryBudgetExpired = false;
	};

	// A reduced DLSS post-load recovery can otherwise deadlock when its creator
	// admission needs GPU headroom that is still occupied by the invalid previous
	// vendor generation. Allow that exact owner to perform only the memory-releasing
	// teardown first. Allocation remains behind every ordinary creator gate.
	[[nodiscard]] constexpr PostLoadVendorTeardownAction SelectPostLoadVendorTeardownAction(
		const PostLoadVendorTeardownAdmission& a_state) noexcept
	{
		const bool exactRecoveryOwner =
			a_state.recoveryOwned && a_state.loadingSerialOwned;
		const bool exactTeardownTarget =
			a_state.deadlineExpired &&
			exactRecoveryOwner &&
			!a_state.physicalMutationStarted &&
			a_state.recoveryRelatch &&
			a_state.reducedDLSSContract &&
			a_state.stableDLSSContractExisted &&
			!a_state.stableVendorResourcesTruthful &&
			(a_state.destroysDLSSResources ||
				a_state.phase == PostLoadVendorTeardownPhase::Released) &&
			!a_state.destroysOtherVendorResources &&
			!a_state.preservesStablePresentationResources;
		if (!exactTeardownTarget)
			return PostLoadVendorTeardownAction::Inactive;

		if (a_state.phase == PostLoadVendorTeardownPhase::Draining) {
			if (!a_state.attemptConsumed)
				return PostLoadVendorTeardownAction::Inactive;
			if (!a_state.deviceHealthy)
				return PostLoadVendorTeardownAction::AbortForDeviceLoss;
			if (a_state.retryBudgetExpired)
				return PostLoadVendorTeardownAction::FallbackToNative;
			return PostLoadVendorTeardownAction::ContinueTeardown;
		}

		const bool creatorAdmissionReady =
			a_state.cleanupAndTrimComplete &&
			a_state.retirementReady &&
			a_state.memorySampleFresh &&
			a_state.pressureAcceptable &&
			a_state.gpuHeadroomSufficient &&
			a_state.projectedSystemCommitSafe &&
			a_state.deviceHealthy &&
			a_state.noRecentOutOfMemory;
		if (a_state.phase == PostLoadVendorTeardownPhase::Released) {
			if (!a_state.attemptConsumed)
				return PostLoadVendorTeardownAction::Inactive;
			if (!a_state.deviceHealthy)
				return PostLoadVendorTeardownAction::AbortForDeviceLoss;
			if (a_state.retryBudgetExpired)
				return PostLoadVendorTeardownAction::FallbackToNative;
			return creatorAdmissionReady ?
			           PostLoadVendorTeardownAction::AdmitCreator :
			           PostLoadVendorTeardownAction::WaitForCreatorAdmission;
		}

		const bool teardownOnlyAdmissionReady =
			!a_state.attemptConsumed &&
			a_state.cleanupAndTrimComplete &&
			a_state.retirementReady &&
			a_state.memorySampleFresh &&
			a_state.highGPUPressure &&
			a_state.projectedSystemCommitSafe &&
			a_state.deviceHealthy &&
			a_state.noRecentOutOfMemory;
		return teardownOnlyAdmissionReady ?
		           PostLoadVendorTeardownAction::BeginTeardown :
		           PostLoadVendorTeardownAction::Inactive;
	}

	[[nodiscard]] constexpr bool HasElapsedMonotonicDeadline(
		std::uint64_t a_startTickMs,
		std::uint64_t a_currentTickMs,
		std::uint64_t a_deadlineMs) noexcept
	{
		return a_startTickMs != 0 &&
		       a_deadlineMs != 0 &&
		       a_currentTickMs - a_startTickMs >= a_deadlineMs;
	}

	struct ExtendedRecoveryLivenessState
	{
		std::uint64_t holdEpoch = 0;
		std::uint64_t holdStartTickMs = 0;
		std::uint64_t currentTickMs = 0;
		std::uint64_t cueStartDelayMs = 0;
		bool terminalFailureClaimed = false;
	};

	// The cue is generated entirely from the protected keepalive; it never makes
	// an incoherent render target eligible. A missing clock or terminal claim
	// fails back to opaque black.
	[[nodiscard]] constexpr bool ShouldShowExtendedRecoveryLivenessCue(
		const ExtendedRecoveryLivenessState& a_state) noexcept
	{
		return a_state.holdEpoch != 0 &&
		       !a_state.terminalFailureClaimed &&
		       HasElapsedMonotonicDeadline(
				   a_state.holdStartTickMs,
				   a_state.currentTickMs,
				   a_state.cueStartDelayMs);
	}

	enum class PostMutationProgressPhase : std::uint8_t
	{
		None,
		MutationEntered,
		TableChanged,
		EmergencyRecoveryRequested,
		RecoveryResourcesReady,
		EmergencyCreatorClaimed,
		EngineTargetsReconciled,
		ContractPublished,
		PresentationStabilizing
	};

	[[nodiscard]] constexpr bool IsPostMutationRecoveryActivelyProgressing(
		PostMutationProgressPhase a_phase) noexcept
	{
		return a_phase >= PostMutationProgressPhase::EmergencyCreatorClaimed;
	}

	struct PostMutationTerminalDeadlinePolicy
	{
		PostMutationProgressPhase progressPhase =
			PostMutationProgressPhase::None;
		bool debuggerAttached = false;
		std::uint64_t stalledDeadlineMs = 0;
		std::uint64_t progressingDeadlineMs = 0;
		std::uint64_t debuggerDeadlineMs = 0;
	};

	// Time alone does not make a coherent recovery impossible. A chain which has
	// claimed its one-shot creator, or reached a later reconciliation/publication
	// milestone, receives the longer recovery ceiling. Reversible resource and
	// memory readiness does not. A debugger receives its own deliberately generous
	// ceiling. The caller keeps the chain start immutable, so retry/owner churn
	// cannot renew any of these budgets.
	[[nodiscard]] constexpr std::uint64_t SelectPostMutationTerminalDeadline(
		const PostMutationTerminalDeadlinePolicy& a_policy) noexcept
	{
		if (a_policy.debuggerAttached)
			return a_policy.debuggerDeadlineMs;
		return IsPostMutationRecoveryActivelyProgressing(
				   a_policy.progressPhase) ?
		           a_policy.progressingDeadlineMs :
		           a_policy.stalledDeadlineMs;
	}

	struct PostMutationEmergencyMemoryAdmission
	{
		bool systemCommitValid = false;
		std::uint64_t currentCommitBytes = 0;
		std::uint64_t commitLimitBytes = 0;
		std::uint64_t estimatedAdditionalBytes = 0;
		std::uint64_t projectionMultiplier = 0;
		std::uint64_t minimumProjectedAdditionalBytes = 0;
		std::uint64_t reserveBytes = 0;
	};

	struct PostMutationEmergencyMemoryEvaluation
	{
		bool projectionValid = false;
		std::uint64_t projectedAdditionalBytes = 0;
		std::uint64_t projectedCommitBytes = 0;
		std::uint64_t admissionLimitBytes = 0;
		bool safe = false;
	};

	// Emergency recovery may consume the normal conservative reserve, but it must
	// not consume the machine's final commit. The target-specific normal projection
	// remains authoritative; a minimum projection covers fixed creator/system
	// transients which the resource-key estimate does not model.
	[[nodiscard]] constexpr PostMutationEmergencyMemoryEvaluation
	EvaluatePostMutationEmergencyMemory(
		const PostMutationEmergencyMemoryAdmission& a_state) noexcept
	{
		PostMutationEmergencyMemoryEvaluation result{};
		if (a_state.projectionMultiplier == 0 ||
			a_state.estimatedAdditionalBytes >
				std::numeric_limits<std::uint64_t>::max() /
					a_state.projectionMultiplier) {
			return result;
		}

		result.projectionValid = true;
		result.projectedAdditionalBytes =
			std::max(
				a_state.estimatedAdditionalBytes * a_state.projectionMultiplier,
				a_state.minimumProjectedAdditionalBytes);
		if (!a_state.systemCommitValid ||
			a_state.commitLimitBytes == 0 ||
			a_state.commitLimitBytes <= a_state.reserveBytes) {
			return result;
		}

		result.admissionLimitBytes =
			a_state.commitLimitBytes - a_state.reserveBytes;
		if (a_state.currentCommitBytes >
			std::numeric_limits<std::uint64_t>::max() -
				result.projectedAdditionalBytes) {
			return result;
		}

		result.projectedCommitBytes =
			a_state.currentCommitBytes + result.projectedAdditionalBytes;
		result.safe = result.projectedCommitBytes < result.admissionLimitBytes;
		return result;
	}

	[[nodiscard]] constexpr bool CanAdmitPostMutationEmergencyMemory(
		const PostMutationEmergencyMemoryAdmission& a_state) noexcept
	{
		return EvaluatePostMutationEmergencyMemory(a_state).safe;
	}

	struct RelatchAllocationEstimate
	{
		std::uint64_t currentBytes = 0;
		std::uint64_t targetBytes = 0;
		bool reuseRenderTargets = false;
		bool targetUsesVendorResources = false;
		bool reuseSharedVendorResources = false;
		bool canReuseVendorRuntime = false;
		bool reuseWarmTargetRuntime = false;
		bool targetVendorRuntimeReady = false;
		bool recreateTargetVendorResources = false;
		bool commonTargetVendorResourcesReady = false;
	};

	struct RelatchAllocationEvaluation
	{
		std::uint64_t additionalBytes = 0;
		bool requiresFullTargetAllocation = false;
	};

	// Logical key compatibility cannot prove that physical targets still exist.
	// Any scheduled target/vendor recreation, shared-resource teardown, or
	// missing physical runtime therefore budgets the complete target profile
	// instead of only logical growth.
	[[nodiscard]] constexpr RelatchAllocationEvaluation EvaluateRelatchAllocation(
		const RelatchAllocationEstimate& a_state) noexcept
	{
		RelatchAllocationEvaluation result{};
		result.requiresFullTargetAllocation =
			!a_state.reuseRenderTargets ||
			(a_state.targetUsesVendorResources &&
				(!a_state.reuseSharedVendorResources ||
					a_state.recreateTargetVendorResources ||
					!a_state.commonTargetVendorResourcesReady ||
					((!a_state.canReuseVendorRuntime ||
						 !a_state.targetVendorRuntimeReady) &&
						!a_state.reuseWarmTargetRuntime)));
		result.additionalBytes = result.requiresFullTargetAllocation ?
		                             a_state.targetBytes :
		                         a_state.targetBytes > a_state.currentBytes ?
		                             a_state.targetBytes - a_state.currentBytes :
		                             0;
		return result;
	}

	[[nodiscard]] constexpr std::uint64_t EstimateRelatchAdditionalBytes(
		const RelatchAllocationEstimate& a_state) noexcept
	{
		return EvaluateRelatchAllocation(a_state).additionalBytes;
	}

	struct PostMutationSerializationRetirementAdmission
	{
		std::uint64_t serializationEpoch = 0;
		std::uint64_t expectedSerializationEpoch = 0;
		std::uint64_t unresolvedPhysicalMutationEpoch = 0;
		bool terminalFailureClaimed = false;
	};

	[[nodiscard]] constexpr bool CanRetirePostMutationSerialization(
		const PostMutationSerializationRetirementAdmission& a_state) noexcept
	{
		return a_state.serializationEpoch != 0 &&
		       a_state.expectedSerializationEpoch != 0 &&
		       a_state.serializationEpoch ==
		           a_state.expectedSerializationEpoch &&
		       a_state.unresolvedPhysicalMutationEpoch == 0 &&
		       !a_state.terminalFailureClaimed;
	}

	enum class PostMutationRecoveryAction : std::uint8_t
	{
		NotApplicable,
		ContinueConservative,
		AttemptOnce
	};

	struct PostMutationRecoveryAdmission
	{
		std::uint64_t mutationEpoch = 0;
		std::uint64_t mutationStartTickMs = 0;
		std::uint64_t currentTickMs = 0;
		std::uint64_t emergencyAttemptDelayMs = 0;
		bool attemptConsumed = false;
		bool recoveryOwned = false;
		bool loadingSerialOwned = false;
		bool cleanupAndTrimComplete = false;
		bool retirementReady = false;
		bool deviceHealthy = false;
		bool targetValid = false;
		bool emergencyMemorySafe = false;
	};

	// Emergency admission retains the target-specific projection and a fixed floor,
	// but may consume the normal conservative reserve down to a smaller final
	// reserve. Once physical mutation has started, one serialized attempt is
	// preferable to a permanent black hold; it is not a free pass to consume all
	// remaining machine commit. Ownership, cleanup, retirement, target validity,
	// and device health also remain hard.
	[[nodiscard]] constexpr PostMutationRecoveryAction SelectPostMutationRecoveryAction(
		const PostMutationRecoveryAdmission& a_state) noexcept
	{
		if (a_state.mutationEpoch == 0 || a_state.mutationStartTickMs == 0)
			return PostMutationRecoveryAction::NotApplicable;
		if (!a_state.attemptConsumed &&
			HasElapsedMonotonicDeadline(
				a_state.mutationStartTickMs,
				a_state.currentTickMs,
				a_state.emergencyAttemptDelayMs) &&
			a_state.recoveryOwned &&
			a_state.loadingSerialOwned &&
			a_state.cleanupAndTrimComplete &&
			a_state.retirementReady &&
			a_state.deviceHealthy &&
			a_state.targetValid &&
			a_state.emergencyMemorySafe) {
			return PostMutationRecoveryAction::AttemptOnce;
		}
		return PostMutationRecoveryAction::ContinueConservative;
	}

	[[nodiscard]] constexpr bool CanQueuePostMutationEmergencyRecovery(
		bool a_requestPending,
		bool a_emergencyAttemptConsumed,
		bool a_emergencyRecoveryRequested) noexcept
	{
		// One recovery request owns the complete serialized mutation chain. The
		// queue-ready flag clears when its consumer starts, before presentation can
		// collect stable stereo evidence, so it cannot by itself prevent a second
		// request from being published during that stabilization window.
		return !a_requestPending &&
		       !a_emergencyAttemptConsumed &&
		       !a_emergencyRecoveryRequested;
	}

	struct PostMutationEmergencyRecoveryTiming
	{
		PostMutationProgressPhase progressPhase =
			PostMutationProgressPhase::None;
		std::uint64_t mutationStartTickMs = 0;
		std::uint64_t lastProgressTickMs = 0;
		std::uint64_t currentTickMs = 0;
		std::uint64_t initialDelayMs = 0;
		std::uint64_t progressingStallDelayMs = 0;
	};

	// A newly published or stabilizing physical generation is not a stalled
	// generation. Anchor the emergency request to the latest forward progress and
	// give irreversible creator/presentation phases the longer no-progress window.
	[[nodiscard]] constexpr bool HasPostMutationEmergencyRecoveryStalled(
		const PostMutationEmergencyRecoveryTiming& a_timing) noexcept
	{
		if (a_timing.mutationStartTickMs == 0)
			return false;
		const std::uint64_t progressAnchor =
			a_timing.lastProgressTickMs != 0 ?
				a_timing.lastProgressTickMs :
				a_timing.mutationStartTickMs;
		const std::uint64_t delay =
			IsPostMutationRecoveryActivelyProgressing(a_timing.progressPhase) ?
				a_timing.progressingStallDelayMs :
				a_timing.initialDelayMs;
		return HasElapsedMonotonicDeadline(
			progressAnchor,
			a_timing.currentTickMs,
			delay);
	}

	struct PostLoadRecoveryStableFallbackOwnership
	{
		bool recoveryActive = false;
		bool physicalMutationStarted = false;
		std::uint64_t recoveryEpoch = 0;
		std::uint64_t expectedRecoveryEpoch = 0;
		std::uint64_t transitionEpoch = 0;
		std::uint64_t expectedTransitionEpoch = 0;
		std::uint64_t loadingSerial = 0;
		std::uint64_t currentLoadingSerial = 0;
	};

	[[nodiscard]] constexpr bool CanClaimPostLoadRecoveryStableFallback(
		const PostLoadRecoveryStableFallbackOwnership& a_state) noexcept
	{
		return a_state.recoveryActive &&
		       !a_state.physicalMutationStarted &&
		       a_state.expectedRecoveryEpoch != 0 &&
		       a_state.recoveryEpoch == a_state.expectedRecoveryEpoch &&
		       a_state.expectedTransitionEpoch != 0 &&
		       a_state.transitionEpoch == a_state.expectedTransitionEpoch &&
		       a_state.loadingSerial == a_state.currentLoadingSerial;
	}

	enum class PostLoadStableFallbackRequestAction : std::uint8_t
	{
		Discard,
		ReplayAfterStableRetention,
		HoldStartupNativeUntilRestart
	};

	struct PostLoadStableFallbackRequest
	{
		bool retainedStableContract = false;
		bool desiredTargetValid = false;
		bool desiredTargetActive = false;
		bool desiredTargetHasImmutableIdentity = false;
		bool desiredTargetRecoveryOrigin = false;
	};

	// A retained resource-backed contract can safely remain authoritative while a
	// real immutable request waits for another attempt. With no stable contract,
	// replay would immediately leave the coherent startup None presentation and
	// re-enter physical/vendor activation against the same failed memory state.
	[[nodiscard]] constexpr PostLoadStableFallbackRequestAction
	SelectPostLoadStableFallbackRequestAction(
		const PostLoadStableFallbackRequest& a_state) noexcept
	{
		if (!a_state.retainedStableContract) {
			return a_state.desiredTargetValid && a_state.desiredTargetActive ?
			           PostLoadStableFallbackRequestAction::HoldStartupNativeUntilRestart :
			           PostLoadStableFallbackRequestAction::Discard;
		}

		return a_state.desiredTargetValid &&
		               a_state.desiredTargetHasImmutableIdentity &&
		               !a_state.desiredTargetRecoveryOrigin ?
		           PostLoadStableFallbackRequestAction::ReplayAfterStableRetention :
		           PostLoadStableFallbackRequestAction::Discard;
	}

	struct StartupNativeFallbackExplicitRetryAdmission
	{
		bool fallbackActive = false;
		bool explicitCSMenuRequest = false;
		bool savedTargetActive = false;
		bool startupPresentationReleased = false;
		bool completedWorldFrame = false;
		bool exactNativeRuntimePlan = false;
		bool bootLatchAbsent = false;
		bool transitionIdle = false;
		bool physicalRecoveryResolved = false;
		bool memorySampleFresh = false;
		bool memoryPressureRecovered = false;
		bool deviceHealthy = false;
		bool noRecentOutOfMemory = false;
		bool shaderPipelineEnabled = false;
	};

	// The terminal startup fallback blocks every automatic path. A fresh CS-menu
	// action may create a new immutable request only after the unchanged native
	// presentation and all admission evidence are current again. The normal
	// relatch planner still owns projected residency and system-commit admission.
	[[nodiscard]] constexpr bool CanAdmitStartupNativeFallbackExplicitRetry(
		const StartupNativeFallbackExplicitRetryAdmission& a_state) noexcept
	{
		return a_state.fallbackActive &&
		       a_state.explicitCSMenuRequest &&
		       a_state.savedTargetActive &&
		       a_state.startupPresentationReleased &&
		       a_state.completedWorldFrame &&
		       a_state.exactNativeRuntimePlan &&
		       a_state.bootLatchAbsent &&
		       a_state.transitionIdle &&
		       a_state.physicalRecoveryResolved &&
		       a_state.memorySampleFresh &&
		       a_state.memoryPressureRecovered &&
		       a_state.deviceHealthy &&
		       a_state.noRecentOutOfMemory &&
		       a_state.shaderPipelineEnabled;
	}

	enum class StartupNativeFallbackControl : std::uint8_t
	{
		None,
		DisableSavedProfile,
		RetrySavedProfile
	};

	enum class StartupNativeFallbackControlAction : std::uint8_t
	{
		PassThrough,
		Reject,
		ResolveDisabled,
		ResolveRetry
	};

	struct StartupNativeFallbackControlRequest
	{
		bool fallbackActive = false;
		bool targetActive = false;
		bool csMenuOrigin = false;
		bool retryAdmitted = false;
		StartupNativeFallbackControl control =
			StartupNativeFallbackControl::None;
	};

	// Once terminal native fallback is armed, ordinary UI edits, API requests,
	// and post-load profile sync must not release or mutate it. Only an explicit
	// CS-menu disable, or a CS-menu retry that passed the full recovery admission,
	// may publish the request which resolves the fallback.
	[[nodiscard]] constexpr StartupNativeFallbackControlAction
	SelectStartupNativeFallbackControlAction(
		const StartupNativeFallbackControlRequest& a_request) noexcept
	{
		if (!a_request.fallbackActive)
			return StartupNativeFallbackControlAction::PassThrough;
		if (!a_request.csMenuOrigin)
			return StartupNativeFallbackControlAction::Reject;

		if (a_request.targetActive) {
			if (a_request.control ==
					StartupNativeFallbackControl::RetrySavedProfile &&
				a_request.retryAdmitted) {
				return StartupNativeFallbackControlAction::ResolveRetry;
			}
			return StartupNativeFallbackControlAction::Reject;
		}

		if (a_request.control ==
			StartupNativeFallbackControl::DisableSavedProfile) {
			return StartupNativeFallbackControlAction::ResolveDisabled;
		}
		return StartupNativeFallbackControlAction::Reject;
	}

	[[nodiscard]] constexpr bool CanResolveStartupNativeFallback(
		StartupNativeFallbackControlAction a_action,
		bool a_immutableRequestPublished) noexcept
	{
		return a_immutableRequestPublished &&
		       (a_action ==
					   StartupNativeFallbackControlAction::ResolveDisabled ||
				   a_action ==
					   StartupNativeFallbackControlAction::ResolveRetry);
	}

	struct PostLoadRecoveryTransitionBinding
	{
		bool recoveryActive = false;
		bool physicalMutationStarted = false;
		std::uint64_t recoveryEpoch = 0;
		std::uint64_t expectedRecoveryEpoch = 0;
		std::uint64_t transitionEpoch = 0;
		std::uint64_t expectedTransitionEpoch = 0;
		std::uint64_t loadingSerial = 0;
		std::uint64_t currentLoadingSerial = 0;
	};

	// A recovery may be bound once, before physical mutation, or observed again
	// by the same exact transition. Zero is accepted only as the unstarted state;
	// it is never accepted by the terminal fallback claim above.
	[[nodiscard]] constexpr bool CanBindPostLoadRecoveryTransition(
		const PostLoadRecoveryTransitionBinding& a_state) noexcept
	{
		return a_state.recoveryActive &&
		       !a_state.physicalMutationStarted &&
		       a_state.expectedRecoveryEpoch != 0 &&
		       a_state.recoveryEpoch == a_state.expectedRecoveryEpoch &&
		       a_state.expectedTransitionEpoch != 0 &&
		       (a_state.transitionEpoch == 0 ||
				   a_state.transitionEpoch == a_state.expectedTransitionEpoch) &&
		       a_state.loadingSerial == a_state.currentLoadingSerial;
	}

	struct PostMutationRecoveryTransitionTransfer
	{
		bool recoveryActive = false;
		bool physicalMutationStarted = false;
		std::uint64_t recoveryEpoch = 0;
		std::uint64_t expectedRecoveryEpoch = 0;
		std::uint64_t transitionEpoch = 0;
		std::uint64_t expectedSourceTransitionEpoch = 0;
		std::uint64_t destinationTransitionEpoch = 0;
		std::uint64_t loadingSerial = 0;
		std::uint64_t currentLoadingSerial = 0;
	};

	[[nodiscard]] constexpr bool CanTransferPostMutationRecoveryTransition(
		const PostMutationRecoveryTransitionTransfer& a_state) noexcept
	{
		return a_state.recoveryActive &&
		       a_state.physicalMutationStarted &&
		       a_state.expectedRecoveryEpoch != 0 &&
		       a_state.recoveryEpoch == a_state.expectedRecoveryEpoch &&
		       a_state.expectedSourceTransitionEpoch != 0 &&
		       a_state.transitionEpoch == a_state.expectedSourceTransitionEpoch &&
		       a_state.destinationTransitionEpoch != 0 &&
		       a_state.loadingSerial == a_state.currentLoadingSerial;
	}

	enum class PostLoadRecoveryRelatchOwnerAction : std::uint8_t
	{
		RejectStale,
		BindTarget,
		EvaluateTarget,
		EvaluateTransferSource
	};

	struct PostLoadRecoveryRelatchOwnership
	{
		bool recoveryActive = false;
		bool physicalMutationStarted = false;
		std::uint64_t recoveryEpoch = 0;
		std::uint64_t expectedRecoveryEpoch = 0;
		std::uint64_t transitionEpoch = 0;
		std::uint64_t targetTransitionEpoch = 0;
		std::uint64_t serializationEpoch = 0;
		std::uint64_t loadingSerial = 0;
		std::uint64_t currentLoadingSerial = 0;
	};

	// Before physical mutation, a recovery can bind exactly once to its target.
	// After mutation, the immutable serialization owner remains authoritative;
	// admission may evaluate its queued successor, but only the atomic transfer
	// path is allowed to replace the source transition with that successor.
	[[nodiscard]] constexpr PostLoadRecoveryRelatchOwnerAction SelectPostLoadRecoveryRelatchOwnerAction(
		const PostLoadRecoveryRelatchOwnership& a_state) noexcept
	{
		if (!a_state.recoveryActive ||
			a_state.expectedRecoveryEpoch == 0 ||
			a_state.recoveryEpoch != a_state.expectedRecoveryEpoch ||
			a_state.targetTransitionEpoch == 0 ||
			a_state.loadingSerial != a_state.currentLoadingSerial) {
			return PostLoadRecoveryRelatchOwnerAction::RejectStale;
		}

		if (!a_state.physicalMutationStarted) {
			if (a_state.serializationEpoch != 0)
				return PostLoadRecoveryRelatchOwnerAction::RejectStale;
			if (a_state.transitionEpoch == 0)
				return PostLoadRecoveryRelatchOwnerAction::BindTarget;
			return a_state.transitionEpoch == a_state.targetTransitionEpoch ?
			           PostLoadRecoveryRelatchOwnerAction::EvaluateTarget :
			           PostLoadRecoveryRelatchOwnerAction::RejectStale;
		}

		if (a_state.serializationEpoch == 0 ||
			a_state.transitionEpoch != a_state.serializationEpoch) {
			return PostLoadRecoveryRelatchOwnerAction::RejectStale;
		}
		return a_state.serializationEpoch == a_state.targetTransitionEpoch ?
		           PostLoadRecoveryRelatchOwnerAction::EvaluateTarget :
		           PostLoadRecoveryRelatchOwnerAction::EvaluateTransferSource;
	}

	enum class PresentationDeadlineAction : std::uint8_t
	{
		ReleasePresentation,
		RequestPreMutationFallback,
		ContinueCoveredRecovery
	};

	[[nodiscard]] constexpr PresentationDeadlineAction SelectPresentationDeadlineAction(
		bool a_transitionPendingOrApplying,
		bool a_physicalMutationUnresolved) noexcept
	{
		if (a_physicalMutationUnresolved)
			return PresentationDeadlineAction::ContinueCoveredRecovery;
		if (a_transitionPendingOrApplying)
			return PresentationDeadlineAction::RequestPreMutationFallback;
		return PresentationDeadlineAction::ReleasePresentation;
	}

	[[nodiscard]] constexpr bool OwnsPresentationDeadlineFallback(
		std::uint64_t a_fallbackHoldEpoch,
		std::uint64_t a_currentHoldEpoch,
		std::uint64_t a_fallbackLoadingSerial,
		std::uint64_t a_currentLoadingSerial) noexcept
	{
		return a_fallbackHoldEpoch != 0 &&
		       a_fallbackHoldEpoch == a_currentHoldEpoch &&
		       a_fallbackLoadingSerial == a_currentLoadingSerial;
	}

	struct PreMutationNativeFallbackDeadlineClock
	{
		std::uint64_t fallbackLoadingSerial = 0;
		std::uint64_t currentLoadingSerial = 0;
		std::uint64_t fallbackStartTickMs = 0;
		std::uint64_t currentLoadingCloseTickMs = 0;
		bool currentLoadingSerialOpen = false;
	};

	// A real LoadingMenu owns renderer mutation and therefore cannot consume the
	// provider-neutral worker's creator-entry budget. A newer serial transfers the
	// longer deadline to its authoritative close edge; duplicate service for the
	// same serial keeps the already-published start and cannot renew the clock.
	// Zero means that the caller must wait while the serial is open, or initialize
	// the closed generation exactly once if no diagnostic close tick was available.
	[[nodiscard]] constexpr std::uint64_t SelectPreMutationNativeFallbackStartTick(
		const PreMutationNativeFallbackDeadlineClock& a_clock) noexcept
	{
		if (a_clock.currentLoadingSerialOpen)
			return 0;
		if (a_clock.currentLoadingSerial != 0 &&
			a_clock.currentLoadingSerial != a_clock.fallbackLoadingSerial) {
			return a_clock.currentLoadingCloseTickMs;
		}
		return a_clock.fallbackStartTickMs;
	}

	struct PostMutationTerminalAdmission
	{
		std::uint64_t serializationEpoch = 0;
		std::uint64_t expectedSerializationEpoch = 0;
		std::uint64_t unresolvedPhysicalMutationEpoch = 0;
		std::uint64_t chainSerial = 0;
		std::uint64_t expectedChainSerial = 0;
		std::uint64_t chainStartTickMs = 0;
		std::uint64_t expectedChainStartTickMs = 0;
		std::uint64_t currentTickMs = 0;
		std::uint64_t terminalDeadlineMs = 0;
		bool terminalAlreadySignaled = false;
	};

	// Terminal ownership is an exact, immutable chain tuple. A published physical
	// mutation may already have cleared while its serialization owner remains live,
	// but a different nonzero physical epoch is never part of the claimed chain.
	// A zero deadline is deliberately reserved for immediate terminal conditions,
	// such as confirmed device loss or irreparable post-mutation resource contents.
	[[nodiscard]] constexpr bool CanClaimPostMutationTerminalFailure(
		const PostMutationTerminalAdmission& a_state) noexcept
	{
		if (a_state.terminalAlreadySignaled ||
			a_state.expectedSerializationEpoch == 0 ||
			a_state.serializationEpoch != a_state.expectedSerializationEpoch ||
			a_state.expectedChainSerial == 0 ||
			a_state.chainSerial != a_state.expectedChainSerial ||
			a_state.expectedChainStartTickMs == 0 ||
			a_state.chainStartTickMs != a_state.expectedChainStartTickMs ||
			(a_state.unresolvedPhysicalMutationEpoch != 0 &&
				a_state.unresolvedPhysicalMutationEpoch != a_state.serializationEpoch)) {
			return false;
		}

		return a_state.terminalDeadlineMs == 0 ||
		       HasElapsedMonotonicDeadline(
				   a_state.expectedChainStartTickMs,
				   a_state.currentTickMs,
				   a_state.terminalDeadlineMs);
	}

	[[nodiscard]] constexpr bool UsesVendorEvaluation(bool a_vendorMethod) noexcept
	{
		return a_vendorMethod;
	}

	[[nodiscard]] constexpr bool RequiresFSRCompatibility(bool a_fsrEvaluation) noexcept
	{
		return a_fsrEvaluation;
	}

	struct PresentationStabilizationAdmission
	{
		bool renderScaleActive = false;
		bool vendorMethod = false;
		bool nativeTargetConvergenceDeferred = false;
		bool nativeGuardOwned = false;
	};

	struct PresentationStabilizationPlan
	{
		bool vendorSubmitStage = false;
		bool nativePresentation = false;

		[[nodiscard]] constexpr bool RequiresProof() const noexcept
		{
			return vendorSubmitStage || nativePresentation;
		}
	};

	// Vendor evaluation does not imply vendor presentation. Fixed-resolution
	// DLSS/FSR evaluates in the main pass and must prove the native submit path.
	[[nodiscard]] constexpr PresentationStabilizationPlan
	SelectPresentationStabilization(
		const PresentationStabilizationAdmission& a_state) noexcept
	{
		return {
			.vendorSubmitStage =
				a_state.renderScaleActive && a_state.vendorMethod,
			.nativePresentation =
				!a_state.renderScaleActive &&
				(a_state.vendorMethod ||
					a_state.nativeTargetConvergenceDeferred ||
					a_state.nativeGuardOwned),
		};
	}

	struct CompatibleFSRRelatchReuseAdmission
	{
		bool targetIsFSR = false;
		bool previousWasFSR = false;
		bool resetPending = false;
		bool memoryPressureNormal = false;
		bool postLoadResetPending = false;
		bool preservingActiveContract = false;
		bool deviceLost = false;
		bool resourcesCompatible = false;
	};

	// Request origin does not change resource ownership. Preserve compatible live
	// FSR contexts unless concrete reset, pressure, or failure evidence owns them.
	[[nodiscard]] constexpr bool CanReuseCompatibleFSRResources(
		const CompatibleFSRRelatchReuseAdmission& a_state) noexcept
	{
		return a_state.targetIsFSR &&
		       a_state.previousWasFSR &&
		       !a_state.resetPending &&
		       a_state.memoryPressureNormal &&
		       !a_state.postLoadResetPending &&
		       !a_state.preservingActiveContract &&
		       !a_state.deviceLost &&
		       a_state.resourcesCompatible;
	}

	struct PreparedDLSSActivationReuseAdmission
	{
		bool directMenuRelatch = false;
		bool targetActive = false;
		bool targetIsDLSS = false;
		bool previousWasDLSS = false;
		bool resetPending = false;
		bool memoryPressureNormal = false;
		bool postLoadResetPending = false;
		bool preservingActiveContract = false;
		bool deviceLost = false;
		bool exactFullEyeProviderReady = false;
	};

	// A 1:1 DLSS provider and a Render Scale DLSS target share bounded, keyed
	// Streamline viewports. Preserve those vendor allocations only when both eyes
	// of the exact target profile are already proven and no recovery signal owns
	// teardown. Target-sized common textures remain generation-owned separately.
	[[nodiscard]] constexpr bool CanReusePreparedDLSSForActivation(
		const PreparedDLSSActivationReuseAdmission& a_state) noexcept
	{
		return a_state.directMenuRelatch &&
		       a_state.targetActive &&
		       a_state.targetIsDLSS &&
		       a_state.previousWasDLSS &&
		       !a_state.resetPending &&
		       a_state.memoryPressureNormal &&
		       !a_state.postLoadResetPending &&
		       !a_state.preservingActiveContract &&
		       !a_state.deviceLost &&
		       a_state.exactFullEyeProviderReady;
	}

	struct RuntimeFSRFallbackReuseAdmission
	{
		bool isVR = false;
		bool targetIsFSR = false;
		bool runtimePathChanged = false;
		bool runtimeFailureLatched = false;
		bool fsrResetPending = false;
		bool postLoadResetPending = false;
		bool primaryDeviceLost = false;
		bool hostResourcesCompatible = false;
	};

	// Runtime-provider quarantine detaches only the optional D3D12 ownership
	// domain. Keep a compatible D3D11 host context and reset its temporal history
	// instead of repeatedly destroying and rebuilding it while falling back.
	[[nodiscard]] constexpr bool CanReuseHostFSRAfterRuntimeFailure(
		const RuntimeFSRFallbackReuseAdmission& a_state) noexcept
	{
		return a_state.isVR &&
		       a_state.targetIsFSR &&
		       a_state.runtimePathChanged &&
		       a_state.runtimeFailureLatched &&
		       !a_state.fsrResetPending &&
		       !a_state.postLoadResetPending &&
		       !a_state.primaryDeviceLost &&
		       a_state.hostResourcesCompatible;
	}

	struct CommonResourceRecoveryAdmission
	{
		bool resourcesMissing = false;
		bool lifecycleOwnerActive = false;
		bool deviceLost = false;
		bool sameTerminalRequest = false;
	};

	// Common resource creation is synchronous. Once an unchanged request has
	// failed or returned an incomplete set, keep presentation in its coherent
	// fallback and wait for a real key-changing event instead of destroying and
	// recreating the same resources every frame.
	[[nodiscard]] constexpr bool CanAttemptCommonResourceRecovery(
		const CommonResourceRecoveryAdmission& a_state) noexcept
	{
		return a_state.resourcesMissing &&
		       !a_state.lifecycleOwnerActive &&
		       !a_state.deviceLost &&
		       !a_state.sameTerminalRequest;
	}

	[[nodiscard]] constexpr bool NeedsFSRResourceRecreate(
		bool a_fsrEvaluation,
		bool a_preservedResources) noexcept
	{
		return a_fsrEvaluation && !a_preservedResources;
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
