#include "Features/Upscaling/VRRenderScaleQualificationPolicy.h"

#include <cstdint>
#include <limits>

namespace
{
	using namespace VRRenderScaleQualificationPolicy;

	constexpr TargetProfile ActiveDLSS()
	{
		return {
			.method = Method::DLSS,
			.qualityMode = 6,
			.renderScaleMode = true,
			.matchDLSSProfile = true,
			.dlssProfile = 1,
		};
	}

	constexpr StabilityFacts CommonStableFacts()
	{
		StabilityFacts facts{
			.stressSession = true,
			.exactCell = true,
			.loadedInWorld = true,
			.freshObservation = true,
			.publicSnapshot = true,
			.providerReady = true,
			.profilesAgree = true,
			.dimensionsPositive = true,
			.apiOperationClear = true,
			.apiConditionsClear = true,
			.controllerSettled = true,
			.workGateClear = true,
			.relatchClear = true,
			.recoveryClear = true,
			.memoryTrimClear = true,
			.retirementClear = true,
			.physicalMutationClear = true,
			.terminalClear = true,
			.foveationSettingsMatch = true,
			.diagnosticsClear = true,
		};
		facts.shaderCompilationIdle = true;
		facts.resourcePublicationCurrent = true;
		facts.providerTerminalClear = true;
		facts.requiredShaderCompilationComplete = true;
		return facts;
	}

	constexpr StabilityFacts ActiveDLSSStableFacts()
	{
		auto facts = CommonStableFacts();
		facts.apiActiveContract = true;
		facts.physicalActiveContract = true;
		facts.presentationPhaseStable = true;
		facts.fidelityStable = true;
		facts.vendorPresentationStable = true;
		facts.lifecycleStable = true;
		return facts;
	}

	constexpr bool CoversExactProfileMatching()
	{
		const auto target = ActiveDLSS();
		Profile profile{
			.valid = true,
			.method = Method::DLSS,
			.qualityMode = 6,
			.renderScaleMode = true,
			.dlssProfile = 1,
		};
		if (!MatchesTarget(profile, target))
			return false;
		if (IsValidTarget(TargetProfile{ .method = Method::DLSS,
				.qualityMode = 0,
				.renderScaleMode = true }) ||
			IsValidTarget(TargetProfile{ .method = Method::DLSS,
				.qualityMode = 1,
				.renderScaleMode = false }) ||
			IsValidTarget(TargetProfile{ .method = Method::DLSS,
				.qualityMode = 1,
				.renderScaleMode = true,
				.matchFSRRuntime = true }) ||
			IsValidTarget(TargetProfile{ .method = Method::FSR,
				.qualityMode = 1,
				.renderScaleMode = true,
				.matchDLSSProfile = true })) {
			return false;
		}
		profile.qualityMode = 5;
		if (MatchesTarget(profile, target))
			return false;
		profile.qualityMode = 6;
		profile.dlssProfile = 0;
		if (MatchesTarget(profile, target))
			return false;
		Profile sameDLSS = profile;
		profile.fsr4Runtime = true;
		if (!ProfilesAgree(profile, sameDLSS))
			return false;

		TargetProfile fsr{
			.method = Method::FSR,
			.qualityMode = 3,
			.renderScaleMode = true,
			.matchFSRRuntime = true,
			.fsr4Runtime = true,
		};
		Profile fsrProfile{
			.valid = true,
			.method = Method::FSR,
			.qualityMode = 3,
			.renderScaleMode = true,
			.fsr4Runtime = true,
		};
		return MatchesTarget(fsrProfile, fsr) &&
		       !MatchesTarget(Profile{ .valid = true, .method = Method::FSR, .qualityMode = 3, .renderScaleMode = true, .fsr4Runtime = false }, fsr);
	}

	constexpr bool CoversObservedProfileTargets()
	{
		const Profile dlss{
			.valid = true,
			.method = Method::DLSS,
			.qualityMode = 2,
			.renderScaleMode = true,
			.dlssProfile = 1,
		};
		const auto dlssTarget = ExactObservationTarget(dlss);
		const Profile fsr{
			.valid = true,
			.method = Method::FSR,
			.qualityMode = 0,
			.renderScaleMode = false,
			.fsr4Runtime = true,
		};
		const auto fsrTarget = ExactObservationTarget(fsr);
		return IsValidTarget(dlssTarget) && MatchesTarget(dlss, dlssTarget) &&
		       dlssTarget.matchDLSSProfile && !dlssTarget.matchFSRRuntime &&
		       IsValidTarget(fsrTarget) && MatchesTarget(fsr, fsrTarget) &&
		       fsrTarget.matchFSRRuntime && !fsrTarget.matchDLSSProfile &&
		       !IsValidTarget(ExactObservationTarget(Profile{}));
	}

