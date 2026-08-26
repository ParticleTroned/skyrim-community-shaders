#include "Features/Upscaling/VRPresentationStretchTelemetryPolicy.h"

#include <limits>

namespace
{
	constexpr bool CoversPresentationStretchEpisodeLifecycle()
	{
		using namespace VRPresentationStretchTelemetryPolicy;
		State state{};
		Observe(state, { ObservationKind::AllowedStretch, 0, 10, 100, 1000 });
		Observe(state, { ObservationKind::AllowedStretch, 1, 10, 100, 1010 });
		const auto started = Inspect(state, 1500);
		if (started.episodes != 1 || started.completedEpisodes != 0 ||
			!started.active || started.activeFrames != 1 ||
			!started.activeQpcTimingAvailable || started.activeQpcTicks != 500) {
			return false;
		}

		Observe(state, { ObservationKind::AllowedStretch, 0, 11, 101, 2000 });
		Observe(state, { ObservationKind::AllowedStretch, 1, 11, 101, 2010 });
		const auto continued = Inspect(state, 2500);
		if (continued.episodes != 1 || continued.completedEpisodes != 0 ||
			!continued.active || continued.activeFrames != 2 ||
			continued.activeQpcTicks != 1500) {
			return false;
		}

		Observe(state, { ObservationKind::VendorFailureStretch, 0, 12, 102, 3000 });
		Observe(state, { ObservationKind::VendorFailureStretch, 1, 12, 102, 3010 });
		const auto completed = Inspect(state, 3500);
		return completed.episodes == 1 && completed.completedEpisodes == 1 &&
		       completed.timedCompletedEpisodes == 1 && !completed.active &&
		       completed.completedFrames == 2 &&
		       completed.completedQpcTicks == 2000 &&
		       completed.maximumFrames == 2 &&
		       completed.maximumQpcTicks == 2000;
	}

	constexpr bool CoversPresentationStretchFailureSeparation()
	{
		using namespace VRPresentationStretchTelemetryPolicy;
		State mixedVendorCycle{};
		Observe(mixedVendorCycle, { ObservationKind::AllowedStretch, 0, 15, 150, 100 });
		Observe(mixedVendorCycle, { ObservationKind::Other, 1, 15, 150, 110 });
		const auto mixedVendor = Inspect(mixedVendorCycle, 150);
		if (mixedVendor.episodes != 0 || mixedVendor.completedEpisodes != 0 ||
			mixedVendor.active) {
			return false;
		}

		State vendorFailure{};
		Observe(vendorFailure, { ObservationKind::AllowedStretch, 0, 20, 200, 100 });
		Observe(vendorFailure, { ObservationKind::AllowedStretch, 1, 20, 200, 110 });
		Observe(vendorFailure, { ObservationKind::VendorFailureStretch, 0, 21, 201, 200 });
		Observe(vendorFailure, { ObservationKind::AllowedStretch, 1, 21, 201, 210 });
		const auto afterVendorFailure = Inspect(vendorFailure, 300);
		if (afterVendorFailure.episodes != 1 ||
			afterVendorFailure.completedEpisodes != 1 ||
			afterVendorFailure.completedFrames != 1 ||
			afterVendorFailure.active) {
			return false;
		}

		State boundsMismatch{};
		Observe(boundsMismatch, { ObservationKind::AllowedStretch, 0, 30, 300, 1000 });
		Observe(boundsMismatch, { ObservationKind::AllowedStretch, 1, 30, 300, 1010 });
		Observe(boundsMismatch, { ObservationKind::BoundsMismatchFallback, 0, 31, 301, 2000 });
		Observe(boundsMismatch, { ObservationKind::BoundsMismatchFallback, 1, 31, 301, 2010 });
		const auto afterBoundsMismatch = Inspect(boundsMismatch, 2500);
		return afterBoundsMismatch.episodes == 1 &&
		       afterBoundsMismatch.completedEpisodes == 1 &&
		       afterBoundsMismatch.completedFrames == 1 &&
		       !afterBoundsMismatch.active;
	}

