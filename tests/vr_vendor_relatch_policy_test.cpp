#include "Features/Upscaling/VRVendorRelatchPolicy.h"

#include <cstdint>
#include <limits>

namespace
{
	using namespace VRVendorRelatchPolicy;

	constexpr bool CoversWorkGateMasks()
	{
		if (ToMask(WorkGateSource::None) != 0u ||
			ToMask(WorkGateSource::ProcessStartup) != (1u << 0) ||
			ToMask(WorkGateSource::MainMenu) != (1u << 1) ||
			ToMask(WorkGateSource::LoadingMenu) != (1u << 2) ||
			ToMask(WorkGateSource::PreLoadGame) != (1u << 3) ||
			ToMask(WorkGateSource::GameLoadNotification) != (1u << 4)) {
			return false;
		}

		constexpr WorkGateMask expectedGameEntrySources =
			ToMask(WorkGateSource::ProcessStartup) |
			ToMask(WorkGateSource::MainMenu) |
			ToMask(WorkGateSource::PreLoadGame) |
			ToMask(WorkGateSource::GameLoadNotification);
		if (kNoWorkGateSources != 0u ||
			kGameEntryWorkGateSources != expectedGameEntrySources ||
			kAllWorkGateSources != (expectedGameEntrySources | ToMask(WorkGateSource::LoadingMenu)) ||
			HasSource(kGameEntryWorkGateSources, WorkGateSource::LoadingMenu)) {
			return false;
		}

		for (WorkGateMask sources = 0; sources <= kAllWorkGateSources; ++sources) {
			if (HasAny(sources) != (sources != 0u) ||
				AcquireSource(sources, WorkGateSource::None) != sources ||
				ReleaseSource(sources, WorkGateSource::None) != sources ||
				HasSource(sources, WorkGateSource::None)) {
				return false;
			}

			for (std::uint32_t bit = 0; bit < 5; ++bit) {
				const auto source = static_cast<WorkGateSource>(1u << bit);
				const WorkGateMask sourceMask = ToMask(source);
				const WorkGateMask acquired = AcquireSource(sources, source);
				const WorkGateMask released = ReleaseSource(sources, source);
				if (HasSource(sources, source) != ((sources & sourceMask) != 0u) ||
					acquired != (sources | sourceMask) ||
					released != (sources & ~sourceMask) ||
					AcquireSource(acquired, source) != acquired ||
					ReleaseSource(released, source) != released) {
					return false;
				}
			}

			for (WorkGateMask candidates = 0; candidates <= kAllWorkGateSources; ++candidates) {
				if (HasAny(sources, candidates) != ((sources & candidates) != 0u)) {
					return false;
				}
			}
		}

		return true;
	}

	constexpr bool CoversWorkGateState()
	{
		constexpr WorkGateMask firstMask = ToMask(WorkGateSource::LoadingMenu);
		constexpr WorkGateState first = AdvanceState(0, firstMask);
		if (GetStateMask(first) != firstMask || GetStateEpoch(first) != 1u)
			return false;

		// Re-acquiring the same source advances ownership even though its bit is
		// unchanged, so convergence from the older epoch cannot clear it.
		constexpr WorkGateState reacquired = AdvanceState(first, firstMask);
		if (GetStateMask(reacquired) != firstMask || GetStateEpoch(reacquired) != 2u)
			return false;

		constexpr WorkGateMask secondMask =
			firstMask | ToMask(WorkGateSource::GameLoadNotification);
		constexpr WorkGateState second = AdvanceState(reacquired, secondMask);
		if (GetStateMask(second) != secondMask || GetStateEpoch(second) != 3u)
			return false;

		constexpr WorkGateState wrapped =
			(static_cast<WorkGateState>(std::numeric_limits<std::uint32_t>::max()) << kWorkGateStateMaskBits) |
			firstMask;
		constexpr WorkGateState afterWrap = AdvanceState(wrapped, secondMask);
		return GetStateMask(afterWrap) == secondMask && GetStateEpoch(afterWrap) == 0u;
	}

	constexpr bool CoversGameEntryConvergence()
	{
		for (std::uint32_t bits = 0; bits < (1u << 9); ++bits) {
			const GameEntryConvergence state{
				.hasGateOwner = (bits & (1u << 0)) != 0,
				.mainMenuActive = (bits & (1u << 1)) != 0,
				.loadingPresentationActive = (bits & (1u << 2)) != 0,
				.raceSexPresentationActive = (bits & (1u << 3)) != 0,
				.saveLoadProtectionActive = (bits & (1u << 4)) != 0,
				.completedWorldFrame = (bits & (1u << 5)) != 0,
				.recoveryPending = (bits & (1u << 6)) != 0,
				.relatchPending = (bits & (1u << 7)) != 0,
				.profileTransitionPending = (bits & (1u << 8)) != 0,
			};
			const bool expected =
				state.hasGateOwner &&
				!state.mainMenuActive &&
				!state.loadingPresentationActive &&
				!state.raceSexPresentationActive &&
				!state.saveLoadProtectionActive &&
				state.completedWorldFrame &&
				!state.recoveryPending &&
				!state.relatchPending &&
				!state.profileTransitionPending;
			if (CanReleaseGameEntryVendorGate(state) != expected) {
				return false;
			}
		}

		return true;
	}

	constexpr bool CoversStartupMainMenuStateDefinition()
	{
		StartupMainMenuStateDefinition state{};
		if (ShouldDefineStartupMainMenuState(state))
			return false;

		state.isVR = true;
		state.startupMainMenuObserved = true;
		state.shaderCompilationComplete = true;
		if (!ShouldDefineStartupMainMenuState(state))
			return false;

		state.completedWorldFrame = true;
		if (ShouldDefineStartupMainMenuState(state))
			return false;

		state.completedWorldFrame = false;
		state.alreadyDefined = true;
		return !ShouldDefineStartupMainMenuState(state);
	}