	constexpr bool CoversActiveAndNativeStability()
	{
		auto activeFacts = ActiveDLSSStableFacts();
		if (EvaluateStability(ActiveDLSS(), activeFacts) != kFailureNone)
			return false;
		activeFacts.vendorPresentationStable = false;
		if ((EvaluateStability(ActiveDLSS(), activeFacts) &
				static_cast<std::uint64_t>(kFailureVendorPresentation)) == 0) {
			return false;
		}
		activeFacts.vendorPresentationStable = true;
		activeFacts.foveationSettingsMatch = false;
		if ((EvaluateStability(ActiveDLSS(), activeFacts) &
				static_cast<std::uint64_t>(kFailureFoveationSettings)) == 0) {
			return false;
		}

		TargetProfile native{
			.method = Method::FSR,
			.qualityMode = 0,
			.renderScaleMode = false,
		};
		auto nativeFacts = CommonStableFacts();
		nativeFacts.apiNativeContract = true;
		nativeFacts.physicalNativeContract = true;
		nativeFacts.nativePresentationStable = true;
		return EvaluateStability(native, nativeFacts) == kFailureNone;
	}

	constexpr bool CoversQualificationMilestones()
	{
		auto facts = ActiveDLSSStableFacts();
		facts.workGateClear = false;
		facts.memoryTrimClear = false;
		facts.retirementClear = false;
		auto evaluation = EvaluateMilestones(ActiveDLSS(), facts);
		if (!evaluation.PresentationStable() || evaluation.CleanupDrained() ||
			evaluation.StrictSatisfied() ||
			(evaluation.cleanupFailures &
				static_cast<std::uint64_t>(kFailureWorkGate)) == 0 ||
			(evaluation.cleanupFailures &
				static_cast<std::uint64_t>(kFailureMemoryTrim)) == 0 ||
			(evaluation.cleanupFailures &
				static_cast<std::uint64_t>(kFailureRetirement)) == 0 ||
			!IsMilestoneSatisfied(Milestone::Presentation, evaluation) ||
			IsMilestoneSatisfied(Milestone::Cleanup, evaluation) ||
			IsMilestoneSatisfied(Milestone::Strict, evaluation)) {
			return false;
		}

		facts.workGateClear = true;
		facts.memoryTrimClear = true;
		facts.retirementClear = true;
		facts.diagnosticsClear = false;
		evaluation = EvaluateMilestones(ActiveDLSS(), facts);
		if (!evaluation.PresentationStable() || !evaluation.CleanupDrained() ||
			!evaluation.StrictSatisfied() ||
			EvaluateStability(ActiveDLSS(), facts) !=
				EvaluateStrictStability(ActiveDLSS(), facts)) {
			return false;
		}

		facts.physicalMutationClear = false;
		if ((EvaluatePresentationStability(ActiveDLSS(), facts) &
				static_cast<std::uint64_t>(kFailurePhysicalMutation)) == 0) {
			return false;
		}
		facts.physicalMutationClear = true;
		facts.providerTerminalClear = false;
		if ((EvaluatePresentationStability(ActiveDLSS(), facts) &
				static_cast<std::uint64_t>(kFailureProviderTerminal)) == 0) {
			return false;
		}
		facts.providerTerminalClear = true;
		facts.resourcePublicationCurrent = false;
		if ((EvaluatePresentationStability(ActiveDLSS(), facts) &
				static_cast<std::uint64_t>(kFailureResourcePublication)) == 0) {
			return false;
		}
		facts.resourcePublicationCurrent = true;
		facts.terminalClear = false;
		return (EvaluatePresentationStability(ActiveDLSS(), facts) &
				   static_cast<std::uint64_t>(kFailureTerminal)) != 0;
	}

	constexpr bool CoversContractSpecificShaderCompilation()
	{
		TargetProfile fsrTarget{
			.method = Method::FSR,
			.qualityMode = 3,
			.renderScaleMode = true,
		};
		auto facts = CommonStableFacts();
		facts.apiActiveContract = true;
		facts.physicalActiveContract = true;
		facts.presentationPhaseStable = true;
		facts.fidelityStable = true;
		facts.vendorPresentationStable = true;
		facts.lifecycleStable = true;
		facts.fsrDispatchStable = true;
		facts.shaderCompilationIdle = false;
		facts.requiredShaderCompilationComplete = true;
		const auto evaluation = EvaluateMilestones(fsrTarget, facts);
		return evaluation.PresentationStable() &&
		       !evaluation.CleanupDrained() &&
		       (evaluation.cleanupFailures &
				   static_cast<std::uint64_t>(kFailureShaderCompilation)) != 0 &&
		       !evaluation.StrictSatisfied();
	}

