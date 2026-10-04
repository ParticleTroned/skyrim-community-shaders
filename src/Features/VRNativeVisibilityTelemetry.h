#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "VRDepthCullingTelemetryPolicy.h"
#	include "VRDepthCullingTemporalPolicy.h"

#	include <algorithm>
#	include <atomic>
#	include <cstdint>
#	include <initializer_list>

namespace VRNativeVisibilityTelemetry
{
	struct Status
	{
		std::uint64_t batches = 0;
		std::uint64_t emptyBatches = 0;
		std::uint64_t unreadableBatches = 0;
		std::uint64_t testedObjects = 0;
		std::uint64_t occludedBeforeRecovery = 0;
		std::uint64_t visibleBeforeRecovery = 0;
		std::uint64_t occludedAfterRecovery = 0;
		std::uint64_t visibleAfterRecovery = 0;
	};

	struct Batch
	{
		const std::uint32_t* results = nullptr;
		std::uint32_t count = 0;
		std::uint32_t selector = 2;
	};

	/** Native CPU result observations share the existing telemetry reset and freeze boundary. */
	class Counters
	{
	public:
		void Record(std::uint32_t a_count, std::uint32_t a_beforeOccluded, std::uint32_t a_afterOccluded) noexcept
		{
			batches.fetch_add(1, std::memory_order_relaxed);
			testedObjects.fetch_add(a_count, std::memory_order_relaxed);
			occludedBefore.fetch_add(a_beforeOccluded, std::memory_order_relaxed);
			visibleBefore.fetch_add(a_count - a_beforeOccluded, std::memory_order_relaxed);
			occludedAfter.fetch_add(a_afterOccluded, std::memory_order_relaxed);
			visibleAfter.fetch_add(a_count - a_afterOccluded, std::memory_order_relaxed);
		}

		void RecordEmpty() noexcept { emptyBatches.fetch_add(1, std::memory_order_relaxed); }
		void RecordUnreadable() noexcept { unreadableBatches.fetch_add(1, std::memory_order_relaxed); }

		[[nodiscard]] Status Read() const noexcept
		{
			return { batches.load(std::memory_order_relaxed), emptyBatches.load(std::memory_order_relaxed),
				unreadableBatches.load(std::memory_order_relaxed), testedObjects.load(std::memory_order_relaxed),
				occludedBefore.load(std::memory_order_relaxed), visibleBefore.load(std::memory_order_relaxed),
				occludedAfter.load(std::memory_order_relaxed), visibleAfter.load(std::memory_order_relaxed) };
		}

		void Reset() noexcept
		{
			for (auto* counter : { &batches, &emptyBatches, &unreadableBatches, &testedObjects,
					 &occludedBefore, &visibleBefore, &occludedAfter, &visibleAfter })
				counter->store(0, std::memory_order_relaxed);
		}

	private:
		std::atomic_uint64_t batches{ 0 }, emptyBatches{ 0 }, unreadableBatches{ 0 }, testedObjects{ 0 };
		std::atomic_uint64_t occludedBefore{ 0 }, visibleBefore{ 0 }, occludedAfter{ 0 }, visibleAfter{ 0 };
	};

	/** The native caller retains this array until recovery returns; one writer owns both counts. */
	class Scope
	{
	public:
		template <class ReadBatch>
		Scope(Counters& a_counters, VRDepthCullingTelemetryPolicy::WriterGate& a_gate, ReadBatch&& a_readBatch) :
			writer(a_gate), counters(a_counters)
		{
			if (!writer)
				return;
			const auto candidate = a_readBatch();
			if (candidate.selector > 1 || candidate.count > VRDepthCullingTemporalPolicy::kMaximumObjects) {
				counters.RecordUnreadable();
				return;
			}
			if (candidate.count == 0) {
				counters.RecordEmpty();
				return;
			}
			if (!candidate.results) {
				counters.RecordUnreadable();
				return;
			}
			batch = candidate;
			beforeOccluded = static_cast<std::uint32_t>(std::count(batch.results, batch.results + batch.count, 0u));
		}
		~Scope()
		{
			if (batch.results)
				counters.Record(batch.count, beforeOccluded,
					static_cast<std::uint32_t>(std::count(batch.results, batch.results + batch.count, 0u)));
		}
		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;

	private:
		VRDepthCullingTelemetryPolicy::WriterScope writer;
		Counters& counters;
		Batch batch{};
		std::uint32_t beforeOccluded = 0;
	};
}

#endif
