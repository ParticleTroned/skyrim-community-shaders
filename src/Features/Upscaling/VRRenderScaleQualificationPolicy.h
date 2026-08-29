#pragma once

#include <cstdint>
#include <limits>
#include <string_view>

namespace VRRenderScaleQualificationPolicy
{
	inline constexpr std::uint32_t kMaximumQualityMode = 6;
	inline constexpr std::string_view kElapsedMillisecondsReceiptField = "elapsedMs";
	inline constexpr std::string_view kElapsedFramesReceiptField = "elapsedFrames";

	enum class Milestone : std::uint8_t
	{
		Strict,
		Presentation,
		Cleanup
	};

	[[nodiscard]] constexpr std::string_view GetMilestoneName(
		Milestone a_milestone) noexcept
	{
		switch (a_milestone) {
		case Milestone::Strict:
			return "strict";
		case Milestone::Presentation:
			return "presentation";
		case Milestone::Cleanup:
			return "cleanup";
		}
		return "unknown";
	}

	[[nodiscard]] constexpr bool TryParseMilestone(
		std::string_view a_name,
		Milestone& a_milestone) noexcept
	{
		if (a_name == "strict")
			a_milestone = Milestone::Strict;
		else if (a_name == "presentation")
			a_milestone = Milestone::Presentation;
		else if (a_name == "cleanup")
			a_milestone = Milestone::Cleanup;
		else
			return false;
		return true;
	}

	enum class Method : std::uint8_t
	{
		Unknown,
		FSR,
		DLSS,
		None,
		TAA
	};

	enum class PhysicalBackend : std::uint8_t
	{
		None,
		DLSS,
		FSRHost,
		FSRRuntime,
		FSR4Runtime
	};

	[[nodiscard]] constexpr bool MatchesActivePhysicalBackend(
		Method a_method,
		PhysicalBackend a_backend) noexcept
	{
		if (a_method == Method::DLSS)
			return a_backend == PhysicalBackend::DLSS;
		if (a_method != Method::FSR)
			return false;
		return a_backend == PhysicalBackend::FSRHost ||
		       a_backend == PhysicalBackend::FSRRuntime ||
		       a_backend == PhysicalBackend::FSR4Runtime;
	}

	struct TargetProfile
	{
		Method method = Method::Unknown;
		std::uint32_t qualityMode = 0;
		bool renderScaleMode = false;
		bool matchDLSSProfile = false;
		std::uint32_t dlssProfile = 0;
		bool matchFSRRuntime = false;
		bool fsr4Runtime = false;
	};

	struct Profile
	{
		bool valid = false;
		Method method = Method::Unknown;
		std::uint32_t qualityMode = 0;
		bool renderScaleMode = false;
		std::uint32_t dlssProfile = 0;
		bool fsr4Runtime = false;
	};

	[[nodiscard]] constexpr bool UsesVendorEvaluation(
		const TargetProfile& a_target) noexcept
	{
		return a_target.method == Method::DLSS || a_target.method == Method::FSR;
	}

	[[nodiscard]] constexpr bool UsesNativeAPIEvaluation(
		const TargetProfile& a_target) noexcept
	{
		return (UsesVendorEvaluation(a_target) ||
				   a_target.method == Method::None ||
				   a_target.method == Method::TAA) &&
		       !a_target.renderScaleMode;
	}

	[[nodiscard]] constexpr bool UsesNativeVendorEvaluation(
		const TargetProfile& a_target) noexcept
	{
		return UsesVendorEvaluation(a_target) && UsesNativeAPIEvaluation(a_target);
	}

	[[nodiscard]] constexpr bool IsTargetPropertyAllowed(
		std::string_view a_name) noexcept
	{
		return a_name == "method" ||
		       a_name == "qualityMode" ||
		       a_name == "renderScaleMode" ||
		       a_name == "dlssProfile" ||
		       a_name == "fsrRuntime";
	}