	constexpr bool CoversMilestoneParsingAndFirstTimestamps()
	{
		Milestone milestone = Milestone::Cleanup;
		if (!TryParseMilestone("strict", milestone) ||
			milestone != Milestone::Strict ||
			!TryParseMilestone("presentation", milestone) ||
			milestone != Milestone::Presentation ||
			!TryParseMilestone("cleanup", milestone) ||
			milestone != Milestone::Cleanup ||
			TryParseMilestone("visual", milestone) ||
			GetMilestoneName(Milestone::Strict) != "strict") {
			return false;
		}

		FirstObservation presentation;
		FirstObservation cleanup;
		FirstObservation strict;
		RecordFirstObservation(true, 100, 20, presentation);
		RecordFirstObservation(true, 150, 25, presentation);
		RecordFirstObservation(false, 175, 27, cleanup);
		RecordFirstObservation(true, 200, 30, cleanup);
		RecordFirstObservation(true, 200, 30, strict);
		return presentation.tick == 100 && presentation.frame == 20 &&
		       cleanup.tick == 200 && cleanup.frame == 30 &&
		       strict.tick == 200 && strict.frame == 30;
	}

	constexpr bool CoversStaleSourceRejection()
	{
		return !IsFreshTransition(false, true, 10, 11, false) &&
		       IsFreshTransition(true, true, 10, 11, false) &&
		       !IsFreshTransition(true, true, 10, 10, false) &&
		       !IsFreshTransition(true, false, 10, 11, false) &&
		       IsFreshTransition(true, false, 10, 10, true);
	}

	constexpr bool CoversOwnership()
	{
		return CanBegin(0) && !CanBegin(7) &&
		       OwnsTransition(7, 7) && !OwnsTransition(7, 8) &&
		       !OwnsTransition(0, 0) &&
		       OwnsTransitionInstance(7, 11, 7, 11) &&
		       !OwnsTransitionInstance(7, 11, 7, 12) &&
		       !OwnsTransitionInstance(7, 11, 8, 11) &&
		       !OwnsTransitionInstance(7, 0, 7, 0);
	}

	constexpr bool CoversNestedPropertyPolicy()
	{
		return IsTargetPropertyAllowed("method") &&
		       IsTargetPropertyAllowed("qualityMode") &&
		       IsTargetPropertyAllowed("renderScaleMode") &&
		       IsTargetPropertyAllowed("dlssProfile") &&
		       IsTargetPropertyAllowed("fsrRuntime") &&
		       !IsTargetPropertyAllowed("fsrPreset") &&
		       IsFoveationPropertyAllowed("foveatedVendorDispatch") &&
		       IsFoveationPropertyAllowed("foveatedCenterArea") &&
		       IsFoveationPropertyAllowed("peripheryTAAEnable") &&
		       IsFoveationPropertyAllowed("peripheryTAACenterArea") &&
		       IsFoveationPropertyAllowed("peripheryTAAOuterScale") &&
		       !IsFoveationPropertyAllowed("periphery_taa_enable") &&
		       !IsFoveationPropertyAllowed("floatTolerance");
	}