	constexpr bool CoversRenderScaleRuntimeActivation()
	{
		RenderScaleRuntimeActivation state{};
		if (CanPresentRenderScaleRuntime(state))
			return false;

		state.loaded = true;
		if (!CanPresentRenderScaleRuntime(state))
			return false;

		state.isVR = true;
		if (CanPresentRenderScaleRuntime(state))
			return false;

		state.completedWorldFrame = true;
		if (!CanPresentRenderScaleRuntime(state))
			return false;

		state.loadingPresentationActive = true;
		if (CanPresentRenderScaleRuntime(state))
			return false;

		state.unresolvedProfileSync = true;
		state.postLoadResetPending = true;
		state.establishedPhysicalContract = true;
		if (!CanPresentRenderScaleRuntime(state))
			return false;

		state.establishedPhysicalContract = false;
		return !CanPresentRenderScaleRuntime(state);
	}

	constexpr bool CoversRenderTransitionCoverAdmission()
	{
		for (std::uint32_t bits = 0; bits < (1u << 5); ++bits) {
			const RenderTransitionCoverAdmission state{
				.isVR = (bits & (1u << 0)) != 0,
				.renderChangePublished = (bits & (1u << 1)) != 0,
				.loadingSerialMatches = (bits & (1u << 2)) != 0,
				.loadingTransitionOpenOrTail = (bits & (1u << 3)) != 0,
				.presentationCoverActive = (bits & (1u << 4)) != 0,
			};
			const bool expected =
				state.isVR &&
				state.renderChangePublished &&
				state.loadingSerialMatches &&
				state.loadingTransitionOpenOrTail &&
				!state.presentationCoverActive;
			if (ShouldArmRenderTransitionCover(state) != expected)
				return false;
		}

		return true;
	}

	constexpr bool CoversLoadingFadeHoldAdmission()
	{
		for (std::uint32_t bits = 0; bits < (1u << 6); ++bits) {
			const LoadingFadeHoldAdmission state{
				.isVR = (bits & (1u << 0)) != 0,
				.blackFade = (bits & (1u << 1)) != 0,
				.fadingIn = (bits & (1u << 2)) != 0,
				.presentationCoverActive = (bits & (1u << 3)) != 0,
				.loadingMenuClosed = (bits & (1u << 4)) != 0,
				.releaseAlreadyScheduled = (bits & (1u << 5)) != 0,
			};
			const bool expected =
				state.isVR &&
				state.blackFade &&
				state.fadingIn &&
				state.presentationCoverActive &&
				state.loadingMenuClosed &&
				!state.releaseAlreadyScheduled;
			if (ShouldHoldLoadingFadeIn(state) != expected)
				return false;
		}

		return true;
	}

	constexpr bool CoversLoadingPresentationReleaseAdmission()
	{
		LoadingPresentationReleaseAdmission ready{};
		if (!IsLoadingPresentationReleaseReady(ready))
			return false;

		for (std::uint32_t bit = 0; bit < 9; ++bit) {
			LoadingPresentationReleaseAdmission blocked{};
			switch (bit) {
			case 0:
				blocked.engineSaveLoadActivityActive = true;
				break;
			case 1:
				blocked.statePostLoadResetPending = true;
				break;
			case 2:
				blocked.hmdClearMaskDeferred = true;
				break;
			case 3:
				blocked.upscalingPostLoadResetPending = true;
				break;
			case 4:
				blocked.renderTargetRecreatePending = true;
				break;
			case 5:
				blocked.renderTargetRecreateInProgress = true;
				break;
			case 6:
				blocked.upscalingTransitionPending = true;
				break;
			case 7:
				blocked.stabilizerSyncScheduled = true;
				break;
			case 8:
				blocked.stabilizerSyncUnresolved = true;
				break;
			default:
				return false;
			}
			if (IsLoadingPresentationReleaseReady(blocked))
				return false;
		}

		return true;
	}

	constexpr bool CoversBufferedDoorRequestCoalescing()
	{
		constexpr BufferedDoorRequestCoalescingAdmission match{
			.existingRequestValid = true,
			.incomingRequestValid = true,
			.existingBufferedDoorHandoff = true,
			.incomingBufferedDoorHandoff = true,
			.sameOrigin = true,
			.sameCompleteTarget = true,
			.existingLoadingSerial = 41,
			.incomingLoadingSerial = 41,
		};
		if (!CanCoalesceBufferedDoorRequest(match))
			return false;

		auto rejected = match;
		rejected.existingRequestValid = false;
		if (CanCoalesceBufferedDoorRequest(rejected))
			return false;
		rejected = match;
		rejected.incomingRequestValid = false;
		if (CanCoalesceBufferedDoorRequest(rejected))
			return false;
		rejected = match;
		rejected.existingBufferedDoorHandoff = false;
		if (CanCoalesceBufferedDoorRequest(rejected))
			return false;
		rejected = match;
		rejected.incomingBufferedDoorHandoff = false;
		if (CanCoalesceBufferedDoorRequest(rejected))
			return false;
		rejected = match;
		rejected.sameOrigin = false;
		if (CanCoalesceBufferedDoorRequest(rejected))
			return false;
		rejected = match;
		rejected.sameCompleteTarget = false;
		if (CanCoalesceBufferedDoorRequest(rejected))
			return false;
		rejected = match;
		rejected.existingLoadingSerial = 0;
		if (CanCoalesceBufferedDoorRequest(rejected))
			return false;
		rejected = match;
		rejected.incomingLoadingSerial = 42;
		return !CanCoalesceBufferedDoorRequest(rejected);
	}

