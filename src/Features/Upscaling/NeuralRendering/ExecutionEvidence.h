#pragma once

#include "../DLSSViewportCrop.h"
#include "ComputeSubrect.h"
#include "PipelinePolicy.h"
#include "Utils/PassTimingCapture.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <utility>

namespace NeuralRendering
{
	struct CharacterPreparationEvidence;
	inline constexpr std::size_t kMaximumExecutionRegions = 4;
	inline constexpr std::uint32_t kExecutionTimestampQueriesPerContext = 2u + 2u * static_cast<std::uint32_t>(kMaximumExecutionRegions);
	[[nodiscard]] constexpr std::uint32_t ExecutionEvaluationQuery(std::uint32_t context, std::uint32_t region) noexcept
	{
		return context * kExecutionTimestampQueriesPerContext + 2u + region * 2u;
	}
	/** Tightly packed logical texture size; excludes driver alignment and residency. */
	[[nodiscard]] std::optional<std::uint64_t> LogicalTextureBytes(std::uint32_t format, std::uint32_t width, std::uint32_t height) noexcept;

	/** Caller-owned facts stay unavailable until supplied by the admitted route. */
	struct ExecutionContext
	{
		std::optional<RenderingMode> renderingMode;
		std::optional<bool> fovOnly;
		std::optional<std::uint64_t> captureEpoch;
		std::optional<std::uint64_t> configurationEpoch;
		// Captured records outlive the caller; use process-lifetime literals only.
		std::string_view sourceContext;
		std::uint64_t sourceTransactionId = 0;
		std::optional<UpscalingDLSS::ViewportCrop> dlssViewportCrop;
		std::optional<std::array<float, 2>> jitterPixels;
		std::optional<std::array<std::uint32_t, 2>> sourceColorOrigin, sourceGuideOrigin;
		Util::PassTimingHandle inputPreparation;
		bool operator==(const ExecutionContext&) const = default;
	};

	enum class ExecutionTimingState : std::uint32_t
	{
		NotRequested,
		Pending,
		Complete,
		Failed,
		Unavailable
	};

	struct ExecutionTiming
	{
		ExecutionTimingState state = ExecutionTimingState::NotRequested;
		std::optional<std::uint64_t> microseconds;
	};

	[[nodiscard]] inline ExecutionTiming ResolveExecutionGpuTiming(std::uint64_t begin, std::uint64_t end, std::uint64_t frequency) noexcept
	{
		if (!frequency || end < begin)
			return { ExecutionTimingState::Failed, std::nullopt };
		const auto microseconds = static_cast<long double>(end - begin) * 1000000.0L / static_cast<long double>(frequency);
		if (microseconds >= static_cast<long double>(std::numeric_limits<std::uint64_t>::max()))
			return { ExecutionTimingState::Failed, std::nullopt };
		return { ExecutionTimingState::Complete, static_cast<std::uint64_t>(microseconds) };
	}

	enum ExecutionResetReason : std::uint32_t
	{
		ResetNone = 0,
		ResetCaller = 1u << 0,
		ResetHistoryInvalid = 1u << 1,
		ResetHistoryKeyChanged = 1u << 2,
		ResetEvaluationDiscontinuity = 1u << 3,
		ResetSourceDiscontinuity = 1u << 4,
		ResetSynchronized = 1u << 5,
		ResetClusterPeer = 1u << 6
	};

	enum ExecutionRebuildReason : std::uint32_t
	{
		RebuildNone = 0,
		RebuildUnallocated = 1u << 0,
		RebuildResourceContractChanged = 1u << 1
	};

	struct ExecutionTexture
	{
		UpscalingDLSS::Extent extent{};
		std::uint32_t format = 0;
		ComputeSubrect work{};
		std::optional<std::uint64_t> allocationLogicalBytes;
		std::optional<std::uint64_t> workLogicalBytes;
	};

	struct ExecutionRegionDescriptor
	{
		std::uint32_t physicalSlot = 0, logicalSlot = 0, eye = 0, region = 0;
		std::uint64_t regionIdentity = 0, clusterIdentity = 0;
		ExecutionContext context{};
		std::shared_ptr<const CharacterPreparationEvidence> characterEvidence;
		ExecutionTexture color{}, depth{}, motion{}, output{}, controlMask{};
		UpscalingDLSS::ViewportCrop viewportCrop{};
		float motionVectorScaleX = 0, motionVectorScaleY = 0;
		std::uint32_t depthSourceFormat = 0, depthViewFormat = 0;
		bool characterVisualIsolation = false, featureUpscaling = false;
	};

	/** Frozen only after resource/crop validation; capacity is never reported as work. */
	struct ExecutionDescriptor
	{
		std::uint64_t submissionId = 0, generation = 0, colorRevision = 0, inputEpoch = 0;
		std::uint32_t frame = 0, sourceWorldFrame = 0;
		FeatureSlotRoute route = FeatureSlotRoute::Unexpected;
		InsertionPoint insertion = kDefaultInsertionPoint;
		ExecutionContext context{};
		std::uint32_t logicalEyeCount = 0, regionCount = 0, plannedPhysicalSlotMask = 0;
		bool colorProcessing = false, transportBypass = false;
		std::array<ExecutionRegionDescriptor, kMaximumExecutionRegions> regions{};
	};

