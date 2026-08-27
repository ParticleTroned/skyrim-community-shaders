#pragma once

#include <cstdint>
#include <limits>

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <atomic>
#endif

namespace CSX::VRDepthCullingDiagnostics
{
	constexpr auto kNoFrame = std::numeric_limits<std::uint32_t>::max();

	enum class DrawCategory : std::uint8_t
	{
		Lighting,
		DistantTree,
		Grass
	};

	enum class ControlMode : std::uint8_t
	{
		Live,
		ForcedVisible
	};

	struct ClassifiedDraws
	{
		std::uint64_t occluded = 0;
		std::uint64_t visible = 0;
		std::uint64_t occludedLighting = 0;
		std::uint64_t visibleLighting = 0;
		std::uint64_t occludedDistantTree = 0;
		std::uint64_t visibleDistantTree = 0;
		std::uint64_t occludedGrass = 0;
		std::uint64_t visibleGrass = 0;
	};

	struct PipelineStatistics
	{
		std::uint64_t iaVertices = 0;
		std::uint64_t iaPrimitives = 0;
		std::uint64_t vsInvocations = 0;
		std::uint64_t gsInvocations = 0;
		std::uint64_t gsPrimitives = 0;
		std::uint64_t clipperInvocations = 0;
		std::uint64_t clipperPrimitives = 0;
		std::uint64_t psInvocations = 0;
		std::uint64_t hsInvocations = 0;
		std::uint64_t dsInvocations = 0;
		std::uint64_t csInvocations = 0;
	};

#define CSX_DEPTH_CULLING_SNAPSHOT_FIELDS(X)                               \
	X(std::uint32_t, firstFrame, kNoFrame)                                  \
	X(std::uint32_t, lastFrame, kNoFrame)                                   \
	X(std::uint32_t, lastReadyFrame, kNoFrame)                              \
	X(std::uint32_t, lastDepthCullingFrame, kNoFrame)                       \
	X(std::uint32_t, lastObjectCount, 0)                                    \
	X(std::uint32_t, lastVisibilityFrame, kNoFrame)                         \
	X(std::uint32_t, lastVisibilityObjectCount, 0)                          \
	X(std::uint32_t, lastOccludedObjects, 0)                                \
	X(std::uint64_t, framesObserved, 0)                                     \
	X(std::uint64_t, enabledFrames, 0)                                      \
	X(std::uint64_t, accumulationCalls, 0)                                  \
	X(std::uint64_t, previousResultsNeutralized, 0)                         \
	X(std::uint64_t, readyPassCalls, 0)                                     \
	X(std::uint64_t, readyFrames, 0)                                        \
	X(std::uint64_t, bindAttempts, 0)                                       \
	X(std::uint64_t, boundDraws, 0)                                         \
	X(std::uint64_t, boundLightingDraws, 0)                                 \
	X(std::uint64_t, boundDistantTreeDraws, 0)                              \
	X(std::uint64_t, boundGrassDraws, 0)                                    \
	X(std::uint64_t, currentFrameBoundDraws, 0)                             \
	X(std::uint64_t, lastCompletedFrameBoundDraws, 0)                       \
	X(std::uint64_t, contextUnavailable, 0)                                 \
	X(std::uint64_t, featureDisabled, 0)                                    \
	X(std::uint64_t, stateUnavailable, 0)                                   \
	X(std::uint64_t, notInWorld, 0)                                         \
	X(std::uint64_t, reflections, 0)                                        \
	X(std::uint64_t, resultNotReady, 0)                                     \
	X(std::uint64_t, geometryUnavailable, 0)                                \
	X(std::uint64_t, objectFrameMismatch, 0)                                \
	X(std::uint64_t, objectIndexUnavailable, 0)                             \
	X(std::uint64_t, objectIndexOutOfRange, 0)                              \
	X(std::uint64_t, resultBufferUnavailable, 0)                            \
	X(std::uint64_t, srvUnavailable, 0)                                     \
	X(std::uint64_t, forcedVisibleSrvUnavailable, 0)                        \
	X(std::uint64_t, readbackCopiesQueued, 0)                               \
	X(std::uint64_t, readbackCopiesCompleted, 0)                            \
	X(std::uint64_t, readbackCopiesDropped, 0)                              \
	X(std::uint64_t, readbackMapsNotReady, 0)                               \
	X(std::uint64_t, readbackErrors, 0)                                     \
	X(std::uint64_t, visibilityFramesSampled, 0)                            \
	X(std::uint64_t, visibilityObjectsSampled, 0)                           \
	X(std::uint64_t, occludedObjects, 0)                                    \
	X(std::uint64_t, visibleObjects, 0)                                     \
	X(std::uint64_t, occludedObjectsWithCoveredDraws, 0)                    \
	X(std::uint64_t, visibleObjectsWithCoveredDraws, 0)                     \
	X(std::uint64_t, occludedDraws, 0)                                      \
	X(std::uint64_t, visibleDraws, 0)                                       \
	X(std::uint64_t, occludedLightingDraws, 0)                              \
	X(std::uint64_t, visibleLightingDraws, 0)                               \
	X(std::uint64_t, occludedDistantTreeDraws, 0)                           \
	X(std::uint64_t, visibleDistantTreeDraws, 0)                            \
	X(std::uint64_t, occludedGrassDraws, 0)                                 \
	X(std::uint64_t, visibleGrassDraws, 0)                                  \
	X(std::uint64_t, pipelineQueriesBegun, 0)                               \
	X(std::uint64_t, pipelineQueriesEnded, 0)                               \
	X(std::uint64_t, pipelineQueriesCompleted, 0)                           \
	X(std::uint64_t, pipelineQueriesDropped, 0)                             \
	X(std::uint64_t, pipelineQueriesNotReady, 0)                            \
	X(std::uint64_t, pipelineQueryErrors, 0)                                \
	X(std::uint64_t, pipelineTimestampDisjoint, 0)                          \
	X(std::uint64_t, pipelineStatsSamples, 0)                               \
	X(std::uint64_t, pipelineCoveredLightingDraws, 0)                       \
	X(std::uint64_t, pipelineTimingSamples, 0)                              \
	X(std::uint64_t, pipelineRegionNanoseconds, 0)                          \
	X(std::uint64_t, coverageSpanTimingSamples, 0)                          \
	X(std::uint64_t, coverageSpanRegionNanoseconds, 0)                      \
	X(std::uint64_t, pipelineIAVertices, 0)                                 \
	X(std::uint64_t, pipelineIAPrimitives, 0)                               \
	X(std::uint64_t, pipelineVSInvocations, 0)                              \
	X(std::uint64_t, pipelineGSInvocations, 0)                              \
	X(std::uint64_t, pipelineGSPrimitives, 0)                               \
	X(std::uint64_t, pipelineClipperInvocations, 0)                         \
	X(std::uint64_t, pipelineClipperPrimitives, 0)                          \
	X(std::uint64_t, pipelinePSInvocations, 0)                              \
	X(std::uint64_t, pipelineHSInvocations, 0)                              \
	X(std::uint64_t, pipelineDSInvocations, 0)                              \
	X(std::uint64_t, pipelineCSInvocations, 0)