	[[nodiscard]] constexpr bool IsFoveationPropertyAllowed(
		std::string_view a_name) noexcept
	{
		return a_name == "foveatedVendorDispatch" ||
		       a_name == "foveatedCenterArea" ||
		       a_name == "peripheryTAAEnable" ||
		       a_name == "peripheryTAACenterArea" ||
		       a_name == "peripheryTAAOuterScale";
	}

	[[nodiscard]] constexpr bool IsValidTarget(const TargetProfile& a_target) noexcept
	{
		if (a_target.method == Method::Unknown ||
			a_target.qualityMode > kMaximumQualityMode ||
			a_target.renderScaleMode != (a_target.qualityMode != 0)) {
			return false;
		}
		if ((a_target.method == Method::None || a_target.method == Method::TAA) &&
			(a_target.qualityMode != 0 || a_target.matchDLSSProfile ||
				a_target.matchFSRRuntime)) {
			return false;
		}
		if (a_target.method == Method::DLSS && a_target.matchFSRRuntime)
			return false;
		if (a_target.method == Method::FSR && a_target.matchDLSSProfile)
			return false;
		return true;
	}

	[[nodiscard]] constexpr bool MatchesTarget(
		const Profile& a_profile,
		const TargetProfile& a_target) noexcept
	{
		if (!a_profile.valid || !IsValidTarget(a_target) ||
			a_profile.method != a_target.method ||
			a_profile.qualityMode != a_target.qualityMode ||
			a_profile.renderScaleMode != a_target.renderScaleMode) {
			return false;
		}
		if (a_target.method == Method::DLSS && a_target.matchDLSSProfile &&
			a_profile.dlssProfile != a_target.dlssProfile) {
			return false;
		}
		if (a_target.method == Method::FSR && a_target.matchFSRRuntime &&
			a_profile.fsr4Runtime != a_target.fsr4Runtime) {
			return false;
		}
		return true;
	}

	[[nodiscard]] constexpr bool ProfilesAgree(
		const Profile& a_left,
		const Profile& a_right) noexcept
	{
		return a_left.valid && a_right.valid &&
		       a_left.method == a_right.method &&
		       a_left.qualityMode == a_right.qualityMode &&
		       a_left.renderScaleMode == a_right.renderScaleMode &&
		       (a_left.method != Method::DLSS ||
				   a_left.dlssProfile == a_right.dlssProfile) &&
		       (a_left.method != Method::FSR ||
				   a_left.fsr4Runtime == a_right.fsr4Runtime);
	}

	[[nodiscard]] constexpr TargetProfile ExactObservationTarget(
		const Profile& a_profile) noexcept
	{
		if (!a_profile.valid)
			return {};
		return {
			.method = a_profile.method,
			.qualityMode = a_profile.qualityMode,
			.renderScaleMode = a_profile.renderScaleMode,
			.matchDLSSProfile = a_profile.method == Method::DLSS,
			.dlssProfile = a_profile.dlssProfile,
			.matchFSRRuntime = a_profile.method == Method::FSR,
			.fsr4Runtime = a_profile.fsr4Runtime,
		};
	}

	[[nodiscard]] constexpr bool CanBegin(std::uint64_t a_activeTransitionID) noexcept
	{
		return a_activeTransitionID == 0;
	}

	[[nodiscard]] constexpr bool OwnsTransition(
		std::uint64_t a_activeTransitionID,
		std::uint64_t a_requestedTransitionID) noexcept
	{
		return a_activeTransitionID != 0 &&
		       a_activeTransitionID == a_requestedTransitionID;
	}

	[[nodiscard]] constexpr bool OwnsTransitionInstance(
		std::uint64_t a_activeTransitionID,
		std::uint64_t a_activeOwnershipToken,
		std::uint64_t a_requestedTransitionID,
		std::uint64_t a_requestedOwnershipToken) noexcept
	{
		return OwnsTransition(a_activeTransitionID, a_requestedTransitionID) &&
		       a_activeOwnershipToken != 0 &&
		       a_activeOwnershipToken == a_requestedOwnershipToken;
	}

