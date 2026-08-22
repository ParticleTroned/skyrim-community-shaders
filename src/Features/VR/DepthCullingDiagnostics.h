#pragma once

#include <cstdint>
#include <limits>

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <atomic>
#endif

namespace CSX::VRDepthCullingDiagnostics
{
	constexpr auto kNoFrame = std::numeric_limits<std::uint32_t>::max();

	struct Snapshot
	{
		bool collecting = false;
		std::uint32_t firstFrame = kNoFrame;
		std::uint32_t lastFrame = kNoFrame;
		std::uint32_t lastReadyFrame = kNoFrame;
		std::uint32_t lastDepthCullingFrame = kNoFrame;
		std::uint32_t lastObjectCount = 0;
		std::uint64_t framesObserved = 0;
		std::uint64_t enabledFrames = 0;
		std::uint64_t accumulationCalls = 0;
		std::uint64_t previousResultsNeutralized = 0;
		std::uint64_t readyPassCalls = 0;
		std::uint64_t readyFrames = 0;
		std::uint64_t bindAttempts = 0;
		std::uint64_t boundDraws = 0;
		std::uint64_t currentFrameBoundDraws = 0;
		std::uint64_t lastCompletedFrameBoundDraws = 0;
		std::uint64_t contextUnavailable = 0;
		std::uint64_t featureDisabled = 0;
		std::uint64_t stateUnavailable = 0;
		std::uint64_t notInWorld = 0;
		std::uint64_t reflections = 0;
		std::uint64_t resultNotReady = 0;
		std::uint64_t geometryUnavailable = 0;
		std::uint64_t objectFrameMismatch = 0;
		std::uint64_t objectIndexUnavailable = 0;
		std::uint64_t objectIndexOutOfRange = 0;
		std::uint64_t resultBufferUnavailable = 0;
		std::uint64_t srvUnavailable = 0;
	};

	#ifdef DEVBENCH_BRIDGE_ENABLED
	class Counters
	{
	public:
		bool IsCollecting() const { return collecting.load(std::memory_order_relaxed); }

		void Start()
		{
			collecting.store(false, std::memory_order_release);
			ResetValues();
			collecting.store(true, std::memory_order_release);
		}

		void Stop() { collecting.store(false, std::memory_order_release); }

		void Reset()
		{
			const auto resume = collecting.exchange(false, std::memory_order_acq_rel);
			ResetValues();
			collecting.store(resume, std::memory_order_release);
		}

		void BeginFrame(std::uint32_t a_frame, bool a_enabled)
		{
			if (!IsCollecting())
				return;

			auto expected = kNoFrame;
			firstFrame.compare_exchange_strong(expected, a_frame, std::memory_order_relaxed);
			const auto previous = lastFrame.exchange(a_frame, std::memory_order_relaxed);
			if (previous != kNoFrame && previous != a_frame) {
				lastCompletedFrameBoundDraws.store(
					currentFrameBoundDraws.exchange(0, std::memory_order_relaxed),
					std::memory_order_relaxed);
			}
			framesObserved.fetch_add(1, std::memory_order_relaxed);
			if (a_enabled)
				enabledFrames.fetch_add(1, std::memory_order_relaxed);
		}

		void RecordAccumulation(bool a_neutralized)
		{
			if (!IsCollecting())
				return;
			accumulationCalls.fetch_add(1, std::memory_order_relaxed);
			if (a_neutralized)
				previousResultsNeutralized.fetch_add(1, std::memory_order_relaxed);
		}

		void RecordReady(
			bool a_ready,
			std::uint32_t a_frame,
			std::uint32_t a_depthCullingFrame,
			std::uint32_t a_objectCount)
		{
			if (!IsCollecting())
				return;
			readyPassCalls.fetch_add(1, std::memory_order_relaxed);
			if (!a_ready)
				return;
			readyFrames.fetch_add(1, std::memory_order_relaxed);
			lastReadyFrame.store(a_frame, std::memory_order_relaxed);
			lastDepthCullingFrame.store(a_depthCullingFrame, std::memory_order_relaxed);
			lastObjectCount.store(a_objectCount, std::memory_order_relaxed);
		}

		void RecordBindAttempt()
		{
			if (IsCollecting())
				bindAttempts.fetch_add(1, std::memory_order_relaxed);
		}

		void RecordBoundDraw()
		{
			if (!IsCollecting())
				return;
			boundDraws.fetch_add(1, std::memory_order_relaxed);
			currentFrameBoundDraws.fetch_add(1, std::memory_order_relaxed);
		}

		#define CSX_DEPTH_CULLING_DIAGNOSTIC_COUNTER(method, field) \
			void Record##method()                                     \
			{                                                       \
				if (IsCollecting())                                    \
					field.fetch_add(1, std::memory_order_relaxed);          \
			}