	constexpr bool CoversStabilizerDestinationSyncReadiness()
	{
		if (SelectStabilizerSourceCell(0x1234u, 0x5678u) != 0x1234u)
			return false;
		if (SelectStabilizerSourceCell(0u, 0x5678u) != 0x5678u)
			return false;

		for (std::uint32_t bits = 0; bits < (1u << 2); ++bits) {
			const bool settingsMatch = (bits & (1u << 0)) != 0;
			const bool controllerMatch = (bits & (1u << 1)) != 0;
			if (ShouldPublishStabilizerDestinationProfile(
					settingsMatch,
					controllerMatch) !=
				(!settingsMatch || !controllerMatch)) {
				return false;
			}
		}

		for (std::uint32_t bits = 0; bits < (1u << 7); ++bits) {
			const StabilizerControllerTargetAdmission admission{
				.profileValid = (bits & (1u << 0)) != 0,
				.targetEpochKnown = (bits & (1u << 1)) != 0,
				.profileOwnsTargetEpoch = (bits & (1u << 2)) != 0,
				.methodMatches = (bits & (1u << 3)) != 0,
				.qualityMatches = (bits & (1u << 4)) != 0,
				.renderScaleModeMatches = (bits & (1u << 5)) != 0,
				.dlssPresetMatchesOrIrrelevant = (bits & (1u << 6)) != 0,
			};
			const bool expected = bits == ((1u << 7) - 1u);
			if (MatchesStabilizerControllerTarget(admission) != expected)
				return false;
		}

		for (std::uint32_t bits = 0; bits < (1u << 6); ++bits) {
			const StabilizerDestinationSyncReadiness readiness{
				.completedWorldFrameAfterClose = (bits & (1u << 0)) != 0,
				.sourceCellKnown = (bits & (1u << 1)) != 0,
				.currentCellKnown = (bits & (1u << 2)) != 0,
				.destinationCellChanged = (bits & (1u << 3)) != 0,
				.completedWorldFrameAfterDestinationObservation =
					(bits & (1u << 4)) != 0,
				.identityFallbackElapsed = (bits & (1u << 5)) != 0,
			};
			const bool expected =
				readiness.completedWorldFrameAfterClose &&
				(!readiness.sourceCellKnown ||
					(!readiness.currentCellKnown ?
							readiness.identityFallbackElapsed :
						readiness.destinationCellChanged ?
							readiness.completedWorldFrameAfterDestinationObservation :
							readiness.identityFallbackElapsed));
			if (IsStabilizerDestinationSyncReady(readiness) != expected)
				return false;
		}

		return true;
	}

	constexpr bool CoversLifecycleMutationAdmission()
	{
		for (std::uint32_t bits = 0; bits < (1u << 4); ++bits) {
			for (WorkGateMask gateSources = 0; gateSources <= kAllWorkGateSources; ++gateSources) {
				const LifecycleMutationAdmission state{
					.isVR = (bits & (1u << 0)) != 0,
					.gateSources = gateSources,
					.postLoadResetPending = (bits & (1u << 1)) != 0,
					.relatchPending = (bits & (1u << 2)) != 0,
					.relatchInProgress = (bits & (1u << 3)) != 0,
				};
				const bool expected =
					!state.isVR ||
					(state.gateSources == kNoWorkGateSources &&
						!state.postLoadResetPending &&
						!state.relatchPending &&
						!state.relatchInProgress);
				if (CanMutateVendorLifecycle(state) != expected) {
					return false;
				}
			}
		}

		return true;
	}

	constexpr bool CoversDispatchAdmission()
	{
		for (std::uint32_t bits = 0; bits < (1u << 4); ++bits) {
			const DispatchAdmission state{
				.isVR = (bits & (1u << 0)) != 0,
				.vendorEvaluationSelected = (bits & (1u << 1)) != 0,
				.resourcesReady = (bits & (1u << 2)) != 0,
				.relatchInProgress = (bits & (1u << 3)) != 0,
			};
			const bool expected =
				state.vendorEvaluationSelected &&
				state.resourcesReady &&
				(!state.isVR || !state.relatchInProgress);
			if (CanDispatchVendorEvaluation(state) != expected) {
				return false;
			}
		}

		return true;
	}

	constexpr bool CoversNativeRestorePresentationAdmission()
	{
		for (std::uint32_t bits = 0; bits < (1u << 3); ++bits) {
			const NativeRestorePresentationAdmission admission{
				.targetUsesVendorEvaluation = (bits & (1u << 0)) != 0,
				.exactPhysicalNativeContinuity = (bits & (1u << 1)) != 0,
				.exactRuntimeContract = (bits & (1u << 2)) != 0,
			};
			const bool expected =
				admission.exactPhysicalNativeContinuity &&
				(admission.exactRuntimeContract ||
					!admission.targetUsesVendorEvaluation);
			if (CanAcceptNativeRestorePresentation(admission) != expected)
				return false;
		}
		return true;
	}

	constexpr bool CoversBoundedNativeRestorePresentationRecovery()
	{
		if (kNativeRestoreMaximumPhysicalRecoveryAttempts != 1u ||
			kNativeRestoreMaximumRecoveryAttempts != 2u) {
			return false;
		}

		for (std::uint32_t attempt = 0; attempt <= 4u; ++attempt) {
			for (std::uint32_t bits = 0; bits < (1u << 2); ++bits) {
				const bool candidatePending = (bits & (1u << 0)) != 0;
				const bool duplicateCycle = (bits & (1u << 1)) != 0;
				const bool expectedSchedule =
					attempt < kNativeRestoreMaximumRecoveryAttempts &&
					!candidatePending &&
					!duplicateCycle;
				if (CanScheduleNativeRestorePresentationRecovery(
						attempt,
						candidatePending,
						duplicateCycle) != expectedSchedule) {
					return false;
				}
			}

			const auto expected = attempt == 1u ?
			                          NativeRestorePresentationRecoveryAction::PhysicalRetry :
			                      attempt == 2u ?
			                          NativeRestorePresentationRecoveryAction::LogicalFallback :
			                          NativeRestorePresentationRecoveryAction::Exhausted;
			if (SelectNativeRestorePresentationRecoveryAction(attempt) != expected)
				return false;
		}
		return SelectNativeRestorePresentationRecoveryAction(
				   std::numeric_limits<std::uint32_t>::max()) ==
		       NativeRestorePresentationRecoveryAction::Exhausted;
	}