	constexpr bool CoversPresentationStretchCycleIdentityEdges()
	{
		using namespace VRPresentationStretchTelemetryPolicy;
		State duplicate{};
		Observe(duplicate, { ObservationKind::AllowedStretch, 0, 60, 600, 1000 });
		Observe(duplicate, { ObservationKind::AllowedStretch, 0, 60, 600, 1010 });
		const auto sameEyeDuplicate = Inspect(duplicate, 1050);
		if (sameEyeDuplicate.episodes != 0 || sameEyeDuplicate.active ||
			sameEyeDuplicate.activeFrames != 0) {
			return false;
		}
		Observe(duplicate, { ObservationKind::AllowedStretch, 1, 60, 600, 1020 });
		const auto deduplicated = Inspect(duplicate, 1100);
		if (deduplicated.episodes != 1 || deduplicated.activeFrames != 1)
			return false;

		State incompleteStop{};
		Observe(incompleteStop, { ObservationKind::AllowedStretch, 0, 61, 605, 1150 });
		const auto stoppedBetweenEyes = Stop(incompleteStop, 1175);
		if (stoppedBetweenEyes.activeAtStop ||
			!stoppedBetweenEyes.incompleteStereoCycleAtStop ||
			stoppedBetweenEyes.incompleteStereoEyeMaskAtStop != 0x1 ||
			stoppedBetweenEyes.snapshot.episodes != 0 ||
			stoppedBetweenEyes.snapshot.completedEpisodes != 0 ||
			stoppedBetweenEyes.snapshot.active) {
			return false;
		}

		State activeThenIncomplete{};
		Observe(activeThenIncomplete, { ObservationKind::AllowedStretch, 0, 63, 620, 2000 });
		Observe(activeThenIncomplete, { ObservationKind::AllowedStretch, 1, 63, 620, 2010 });
		Observe(activeThenIncomplete, { ObservationKind::AllowedStretch, 0, 64, 621, 2100 });
		const auto activeTailStop = Stop(activeThenIncomplete, 2150);
		if (!activeTailStop.incompleteStereoCycleAtStop ||
			activeTailStop.incompleteStereoEyeMaskAtStop != 0x1 ||
			!activeTailStop.activeAtStop || activeTailStop.activeFramesAtStop != 1 ||
			!activeTailStop.activeQpcTimingAvailableAtStop ||
			activeTailStop.activeQpcTicksAtStop != 150 ||
			activeTailStop.snapshot.episodes != 1 ||
			activeTailStop.snapshot.completedEpisodes != 1 ||
			activeTailStop.snapshot.timedCompletedEpisodes != 1 ||
			activeTailStop.snapshot.completedFrames != 1 ||
			activeTailStop.snapshot.completedQpcTicks != 150 ||
			activeTailStop.snapshot.maximumFrames != 1) {
			return false;
		}

		State peerDisqualifies{};
		Observe(peerDisqualifies, { ObservationKind::AllowedStretch, 0, 61, 610, 1200 });
		Observe(peerDisqualifies, { ObservationKind::AllowedStretch, 1, 61, 610, 1210 });
		Observe(peerDisqualifies, { ObservationKind::AllowedStretch, 0, 62, 611, 1300 });
		const auto beforePeer = Inspect(peerDisqualifies, 1350);
		Observe(peerDisqualifies, { ObservationKind::Other, 1, 62, 611, 1310 });
		const auto afterPeer = Inspect(peerDisqualifies, 1400);
		if (beforePeer.episodes != 1 || beforePeer.completedEpisodes != 0 ||
			!beforePeer.active || beforePeer.activeFrames != 1 ||
			afterPeer.episodes != beforePeer.episodes ||
			afterPeer.completedEpisodes != 1 || afterPeer.active) {
			return false;
		}

		State gap{};
		Observe(gap, { ObservationKind::AllowedStretch, 0, 60, 600, 1000 });
		Observe(gap, { ObservationKind::AllowedStretch, 1, 60, 600, 1010 });
		Observe(gap, { ObservationKind::AllowedStretch, 0, 62, 602, 2000 });
		Observe(gap, { ObservationKind::AllowedStretch, 1, 62, 602, 2010 });
		const auto afterGap = Inspect(gap, 2100);
		if (afterGap.episodes != 2 || afterGap.completedEpisodes != 1 ||
			afterGap.timedCompletedEpisodes != 1 ||
			afterGap.completedFrames != 1 ||
			afterGap.completedQpcTicks != 10 ||
			afterGap.maximumQpcTicks != 10 || !afterGap.active ||
			afterGap.activeFrames != 1 || afterGap.activeQpcTicks != 100) {
			return false;
		}

		if (!IsConsecutiveCycle(
				kCompositorCycleTokenMaximum,
				100,
				1,
				101) ||
			!IsConsecutiveCycle(
				0,
				std::numeric_limits<std::uint32_t>::max(),
				0,
				1) ||
			IsConsecutiveCycle(10, 10, 12, 12)) {
			return false;
		}

		State cycleWrap{};
		Observe(cycleWrap, { ObservationKind::AllowedStretch, 0, 70, kCompositorCycleTokenMaximum, 3000 });
		Observe(cycleWrap, { ObservationKind::AllowedStretch, 1, 70, kCompositorCycleTokenMaximum, 3010 });
		Observe(cycleWrap, { ObservationKind::AllowedStretch, 0, 71, 1, 4000 });
		Observe(cycleWrap, { ObservationKind::AllowedStretch, 1, 71, 1, 4010 });
		const auto wrapped = Inspect(cycleWrap, 4500);

		State frameWrap{};
		Observe(frameWrap, { ObservationKind::AllowedStretch, 0, std::numeric_limits<std::uint32_t>::max(), 0, 5000 });
		Observe(frameWrap, { ObservationKind::AllowedStretch, 1, std::numeric_limits<std::uint32_t>::max(), 0, 5010 });
		Observe(frameWrap, { ObservationKind::AllowedStretch, 0, 1, 0, 6000 });
		Observe(frameWrap, { ObservationKind::AllowedStretch, 1, 1, 0, 6010 });
		const auto frameWrapped = Inspect(frameWrap, 6500);
		return wrapped.episodes == 1 && wrapped.completedEpisodes == 0 &&
		       wrapped.active && wrapped.activeFrames == 2 &&
		       frameWrapped.episodes == 1 && frameWrapped.active &&
		       frameWrapped.activeFrames == 2;
	}

