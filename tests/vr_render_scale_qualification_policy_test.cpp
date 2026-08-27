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
		return {
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

	constexpr bool CoversActiveAndNativeStability()
	{
		auto activeFacts = CommonStableFacts();
		activeFacts.apiActiveContract = true;
		activeFacts.physicalActiveContract = true;
		activeFacts.presentationPhaseStable = true;
		activeFacts.fidelityStable = true;
		activeFacts.vendorPresentationStable = true;
		activeFacts.lifecycleStable = true;
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
		if (!HasTerminalDiagnosticFailure(vendorFailure))
			return false;
		TerminalDiagnosticDeltas inactiveTraceFailure{
			.traceDroppedRecords = 1,
		};
		if (HasTerminalDiagnosticFailure(inactiveTraceFailure))
			return false;
		inactiveTraceFailure.traceApplicable = true;
		if (!HasTerminalDiagnosticFailure(inactiveTraceFailure))
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
	static_assert(CoversActiveAndNativeStability());
	static_assert(CoversStaleSourceRejection());
	static_assert(CoversOwnership());
	static_assert(CoversNestedPropertyPolicy());
	static_assert(CoversTerminalDiagnosticPolicy());
	static_assert(CoversTimeoutMath());
}

int main() {}