	constexpr bool CoversVendorResourcePredicates()
	{
		for (std::uint32_t bits = 0; bits < (1u << 3); ++bits) {
			const bool vendorEvaluation = (bits & (1u << 0)) != 0;
			const bool preservedResources = (bits & (1u << 1)) != 0;
			const bool recreatedResources = (bits & (1u << 2)) != 0;
			if (UsesVendorEvaluation(vendorEvaluation) != vendorEvaluation ||
				RequiresFSRCompatibility(vendorEvaluation) != vendorEvaluation ||
				NeedsFSRResourceRecreate(vendorEvaluation, preservedResources) !=
					(vendorEvaluation && !preservedResources) ||
				NeedsDeferredFSRReset(vendorEvaluation, preservedResources, recreatedResources) !=
					(vendorEvaluation && !preservedResources && !recreatedResources)) {
				return false;
			}
		}

		return true;
	}

	constexpr bool CoversStereoRelatchAdmission()
	{
		for (std::uint32_t currentFrame = 0; currentFrame < 3; ++currentFrame) {
			for (std::uint32_t admissionFrame = 0; admissionFrame < 3; ++admissionFrame) {
				for (std::uint32_t eyeMask = 0; eyeMask < 8; ++eyeMask) {
					for (std::uint64_t relatchEpoch = 0; relatchEpoch < 3; ++relatchEpoch) {
						for (std::uint64_t deferredEpoch = 0; deferredEpoch < 3; ++deferredEpoch) {
							const std::uint32_t stereoMask = eyeMask & 0x3u;
							const bool expected =
								relatchEpoch != 0 &&
								relatchEpoch != deferredEpoch &&
								currentFrame == admissionFrame &&
								(stereoMask == 0x1u || stereoMask == 0x2u);
							if (ShouldDeferPhysicalRelatchForStereo(
									currentFrame,
									admissionFrame,
									eyeMask,
									relatchEpoch,
									deferredEpoch) != expected) {
								return false;
							}
						}
					}
				}
			}
		}
		return true;
	}

	constexpr bool CoversNativeRestoreSequence()
	{
		constexpr std::uint64_t epoch = 7;
		NativeRestoreProgress progress{};
		std::uint32_t vendorTeardownStarts = 0;
		std::uint32_t sharedRetirementStarts = 0;
		std::uint32_t physicalRecreations = 0;

		// First admission starts one teardown transaction.
		if (SelectNativeRestoreAction(
				epoch,
				progress.ownerEpoch,
				progress.phase) !=
			NativeRestoreAction::BeginVendorTeardown) {
			return false;
		}
		if (!BeginNativeRestore(progress, epoch))
			return false;
		if (!HasNativeRestoreTransaction(progress))
			return false;
		++vendorTeardownStarts;

		// Repeated backend-pending frames only poll that transaction.
		for (std::uint32_t poll = 0; poll < 2; ++poll) {
			if (SelectNativeRestoreAction(
					epoch,
					progress.ownerEpoch,
					progress.phase) !=
				NativeRestoreAction::PollVendorTeardown) {
				return false;
			}
		}
		if (BeginNativeRestore(progress, epoch + 1) ||
			SelectNativeRestoreAction(
				epoch + 1,
				progress.ownerEpoch,
				progress.phase) !=
				NativeRestoreAction::PollVendorTeardown) {
			return false;
		}

		// Shared resources can retire before a later vendor step becomes ready.
		// Preserve that exact serial while the vendor transaction is still pending.
		constexpr std::uint64_t retirementSerial = 41;
		if (!RecordNativeRestoreRetirement(
				progress,
				epoch,
				retirementSerial) ||
			progress.phase != NativeRestorePhase::VendorTeardownPending ||
			CompleteNativeRestoreRetirement(
				progress,
				epoch,
				retirementSerial)) {
			return false;
		}

		// A later ready poll advances to the already-issued retirement instead of
		// incorrectly treating the transaction as a no-resource completion.
		if (!CompleteNativeRestoreVendorTeardown(progress, epoch) ||
			progress.phase != NativeRestorePhase::SharedRetirementPending ||
			progress.retirementSerial != retirementSerial) {
			return false;
		}
		++sharedRetirementStarts;
		if (SelectNativeRestoreAction(
				epoch,
				progress.ownerEpoch,
				progress.phase) !=
				NativeRestoreAction::DrainCurrentRetirement ||
			CompleteNativeRestoreRetirement(
				progress,
				epoch,
				retirementSerial - 1) ||
			!CompleteNativeRestoreRetirement(
				progress,
				epoch,
				retirementSerial)) {
			return false;
		}

		// The general cleanup service may observe the fence between relatch ticks.
		// Its durable completion must resume recreation instead of restarting reset.
		if (SelectNativeRestoreAction(
				epoch,
				progress.ownerEpoch,
				progress.phase) !=
			NativeRestoreAction::RecreatePhysicalTargets) {
			return false;
		}
		++physicalRecreations;

		if (vendorTeardownStarts != 1 ||
			sharedRetirementStarts != 1 ||
			physicalRecreations != 1) {
			return false;
		}

		// A no-resource teardown is completed explicitly; it does not inherit the
		// latest unrelated retirement serial.
		progress = {};
		if (HasNativeRestoreTransaction(progress))
			return false;
		if (!BeginNativeRestore(progress, epoch + 1) ||
			!CompleteNativeRestoreVendorTeardown(progress, epoch + 1) ||
			progress.retirementSerial != 0 ||
			progress.phase != NativeRestorePhase::Complete) {
			return false;
		}
		if (SelectNativeRestoreAction(
				epoch + 1,
				progress.ownerEpoch,
				progress.phase) !=
			NativeRestoreAction::RecreatePhysicalTargets) {
			return false;
		}

		// Guard admission is valid only for the controller's exact owner epoch.
		// A terminal abort clears the transaction and permits a later epoch to
		// begin instead of inheriting an unbounded retry/black-keepalive loop.
		if (!CanArmNativeRestoreGuard(epoch + 1, epoch + 1) ||
			CanArmNativeRestoreGuard(epoch + 1, epoch + 2) ||
			AbortNativeRestore(progress, epoch) ||
			!AbortNativeRestore(progress, epoch + 1) ||
			HasNativeRestoreTransaction(progress) ||
			!BeginNativeRestore(progress, epoch + 2) ||
			progress.ownerEpoch != epoch + 2 ||
			progress.phase != NativeRestorePhase::VendorTeardownPending) {
			return false;
		}

		return true;
	}