	constexpr bool CoversPresentationStretchActiveAtStopAccounting()
	{
		using namespace VRPresentationStretchTelemetryPolicy;
		State state{};
		Observe(state, { ObservationKind::AllowedStretch, 0, 40, 400, 4000 });
		Observe(state, { ObservationKind::AllowedStretch, 1, 40, 400, 4010 });
		const auto stopped = Stop(state, 5500);
		return stopped.activeAtStop && stopped.activeFramesAtStop == 1 &&
		       !stopped.incompleteStereoCycleAtStop &&
		       stopped.incompleteStereoEyeMaskAtStop == 0 &&
		       stopped.activeQpcTimingAvailableAtStop &&
		       stopped.activeQpcTicksAtStop == 1500 &&
		       stopped.snapshot.episodes == 1 &&
		       stopped.snapshot.completedEpisodes == 1 &&
		       stopped.snapshot.timedCompletedEpisodes == 1 &&
		       stopped.snapshot.completedFrames == 1 &&
		       stopped.snapshot.completedQpcTicks == 1500 &&
		       stopped.snapshot.maximumFrames == 1 &&
		       stopped.snapshot.maximumQpcTicks == 1500 &&
		       !stopped.snapshot.active;
	}

	constexpr bool CoversPresentationStretchUnavailableTimingAndSaturation()
	{
		using namespace VRPresentationStretchTelemetryPolicy;
		State unavailable{};
		Observe(unavailable, { ObservationKind::AllowedStretch, 0, 50, 500, 0 });
		Observe(unavailable, { ObservationKind::AllowedStretch, 1, 50, 500, 0 });
		const auto unavailableStop = Stop(unavailable, 0);
		if (unavailableStop.snapshot.completedEpisodes != 1 ||
			unavailableStop.snapshot.timedCompletedEpisodes != 0 ||
			unavailableStop.snapshot.completedQpcTicks != 0 ||
			unavailableStop.activeQpcTimingAvailableAtStop) {
			return false;
		}

		State regressed{};
		Observe(regressed, { ObservationKind::AllowedStretch, 0, 51, 501, 1000 });
		Observe(regressed, { ObservationKind::AllowedStretch, 1, 51, 501, 1010 });
		Observe(regressed, { ObservationKind::AllowedStretch, 0, 52, 502, 2000 });
		Observe(regressed, { ObservationKind::AllowedStretch, 1, 52, 502, 2010 });
		const auto regressedStop = Stop(regressed, 1500);
		if (regressedStop.snapshot.completedEpisodes != 1 ||
			regressedStop.snapshot.completedFrames != 2 ||
			regressedStop.snapshot.timedCompletedEpisodes != 0 ||
			regressedStop.snapshot.completedQpcTicks != 0 ||
			regressedStop.activeQpcTimingAvailableAtStop) {
			return false;
		}

		constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
		State saturated{};
		saturated.metrics.episodes = maximum;
		saturated.metrics.completedEpisodes = maximum;
		saturated.metrics.timedCompletedEpisodes = maximum;
		saturated.metrics.completedFrames = maximum - 2;
		saturated.metrics.completedQpcTicks = maximum - 3;
		saturated.metrics.active = true;
		saturated.metrics.activeFrames = 10;
		saturated.metrics.activeStartQpc = 100;
		saturated.metrics.activeLastAcceptedQpc = 100;
		saturated.metrics.activeQpcTimingValid = true;
		CompleteActiveEpisode(saturated.metrics, 200);
		return saturated.metrics.episodes == maximum &&
		       saturated.metrics.completedEpisodes == maximum &&
		       saturated.metrics.timedCompletedEpisodes == maximum &&
		       saturated.metrics.completedFrames == maximum &&
		       saturated.metrics.completedQpcTicks == maximum;
	}