	[[nodiscard]] constexpr bool FrameAdvanced(
		std::uint32_t a_beginFrame,
		std::uint32_t a_observedFrame) noexcept
	{
		return static_cast<std::int32_t>(a_observedFrame - a_beginFrame) > 0;
	}

	struct NativeVendorEyeEvidence
	{
		bool valid = false;
		std::uint32_t presentationFrame = 0;
		std::uint32_t dispatchFrame = 0;
		PhysicalBackend backend = PhysicalBackend::None;
		std::uint64_t dispatchSerial = 0;
		bool runtimeFallback = false;
	};

	[[nodiscard]] constexpr bool HasCoherentNativeVendorEvaluation(
		const TargetProfile& a_target,
		std::uint32_t a_beginFrame,
		const NativeVendorEyeEvidence& a_left,
		const NativeVendorEyeEvidence& a_right) noexcept
	{
		if (!UsesNativeVendorEvaluation(a_target) ||
			!a_left.valid || !a_right.valid ||
			a_left.presentationFrame != a_right.presentationFrame ||
			!FrameAdvanced(a_beginFrame, a_left.presentationFrame) ||
			a_left.dispatchFrame != a_left.presentationFrame ||
			a_right.dispatchFrame != a_right.presentationFrame ||
			a_left.backend != a_right.backend ||
			!MatchesActivePhysicalBackend(a_target.method, a_left.backend) ||
			a_left.runtimeFallback != a_right.runtimeFallback) {
			return false;
		}
		if (a_target.method == Method::DLSS)
			return !a_left.runtimeFallback;
		return a_left.dispatchSerial != 0 &&
		       a_left.dispatchSerial == a_right.dispatchSerial;
	}

	[[nodiscard]] constexpr std::uint32_t ElapsedFrames(
		std::uint32_t a_beginFrame,
		std::uint32_t a_observedFrame) noexcept
	{
		return FrameAdvanced(a_beginFrame, a_observedFrame) ?
		           a_observedFrame - a_beginFrame :
		           0;
	}

	[[nodiscard]] constexpr bool CounterRegressed(
		std::uint64_t a_current,
		std::uint64_t a_baseline) noexcept
	{
		return a_current < a_baseline;
	}

	[[nodiscard]] constexpr std::uint64_t MonotonicCounterDelta(
		std::uint64_t a_current,
		std::uint64_t a_baseline) noexcept
	{
		return CounterRegressed(a_current, a_baseline) ?
		           0 :
		           a_current - a_baseline;
	}

	[[nodiscard]] constexpr bool SameCounterGeneration(
		std::uint64_t a_currentEpoch,
		std::uint32_t a_currentGeneration,
		std::uint64_t a_baselineEpoch,
		std::uint32_t a_baselineGeneration) noexcept
	{
		return a_currentEpoch == a_baselineEpoch &&
		       a_currentGeneration == a_baselineGeneration;
	}

	[[nodiscard]] constexpr bool GenerationCounterRegressed(
		std::uint64_t a_current,
		std::uint64_t a_baseline,
		std::uint64_t a_currentEpoch,
		std::uint32_t a_currentGeneration,
		std::uint64_t a_baselineEpoch,
		std::uint32_t a_baselineGeneration) noexcept
	{
		return SameCounterGeneration(
				   a_currentEpoch,
				   a_currentGeneration,
				   a_baselineEpoch,
				   a_baselineGeneration) &&
		       CounterRegressed(a_current, a_baseline);
	}

	[[nodiscard]] constexpr std::uint64_t GenerationCounterDelta(
		std::uint64_t a_current,
		std::uint64_t a_baseline,
		std::uint64_t a_currentEpoch,
		std::uint32_t a_currentGeneration,
		std::uint64_t a_baselineEpoch,
		std::uint32_t a_baselineGeneration) noexcept
	{
		return SameCounterGeneration(
				   a_currentEpoch,
				   a_currentGeneration,
				   a_baselineEpoch,
				   a_baselineGeneration) ?
		           MonotonicCounterDelta(a_current, a_baseline) :
		           a_current;
	}