		CSX_DEPTH_CULLING_DIAGNOSTIC_COUNTER(ContextUnavailable, contextUnavailable)
		CSX_DEPTH_CULLING_DIAGNOSTIC_COUNTER(FeatureDisabled, featureDisabled)
		CSX_DEPTH_CULLING_DIAGNOSTIC_COUNTER(StateUnavailable, stateUnavailable)
		CSX_DEPTH_CULLING_DIAGNOSTIC_COUNTER(NotInWorld, notInWorld)
		CSX_DEPTH_CULLING_DIAGNOSTIC_COUNTER(Reflections, reflections)
		CSX_DEPTH_CULLING_DIAGNOSTIC_COUNTER(ResultNotReady, resultNotReady)
		CSX_DEPTH_CULLING_DIAGNOSTIC_COUNTER(GeometryUnavailable, geometryUnavailable)
		CSX_DEPTH_CULLING_DIAGNOSTIC_COUNTER(ObjectFrameMismatch, objectFrameMismatch)
		CSX_DEPTH_CULLING_DIAGNOSTIC_COUNTER(ObjectIndexUnavailable, objectIndexUnavailable)
		CSX_DEPTH_CULLING_DIAGNOSTIC_COUNTER(ObjectIndexOutOfRange, objectIndexOutOfRange)
		CSX_DEPTH_CULLING_DIAGNOSTIC_COUNTER(ResultBufferUnavailable, resultBufferUnavailable)
		CSX_DEPTH_CULLING_DIAGNOSTIC_COUNTER(SrvUnavailable, srvUnavailable)

		#undef CSX_DEPTH_CULLING_DIAGNOSTIC_COUNTER

		Snapshot Capture() const
		{
			return {
				.collecting = collecting.load(std::memory_order_acquire),
				.firstFrame = firstFrame.load(std::memory_order_relaxed),
				.lastFrame = lastFrame.load(std::memory_order_relaxed),
				.lastReadyFrame = lastReadyFrame.load(std::memory_order_relaxed),
				.lastDepthCullingFrame = lastDepthCullingFrame.load(std::memory_order_relaxed),
				.lastObjectCount = lastObjectCount.load(std::memory_order_relaxed),
				.framesObserved = framesObserved.load(std::memory_order_relaxed),
				.enabledFrames = enabledFrames.load(std::memory_order_relaxed),
				.accumulationCalls = accumulationCalls.load(std::memory_order_relaxed),
				.previousResultsNeutralized = previousResultsNeutralized.load(std::memory_order_relaxed),
				.readyPassCalls = readyPassCalls.load(std::memory_order_relaxed),
				.readyFrames = readyFrames.load(std::memory_order_relaxed),
				.bindAttempts = bindAttempts.load(std::memory_order_relaxed),
				.boundDraws = boundDraws.load(std::memory_order_relaxed),
				.currentFrameBoundDraws = currentFrameBoundDraws.load(std::memory_order_relaxed),
				.lastCompletedFrameBoundDraws = lastCompletedFrameBoundDraws.load(std::memory_order_relaxed),
				.contextUnavailable = contextUnavailable.load(std::memory_order_relaxed),
				.featureDisabled = featureDisabled.load(std::memory_order_relaxed),
				.stateUnavailable = stateUnavailable.load(std::memory_order_relaxed),
				.notInWorld = notInWorld.load(std::memory_order_relaxed),
				.reflections = reflections.load(std::memory_order_relaxed),
				.resultNotReady = resultNotReady.load(std::memory_order_relaxed),
				.geometryUnavailable = geometryUnavailable.load(std::memory_order_relaxed),
				.objectFrameMismatch = objectFrameMismatch.load(std::memory_order_relaxed),
				.objectIndexUnavailable = objectIndexUnavailable.load(std::memory_order_relaxed),
				.objectIndexOutOfRange = objectIndexOutOfRange.load(std::memory_order_relaxed),
				.resultBufferUnavailable = resultBufferUnavailable.load(std::memory_order_relaxed),
				.srvUnavailable = srvUnavailable.load(std::memory_order_relaxed),
			};
		}

	private:
		void ResetValues()
		{
			firstFrame.store(kNoFrame, std::memory_order_relaxed);
			lastFrame.store(kNoFrame, std::memory_order_relaxed);
			lastReadyFrame.store(kNoFrame, std::memory_order_relaxed);
			lastDepthCullingFrame.store(kNoFrame, std::memory_order_relaxed);
			lastObjectCount.store(0, std::memory_order_relaxed);
			#define CSX_RESET_DEPTH_CULLING_COUNTER(name) name.store(0, std::memory_order_relaxed)
			CSX_RESET_DEPTH_CULLING_COUNTER(framesObserved);
			CSX_RESET_DEPTH_CULLING_COUNTER(enabledFrames);
			CSX_RESET_DEPTH_CULLING_COUNTER(accumulationCalls);
			CSX_RESET_DEPTH_CULLING_COUNTER(previousResultsNeutralized);
			CSX_RESET_DEPTH_CULLING_COUNTER(readyPassCalls);
			CSX_RESET_DEPTH_CULLING_COUNTER(readyFrames);
			CSX_RESET_DEPTH_CULLING_COUNTER(bindAttempts);
			CSX_RESET_DEPTH_CULLING_COUNTER(boundDraws);
			CSX_RESET_DEPTH_CULLING_COUNTER(currentFrameBoundDraws);
			CSX_RESET_DEPTH_CULLING_COUNTER(lastCompletedFrameBoundDraws);
			CSX_RESET_DEPTH_CULLING_COUNTER(contextUnavailable);
			CSX_RESET_DEPTH_CULLING_COUNTER(featureDisabled);
			CSX_RESET_DEPTH_CULLING_COUNTER(stateUnavailable);
			CSX_RESET_DEPTH_CULLING_COUNTER(notInWorld);
			CSX_RESET_DEPTH_CULLING_COUNTER(reflections);
			CSX_RESET_DEPTH_CULLING_COUNTER(resultNotReady);
			CSX_RESET_DEPTH_CULLING_COUNTER(geometryUnavailable);
			CSX_RESET_DEPTH_CULLING_COUNTER(objectFrameMismatch);
			CSX_RESET_DEPTH_CULLING_COUNTER(objectIndexUnavailable);
			CSX_RESET_DEPTH_CULLING_COUNTER(objectIndexOutOfRange);
			CSX_RESET_DEPTH_CULLING_COUNTER(resultBufferUnavailable);
			CSX_RESET_DEPTH_CULLING_COUNTER(srvUnavailable);
			#undef CSX_RESET_DEPTH_CULLING_COUNTER
		}