	constexpr bool CoversPresentationStretchFrameAcceptanceBound()
	{
		using namespace VRPresentationStretchTelemetryPolicy;
		State state{};
		Observe(state, { ObservationKind::AllowedStretch, 0, 80, 800, 1000 });
		Observe(state, { ObservationKind::AllowedStretch, 1, 80, 800, 1010 });
		Observe(state, { ObservationKind::AllowedStretch, 0, 81, 801, 1100 });
		Observe(state, { ObservationKind::AllowedStretch, 1, 81, 801, 1110 });
		const auto accepted = Inspect(state, 1150);
		Observe(state, { ObservationKind::AllowedStretch, 0, 82, 802, 1200 });
		Observe(state, { ObservationKind::AllowedStretch, 1, 82, 802, 1210 });
		const auto rejected = Stop(state, 1250);
		return kMaximumAcceptedPresentationStretchFrames == 2 &&
		       accepted.activeFrames <= kMaximumAcceptedPresentationStretchFrames &&
		       rejected.snapshot.maximumFrames >
		           kMaximumAcceptedPresentationStretchFrames;
	}

	static_assert(CoversPresentationStretchEpisodeLifecycle());
	static_assert(CoversPresentationStretchFailureSeparation());
	static_assert(CoversPresentationStretchCycleIdentityEdges());
	static_assert(CoversPresentationStretchActiveAtStopAccounting());
	static_assert(CoversPresentationStretchUnavailableTimingAndSaturation());
	static_assert(CoversPresentationStretchFrameAcceptanceBound());
}

int main() {}