	[[nodiscard]] constexpr bool IsFoveationInvariantViolation(
		bool a_targetSupplied,
		bool a_settingsMatch) noexcept
	{
		return a_targetSupplied && !a_settingsMatch;
	}

	[[nodiscard]] constexpr bool IsTransientObservationDispatchError(
		std::string_view a_errorCode) noexcept
	{
		return a_errorCode == "main_thread_timeout";
	}

	[[nodiscard]] constexpr bool HasRequiredPresentationHistory(
		bool a_vendorPresentation,
		std::uint32_t a_leftConsecutiveFrames,
		std::uint32_t a_rightConsecutiveFrames,
		std::uint32_t a_bothEyesConsecutiveFrames) noexcept
	{
		const std::uint32_t requiredEyeFrames = a_vendorPresentation ? 2u : 1u;
		return a_leftConsecutiveFrames >= requiredEyeFrames &&
		       a_rightConsecutiveFrames >= requiredEyeFrames &&
		       (!a_vendorPresentation || a_bothEyesConsecutiveFrames >= 2);
	}

	[[nodiscard]] constexpr bool HasCoherentPresentationFrames(
		bool a_vendorPresentation,
		std::uint32_t a_beginFrame,
		std::uint32_t a_leftFrame,
		std::uint32_t a_rightFrame,
		std::uint64_t a_leftCycle,
		std::uint64_t a_rightCycle,
		std::uint32_t a_lastBothEyesFrame,
		std::uint64_t a_lastBothEyesCycle) noexcept
	{
		if (!a_vendorPresentation) {
			return a_leftFrame == a_rightFrame &&
			       FrameAdvanced(a_beginFrame, a_leftFrame) &&
			       a_leftCycle != 0 && a_leftCycle == a_rightCycle;
		}
		if (!FrameAdvanced(a_beginFrame, a_lastBothEyesFrame) ||
			a_lastBothEyesCycle == 0 ||
			a_lastBothEyesCycle == std::numeric_limits<std::uint64_t>::max()) {
			return false;
		}

		const auto eyeMatchesCompletedOrNext = [=](
												   std::uint32_t a_frame, std::uint64_t a_cycle) {
			const bool completed = a_frame == a_lastBothEyesFrame &&
			                       a_cycle == a_lastBothEyesCycle;
			const bool next = ElapsedFrames(a_lastBothEyesFrame, a_frame) == 1 &&
			                  a_cycle == a_lastBothEyesCycle + 1;
			return completed || next;
		};
		const bool leftCompleted = a_leftFrame == a_lastBothEyesFrame &&
		                           a_leftCycle == a_lastBothEyesCycle;
		const bool rightCompleted = a_rightFrame == a_lastBothEyesFrame &&
		                            a_rightCycle == a_lastBothEyesCycle;
		return eyeMatchesCompletedOrNext(a_leftFrame, a_leftCycle) &&
		       eyeMatchesCompletedOrNext(a_rightFrame, a_rightCycle) &&
		       (leftCompleted || rightCompleted);
	}

	[[nodiscard]] constexpr bool IsFreshTransition(
		bool a_destinationMatches,
		bool a_destinationDiffersFromSource,
		std::uint32_t a_beginFrame,
		std::uint32_t a_observedFrame,
		bool a_profileStateChanged) noexcept
	{
		if (!a_destinationMatches)
			return false;
		return a_destinationDiffersFromSource ?
		           FrameAdvanced(a_beginFrame, a_observedFrame) :
		           a_profileStateChanged;
	}

