#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <atomic>
#	include <cstdint>
#	include <mutex>
#	include <optional>

namespace GrassTelemetry
{
	struct Work
	{
		uint64_t dispatches = 0, candidateInstances = 0, candidateEyeInstances = 0;
		uint64_t indirectDraws = 0, uploadedRecordBytes = 0;
	};

	/** @brief Retained resources at publication and work from the last completed grass frame. */
	struct Snapshot
	{
		uint64_t frame = 0, publishedSteadyMilliseconds = 0;
		bool enabled = false, hiZRequested = false, meshLODRequested = false, cullingPrepared = false;
		uint64_t identities = 0, residentSources = 0, residentGroups = 0, residentInstances = 0;
		uint64_t buckets = 0, dormantBuckets = 0, lodModels = 0, lodVariants = 0;
		uint64_t cpuSnapshotPayloadBytes = 0, activeRecordBytes = 0, dormantRecordBytes = 0;
		uint64_t cullingBufferBytes = 0, scratchBufferBytes = 0, diagnosticBufferBytes = 0;
		uint64_t lodReferencedBufferBytes = 0;
		uint32_t hiZWidth = 0, hiZHeight = 0, hiZMips = 0;
		uint32_t viewportCount = 0;
		float viewportWidth = 0, viewportHeight = 0;
		Work work;
	};

	/** @brief Coalesce requests and publish value copies without exposing render-owned containers. */
	class Channel
	{
	public:
		void Request() { requested.store(true, std::memory_order_relaxed); }
		bool Requested() const { return requested.load(std::memory_order_relaxed); }
		/** @brief Render-thread admission; at most one publication per second. */
		bool BeginSample(uint64_t steadyMilliseconds)
		{
			if (steadyMilliseconds < nextSample || !requested.exchange(false, std::memory_order_relaxed))
				return false;
			nextSample = steadyMilliseconds + 1000;
			return true;
		}
		void Publish(const Snapshot& value)
		{
			std::scoped_lock lock(mutex);
			snapshot = value;
		}
		std::optional<Snapshot> Read() const
		{
			std::scoped_lock lock(mutex);
			return snapshot;
		}

	private:
		std::atomic_bool requested{ false };
		uint64_t nextSample = 0;
		mutable std::mutex mutex;
		std::optional<Snapshot> snapshot;
	};
}
#endif
