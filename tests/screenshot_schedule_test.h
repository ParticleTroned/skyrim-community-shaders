#pragma once

// Exercise the production scheduler with deterministic clocks and a receipt sink.
inline void RunScreenshotScheduleTests()
{
	using CSX::ScreenshotPolicy::SequenceClock;
	using namespace std::chrono_literals;
	const auto epoch = SequenceClock::Clock::time_point(100s);
	auto require = [](bool condition, const char* message) {
		if (!condition)
			throw std::runtime_error(message);
	};
	SequenceClock clock;
	require(clock.Observe(10, epoch, true, 2, 20) && !clock.started, "paused admission consumed the start delay");
	require(clock.Observe(100, epoch + 1s, false, 2, 20) && clock.nextFrame == 102 && clock.nextTime == epoch + 1020ms,
		"render observation did not establish the schedule epoch");
	require(!clock.Due(true, 101, epoch + 1010ms) && clock.Due(true, 102, epoch + 1020ms), "frame delay shifted");
	clock.Advance(6, 100);
	require(clock.Observe(104, epoch + 1040ms, true, 2, 20) && !clock.Due(true, 200, epoch + 2s), "paused clock stayed due");
	require(clock.Observe(204, epoch + 2040ms, false, 2, 20) && clock.nextFrame == 208 && clock.nextTime == epoch + 2120ms,
		"resume did not preserve the remaining interval");
	require(!clock.Observe(203, epoch + 2050ms, false, 2, 20), "frame regression was accepted");
	require(!clock.Observe(205, epoch + 2030ms, false, 2, 20), "monotonic clock regression was accepted");
	require(!SequenceClock::Missed(false, 0, epoch, 100, 0, epoch + 99ms) &&
				SequenceClock::Missed(false, 0, epoch, 100, 0, epoch + 100ms),
		"wall-clock sampling did not allow the first Present within the interval");
	require(!SequenceClock::Missed(true, 100, epoch, 100, 100, epoch + 1s) &&
				SequenceClock::Missed(true, 100, epoch, 100, 101, epoch),
		"frame cadence used the wall clock or accepted a late slot");

	auto fixture = [&](ScreenshotApi& api) -> ScreenshotApi::SequenceRecord& {
		api.sequences.clear();
		api.requests.clear();
		api.sequenceOrder.push_back("parent");
		auto& sequence = api.sequences["parent"];
		sequence.inFlight = 0;
		sequence.activeChildRequestId.clear();
		sequence.clock.Observe(100, epoch, false, 0, 0);
		return sequence;
	};
	ScreenshotApi slowEncoder;
	auto& slow = fixture(slowEncoder);
	const auto first = slowEncoder.PrepareDueFrameLocked(100, epoch);
	require(first && first->ordinal == 1, "first slot was lost to admission latency");
	require(!slowEncoder.PrepareDueFrameLocked(103, epoch + 30ms) && slow.dropped == 3 && slow.inFlight == 1 &&
				slow.activeChildRequestId == first->childRequestId && slow.nextOrdinal == 5,
		"an encoding stall lost drop receipts or replaced the acquired child's ownership");
	slow.inFlight = 0;
	slow.activeChildRequestId.clear();
	const auto resumed = slowEncoder.PrepareDueFrameLocked(104, epoch + 40ms);
	require(resumed && resumed->ordinal == 5, "encoder recovery captured an overdue slot");

	ScreenshotApi delayed;
	auto& delayedSequence = fixture(delayed);
	const auto current = delayed.PrepareDueFrameLocked(103, epoch + 30ms);
	require(current && current->ordinal == 4 && delayedSequence.dropped == 3, "late observation compressed missed slots");
	ScreenshotApi wall;
	auto& wallSequence = fixture(wall);
	wallSequence.scheduleBasis = "wall_clock";
	const auto wallCurrent = wall.PrepareDueFrameLocked(120, epoch + 250ms);
	require(wallCurrent && wallCurrent->ordinal == 3 && wallSequence.dropped == 2, "wall-clock catch-up lost its original cadence");

	ScreenshotApi gap;
	auto& gapSequence = fixture(gap);
	gapSequence.frameCount = 10000;
	gap.sequenceOrder.push_back("ready");
	auto& ready = gap.sequences["ready"];
	ready.requestId = "ready";
	ready.inFlight = 0;
	ready.activeChildRequestId.clear();
	ready.clock.Observe(10000, epoch + 10s, false, 0, 0);
	require(!gap.PrepareDueFrameLocked(10000, epoch + 10s) &&
				gapSequence.dropped == CSX::ScreenshotPolicy::MaximumSequenceSlotsPerTick,
		"a long gap exceeded the per-Present receipt budget");
	const auto nextSequence = gap.PrepareDueFrameLocked(10000, epoch + 10s);
	require(nextSequence && nextSequence->parentRequestId == "ready", "gap accounting starved another sequence");

	ScreenshotApi paused;
	auto& pausedSequence = fixture(paused);
	pausedSequence.frameCount = 1;
	const auto pending = paused.PrepareDueFrameLocked(100, epoch);
	globals::game::testUI.paused = true;
	const auto interrupted = paused.UpdateSequenceClocksLocked(101, epoch + 10ms);
	require(interrupted.size() == 1 && interrupted.front().first == pending->childRequestId,
		"pause failed to interrupt the last pending eye acquisition");
	paused.requests.at(pending->childRequestId).sourceAcquired = true;
	require(paused.UpdateSequenceClocksLocked(102, epoch + 20ms).empty(), "pause cancelled an acquired image instead of draining it");
	globals::game::testUI.paused = false;

	ScreenshotApi queued;
	auto& queuedSequence = fixture(queued);
	const auto queuedFrame = queued.PrepareDueFrameLocked(100, epoch);
	queued.sequenceDispatchQueue.push_back({ queuedFrame->childRequestId });
	globals::testState.isLoadingMenuOpen = true;
	require(queued.UpdateSequenceClocksLocked(101, epoch + 10ms).empty() && queued.sequenceDispatchQueue.empty() &&
				queuedSequence.dropped == 1 && !queued.PrepareDueFrameLocked(200, epoch + 1s),
		"loading allowed a queued sequence frame to dispatch");
	globals::testState.isLoadingMenuOpen = false;
	queued.UpdateSequenceClocksLocked(201, epoch + 1010ms);
	require(queuedSequence.clock.nextFrame == 201, "pause was charged as missed cadence");
	queued.UpdateSequenceClocksLocked(200, epoch + 1020ms);
	require(queuedSequence.stopRequested && !queued.PrepareDueFrameLocked(202, epoch + 1030ms), "clock regression did not stop scheduling");

	ScreenshotApi stopped;
	auto& stoppedSequence = fixture(stopped);
	stoppedSequence.stopRequested = true;
	require(!stopped.PrepareDueFrameLocked(200, epoch + 1s), "stop created catch-up receipts");
	stoppedSequence.stopRequested = false;
	stoppedSequence.cancelRequested = true;
	require(!stopped.PrepareDueFrameLocked(200, epoch + 1s), "cancellation created catch-up receipts");
	ScreenshotApi cancelledSource;
	auto& cancelledSequence = fixture(cancelledSource);
	const auto cancelledFrame = cancelledSource.PrepareDueFrameLocked(100, epoch);
	auto& cancelledChild = cancelledSource.requests.at(cancelledFrame->childRequestId);
	cancelledChild.cancelRequested = true;
	cancelledSource.FinishSourceLocked(cancelledChild, "dropped", "paused_during_acquisition");
	require(cancelledChild.state == "cancelled" && cancelledSequence.dropped == 0,
		"pause interruption overwrote an explicit cancellation");
}