	[[nodiscard]] constexpr std::uint64_t SaturatingDeadlineTick(
		std::uint64_t a_beginTick,
		std::uint64_t a_timeoutMilliseconds,
		std::uint64_t a_tickFrequency) noexcept
	{
		if (a_tickFrequency == 0)
			return a_beginTick;
		const auto multiply = [](std::uint64_t a_left, std::uint64_t a_right) {
			if (a_left != 0 &&
				a_right > std::numeric_limits<std::uint64_t>::max() / a_left) {
				return std::numeric_limits<std::uint64_t>::max();
			}
			return a_left * a_right;
		};
		const auto add = [](std::uint64_t a_left, std::uint64_t a_right) {
			if (a_right > std::numeric_limits<std::uint64_t>::max() - a_left)
				return std::numeric_limits<std::uint64_t>::max();
			return a_left + a_right;
		};
		const std::uint64_t wholeSeconds = a_timeoutMilliseconds / 1000;
		const std::uint64_t remainingMilliseconds = a_timeoutMilliseconds % 1000;
		const std::uint64_t wholeTicks = multiply(wholeSeconds, a_tickFrequency);
		const std::uint64_t remainingWholeTicks =
			multiply(remainingMilliseconds, a_tickFrequency / 1000);
		const std::uint64_t remainingFractionTicks =
			multiply(remainingMilliseconds, a_tickFrequency % 1000) / 1000;
		const std::uint64_t remainingTicks =
			add(remainingWholeTicks, remainingFractionTicks);
		return add(a_beginTick, add(wholeTicks, remainingTicks));
	}

	[[nodiscard]] constexpr bool IsWithinDeadline(
		std::uint64_t a_observedTick,
		std::uint64_t a_deadlineTick) noexcept
	{
		return a_observedTick <= a_deadlineTick;
	}

	[[nodiscard]] constexpr double ElapsedMilliseconds(
		std::uint64_t a_beginTick,
		std::uint64_t a_endTick,
		std::uint64_t a_tickFrequency) noexcept
	{
		if (a_tickFrequency == 0 || a_endTick <= a_beginTick)
			return 0.0;
		return static_cast<double>(a_endTick - a_beginTick) * 1000.0 /
		       static_cast<double>(a_tickFrequency);
	}

	struct TerminalDiagnosticDeltas
	{
		std::uint64_t stressFailureEvents = 0;
		std::uint64_t stressOverwrittenEvents = 0;
		std::uint64_t presentationStretchEyeObservations = 0;
		std::uint64_t vendorFailureStretchEyeObservations = 0;
		std::uint64_t boundsMismatchFallbackEyeObservations = 0;
		std::uint64_t fidelityMismatches = 0;
		std::uint64_t transitionFailures = 0;
		std::uint64_t outOfMemoryFailures = 0;
		std::uint64_t deviceLostFailures = 0;
		std::uint64_t relevantLifecycleFailures = 0;
		std::uint64_t memoryTrimFailures = 0;
		std::uint64_t retirementFenceFailures = 0;
		bool monotonicCounterRegression = false;
		bool traceApplicable = false;
		bool traceSessionLost = false;
		std::uint64_t traceDroppedRecords = 0;
		std::uint64_t traceConstantsFailures = 0;
		std::uint64_t traceEvaluateFailures = 0;
	};

	[[nodiscard]] constexpr bool HasTerminalDiagnosticFailure(
		const TerminalDiagnosticDeltas& a_delta) noexcept
	{
		return a_delta.stressFailureEvents != 0 ||
		       a_delta.stressOverwrittenEvents != 0 ||
		       a_delta.vendorFailureStretchEyeObservations != 0 ||
		       a_delta.boundsMismatchFallbackEyeObservations != 0 ||
		       a_delta.fidelityMismatches != 0 ||
		       a_delta.transitionFailures != 0 ||
		       a_delta.outOfMemoryFailures != 0 ||
		       a_delta.deviceLostFailures != 0 ||
		       a_delta.relevantLifecycleFailures != 0 ||
		       a_delta.memoryTrimFailures != 0 ||
		       a_delta.retirementFenceFailures != 0 ||
		       a_delta.monotonicCounterRegression ||
		       (a_delta.traceApplicable &&
				   (a_delta.traceSessionLost ||
					   a_delta.traceDroppedRecords != 0 ||
					   a_delta.traceConstantsFailures != 0 ||
					   a_delta.traceEvaluateFailures != 0));
	}