	constexpr bool CoversProvenNativeRestoreRetirementResume()
	{
		constexpr NativeRestoreFenceReadyResumeAdmission ready{
			.relatchPending = true,
			.queuedEpoch = 17,
			.controllerTargetEpoch = 17,
			.progress = {
				.ownerEpoch = 17,
				.retirementSerial = 41,
				.phase = NativeRestorePhase::Complete,
			},
			.operation = {
				.valid = true,
				.destroyDLSSResources = true,
				.destroySharedResources = true,
				.preserveVRIntermediateTextures = false,
			},
			.completedRetirementSerial = 42,
			.queuedFrame = 100,
			.currentFrame = 103,
			.queuedDelayFrames = 6,
			.ordinaryRetryFrames = 6,
			.queuedNativeRestoreRetirementSerial = 41,
			.loadingMenuOpen = false,
			.loadingSerialMatches = true,
		};
		if (!CanResumeNativeRestoreAfterProvenRetirement(ready))
			return false;

		const auto rejected = [](const NativeRestoreFenceReadyResumeAdmission& a_admission) {
			return !CanResumeNativeRestoreAfterProvenRetirement(a_admission);
		};
		auto candidate = ready;
		candidate.relatchPending = false;
		if (!rejected(candidate))
			return false;
		candidate = ready;
		candidate.queuedEpoch = 0;
		if (!rejected(candidate))
			return false;
		candidate = ready;
		candidate.queuedEpoch = 18;
		if (!rejected(candidate))
			return false;
		candidate = ready;
		candidate.controllerTargetEpoch = 18;
		if (!rejected(candidate))
			return false;
		candidate = ready;
		candidate.progress.ownerEpoch = 18;
		if (!rejected(candidate))
			return false;
		candidate = ready;
		candidate.progress.phase = NativeRestorePhase::SharedRetirementPending;
		if (!rejected(candidate))
			return false;
		candidate = ready;
		candidate.progress.retirementSerial = 0;
		if (!rejected(candidate))
			return false;
		candidate = ready;
		candidate.completedRetirementSerial = 40;
		if (!rejected(candidate))
			return false;
		candidate = ready;
		candidate.operation.valid = false;
		if (!rejected(candidate))
			return false;
		candidate = ready;
		candidate.operation.destroyDLSSResources = false;
		if (!rejected(candidate))
			return false;
		candidate = ready;
		candidate.operation.destroySharedResources = false;
		if (!rejected(candidate))
			return false;
		candidate = ready;
		candidate.operation.preserveVRIntermediateTextures = true;
		if (!rejected(candidate))
			return false;
		candidate = ready;
		candidate.ordinaryRetryFrames = 0;
		if (!rejected(candidate))
			return false;
		candidate = ready;
		candidate.queuedDelayFrames = 12;
		if (!rejected(candidate))
			return false;
		candidate = ready;
		candidate.queuedNativeRestoreRetirementSerial = 0;
		if (!rejected(candidate))
			return false;
		candidate = ready;
		candidate.queuedNativeRestoreRetirementSerial = 40;
		if (!rejected(candidate))
			return false;
		candidate = ready;
		candidate.queuedFrame = 0;
		if (!rejected(candidate))
			return false;
		candidate = ready;
		candidate.currentFrame = 99;
		if (!rejected(candidate))
			return false;
		candidate = ready;
		candidate.currentFrame = 100;
		if (!rejected(candidate))
			return false;
		candidate = ready;
		candidate.currentFrame = 106;
		if (!rejected(candidate))
			return false;
		candidate = ready;
		candidate.loadingMenuOpen = true;
		if (!rejected(candidate))
			return false;
		candidate = ready;
		candidate.loadingSerialMatches = false;
		return rejected(candidate);
	}

	constexpr bool CoversNativeRestoreActionSelection()
	{
		for (std::uint64_t requestedEpoch = 0; requestedEpoch < 3; ++requestedEpoch) {
			for (std::uint64_t ownerEpoch = 0; ownerEpoch < 3; ++ownerEpoch) {
				for (std::uint32_t phaseValue = 0; phaseValue < 4; ++phaseValue) {
					const auto phase = static_cast<NativeRestorePhase>(phaseValue);
					NativeRestoreAction expected = NativeRestoreAction::BeginVendorTeardown;
					if (requestedEpoch != 0 && ownerEpoch != requestedEpoch && ownerEpoch != 0) {
						if (phase == NativeRestorePhase::VendorTeardownPending)
							expected = NativeRestoreAction::PollVendorTeardown;
						else if (phase == NativeRestorePhase::SharedRetirementPending)
							expected = NativeRestoreAction::DrainPreviousRetirement;
					} else if (requestedEpoch != 0 && ownerEpoch == requestedEpoch) {
						switch (phase) {
						case NativeRestorePhase::VendorTeardownPending:
							expected = NativeRestoreAction::PollVendorTeardown;
							break;
						case NativeRestorePhase::SharedRetirementPending:
							expected = NativeRestoreAction::DrainCurrentRetirement;
							break;
						case NativeRestorePhase::Complete:
							expected = NativeRestoreAction::RecreatePhysicalTargets;
							break;
						default:
							break;
						}
					}

					if (SelectNativeRestoreAction(
							requestedEpoch,
							ownerEpoch,
							phase) != expected) {
						return false;
					}
				}
			}
		}
		return true;
	}

