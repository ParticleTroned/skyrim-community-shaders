#include "RenderMap/Collector.h"
#include "RenderMap/DevBenchCaptureBounds.h"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace
{
	thread_local bool countAllocations = false;
	thread_local std::size_t allocationCount = 0;
}

void* operator new(std::size_t a_bytes)
{
	if (auto* allocation = std::malloc(a_bytes == 0 ? 1 : a_bytes)) {
		if (countAllocations)
			++allocationCount;
		return allocation;
	}
	throw std::bad_alloc();
}

void operator delete(void* a_allocation) noexcept { std::free(a_allocation); }
void operator delete(void* a_allocation, std::size_t) noexcept { std::free(a_allocation); }
void* operator new[](std::size_t a_bytes) { return ::operator new(a_bytes); }
void operator delete[](void* a_allocation) noexcept { std::free(a_allocation); }
void operator delete[](void* a_allocation, std::size_t) noexcept { std::free(a_allocation); }

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

	void TestLateWindowCollectorAdmission()
	{
		Collector collector;
		auto config = Config();
		config.latePostProcessingWindow = true;
		constexpr std::uint32_t mainSourceTarget = 1;
		constexpr std::uint32_t vrFramebufferTarget = 114;
		config.activationTarget = vrFramebufferTarget;
		config.requestedEventKindMask = EventKindBit(EventKind::kEyeSubmitted);
		config.maxActivationWait = std::chrono::seconds(1);
		config.maxBytes = Collector::RequiredStorageBytes(config);
		auto invalid = config;
		invalid.executionWithinSelectedGeometry = true;
		Check(collector.Start(invalid) == StartResult::kInvalidBounds, "late window accepted restricted geometry");
		invalid = config;
		invalid.requestedEventKindMask = EventKindBit(EventKind::kDraw);
		Check(collector.Start(invalid) == StartResult::kInvalidBounds, "late window omitted requested eyes");
		invalid = config;
		invalid.maxActivationWait = std::chrono::nanoseconds::zero();
		Check(collector.Start(invalid) == StartResult::kInvalidBounds, "late window accepted an unbounded armed wait");
		Check(collector.Start(config) == StartResult::kStarted, "late collector did not arm");
		const auto generation = collector.ActiveGeneration();
		for (int index = 0; index < 5000; ++index)
			Check(collector.Record(EventKind::kDraw) == RecordResult::kFiltered, "collector admitted prefix events");
		Check(collector.ObserveResource({ .d3dObject = 1 }).observationId == 0,
			"collector admitted a prefix catalogue entry");
		Check(!collector.ActivatePostProcessingWindow(mainSourceTarget, 12, 77), "source target activated the destination window");
		Check(!collector.ActivatePostProcessingWindow(vrFramebufferTarget, 0, 77), "zero frame activated the window");
		Check(!collector.ActivatePostProcessingWindow(vrFramebufferTarget, kUnknownFrame, 0), "unknown frame activated the window");
		Check(!collector.ActivatePostProcessingWindow(vrFramebufferTarget, 12, 0), "missing publication activated the window");
		const auto refused = collector.GetCaptureWindow();
		Check(refused.phase == CaptureWindowPhase::kArmed && refused.activationTick == 0 &&
				  refused.activationBoundary.attemptCount == 4 && refused.activationBoundary.targetRejections == 1 &&
				  refused.activationBoundary.frameRejections == 2 && refused.activationBoundary.publicationRejections == 2 &&
				  refused.activationBoundary.lastTarget == vrFramebufferTarget && refused.activationBoundary.lastCpuFrame == 12 &&
				  refused.activationBoundary.lastPublicationGeneration == 0,
			"refused inputs were lost or changed activation eligibility");
		Check(collector.ActivatePostProcessingWindow(vrFramebufferTarget, 12, 77), "VR destination boundary did not activate");
		Check(!collector.ActivatePostProcessingWindow(mainSourceTarget, 0, 0), "bootstrap window reactivated");
		const auto accepted = collector.GetCaptureWindow();
		Check(accepted.activationBoundary.attemptCount == 5 && accepted.activationBoundary.lastCpuFrame == 12 &&
				  accepted.activationBoundary.lastPublicationGeneration == 77 && accepted.activationBoundary.targetRejections == 1,
			"post-activation calls rewrote armed boundary diagnostics");
		std::thread excluded([&] {
			Check(collector.Record(EventKind::kDraw) == RecordResult::kFiltered,
				"foreign thread populated bootstrap events");
			Check(collector.ObserveResource({ .d3dObject = 2 }).observationId == 0,
				"foreign thread populated bootstrap catalogue");
		});
		excluded.join();
		Check(collector.Record(EventKind::kRasterStateObserved) == RecordResult::kRecorded,
			"bootstrap owner could not retain its state");
		collector.CompleteWindowBootstrap(true);
		auto snapshot = collector.Stop();
		Check(snapshot && snapshot->events.size() == 1 && snapshot->resourceObservations.empty() &&
				  snapshot->statistics.droppedStopped == 0 && snapshot->statistics.droppedEventLimit == 0 &&
				  snapshot->window.phase == CaptureWindowPhase::kIncomplete &&
				  snapshot->window.activationBoundary.attemptCount == 5,
			"omitted prefix consumed capacity or was promoted to a complete window");
		Check(collector.Start(Config()) == StartResult::kStarted && collector.ActiveGeneration() != generation,
			"late window drain did not release single-owner admission");
		Check(collector.Stop().has_value(), "successor collector could not drain");

		config.maxActivationWait = std::chrono::nanoseconds(1);
		Check(collector.Start(config) == StartResult::kStarted, "deadline diagnostic capture did not arm");
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
		Check(!collector.ActivatePostProcessingWindow(vrFramebufferTarget, 12, 77), "expired armed window activated");
		const auto expired = collector.Stop();
		Check(expired && expired->window.failure == CaptureWindowFailure::kActivationTimeout &&
				  expired->window.activationBoundary.attemptCount == 0 && expired->events.empty(),
			"expired boundary invented an in-window attempt or admitted events");
	}

	void TestStageShaderEnrichmentDoesNotAllocateLookupNodes()
	{
		Collector collector;
		auto config = Config();
		config.maxStageShaderObservations = 2;
		config.maxBytes = Collector::RequiredStorageBytes(config);
		Check(collector.Start(config) == StartResult::kStarted, "stage lookup capture did not start");
		std::array<StageShaderObservationInput::EngineAlias, kMaximumEngineShaderAliasesPerStage> aliases{};
		for (std::size_t index = 0; index < aliases.size(); ++index)
			aliases[index] = { "Lighting", "Lighting", static_cast<std::uint32_t>(index) };
		StageShaderObservationInput input{
			.stage = ShaderStage::kVertex,
			.d3dObject = 0xCA00,
			.engineAliases = aliases.data(),
			.engineAliasCount = static_cast<std::uint32_t>(aliases.size()),
			.engineAliasTotalCount = static_cast<std::uint32_t>(aliases.size() + 1),
		};
		const auto first = collector.ObserveStageShader(input);
		Check(first.firstSeen && first.observationId != 0, "truncated stage shader was not observed");

		allocationCount = 0;
		countAllocations = true;
		void* probe = ::operator new(sizeof(std::uint64_t));
		::operator delete(probe);
		countAllocations = false;
		Check(allocationCount == 1, "allocation probe did not observe a lookup-sized allocation");
		allocationCount = 0;
		countAllocations = true;
		bool identitiesStable = true;
		for (std::uint32_t index = 0; index < 5000; ++index) {
			const auto repeated = collector.ObserveStageShader(input);
			identitiesStable = identitiesStable && repeated.observationId == first.observationId &&
			                   !repeated.firstSeen && repeated.pointerGeneration == first.pointerGeneration;
		}
		for (std::uint32_t index = 0; index < 5000; ++index) {
			aliases.back().descriptor = 100 + index;
			input.engineAliasTotalCount = 10 + index;
			const auto enriched = collector.ObserveStageShader(input);
			identitiesStable = identitiesStable && enriched.observationId == first.observationId &&
			                   !enriched.firstSeen && enriched.pointerGeneration == first.pointerGeneration;
		}
		countAllocations = false;
		Check(identitiesStable, "compatible stage evidence changed shader identity");
		Check(allocationCount == 0, "compatible stage observations allocated unbudgeted lookup nodes");

		input.wrapperDescriptor = 7;
		Check(collector.ObserveStageShader(input).observationId == first.observationId,
			"wrapper enrichment lost pointer ownership");
		input.wrapperDescriptor = 8;
		const auto reused = collector.ObserveStageShader(input);
		Check(reused.firstSeen && reused.observationId != first.observationId &&
				  reused.pointerGeneration == first.pointerGeneration + 1,
			"conflicting evidence did not create a new pointer generation");
		input.wrapperDescriptor = 9;
		Check(collector.ObserveStageShader(input).observationId == 0,
			"conflicting shader exceeded the fixed record bound");
		const auto snapshot = collector.Stop();
		Check(snapshot && snapshot->stageShaderObservations.size() == 2 &&
				  snapshot->stageShaderObservations.front().engineAliasesTruncated &&
				  snapshot->statistics.droppedStageShaderObservations == 1,
			"bounded enrichment lost truncation or admission evidence");
	}

	void TestDevBenchDefaultBudget()
	{
		auto config = CSX::RenderMap::DevBenchBounds::DefaultConfig();
		config.captureNumericId = 42;
		const auto maximumBytes = config.maxBytes;
		auto catalogueConfig = config;
		catalogueConfig.maxEvents = 0;
		Check(Collector::RequiredStorageBytes(catalogueConfig) < maximumBytes,
			"DevBench default catalogues consume the entire advertised byte budget");

		Collector collector;
		Check(collector.Start(config) == StartResult::kStarted,
			"DevBench default capture profile was rejected");
		Check(collector.Stop().has_value(), "DevBench default capture profile did not stop");
	}

	void TestDevBenchJsonBounds()
	{
		using nlohmann::json;
		for (const auto& bound : DevBenchBounds::kBounds) {
			const std::string field(bound.field);
			for (const auto value : { std::uint64_t{ 1 }, bound.maximum })
				Check(!DevBenchBounds::Validate(json{ { field, value } }), "valid JSON bound was rejected");
			for (const auto value : { std::uint64_t{ 0 }, bound.maximum + 1, std::uint64_t{ 0x100000001 }, (std::numeric_limits<std::uint64_t>::max)() }) {
				const auto error = DevBenchBounds::Validate(json{ { field, value } });
				Check(error && !error->invalidType && error->bound.field == field,
					"oversized JSON bound wrapped or lost its field identity");
			}
			for (const auto& value : { json(-1), json(1.5), json(true), json("1"), json(nullptr) }) {
				const auto error = DevBenchBounds::Validate(json{ { field, value } });
				Check(error && error->invalidType && error->bound.field == field,
					"non-unsigned JSON bound was admitted");
			}
		}
		Check(!DevBenchBounds::Validate(json::parse(R"({"maxFrames":1,"maxScopeDepth":32})")),
			"positive integers from the real JSON parser were rejected");
		Check(DevBenchBounds::Validate(json::parse(R"({"maxEvents":18446744073709551616})"))->invalidType,
			"integer beyond uint64 range was admitted as floating point");
		Check(!DevBenchBounds::Validate(json::object()), "omitted bounds did not retain defaults");
	}

	void TestDevBenchMinimumEventBudget()
	{
		auto config = DevBenchBounds::DefaultConfig();
		auto catalogueConfig = config;
		catalogueConfig.maxEvents = 0;
		const auto fixedBytes = Collector::RequiredStorageBytes(catalogueConfig);
		catalogueConfig.maxEvents = 1;
		const auto minimumBytes = Collector::RequiredStorageBytes(catalogueConfig);
		Collector collector;
		config.maxBytes = fixedBytes + 1;
		Check(collector.Start(config) == StartResult::kInvalidBounds,
			"catalogue threshold unexpectedly admitted a partial event slot");
		config.maxBytes = minimumBytes - 1;
		Check(collector.Start(config) == StartResult::kInvalidBounds, "partial event slot was admitted");
		config.maxBytes = minimumBytes;
		Check(collector.Start(config) == StartResult::kStarted, "exact one-event byte budget was rejected");
		Check(collector.Stop().has_value(), "minimum-budget capture did not stop");
		config.maxResourceObservations *= 2;
		Check(collector.Start(config) == StartResult::kInvalidBounds,
			"changed catalogue bounds reused the default byte minimum");
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

	void TestObservationChainsRetainCaptureGeneration()
	{
		Collector collector;
		Check(collector.Start(Config()) == StartResult::kStarted, "observation source capture did not start");
		const auto staleGeneration = collector.ActiveGeneration();
		auto first = collector.Stop();
		Check(first.has_value() && collector.Start(Config()) == StartResult::kStarted,
			"observation capture turnover failed");
		const auto generation = collector.ActiveGeneration();
		const auto verify = [&](std::uint64_t expectedGeneration, bool admitted) {
			Check((collector.ObserveShader({ .shader = 0xE100 }, expectedGeneration).observationId != 0) == admitted,
				"shader observation ignored its generation");
			Check((collector.ObserveStageShader({ .d3dObject = 0xE101 }, expectedGeneration).observationId != 0) == admitted,
				"stage shader observation ignored its generation");
			const auto resource = collector.ObserveResource({ .d3dObject = 0xE102 }, expectedGeneration);
			Check((resource.observationId != 0) == admitted, "resource observation ignored its generation");
			const auto view = collector.ObserveTargetView({ .d3dObject = 0xE103, .resourceObservationId = resource.observationId }, expectedGeneration);
			Check((view.observationId != 0) == admitted, "view observation ignored its generation");
			Check((collector.ObserveTargetBinding({ .renderTargetObservationIds = { view.observationId }, .renderTargetCount = 1 }, expectedGeneration).observationId != 0) == admitted,
				"target binding observation ignored its generation");
			const auto object = collector.ObserveSceneObject({ .reference = 0xE104 }, expectedGeneration);
			Check((object.observationId != 0) == admitted, "scene object observation ignored its generation");
			Check((collector.ObserveGeometry({ .geometry = 0xE105, .sceneObjectObservationId = object.observationId }, expectedGeneration).observationId != 0) == admitted,
				"geometry observation ignored its generation");
			Check((collector.ObserveMaterialState({ .material = 0xE106 }, expectedGeneration).observationId != 0) == admitted,
				"material observation ignored its generation");
		};
		verify(staleGeneration, false);
		collector.CountFiltered(3, staleGeneration);
		Check(!collector.IsGeometryShaderTypeSelected(0, staleGeneration), "stale geometry filter used successor configuration");
		verify(generation, true);
		collector.CountFiltered(2, generation);
		Check(collector.IsGeometryShaderTypeSelected(0, generation), "current geometry filter rejected its configuration");
		auto snapshot = collector.Stop();
		Check(snapshot && snapshot->shaderObservations.size() == 1 && snapshot->stageShaderObservations.size() == 1 &&
				  snapshot->resourceObservations.size() == 1 && snapshot->targetViewObservations.size() == 1 &&
				  snapshot->targetBindingObservations.size() == 1 && snapshot->sceneObjectObservations.size() == 1 &&
				  snapshot->geometryObservations.size() == 1 && snapshot->materialStateObservations.size() == 1 &&
				  snapshot->statistics.filtered == 2,
			"stale observation chain changed the successor catalogues or filtered count");
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
		TestLateWindowCollectorAdmission();
		TestStageShaderEnrichmentDoesNotAllocateLookupNodes();
		TestDevBenchDefaultBudget();
		TestDevBenchJsonBounds();
		TestDevBenchMinimumEventBudget();
		TestNestedScopes();
		TestCapacityLimits();
		TestFrameLimit();
		TestScopeOverflow();
		TestOutOfOrderScopeCleanup();
		TestTimeLimit();
		TestConcurrentRecording();
		TestObservationChainsRetainCaptureGeneration();
		TestStoppedGuardDoesNotLeak();
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