	/** Reports loss of a required telemetry owner, not a measured render anomaly. */
	[[nodiscard]] constexpr bool HasQualificationControlFailure(
		const TerminalDiagnosticDeltas& a_delta) noexcept
	{
		return a_delta.traceApplicable && a_delta.traceSessionLost;
	}

	enum FailureReason : std::uint64_t
	{
		kFailureNone = 0,
		kFailureStressSession = 1ull << 0,
		kFailureExactCell = 1ull << 1,
		kFailureLoadedInWorld = 1ull << 2,
		kFailureFreshObservation = 1ull << 3,
		kFailurePublicSnapshot = 1ull << 4,
		kFailureProvider = 1ull << 5,
		kFailureProfiles = 1ull << 6,
		kFailureDimensions = 1ull << 7,
		kFailureAPIOperation = 1ull << 8,
		kFailureAPIConditions = 1ull << 9,
		kFailureController = 1ull << 10,
		kFailureWorkGate = 1ull << 11,
		kFailureRelatch = 1ull << 12,
		kFailureRecovery = 1ull << 13,
		kFailureMemoryTrim = 1ull << 14,
		kFailureRetirement = 1ull << 15,
		kFailurePhysicalMutation = 1ull << 16,
		kFailureTerminal = 1ull << 17,
		kFailureAPIActiveContract = 1ull << 18,
		kFailurePhysicalActiveContract = 1ull << 19,
		kFailurePresentationPhase = 1ull << 20,
		kFailureFidelity = 1ull << 21,
		kFailureVendorPresentation = 1ull << 22,
		kFailureLifecycle = 1ull << 23,
		kFailureFSRDispatch = 1ull << 24,
		kFailureShaderCompilation = 1ull << 25,
		kFailureAPINativeContract = 1ull << 26,
		kFailurePhysicalNativeContract = 1ull << 27,
		kFailureNativePresentation = 1ull << 28,
		kFailureFoveationSettings = 1ull << 29,
		kFailureDiagnosticDelta = 1ull << 30,
		kFailureResourcePublication = 1ull << 31,
		kFailureProviderTerminal = 1ull << 32,
	};

	struct StabilityFacts
	{
		bool stressSession = false;
		bool exactCell = false;
		bool loadedInWorld = false;
		bool freshObservation = false;
		bool publicSnapshot = false;
		bool providerReady = false;
		bool profilesAgree = false;
		bool dimensionsPositive = false;
		bool apiOperationClear = false;
		bool apiConditionsClear = false;
		bool controllerSettled = false;
		bool workGateClear = false;
		bool relatchClear = false;
		bool recoveryClear = false;
		bool memoryTrimClear = false;
		bool retirementClear = false;
		bool physicalMutationClear = false;
		bool terminalClear = false;
		bool foveationSettingsMatch = false;
		bool diagnosticsClear = false;
		bool apiActiveContract = false;
		bool physicalActiveContract = false;
		bool presentationPhaseStable = false;
		bool fidelityStable = false;
		bool vendorPresentationStable = false;
		bool lifecycleStable = false;
		bool fsrDispatchStable = false;
		bool shaderCompilationIdle = false;
		bool apiNativeContract = false;
		bool physicalNativeContract = false;
		bool nativePresentationStable = false;
		bool resourcePublicationCurrent = false;
		bool providerTerminalClear = false;
		bool requiredShaderCompilationComplete = false;
	};

	constexpr void RequireFact(
		bool a_satisfied,
		FailureReason a_reason,
		std::uint64_t& a_failures) noexcept
	{
		if (!a_satisfied)
			a_failures |= static_cast<std::uint64_t>(a_reason);
	}

