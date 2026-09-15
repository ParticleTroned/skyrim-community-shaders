#pragma once

#include "PipelinePolicy.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

namespace NeuralRendering::Color
{
	/** Immutable membership of one private colour reconstruction batch. */
	struct MeasurementBatchKey
	{
		std::uint64_t id = 0, revision = 0, generation = 0;
		std::uint32_t frame = 0, sourceWorldFrame = 0, insertion = 0, expectedSlotMask = 0;
		bool atomicStereo = false;
		bool operator==(const MeasurementBatchKey&) const = default;

		[[nodiscard]] bool Valid() const noexcept
		{
			const auto route = ClassifyFeatureSlotMask(expectedSlotMask);
			const auto primary = expectedSlotMask & 0xFu;
			return id != 0 && revision != 0 && insertion < 2 &&
			       sourceWorldFrame != std::numeric_limits<std::uint32_t>::max() &&
			       route != FeatureSlotRoute::Unexpected &&
			       ((expectedSlotMask >> 4u) & ~primary) == 0 &&
			       atomicStereo == (primary == 3u || primary == 12u);
		}
	};

	template <class Sample>
	struct MeasurementBatch
	{
		MeasurementBatchKey key{};
		std::array<Sample, 8> samples{};
		std::uint32_t receivedSlotMask = 0;
		bool invalid = false;

		[[nodiscard]] bool Complete() const noexcept
		{
			return key.Valid() && !invalid && receivedSlotMask == key.expectedSlotMask;
		}
	};

	/** Bounded assembly keeps independently completed GPU readbacks together. */
	template <class Sample, std::size_t Capacity = 16, std::size_t Published = 4>
	class MeasurementBatchHistory
	{
		static_assert(Capacity > 0 && Published > 0 && Published <= Capacity);

	public:
		bool Record(const MeasurementBatchKey& key, std::uint32_t slot, const Sample& sample)
		{
			if (!key.id)
				return false;
			auto& batch = batches_[(key.id - 1u) % Capacity];
			if (batch.key.id > key.id)
				return false;
			if (batch.key.id != key.id) {
				if (batch.key.id && !batch.Complete())
					++evictedIncomplete_;
				batch = {};
				batch.key = key;
			}
			if (batch.invalid || !key.Valid() || batch.key != key || slot >= batch.samples.size() ||
				(key.expectedSlotMask & (1u << slot)) == 0 || (batch.receivedSlotMask & (1u << slot)) != 0) {
				batch.invalid = true;
				return false;
			}
			batch.samples[slot] = sample;
			batch.receivedSlotMask |= 1u << slot;
			return true;
		}

		/** Return complete batches by submission order, never latest-per-slot mixtures. */
		[[nodiscard]] std::array<MeasurementBatch<Sample>, Published> Latest() const
		{
			std::array<const MeasurementBatch<Sample>*, Capacity> complete{};
			std::size_t count = 0;
			for (const auto& batch : batches_)
				if (batch.Complete())
					complete[count++] = &batch;
			std::sort(complete.begin(), complete.begin() + count,
				[](const auto* a, const auto* b) { return a->key.id > b->key.id; });
			std::array<MeasurementBatch<Sample>, Published> result{};
			for (std::size_t i = 0; i < std::min(count, Published); ++i)
				result[i] = *complete[i];
			return result;
		}
		[[nodiscard]] std::uint64_t EvictedIncomplete() const noexcept { return evictedIncomplete_; }

	private:
		std::array<MeasurementBatch<Sample>, Capacity> batches_{};
		std::uint64_t evictedIncomplete_ = 0;
	};
}
