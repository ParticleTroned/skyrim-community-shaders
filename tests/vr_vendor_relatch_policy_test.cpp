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
			(static_cast<WorkGateState>(std::numeric_limits<std::uint32_t>::max()) <<
			 kWorkGateStateMaskBits) |
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

	constexpr bool CoversVendorResourcePredicates()
	{
		for (std::uint32_t bits = 0; bits < (1u << 3); ++bits) {
			const bool vendorEvaluation = (bits & (1u << 0)) != 0;
			const bool preservedResources = (bits & (1u << 1)) != 0;
			const bool recreatedResources = (bits & (1u << 2)) != 0;
			if (UsesVendorEvaluation(vendorEvaluation) != vendorEvaluation ||
				RequiresFSRCompatibility(vendorEvaluation) != vendorEvaluation ||
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

	static_assert(CoversWorkGateMasks());
	static_assert(CoversWorkGateState());
	static_assert(CoversGameEntryConvergence());
	static_assert(CoversLifecycleMutationAdmission());
	static_assert(CoversDispatchAdmission());
	static_assert(CoversVendorResourcePredicates());
	static_assert(CoversStereoRelatchAdmission());
}

int main() {}