	[[nodiscard]] constexpr std::uint64_t EvaluateSharedValidation(
		const StabilityFacts& a_facts) noexcept
	{
		std::uint64_t failures = kFailureNone;
		RequireFact(a_facts.stressSession, kFailureStressSession, failures);
		RequireFact(a_facts.exactCell, kFailureExactCell, failures);
		RequireFact(a_facts.loadedInWorld, kFailureLoadedInWorld, failures);
		RequireFact(a_facts.freshObservation, kFailureFreshObservation, failures);
		RequireFact(a_facts.publicSnapshot, kFailurePublicSnapshot, failures);
		RequireFact(a_facts.providerReady, kFailureProvider, failures);
		RequireFact(a_facts.profilesAgree, kFailureProfiles, failures);
		RequireFact(a_facts.dimensionsPositive, kFailureDimensions, failures);
		RequireFact(a_facts.terminalClear, kFailureTerminal, failures);
		RequireFact(
			a_facts.providerTerminalClear, kFailureProviderTerminal, failures);
		RequireFact(
			a_facts.foveationSettingsMatch,
			kFailureFoveationSettings,
			failures);
		return failures;
	}

	// Presentation authority includes every owner that can still replace or
	// invalidate the proven contract. Passive work gates, fenced retirement and
	// nonblocking trim are cleanup debt and are intentionally evaluated below.
	[[nodiscard]] constexpr std::uint64_t EvaluatePresentationStability(
		const TargetProfile& a_target,
		const StabilityFacts& a_facts) noexcept
	{
		std::uint64_t failures = EvaluateSharedValidation(a_facts);
		RequireFact(a_facts.apiOperationClear, kFailureAPIOperation, failures);
		RequireFact(a_facts.apiConditionsClear, kFailureAPIConditions, failures);
		RequireFact(a_facts.controllerSettled, kFailureController, failures);
		RequireFact(a_facts.relatchClear, kFailureRelatch, failures);
		RequireFact(a_facts.recoveryClear, kFailureRecovery, failures);
		RequireFact(
			a_facts.physicalMutationClear, kFailurePhysicalMutation, failures);
		RequireFact(
			a_facts.resourcePublicationCurrent,
			kFailureResourcePublication,
			failures);

		if (UsesNativeVendorEvaluation(a_target)) {
			RequireFact(
				a_facts.apiNativeContract,
				kFailureAPINativeContract,
				failures);
			RequireFact(
				a_facts.physicalNativeContract,
				kFailurePhysicalNativeContract,
				failures);
			RequireFact(
				a_facts.nativePresentationStable,
				kFailureNativePresentation,
				failures);
		} else if (UsesVendorEvaluation(a_target)) {
			RequireFact(
				a_facts.apiActiveContract,
				kFailureAPIActiveContract,
				failures);
			RequireFact(
				a_facts.physicalActiveContract,
				kFailurePhysicalActiveContract,
				failures);
			RequireFact(
				a_facts.presentationPhaseStable,
				kFailurePresentationPhase,
				failures);
			RequireFact(a_facts.fidelityStable, kFailureFidelity, failures);
			RequireFact(
				a_facts.vendorPresentationStable,
				kFailureVendorPresentation,
				failures);
			RequireFact(a_facts.lifecycleStable, kFailureLifecycle, failures);
			if (a_target.method == Method::FSR) {
				RequireFact(a_facts.fsrDispatchStable, kFailureFSRDispatch, failures);
				RequireFact(
					a_facts.requiredShaderCompilationComplete,
					kFailureShaderCompilation,
					failures);
			}
		} else {
			RequireFact(
				a_facts.apiNativeContract, kFailureAPINativeContract, failures);
			RequireFact(
				a_facts.physicalNativeContract,
				kFailurePhysicalNativeContract,
				failures);
			RequireFact(
				a_facts.nativePresentationStable,
				kFailureNativePresentation,
				failures);
		}
		return failures;
	}