	constexpr bool CoversNativeRestoreTeardownDisposition()
	{
		constexpr std::uint64_t epoch = 11;
		constexpr std::uint64_t retirementSerial = 29;
		for (std::uint32_t value = 0; value < 4; ++value) {
			const auto outcome = static_cast<NativeRestoreTeardownOutcome>(value);
			const auto disposition = SelectNativeRestoreTeardownDisposition(outcome);
			if ((outcome == NativeRestoreTeardownOutcome::Ready) !=
				(disposition == NativeRestoreTeardownDisposition::Complete)) {
				return false;
			}
			if ((outcome == NativeRestoreTeardownOutcome::Pending) !=
				(disposition == NativeRestoreTeardownDisposition::Retry)) {
				return false;
			}
			if ((outcome == NativeRestoreTeardownOutcome::FailedBeforeRelease ||
					outcome == NativeRestoreTeardownOutcome::FailedAfterMutation) !=
				(disposition == NativeRestoreTeardownDisposition::Abort)) {
				return false;
			}

			NativeRestoreProgress progress{};
			if (!BeginNativeRestore(progress, epoch))
				return false;
			if (disposition == NativeRestoreTeardownDisposition::Complete) {
				if (!CompleteNativeRestoreVendorTeardown(progress, epoch))
					return false;
			} else if (disposition == NativeRestoreTeardownDisposition::Abort) {
				if (!AbortNativeRestore(progress, epoch))
					return false;
			}
			const bool permitsImmediateRecreation =
				SelectNativeRestoreAction(epoch, progress.ownerEpoch, progress.phase) ==
				NativeRestoreAction::RecreatePhysicalTargets;
			if (permitsImmediateRecreation !=
				(outcome == NativeRestoreTeardownOutcome::Ready)) {
				return false;
			}

			progress = {};
			if (!BeginNativeRestore(progress, epoch) ||
				!RecordNativeRestoreRetirement(progress, epoch, retirementSerial)) {
				return false;
			}
			if (disposition == NativeRestoreTeardownDisposition::Complete) {
				if (!CompleteNativeRestoreVendorTeardown(progress, epoch))
					return false;
			} else if (disposition == NativeRestoreTeardownDisposition::Abort) {
				if (!AbortNativeRestore(progress, epoch))
					return false;
			}
			if (SelectNativeRestoreAction(epoch, progress.ownerEpoch, progress.phase) ==
				NativeRestoreAction::RecreatePhysicalTargets) {
				return false;
			}
			if (outcome == NativeRestoreTeardownOutcome::Ready) {
				if (!CompleteNativeRestoreRetirement(progress, epoch, retirementSerial) ||
					SelectNativeRestoreAction(epoch, progress.ownerEpoch, progress.phase) !=
						NativeRestoreAction::RecreatePhysicalTargets) {
					return false;
				}
			}
		}
		return true;
	}

	constexpr bool CoversNativeRestoreOperationStrength()
	{
		const NativeRestoreOperation preserveShared{
			.valid = true,
			.destroySharedResources = true,
			.preserveVRIntermediateTextures = true,
		};
		const NativeRestoreOperation retireShared{
			.valid = true,
			.destroySharedResources = true,
			.preserveVRIntermediateTextures = false,
		};
		if (preserveShared.Covers(retireShared) ||
			!retireShared.Covers(preserveShared)) {
			return false;
		}

		const NativeRestoreOperation weakFSRTeardown{
			.valid = true,
			.destroyFSRResources = true,
		};
		const NativeRestoreOperation strongFSRTeardown{
			.valid = true,
			.destroyFSRResources = true,
			.waitForFSRIdleTeardown = true,
		};
		return !weakFSRTeardown.Covers(strongFSRTeardown) &&
		       strongFSRTeardown.Covers(weakFSRTeardown) &&
		       !NativeRestoreOperation{}.Covers(weakFSRTeardown);
	}

	constexpr bool CoversNativeRestoreOwnership()
	{
		constexpr std::uint64_t epoch = 17;
		if (!UsesEpochOwnedNativeRestore({
				.lowPeakNativeRestore = true,
				.previousVendorWasDLSS = true,
				.targetEpoch = epoch,
			})) {
			return false;
		}
		if (!UsesEpochOwnedNativeRestore({
				.lowPeakNativeRestore = true,
				.previousVendorWasFSR = true,
				.targetEpoch = epoch,
			})) {
			return false;
		}
		if (UsesEpochOwnedNativeRestore({
				.lowPeakNativeRestore = true,
				.targetEpoch = epoch,
			})) {
			return false;
		}
		if (UsesEpochOwnedNativeRestore({
				.lowPeakNativeRestore = true,
				.previousVendorWasFSR = true,
			})) {
			return false;
		}
		if (UsesEpochOwnedNativeRestore({
				.previousVendorWasDLSS = true,
				.targetEpoch = epoch,
			}) ||
			UsesEpochOwnedNativeRestore({
				.previousVendorWasFSR = true,
				.targetEpoch = epoch,
			})) {
			return false;
		}
		if (!UsesEpochOwnedNativeRestore({
				.targetEpoch = epoch,
				.progressOwnerEpoch = epoch,
			})) {
			return false;
		}
		return !UsesEpochOwnedNativeRestore({
			.targetEpoch = epoch,
			.progressOwnerEpoch = epoch + 1,
		});
	}