	struct RuntimeExecutionEvidence
	{
		bool createAttempted = false, createSucceeded = false;
		bool evaluateAttempted = false, evaluateSucceeded = false;
		const char* createReason = nullptr;
		std::optional<std::uint32_t> createResult, evaluateResult;
		std::optional<std::uint64_t> createCpuMicroseconds, evaluateCpuMicroseconds;
	};

	struct ExecutionRegionOutcome
	{
		RuntimeExecutionEvidence runtime{};
		ExecutionTiming evaluationGpu{};
		std::uint32_t resetReasons = ResetNone, rebuildReasons = RebuildNone;
		bool effectiveReset = false, resourcesReady = false, outputCopyEnqueued = false, privateOutputCommitted = false;
		// Texture payloads only; excludes driver heaps, shaders, CBs and diagnostics.
		std::uint64_t newlyAllocatedLogicalBytes = 0, copiedLogicalBytes = 0;
		std::optional<std::uint64_t> colorRetainedLogicalBytes;
		bool allocationBytesKnown = true, copyBytesKnown = true;
		Util::PassTimingHandle colorPreparePass, colorReconstructPass, depthGuidePass;
		Util::PassTimingHandle colorCopyPass, motionCopyPass, controlCopyPass, outputCopyPass;
	};

	/** One existing CPU fence wait; operation points to a process-lifetime literal. */
	struct ExecutionCpuWaitSample
	{
		const char* operation = nullptr;
		std::uint64_t microseconds = 0;
		std::uint32_t result = 0, error = 0, timeoutMilliseconds = 0;
	};

	struct ExecutionSnapshot
	{
		std::array<ExecutionRegionOutcome, kMaximumExecutionRegions> regions{};
		ExecutionTiming batchGpu{};
		std::uint32_t attemptedPhysicalSlotMask = 0, succeededPhysicalSlotMask = 0, committedPhysicalSlotMask = 0;
		bool finished = false, succeeded = false;
		bool evidenceFailed = false;
		std::uint32_t failureStage = 0;
		std::optional<std::uint64_t> preparationCpuMicroseconds, commitCpuMicroseconds;
		std::optional<std::uint64_t> resourceRetirementCpuMicroseconds, commandBeginCpuMicroseconds;
		std::optional<std::uint64_t> cpuWaitMicroseconds;
		std::uint32_t cpuWaitCalls = 0;
		std::array<ExecutionCpuWaitSample, 16> cpuWaitSamples{};
		std::uint32_t cpuWaitSampleCount = 0, cpuWaitSamplesDropped = 0;
		Util::PassTimingHandle wholePass;
	};

	/** Shared capture ownership retains delayed results without retaining GPU resources. */
	class ExecutionEvidence
	{
	public:
		explicit ExecutionEvidence(ExecutionDescriptor descriptor) : descriptor_(std::move(descriptor)) {}
		[[nodiscard]] const ExecutionDescriptor& Descriptor() const noexcept { return descriptor_; }
		[[nodiscard]] ExecutionSnapshot Snapshot() const noexcept
		{
			try {
				std::scoped_lock lock(mutex_);
				auto result = snapshot_;
				result.evidenceFailed |= updateFailed_.load(std::memory_order_relaxed);
				return result;
			} catch (...) {
				ExecutionSnapshot failed{};
				failed.evidenceFailed = true;
				return failed;
			}
		}
		template <class Callback>
		void Update(Callback&& callback) noexcept
		{
			try {
				std::scoped_lock lock(mutex_);
				std::forward<Callback>(callback)(snapshot_);
			} catch (...) {
				updateFailed_.store(true, std::memory_order_relaxed);
			}
		}
		void FailPendingTimings() noexcept
		{
			Update([](auto& snapshot) {
				const auto fail = [](ExecutionTiming& timing) {
					if (timing.state == ExecutionTimingState::Pending)
						timing = { ExecutionTimingState::Failed, std::nullopt };
				};
				fail(snapshot.batchGpu);
				for (auto& region : snapshot.regions)
					fail(region.evaluationGpu);
			});
		}
		void RecordCpuWait(ExecutionCpuWaitSample sample) noexcept
		{
			Update([&](auto& snapshot) {
				++snapshot.cpuWaitCalls;
				snapshot.cpuWaitMicroseconds = snapshot.cpuWaitMicroseconds.value_or(0) + sample.microseconds;
				if (snapshot.cpuWaitSampleCount < snapshot.cpuWaitSamples.size())
					snapshot.cpuWaitSamples[snapshot.cpuWaitSampleCount++] = sample;
				else
					++snapshot.cpuWaitSamplesDropped;
			});
		}

	private:
		const ExecutionDescriptor descriptor_;
		mutable std::mutex mutex_;
		std::atomic_bool updateFailed_{ false };
		ExecutionSnapshot snapshot_{};
	};

	/** Process-lifetime identity does not restart when graphics ownership is rebuilt. */
	[[nodiscard]] inline std::uint64_t NextExecutionSubmissionId() noexcept
	{
		static std::atomic<std::uint64_t> next{ 1 };
		auto current = next.load(std::memory_order_relaxed);
		while (current != std::numeric_limits<std::uint64_t>::max()) {
			if (next.compare_exchange_weak(current, current + 1, std::memory_order_relaxed))
				return current;
		}
		return 0;
	}
}