	// Cleanup is complete only when no active owner can create more debt and all
	// passive work has drained. Requiring the shared context prevents a cleanup
	// receipt from being attributed to the wrong session, cell or profile.
	[[nodiscard]] constexpr std::uint64_t EvaluateCleanupDrain(
		const TargetProfile& a_target,
		const StabilityFacts& a_facts) noexcept
	{
		std::uint64_t failures = EvaluateSharedValidation(a_facts);
		RequireFact(a_facts.apiOperationClear, kFailureAPIOperation, failures);
		RequireFact(a_facts.apiConditionsClear, kFailureAPIConditions, failures);
		RequireFact(a_facts.controllerSettled, kFailureController, failures);
		RequireFact(a_facts.workGateClear, kFailureWorkGate, failures);
		RequireFact(a_facts.relatchClear, kFailureRelatch, failures);
		RequireFact(a_facts.recoveryClear, kFailureRecovery, failures);
		RequireFact(a_facts.memoryTrimClear, kFailureMemoryTrim, failures);
		RequireFact(a_facts.retirementClear, kFailureRetirement, failures);
		RequireFact(
			a_facts.physicalMutationClear, kFailurePhysicalMutation, failures);
		if (UsesVendorEvaluation(a_target) && a_target.method == Method::FSR) {
			// Legacy strict qualification waited for the global compiler to drain.
			// Keep that debt in cleanup while presentation accepts an already-proven
			// active FSR contract whose required dispatch shaders have completed.
			RequireFact(
				a_facts.shaderCompilationIdle,
				kFailureShaderCompilation,
				failures);
		}
		return failures;
	}

	[[nodiscard]] constexpr std::uint64_t EvaluateStrictStability(
		const TargetProfile& a_target,
		const StabilityFacts& a_facts) noexcept
	{
		return EvaluatePresentationStability(a_target, a_facts) |
		       EvaluateCleanupDrain(a_target, a_facts);
	}

	/** Preserve the original strict evaluator name for existing callers. */
	[[nodiscard]] constexpr std::uint64_t EvaluateStability(
		const TargetProfile& a_target,
		const StabilityFacts& a_facts) noexcept
	{
		return EvaluateStrictStability(a_target, a_facts);
	}

	struct MilestoneEvaluation
	{
		std::uint64_t presentationFailures = kFailureNone;
		std::uint64_t cleanupFailures = kFailureNone;
		std::uint64_t strictFailures = kFailureNone;

		[[nodiscard]] constexpr bool PresentationStable() const noexcept
		{
			return presentationFailures == kFailureNone;
		}

		[[nodiscard]] constexpr bool CleanupDrained() const noexcept
		{
			return cleanupFailures == kFailureNone;
		}

		[[nodiscard]] constexpr bool StrictSatisfied() const noexcept
		{
			return strictFailures == kFailureNone;
		}
	};

	[[nodiscard]] constexpr MilestoneEvaluation EvaluateMilestones(
		const TargetProfile& a_target,
		const StabilityFacts& a_facts) noexcept
	{
		const auto presentation =
			EvaluatePresentationStability(a_target, a_facts);
		const auto cleanup = EvaluateCleanupDrain(a_target, a_facts);
		return {
			.presentationFailures = presentation,
			.cleanupFailures = cleanup,
			.strictFailures = presentation | cleanup,
		};
	}

	[[nodiscard]] constexpr bool IsMilestoneSatisfied(
		Milestone a_milestone,
		const MilestoneEvaluation& a_evaluation) noexcept
	{
		switch (a_milestone) {
		case Milestone::Strict:
			return a_evaluation.StrictSatisfied();
		case Milestone::Presentation:
			return a_evaluation.PresentationStable();
		case Milestone::Cleanup:
			return a_evaluation.CleanupDrained();
		}
		return false;
	}

	struct FirstObservation
	{
		std::uint64_t tick = 0;
		std::uint32_t frame = 0;

		[[nodiscard]] constexpr bool Observed() const noexcept
		{
			return tick != 0;
		}
	};

	constexpr void RecordFirstObservation(
		bool a_satisfied,
		std::uint64_t a_tick,
		std::uint32_t a_frame,
		FirstObservation& a_observation) noexcept
	{
		if (a_satisfied && a_tick != 0 && !a_observation.Observed()) {
			a_observation.tick = a_tick;
			a_observation.frame = a_frame;
		}
	}
}