	constexpr bool CoversStableNativeRestorePreservation()
	{
		constexpr StableNativeRestoreAdmission stable{
			.physicalResizeNeeded = true,
			.previousBootActiveVendor = true,
			.targetRenderScaleActive = false,
			.stableValid = true,
			.stableActiveVendor = true,
			.stableMatchesBootContract = true,
		};
		if (!PreservesStableVendorContractDuringNativeRestore(stable) ||
			UsesLowPeakNativeRestore(stable)) {
			return false;
		}

		auto noStable = stable;
		noStable.stableValid = false;
		if (PreservesStableVendorContractDuringNativeRestore(noStable) ||
			!UsesLowPeakNativeRestore(noStable)) {
			return false;
		}

		auto activation = stable;
		activation.targetRenderScaleActive = true;
		return !PreservesStableVendorContractDuringNativeRestore(activation) &&
		       !UsesLowPeakNativeRestore(activation);
	}

	constexpr bool CoversSystemCommitProjectionGuard()
	{
		if (!UsesSystemCommitProjectionGuard({
				.residencyOverlapAllocation = true,
				.systemCommitValid = true,
				.systemCommitLimitKnown = true,
			})) {
			return false;
		}
		return !UsesSystemCommitProjectionGuard({
				   .residencyOverlapAllocation = false,
				   .systemCommitValid = true,
				   .systemCommitLimitKnown = true,
			   }) &&
		       !UsesSystemCommitProjectionGuard({
				   .residencyOverlapAllocation = true,
				   .systemCommitValid = false,
				   .systemCommitLimitKnown = true,
			   }) &&
		       !UsesSystemCommitProjectionGuard({
				   .residencyOverlapAllocation = true,
				   .systemCommitValid = true,
				   .systemCommitLimitKnown = false,
			   });
	}

	constexpr bool CoversStableDoorContractRetention()
	{
		for (std::uint32_t bits = 0; bits < (1u << 3); ++bits) {
			const bool doorHandoff = (bits & (1u << 0)) != 0;
			const bool hardSafetyDeferred = (bits & (1u << 1)) != 0;
			const bool physicalMutationOccurred = (bits & (1u << 2)) != 0;
			const bool expected =
				doorHandoff && hardSafetyDeferred && !physicalMutationOccurred;
			if (ShouldRetainStableDoorContract(
					doorHandoff,
					hardSafetyDeferred,
					physicalMutationOccurred) != expected) {
				return false;
			}
		}
		return true;
	}

	constexpr bool CoversNativeRestoreMemoryReliefOwnership()
	{
		return !ShouldApplyGenericMemoryReliefCleanup(false, true, true, true) &&
		       !ShouldApplyGenericMemoryReliefCleanup(true, true, true, true) &&
		       ShouldApplyGenericMemoryReliefCleanup(true, false, true, true) &&
		       ShouldApplyGenericMemoryReliefCleanup(true, true, false, true) &&
		       ShouldApplyGenericMemoryReliefCleanup(true, true, true, false);
	}

	constexpr bool CoversDeferredDispatchSelection()
	{
		for (std::uint32_t bits = 0; bits < (1u << 2); ++bits) {
			const DeferredDispatchAdmission state{
				.mutationDeferred = (bits & (1u << 0)) != 0,
				.exactProviderReady = (bits & (1u << 1)) != 0,
			};
			auto expected = DeferredDispatchAction::PresentationStretch;
			if (!state.mutationDeferred || state.exactProviderReady) {
				expected = DeferredDispatchAction::EvaluateExisting;
			}
			if (SelectDeferredDispatchAction(state) != expected)
				return false;
		}

		return true;
	}

	constexpr bool CoversPostLoadRecoverySettleDeadline()
	{
		PostLoadRecoverySettleAdmission state{
			.cleanupDrained = false,
			.currentFrame = 200,
			.admissionWaitStartFrame = 80,
			.settledSamples = 0,
			.requiredSettledSamples = 2,
			.timeoutFrames = 120,
		};
		if (SelectPostLoadRecoverySettleAction(state) !=
			PostLoadRecoverySettleAction::EvaluateDeadlineOnce) {
			return false;
		}

		state.cleanupDrained = true;
		state.currentFrame = 199;
		if (SelectPostLoadRecoverySettleAction(state) !=
			PostLoadRecoverySettleAction::WaitForSettledSamples) {
			return false;
		}

		state.settledSamples = 2;
		if (SelectPostLoadRecoverySettleAction(state) !=
			PostLoadRecoverySettleAction::UseSettledSamples) {
			return false;
		}

		state.currentFrame = 200;
		if (SelectPostLoadRecoverySettleAction(state) !=
			PostLoadRecoverySettleAction::EvaluateDeadlineOnce) {
			return false;
		}

		// The absolute deadline is owned by the recovery, not cleanup completion or
		// the first passing sample. Cleanup stalls and changing samples cannot
		// bypass or restart it once it expires.
		state.settledSamples = 0;
		if (SelectPostLoadRecoverySettleAction(state) !=
			PostLoadRecoverySettleAction::EvaluateDeadlineOnce) {
			return false;
		}
		state.settledSamples = 1;
		return SelectPostLoadRecoverySettleAction(state) ==
		       PostLoadRecoverySettleAction::EvaluateDeadlineOnce;
	}

