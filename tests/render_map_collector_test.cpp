#include "RenderMap/Collector.h"

#include <atomic>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace
{
	using namespace CSX::RenderMap;

	void Check(bool a_condition, std::string_view a_message)
	{
		if (!a_condition)
			throw std::runtime_error(std::string(a_message));
	}

	CollectorConfig Config(std::uint64_t a_maxEvents = 64)
	{
		CollectorConfig config{
			.captureNumericId = 42,
			.maxFrames = 3,
			.maxEvents = a_maxEvents,
			.maxBytes = 1,
			.maxDuration = std::chrono::minutes(1),
			.maxScopeDepth = 8,
		};
		config.maxBytes = Collector::RequiredStorageBytes(config);
		return config;
	}

	void TestBoundsValidation()
	{
		Collector collector;
		auto config = Config();
		auto oneEventBudget = config;
		oneEventBudget.maxEvents = 1;
		config.maxBytes = Collector::RequiredStorageBytes(oneEventBudget) - 1;
		Check(collector.Start(config) == StartResult::kInvalidBounds, "undersized byte bound was accepted");
		config = Config();
		config.maxFrames = 0;
		Check(collector.Start(config) == StartResult::kInvalidBounds, "zero frame bound was accepted");
		config = Config();
		config.maxScopeDepth = static_cast<std::uint8_t>(kMaximumScopeDepth + 1);
		Check(collector.Start(config) == StartResult::kInvalidBounds, "oversized scope depth was accepted");
		config = Config();
		config.maxShaderObservations = 0;
		Check(collector.Start(config) == StartResult::kInvalidBounds, "zero shader-observation bound was accepted");
		config = Config();
		config.maxStageShaderObservations = 0;
		Check(collector.Start(config) == StartResult::kInvalidBounds, "zero stage-shader-observation bound was accepted");
		config = Config();
		config.maxTargetViewObservations = 0;
		Check(collector.Start(config) == StartResult::kInvalidBounds, "zero target-view-observation bound was accepted");
		config = Config();
		config.maxTargetBindingObservations = 0;
		Check(collector.Start(config) == StartResult::kInvalidBounds, "zero target-binding-observation bound was accepted");
	}

	void TestNestedScopes()
	{
		Collector collector;
		Check(collector.Start(Config()) == StartResult::kStarted, "collector did not start");
		Check(collector.Start(Config()) == StartResult::kAlreadyCapturing, "second start was accepted");
		collector.SetThreadFrameContext({ 10, 10, 20, Eye::kLeft, 1 });

		const auto passObservation = collector.AllocateObservationId();
		const auto techniqueObservation = collector.AllocateObservationId();
		Check(passObservation != 0 && techniqueObservation != 0 && passObservation != techniqueObservation,
			"observation IDs were not allocated uniquely");

		{
			auto pass = collector.EnterScope(
				ScopeKind::kRenderPass,
				passObservation,
				EventKind::kRenderPassEnter,
				EventKind::kRenderPassExit);
			Check(pass.IsActive(), "render-pass scope was not entered");
			Check(collector.GetThreadScopes().renderPass.observationId == passObservation, "render-pass scope was not visible");

			{
				auto technique = collector.EnterScope(
					ScopeKind::kTechnique,
					techniqueObservation,
					EventKind::kTechniqueBegin,
					EventKind::kTechniqueEnd);
				Check(technique.IsActive(), "technique scope was not entered");
				Check(collector.Record(EventKind::kDraw, {}, 99, 17) == RecordResult::kRecorded,
					"draw was not recorded");
			}
		}

		auto snapshot = collector.Stop();
		Check(snapshot.has_value(), "collector did not stop");
		Check(!collector.Stop().has_value(), "collector stopped twice");
		Check(snapshot->events.size() == 5, "nested scope capture has the wrong event count");
		Check(snapshot->statistics.recorded == 5, "nested scope recorded count is wrong");
		Check(snapshot->events[0].kind == EventKind::kRenderPassEnter, "pass begin is missing");
		Check(snapshot->events[1].kind == EventKind::kTechniqueBegin, "technique begin is missing");
		Check(snapshot->events[2].kind == EventKind::kDraw, "draw is missing");
		Check(snapshot->events[3].kind == EventKind::kTechniqueEnd, "technique end is missing");
		Check(snapshot->events[4].kind == EventKind::kRenderPassExit, "pass end is missing");

		for (std::size_t index = 0; index < snapshot->events.size(); ++index) {
			Check(snapshot->events[index].sequence == index, "event sequences are not contiguous");
			Check(snapshot->events[index].frame.cpuFrame == 10, "frame context was not copied");
		}
		Check(snapshot->events[0].scopes.renderPass.observationId == passObservation, "pass begin lacks its scope");
		Check(snapshot->events[0].scopes.technique.observationId == 0, "pass begin inherited a technique");
		Check(snapshot->events[2].scopes.renderPass.observationId == passObservation, "draw lacks its pass scope");
		Check(snapshot->events[2].scopes.technique.observationId == techniqueObservation, "draw lacks its technique scope");
		Check(snapshot->events[2].deviceContextObservationId == 99, "draw lacks its device-context identity");
		Check(snapshot->events[2].commandStreamSequence == 17, "draw lacks its command-stream sequence");
		Check(snapshot->events[3].scopes.technique.observationId == techniqueObservation, "technique end popped too early");
		Check(snapshot->events[4].scopes.technique.observationId == 0, "pass end retained the completed technique");
	}

	void TestCapacityLimits()
	{
		{
			Collector collector;
			auto config = Config(2);
			config.maxBytes = Collector::RequiredStorageBytes(config);
			Check(collector.Start(config) == StartResult::kStarted, "event-bound collector did not start");
			Check(collector.Record(EventKind::kCaptureMarker) == RecordResult::kRecorded, "first event was rejected");
			Check(collector.Record(EventKind::kCaptureMarker) == RecordResult::kRecorded, "second event was rejected");
			Check(collector.Record(EventKind::kCaptureMarker) == RecordResult::kEventLimit, "event limit was not enforced");
			Check(!collector.IsCapturing(), "event limit did not quiesce the collector");
			Check(collector.Record(EventKind::kCaptureMarker) == RecordResult::kStopped,
				"quiesced event-bound collector did not use the stopped fast path");
			auto snapshot = collector.Stop();
			Check(snapshot->events.size() == 2, "event-bound capture exceeded capacity");
			Check(snapshot->statistics.attempted == 3, "event-bound attempted count is wrong");
			Check(snapshot->statistics.droppedEventLimit == 1, "event-limit drop was not counted");
			Check(snapshot->stopReason == StopReason::kEventLimit, "event limit did not determine completion reason");
		}

		{
			Collector collector;
			auto config = Config(10);
			auto twoEventBudget = config;
			twoEventBudget.maxEvents = 2;
			config.maxBytes = Collector::RequiredStorageBytes(twoEventBudget);
			Check(collector.Start(config) == StartResult::kStarted, "byte-bound collector did not start");
			Check(collector.Record(EventKind::kCaptureMarker) == RecordResult::kRecorded, "first byte-bound event was rejected");
			Check(collector.Record(EventKind::kCaptureMarker) == RecordResult::kRecorded, "second byte-bound event was rejected");
			Check(collector.Record(EventKind::kCaptureMarker) == RecordResult::kByteLimit, "byte limit was not enforced");
			Check(!collector.IsCapturing(), "byte limit did not quiesce the collector");
			auto snapshot = collector.Stop();
			Check(snapshot->events.size() == 2, "byte-bound capture exceeded capacity");
			Check(snapshot->statistics.droppedByteLimit == 1, "byte-limit drop was not counted");
			Check(snapshot->stopReason == StopReason::kByteLimit, "byte limit did not determine completion reason");
		}
	}

	void TestFrameLimit()
	{
		Collector collector;
		auto config = Config();
		config.maxFrames = 2;
		Check(collector.Start(config) == StartResult::kStarted, "frame-bound collector did not start");
		collector.SetThreadFrameContext({ 100, 100, 100, Eye::kBoth, 3 });
		Check(collector.Record(EventKind::kFrameBegin) == RecordResult::kRecorded, "first frame was rejected");
		collector.SetThreadFrameContext({ 101, 101, 101, Eye::kBoth, 3 });
		Check(collector.Record(EventKind::kFrameBegin) == RecordResult::kRecorded, "second frame was rejected");
		collector.SetThreadFrameContext({ 102, 102, 102, Eye::kBoth, 3 });
		Check(collector.Record(EventKind::kFrameBegin) == RecordResult::kFrameLimit, "frame limit was not enforced");
		Check(!collector.IsCapturing(), "frame limit did not quiesce the collector");
		auto snapshot = collector.Stop();
		Check(snapshot->statistics.droppedFrameLimit == 1, "frame-limit drop was not counted");
		Check(snapshot->stopReason == StopReason::kFrameLimit, "frame limit did not determine completion reason");
	}

	void TestScopeOverflow()
	{
		Collector collector;
		auto config = Config();
		config.maxScopeDepth = 1;
		Check(collector.Start(config) == StartResult::kStarted, "scope-bound collector did not start");
		{
			auto outer = collector.EnterScope(
				ScopeKind::kRenderPass, 1, EventKind::kRenderPassEnter, EventKind::kRenderPassExit);
			auto overflow = collector.EnterScope(
				ScopeKind::kRenderPass, 2, EventKind::kRenderPassEnter, EventKind::kRenderPassExit);
			Check(outer.IsActive(), "outer scope was rejected");
			Check(!overflow.IsActive(), "overflow scope was accepted");
		}
		auto snapshot = collector.Stop();
		Check(snapshot->statistics.scopeOverflow == 1, "scope overflow was not counted");
		Check(snapshot->events.size() == 2, "scope overflow emitted a partial scope");
	}

	void TestOutOfOrderScopeCleanup()
	{
		Collector collector;
		Check(collector.Start(Config()) == StartResult::kStarted, "scope-cleanup collector did not start");
		auto outer = collector.EnterScope(
			ScopeKind::kRenderPass, 11, EventKind::kRenderPassEnter, EventKind::kRenderPassExit);
		auto inner = collector.EnterScope(
			ScopeKind::kRenderPass, 12, EventKind::kRenderPassEnter, EventKind::kRenderPassExit);
		Check(outer.IsActive() && inner.IsActive(), "nested same-kind scopes were not entered");
		outer = {};
		Check(collector.GetThreadScopes().renderPass.observationId == 12,
			"out-of-order cleanup removed the active inner scope");
		inner = {};
		Check(collector.GetThreadScopes().renderPass.observationId == 0,
			"out-of-order cleanup left a stale outer scope");
		auto snapshot = collector.Stop();
		Check(snapshot->statistics.scopeMismatch == 1, "out-of-order cleanup was not counted");
	}

	void TestTimeLimit()
	{
		Collector collector;
		auto config = Config();
		config.maxDuration = std::chrono::microseconds(100);
		Check(collector.Start(config) == StartResult::kStarted, "time-bound collector did not start");
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		Check(collector.Record(EventKind::kCaptureMarker) == RecordResult::kTimeLimit,
			"time limit was not enforced");
		Check(!collector.IsCapturing(), "time limit did not quiesce the collector");
		auto snapshot = collector.Stop();
		Check(snapshot->statistics.droppedTimeLimit == 1, "time-limit drop was not counted");
		Check(snapshot->stopReason == StopReason::kTimeLimit, "time limit did not determine completion reason");
	}

	void TestConcurrentRecording()
	{
		Collector collector;
		constexpr std::size_t threadCount = 8;
		constexpr std::size_t recordsPerThread = 100;
		const auto total = threadCount * recordsPerThread;
		auto config = Config(total);
		config.maxFrames = 1;
		Check(collector.Start(config) == StartResult::kStarted, "concurrent collector did not start");

		std::atomic_bool concurrentFailure{ false };
		std::vector<std::jthread> threads;
		threads.reserve(threadCount);
		for (std::size_t thread = 0; thread < threadCount; ++thread) {
			threads.emplace_back([&collector, &concurrentFailure] {
				for (std::size_t record = 0; record < recordsPerThread; ++record)
					if (collector.Record(EventKind::kPipelineBind) != RecordResult::kRecorded)
						concurrentFailure.store(true, std::memory_order_relaxed);
			});
		}
		threads.clear();
		Check(!concurrentFailure.load(std::memory_order_relaxed), "concurrent record failed");

		auto snapshot = collector.Stop();
		Check(snapshot->events.size() == total, "concurrent capture lost records");
		Check(snapshot->statistics.recorded == total, "concurrent recorded count is wrong");
		std::unordered_set<std::uint64_t> threadIds;
		for (std::size_t index = 0; index < snapshot->events.size(); ++index) {
			Check(snapshot->events[index].sequence == index, "concurrent sequences are not ordered");
			threadIds.insert(snapshot->events[index].threadId);
		}
		Check(threadIds.size() == threadCount, "thread identity was not preserved");
	}

	void TestStoppedGuardDoesNotLeak()
	{
		Collector collector;
		Check(collector.Start(Config()) == StartResult::kStarted, "first guard session did not start");
		auto guard = collector.EnterScope(
			ScopeKind::kRenderPass, 7, EventKind::kRenderPassEnter, EventKind::kRenderPassExit);
		Check(guard.IsActive(), "guard session scope did not start");
		auto first = collector.Stop();
		Check(first->events.size() == 1, "stopped guard emitted an end event after stop");

		Check(collector.Start(Config()) == StartResult::kStarted, "second guard session did not start");
		collector.SetThreadFrameContext({ 5, 5, 5, Eye::kMono, 1 });
		guard = {};
		Check(collector.Record(EventKind::kCaptureMarker) == RecordResult::kRecorded, "second guard session did not record");
		auto second = collector.Stop();
		Check(second->events.size() == 1, "stale guard contaminated the new session");
		Check(second->events[0].scopes.renderPass.observationId == 0, "stale render-pass scope leaked into the new session");
	}
}

int main()
{
	try {
		TestBoundsValidation();
		TestNestedScopes();
		TestCapacityLimits();
		TestFrameLimit();
		TestScopeOverflow();
		TestOutOfOrderScopeCleanup();
		TestTimeLimit();
		TestConcurrentRecording();
		TestStoppedGuardDoesNotLeak();
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