		std::atomic_bool collecting{ false };
		std::atomic_uint32_t firstFrame{ kNoFrame };
		std::atomic_uint32_t lastFrame{ kNoFrame };
		std::atomic_uint32_t lastReadyFrame{ kNoFrame };
		std::atomic_uint32_t lastDepthCullingFrame{ kNoFrame };
		std::atomic_uint32_t lastObjectCount{ 0 };
		std::atomic_uint64_t framesObserved{ 0 };
		std::atomic_uint64_t enabledFrames{ 0 };
		std::atomic_uint64_t accumulationCalls{ 0 };
		std::atomic_uint64_t previousResultsNeutralized{ 0 };
		std::atomic_uint64_t readyPassCalls{ 0 };
		std::atomic_uint64_t readyFrames{ 0 };
		std::atomic_uint64_t bindAttempts{ 0 };
		std::atomic_uint64_t boundDraws{ 0 };
		std::atomic_uint64_t currentFrameBoundDraws{ 0 };
		std::atomic_uint64_t lastCompletedFrameBoundDraws{ 0 };
		std::atomic_uint64_t contextUnavailable{ 0 };
		std::atomic_uint64_t featureDisabled{ 0 };
		std::atomic_uint64_t stateUnavailable{ 0 };
		std::atomic_uint64_t notInWorld{ 0 };
		std::atomic_uint64_t reflections{ 0 };
		std::atomic_uint64_t resultNotReady{ 0 };
		std::atomic_uint64_t geometryUnavailable{ 0 };
		std::atomic_uint64_t objectFrameMismatch{ 0 };
		std::atomic_uint64_t objectIndexUnavailable{ 0 };
		std::atomic_uint64_t objectIndexOutOfRange{ 0 };
		std::atomic_uint64_t resultBufferUnavailable{ 0 };
		std::atomic_uint64_t srvUnavailable{ 0 };
	};
	#else
	class Counters
	{
	public:
		constexpr bool IsCollecting() const { return false; }
		void Start() {}
		void Stop() {}
		void Reset() {}
		void BeginFrame(std::uint32_t, bool) {}
		void RecordAccumulation(bool) {}
		void RecordReady(bool, std::uint32_t, std::uint32_t, std::uint32_t) {}
		void RecordBindAttempt() {}
		void RecordBoundDraw() {}
		#define CSX_DEPTH_CULLING_DIAGNOSTIC_STUB(name) void Record##name() {}
		CSX_DEPTH_CULLING_DIAGNOSTIC_STUB(ContextUnavailable)
		CSX_DEPTH_CULLING_DIAGNOSTIC_STUB(FeatureDisabled)
		CSX_DEPTH_CULLING_DIAGNOSTIC_STUB(StateUnavailable)
		CSX_DEPTH_CULLING_DIAGNOSTIC_STUB(NotInWorld)
		CSX_DEPTH_CULLING_DIAGNOSTIC_STUB(Reflections)
		CSX_DEPTH_CULLING_DIAGNOSTIC_STUB(ResultNotReady)
		CSX_DEPTH_CULLING_DIAGNOSTIC_STUB(GeometryUnavailable)
		CSX_DEPTH_CULLING_DIAGNOSTIC_STUB(ObjectFrameMismatch)
		CSX_DEPTH_CULLING_DIAGNOSTIC_STUB(ObjectIndexUnavailable)
		CSX_DEPTH_CULLING_DIAGNOSTIC_STUB(ObjectIndexOutOfRange)
		CSX_DEPTH_CULLING_DIAGNOSTIC_STUB(ResultBufferUnavailable)
		CSX_DEPTH_CULLING_DIAGNOSTIC_STUB(SrvUnavailable)
		#undef CSX_DEPTH_CULLING_DIAGNOSTIC_STUB
		Snapshot Capture() const { return {}; }
	};
	#endif
}