	struct Snapshot
	{
		bool collecting = false;
		ControlMode controlMode = ControlMode::Live;
		std::uint64_t collectionEpoch = 0;
#define CSX_DECLARE_SNAPSHOT_FIELD(type, name, initial) type name = initial;
		CSX_DEPTH_CULLING_SNAPSHOT_FIELDS(CSX_DECLARE_SNAPSHOT_FIELD)
#undef CSX_DECLARE_SNAPSHOT_FIELD
	};

#ifdef DEVBENCH_BRIDGE_ENABLED
	class Counters
	{
	public:
		bool IsCollecting() const { return collecting.load(std::memory_order_relaxed); }
		std::uint64_t CollectionEpoch() const { return collectionEpoch.load(std::memory_order_acquire); }
		ControlMode GetControlMode() const { return controlMode.load(std::memory_order_relaxed); }
		void SetControlMode(ControlMode a_mode) { controlMode.store(a_mode, std::memory_order_release); }

		void Start()
		{
			collecting.store(false, std::memory_order_release);
			collectionEpoch.fetch_add(1, std::memory_order_acq_rel);
			ResetValues();
			collecting.store(true, std::memory_order_release);
		}
		void Stop() { collecting.store(false, std::memory_order_release); }
		void Reset()
		{
			const auto resume = collecting.exchange(false, std::memory_order_acq_rel);
			collectionEpoch.fetch_add(1, std::memory_order_acq_rel);
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

		void RecordReady(bool a_ready, std::uint32_t a_frame, std::uint32_t a_depthFrame, std::uint32_t a_objectCount)
		{
			if (!IsCollecting())
				return;
			readyPassCalls.fetch_add(1, std::memory_order_relaxed);
			if (!a_ready)
				return;
			readyFrames.fetch_add(1, std::memory_order_relaxed);
			lastReadyFrame.store(a_frame, std::memory_order_relaxed);
			lastDepthCullingFrame.store(a_depthFrame, std::memory_order_relaxed);
			lastObjectCount.store(a_objectCount, std::memory_order_relaxed);
		}

		void RecordBindAttempt()
		{
			if (IsCollecting())
				bindAttempts.fetch_add(1, std::memory_order_relaxed);
		}
		void RecordBoundDraw(DrawCategory a_category = DrawCategory::Lighting)
		{
			if (!IsCollecting())
				return;
			boundDraws.fetch_add(1, std::memory_order_relaxed);
			currentFrameBoundDraws.fetch_add(1, std::memory_order_relaxed);
			switch (a_category) {
			case DrawCategory::Lighting:
				boundLightingDraws.fetch_add(1, std::memory_order_relaxed);
				break;
			case DrawCategory::DistantTree:
				boundDistantTreeDraws.fetch_add(1, std::memory_order_relaxed);
				break;
			case DrawCategory::Grass:
				boundGrassDraws.fetch_add(1, std::memory_order_relaxed);
				break;
			}
		}

#define CSX_SIMPLE_RECORD(method, field)              \
	void Record##method()                              \
	{                                                  \
		if (IsCollecting())                              \
			field.fetch_add(1, std::memory_order_relaxed); \
	}
		CSX_SIMPLE_RECORD(ContextUnavailable, contextUnavailable)
		CSX_SIMPLE_RECORD(FeatureDisabled, featureDisabled)
		CSX_SIMPLE_RECORD(StateUnavailable, stateUnavailable)
		CSX_SIMPLE_RECORD(NotInWorld, notInWorld)
		CSX_SIMPLE_RECORD(Reflections, reflections)
		CSX_SIMPLE_RECORD(ResultNotReady, resultNotReady)
		CSX_SIMPLE_RECORD(GeometryUnavailable, geometryUnavailable)
		CSX_SIMPLE_RECORD(ObjectFrameMismatch, objectFrameMismatch)
		CSX_SIMPLE_RECORD(ObjectIndexUnavailable, objectIndexUnavailable)
		CSX_SIMPLE_RECORD(ObjectIndexOutOfRange, objectIndexOutOfRange)
		CSX_SIMPLE_RECORD(ResultBufferUnavailable, resultBufferUnavailable)
		CSX_SIMPLE_RECORD(SrvUnavailable, srvUnavailable)
		CSX_SIMPLE_RECORD(ForcedVisibleSrvUnavailable, forcedVisibleSrvUnavailable)
		CSX_SIMPLE_RECORD(ReadbackQueued, readbackCopiesQueued)
		CSX_SIMPLE_RECORD(ReadbackCompleted, readbackCopiesCompleted)
		CSX_SIMPLE_RECORD(ReadbackDropped, readbackCopiesDropped)
		CSX_SIMPLE_RECORD(ReadbackNotReady, readbackMapsNotReady)
		CSX_SIMPLE_RECORD(ReadbackError, readbackErrors)
		CSX_SIMPLE_RECORD(PipelineQueryBegun, pipelineQueriesBegun)
		CSX_SIMPLE_RECORD(PipelineQueryEnded, pipelineQueriesEnded)
		CSX_SIMPLE_RECORD(PipelineQueryCompleted, pipelineQueriesCompleted)
		CSX_SIMPLE_RECORD(PipelineQueryDropped, pipelineQueriesDropped)
		CSX_SIMPLE_RECORD(PipelineQueryNotReady, pipelineQueriesNotReady)
		CSX_SIMPLE_RECORD(PipelineQueryError, pipelineQueryErrors)
		CSX_SIMPLE_RECORD(PipelineTimestampDisjoint, pipelineTimestampDisjoint)
#undef CSX_SIMPLE_RECORD

		void RecordPipelineTiming(std::uint64_t a_epoch, std::uint64_t a_nanoseconds)
		{
			if (!IsCollecting() || a_epoch != CollectionEpoch())
				return;
			pipelineTimingSamples.fetch_add(1, std::memory_order_relaxed);
			pipelineRegionNanoseconds.fetch_add(a_nanoseconds, std::memory_order_relaxed);
		}

		void RecordCoverageSpanTiming(std::uint64_t a_epoch, std::uint64_t a_nanoseconds)
		{
			if (!IsCollecting() || a_epoch != CollectionEpoch())
				return;
			coverageSpanTimingSamples.fetch_add(1, std::memory_order_relaxed);
			coverageSpanRegionNanoseconds.fetch_add(a_nanoseconds, std::memory_order_relaxed);
		}

		void RecordPipelineStatistics(
			std::uint64_t a_epoch,
			std::uint64_t a_coveredLightingDraws,
			const PipelineStatistics& a_statistics)
		{
			if (!IsCollecting() || a_epoch != CollectionEpoch())
				return;
			pipelineStatsSamples.fetch_add(1, std::memory_order_relaxed);
			pipelineCoveredLightingDraws.fetch_add(a_coveredLightingDraws, std::memory_order_relaxed);
			pipelineIAVertices.fetch_add(a_statistics.iaVertices, std::memory_order_relaxed);
			pipelineIAPrimitives.fetch_add(a_statistics.iaPrimitives, std::memory_order_relaxed);
			pipelineVSInvocations.fetch_add(a_statistics.vsInvocations, std::memory_order_relaxed);
			pipelineGSInvocations.fetch_add(a_statistics.gsInvocations, std::memory_order_relaxed);
			pipelineGSPrimitives.fetch_add(a_statistics.gsPrimitives, std::memory_order_relaxed);
			pipelineClipperInvocations.fetch_add(a_statistics.clipperInvocations, std::memory_order_relaxed);
			pipelineClipperPrimitives.fetch_add(a_statistics.clipperPrimitives, std::memory_order_relaxed);
			pipelinePSInvocations.fetch_add(a_statistics.psInvocations, std::memory_order_relaxed);
			pipelineHSInvocations.fetch_add(a_statistics.hsInvocations, std::memory_order_relaxed);
			pipelineDSInvocations.fetch_add(a_statistics.dsInvocations, std::memory_order_relaxed);
			pipelineCSInvocations.fetch_add(a_statistics.csInvocations, std::memory_order_relaxed);
		}

		void RecordVisibilitySample(
			std::uint64_t a_epoch,
			std::uint32_t a_frame,
			std::uint32_t a_objectCount,
			std::uint32_t a_occludedObjects,
			std::uint32_t a_visibleObjects,
			std::uint32_t a_occludedCoveredObjects,
			std::uint32_t a_visibleCoveredObjects,
			const ClassifiedDraws& a_draws)
		{
			if (!IsCollecting() || a_epoch != CollectionEpoch())
				return;
			lastVisibilityFrame.store(a_frame, std::memory_order_relaxed);
			lastVisibilityObjectCount.store(a_objectCount, std::memory_order_relaxed);
			lastOccludedObjects.store(a_occludedObjects, std::memory_order_relaxed);
			visibilityFramesSampled.fetch_add(1, std::memory_order_relaxed);
			visibilityObjectsSampled.fetch_add(a_objectCount, std::memory_order_relaxed);
			occludedObjects.fetch_add(a_occludedObjects, std::memory_order_relaxed);
			visibleObjects.fetch_add(a_visibleObjects, std::memory_order_relaxed);
			occludedObjectsWithCoveredDraws.fetch_add(a_occludedCoveredObjects, std::memory_order_relaxed);
			visibleObjectsWithCoveredDraws.fetch_add(a_visibleCoveredObjects, std::memory_order_relaxed);
			occludedDraws.fetch_add(a_draws.occluded, std::memory_order_relaxed);
			visibleDraws.fetch_add(a_draws.visible, std::memory_order_relaxed);
			occludedLightingDraws.fetch_add(a_draws.occludedLighting, std::memory_order_relaxed);
			visibleLightingDraws.fetch_add(a_draws.visibleLighting, std::memory_order_relaxed);
			occludedDistantTreeDraws.fetch_add(a_draws.occludedDistantTree, std::memory_order_relaxed);
			visibleDistantTreeDraws.fetch_add(a_draws.visibleDistantTree, std::memory_order_relaxed);
			occludedGrassDraws.fetch_add(a_draws.occludedGrass, std::memory_order_relaxed);
			visibleGrassDraws.fetch_add(a_draws.visibleGrass, std::memory_order_relaxed);
		}

		Snapshot Capture() const
		{
			Snapshot result{};
			result.collecting = collecting.load(std::memory_order_acquire);
			result.controlMode = GetControlMode();
			result.collectionEpoch = CollectionEpoch();
#define CSX_CAPTURE_FIELD(type, name, initial) result.name = name.load(std::memory_order_relaxed);
			CSX_DEPTH_CULLING_SNAPSHOT_FIELDS(CSX_CAPTURE_FIELD)
#undef CSX_CAPTURE_FIELD
			return result;
		}

	private:
		void ResetValues()
		{
#define CSX_RESET_FIELD(type, name, initial) name.store(initial, std::memory_order_relaxed);
			CSX_DEPTH_CULLING_SNAPSHOT_FIELDS(CSX_RESET_FIELD)
#undef CSX_RESET_FIELD
		}

		std::atomic_bool collecting{ false };
		std::atomic<ControlMode> controlMode{ ControlMode::Live };
		std::atomic_uint64_t collectionEpoch{ 0 };
#define CSX_DECLARE_ATOMIC(type, name, initial) std::atomic<type> name{ initial };
		CSX_DEPTH_CULLING_SNAPSHOT_FIELDS(CSX_DECLARE_ATOMIC)
#undef CSX_DECLARE_ATOMIC
	};
#else
	class Counters
	{
	public:
		constexpr bool IsCollecting() const { return false; }
		constexpr std::uint64_t CollectionEpoch() const { return 0; }
		constexpr ControlMode GetControlMode() const { return ControlMode::Live; }
		void SetControlMode(ControlMode) {}
		void Start() {}
		void Stop() {}
		void Reset() {}
		void BeginFrame(std::uint32_t, bool) {}
		void RecordAccumulation(bool) {}
		void RecordReady(bool, std::uint32_t, std::uint32_t, std::uint32_t) {}
		void RecordBindAttempt() {}
		void RecordBoundDraw(DrawCategory = DrawCategory::Lighting) {}
#define CSX_STUB_RECORD(name) void Record##name() {}
		CSX_STUB_RECORD(ContextUnavailable)
		CSX_STUB_RECORD(FeatureDisabled)
		CSX_STUB_RECORD(StateUnavailable)
		CSX_STUB_RECORD(NotInWorld)
		CSX_STUB_RECORD(Reflections)
		CSX_STUB_RECORD(ResultNotReady)
		CSX_STUB_RECORD(GeometryUnavailable)
		CSX_STUB_RECORD(ObjectFrameMismatch)
		CSX_STUB_RECORD(ObjectIndexUnavailable)
		CSX_STUB_RECORD(ObjectIndexOutOfRange)
		CSX_STUB_RECORD(ResultBufferUnavailable)
		CSX_STUB_RECORD(SrvUnavailable)
		CSX_STUB_RECORD(ForcedVisibleSrvUnavailable)
		CSX_STUB_RECORD(ReadbackQueued)
		CSX_STUB_RECORD(ReadbackCompleted)
		CSX_STUB_RECORD(ReadbackDropped)
		CSX_STUB_RECORD(ReadbackNotReady)
		CSX_STUB_RECORD(ReadbackError)
		CSX_STUB_RECORD(PipelineQueryBegun)
		CSX_STUB_RECORD(PipelineQueryEnded)
		CSX_STUB_RECORD(PipelineQueryCompleted)
		CSX_STUB_RECORD(PipelineQueryDropped)
		CSX_STUB_RECORD(PipelineQueryNotReady)
		CSX_STUB_RECORD(PipelineQueryError)
		CSX_STUB_RECORD(PipelineTimestampDisjoint)
#undef CSX_STUB_RECORD
		void RecordPipelineTiming(std::uint64_t, std::uint64_t) {}
		void RecordCoverageSpanTiming(std::uint64_t, std::uint64_t) {}
		void RecordPipelineStatistics(std::uint64_t, std::uint64_t, const PipelineStatistics&) {}
		void RecordVisibilitySample(std::uint64_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, const ClassifiedDraws&) {}
		Snapshot Capture() const { return {}; }
	};
#endif

#undef CSX_DEPTH_CULLING_SNAPSHOT_FIELDS
}