	constexpr bool CoversPostLoadRecoveryDeadlineAdmission()
	{
		PostLoadRecoveryDeadlineAdmission ready{
			.deadlineExpired = true,
			.attemptConsumed = false,
			.physicalMutationStarted = false,
			.recoveryOwned = true,
			.loadingSerialOwned = true,
			.cleanupAndTrimComplete = true,
			.retirementReady = true,
			.memorySampleFresh = true,
			.pressureAcceptable = true,
			.gpuHeadroomSufficient = true,
			.projectedSystemCommitSafe = true,
			.deviceHealthy = true,
			.noRecentOutOfMemory = true,
		};
		if (SelectPostLoadRecoveryDeadlineAction(ready) !=
			PostLoadRecoveryDeadlineAction::AttemptOnce) {
			return false;
		}

		auto blocked = ready;
		blocked.deadlineExpired = false;
		if (SelectPostLoadRecoveryDeadlineAction(blocked) !=
			PostLoadRecoveryDeadlineAction::NotExpired) {
			return false;
		}
		blocked.physicalMutationStarted = true;
		if (SelectPostLoadRecoveryDeadlineAction(blocked) !=
			PostLoadRecoveryDeadlineAction::NotExpired) {
			return false;
		}

		auto mutated = ready;
		mutated.physicalMutationStarted = true;
		if (SelectPostLoadRecoveryDeadlineAction(mutated) !=
			PostLoadRecoveryDeadlineAction::ContinueMutatedRecovery) {
			return false;
		}
		mutated.attemptConsumed = true;
		mutated.projectedSystemCommitSafe = false;
		if (SelectPostLoadRecoveryDeadlineAction(mutated) !=
			PostLoadRecoveryDeadlineAction::ContinueMutatedRecovery) {
			return false;
		}
		mutated.loadingSerialOwned = false;
		if (SelectPostLoadRecoveryDeadlineAction(mutated) !=
			PostLoadRecoveryDeadlineAction::RetainStableContract) {
			return false;
		}

		blocked = ready;
		blocked.attemptConsumed = true;
		if (SelectPostLoadRecoveryDeadlineAction(blocked) !=
			PostLoadRecoveryDeadlineAction::RetainStableContract) {
			return false;
		}

		for (std::uint32_t bit = 0; bit < 10; ++bit) {
			blocked = ready;
			switch (bit) {
			case 0:
				blocked.recoveryOwned = false;
				break;
			case 1:
				blocked.loadingSerialOwned = false;
				break;
			case 2:
				blocked.cleanupAndTrimComplete = false;
				break;
			case 3:
				blocked.retirementReady = false;
				break;
			case 4:
				blocked.memorySampleFresh = false;
				break;
			case 5:
				blocked.pressureAcceptable = false;
				break;
			case 6:
				blocked.gpuHeadroomSufficient = false;
				break;
			case 7:
				blocked.projectedSystemCommitSafe = false;
				break;
			case 8:
				blocked.deviceHealthy = false;
				break;
			case 9:
				blocked.noRecentOutOfMemory = false;
				break;
			default:
				return false;
			}
			if (SelectPostLoadRecoveryDeadlineAction(blocked) !=
				PostLoadRecoveryDeadlineAction::RetainStableContract) {
				return false;
			}
		}
		return true;
	}

	constexpr bool CoversPostLoadRecoveryStableFallbackOwnership()
	{
		PostLoadRecoveryStableFallbackOwnership owner{
			.recoveryActive = true,
			.physicalMutationStarted = false,
			.recoveryEpoch = 7,
			.expectedRecoveryEpoch = 7,
			.transitionEpoch = 11,
			.expectedTransitionEpoch = 11,
			.loadingSerial = 13,
			.currentLoadingSerial = 13,
		};
		if (!CanClaimPostLoadRecoveryStableFallback(owner))
			return false;

		for (std::uint32_t bit = 0; bit < 7; ++bit) {
			auto stale = owner;
			switch (bit) {
			case 0:
				stale.recoveryActive = false;
				break;
			case 1:
				stale.physicalMutationStarted = true;
				break;
			case 2:
				stale.expectedRecoveryEpoch = 0;
				break;
			case 3:
				++stale.recoveryEpoch;
				break;
			case 4:
				stale.expectedTransitionEpoch = 0;
				break;
			case 5:
				++stale.transitionEpoch;
				break;
			case 6:
				++stale.currentLoadingSerial;
				break;
			default:
				return false;
			}
			if (CanClaimPostLoadRecoveryStableFallback(stale))
				return false;
		}
		return true;
	}

	static_assert(CoversWorkGateMasks());
	static_assert(CoversWorkGateState());
	static_assert(CoversGameEntryConvergence());
	static_assert(CoversStartupMainMenuStateDefinition());
	static_assert(CoversRenderScaleRuntimeActivation());
	static_assert(CoversRenderTransitionCoverAdmission());
	static_assert(CoversLoadingFadeHoldAdmission());
	static_assert(CoversLoadingPresentationReleaseAdmission());
	static_assert(CoversStabilizerDestinationSyncReadiness());
	static_assert(CoversBufferedDoorRequestCoalescing());
	static_assert(CoversLifecycleMutationAdmission());
	static_assert(CoversDispatchAdmission());
	static_assert(CoversNativeRestorePresentationAdmission());
	static_assert(CoversBoundedNativeRestorePresentationRecovery());
	static_assert(CoversVendorResourcePredicates());
	static_assert(CoversStereoRelatchAdmission());
	static_assert(CoversNativeRestoreSequence());
	static_assert(CoversProvenNativeRestoreRetirementResume());
	static_assert(CoversNativeRestoreActionSelection());
	static_assert(CoversNativeRestoreTeardownDisposition());
	static_assert(CoversNativeRestoreOperationStrength());
	static_assert(CoversNativeRestoreOwnership());
	static_assert(CoversStableNativeRestorePreservation());
	static_assert(CoversSystemCommitProjectionGuard());
	static_assert(CoversStableDoorContractRetention());
	static_assert(CoversNativeRestoreMemoryReliefOwnership());
	static_assert(CoversDeferredDispatchSelection());
	static_assert(CoversPostLoadRecoverySettleDeadline());
	static_assert(CoversPostLoadRecoveryDeadlineAdmission());
	static_assert(CoversPostLoadRecoveryStableFallbackOwnership());
}

int main() {}