	constexpr bool CoversTerminalDiagnosticPolicy()
	{
		TerminalDiagnosticDeltas stretchOnly{
			.presentationStretchEyeObservations = 4,
		};
		if (HasTerminalDiagnosticFailure(stretchOnly))
			return false;
		TerminalDiagnosticDeltas vendorFailure{
			.vendorFailureStretchEyeObservations = 1,
		};
		if (!HasTerminalDiagnosticFailure(vendorFailure) ||
			HasQualificationControlFailure(vendorFailure))
			return false;
		TerminalDiagnosticDeltas inactiveTraceFailure{
			.traceDroppedRecords = 1,
		};
		if (HasTerminalDiagnosticFailure(inactiveTraceFailure))
			return false;
		inactiveTraceFailure.traceApplicable = true;
		if (!HasTerminalDiagnosticFailure(inactiveTraceFailure))
			return false;
		TerminalDiagnosticDeltas traceSessionLost{
			.traceApplicable = true,
			.traceSessionLost = true,
		};
		if (!HasQualificationControlFailure(traceSessionLost))
			return false;
		TerminalDiagnosticDeltas regression{
			.monotonicCounterRegression = true,
		};
		return HasTerminalDiagnosticFailure(regression) &&
		       CounterRegressed(2, 3) && !CounterRegressed(3, 3) &&
		       MonotonicCounterDelta(5, 3) == 2 &&
		       MonotonicCounterDelta(2, 3) == 0 &&
		       GenerationCounterRegressed(2, 3, 7, 4, 7, 4) &&
		       !GenerationCounterRegressed(0, 3, 8, 0, 7, 4) &&
		       GenerationCounterDelta(5, 3, 7, 4, 7, 4) == 2 &&
		       GenerationCounterDelta(0, 3, 8, 0, 7, 4) == 0 &&
		       GenerationCounterDelta(1, 3, 8, 5, 7, 4) == 1 &&
		       IsFoveationInvariantViolation(true, false) &&
		       !IsFoveationInvariantViolation(true, true) &&
		       !IsFoveationInvariantViolation(false, false) &&
		       HasRequiredPresentationHistory(false, 1, 1, 0) &&
		       !HasRequiredPresentationHistory(false, 0, 1, 0) &&
		       !HasRequiredPresentationHistory(false, 1, 0, 0) &&
		       !HasRequiredPresentationHistory(true, 1, 2, 2) &&
		       HasRequiredPresentationHistory(true, 2, 2, 2) &&
		       IsTransientObservationDispatchError("main_thread_timeout") &&
		       !IsTransientObservationDispatchError("service_unavailable") &&
		       !IsTransientObservationDispatchError("");
	}

	constexpr bool CoversCoherentPresentationFrames()
	{
		return HasCoherentPresentationFrames(
				   false, 10, 11, 11, 7, 7, 0, 0) &&
		       !HasCoherentPresentationFrames(
				   false, 10, 11, 12, 7, 8, 0, 0) &&
		       HasCoherentPresentationFrames(
				   true, 100, 111, 110, 51, 50, 110, 50) &&
		       HasCoherentPresentationFrames(
				   true, 100, 110, 110, 50, 50, 110, 50) &&
		       !HasCoherentPresentationFrames(
				   true, 110, 111, 110, 51, 50, 110, 50) &&
		       !HasCoherentPresentationFrames(
				   true, 100, 111, 110, 50, 50, 110, 50) &&
		       !HasCoherentPresentationFrames(
				   true, 100, 111, 111, 51, 51, 110, 50);
	}

	constexpr bool CoversTimeoutMath()
	{
		constexpr std::uint64_t frequency = 10'000'000;
		constexpr std::uint64_t begin = 25'000'000;
		constexpr std::uint64_t deadline =
			SaturatingDeadlineTick(begin, 120'000, frequency);
		constexpr std::uint64_t maximum =
			std::numeric_limits<std::uint64_t>::max();
		constexpr std::uint64_t adversarialTicks =
			999 * (maximum / 1000) + (999 * (maximum % 1000)) / 1000;
		return deadline == 1'225'000'000 &&
		       IsWithinDeadline(deadline, deadline) &&
		       !IsWithinDeadline(deadline + 1, deadline) &&
		       ElapsedMilliseconds(begin, begin + 5'000'000, frequency) == 500.0 &&
		       SaturatingDeadlineTick(
				   maximum - 1,
				   1,
				   frequency) == maximum &&
		       SaturatingDeadlineTick(0, 999, maximum) == adversarialTicks &&
		       ElapsedFrames(10, 18) == 8 &&
		       ElapsedFrames(std::numeric_limits<std::uint32_t>::max(), 1) == 2 &&
		       ElapsedFrames(18, 10) == 0 &&
		       kElapsedMillisecondsReceiptField == "elapsedMs" &&
		       kElapsedFramesReceiptField == "elapsedFrames";
	}

	static_assert(CoversExactProfileMatching());
	static_assert(CoversObservedProfileTargets());
	static_assert(CoversActiveAndNativeStability());
	static_assert(CoversQualificationMilestones());
	static_assert(CoversContractSpecificShaderCompilation());
	static_assert(CoversMilestoneParsingAndFirstTimestamps());
	static_assert(CoversStaleSourceRejection());
	static_assert(CoversOwnership());
	static_assert(CoversNestedPropertyPolicy());
	static_assert(CoversTerminalDiagnosticPolicy());
	static_assert(CoversCoherentPresentationFrames());
	static_assert(CoversTimeoutMath());
}

int main() {}
