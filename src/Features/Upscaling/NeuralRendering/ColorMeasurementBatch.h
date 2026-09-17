#pragma once

#include "PipelinePolicy.h"
#include "Utils/CaptureRetention.h"
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

		bool Record(const MeasurementBatchKey& identity, std::uint32_t slot, const Sample& sample)
		{
			if (invalid || !identity.Valid() || key != identity || slot >= samples.size() ||
				(identity.expectedSlotMask & (1u << slot)) == 0 || (receivedSlotMask & (1u << slot)) != 0) {
				invalid = true;
				return false;
			}
			samples[slot] = sample;
			receivedSlotMask |= 1u << slot;
			return true;
		}

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
		using Retention = Util::CaptureRetention<MeasurementBatchKey, MeasurementBatch<Sample>>;
		using Lease = typename Retention::Lease;

		/** Pin the exact batch before rolling diagnostic history can replace it. */
		Lease Pin(const MeasurementBatchKey& key)
		{
			if (!key.Valid())
				return {};
			const auto& stored = batches_[(key.id - 1u) % Capacity];
			MeasurementBatch<Sample> initial{};
			initial.key = key;
			if (stored.key == key)
				initial = stored;
			else if (stored.key.id >= key.id)
				initial.invalid = true;
			return retained_.Pin(key, std::move(initial));
		}

		bool Record(const MeasurementBatchKey& key, std::uint32_t slot, const Sample& sample)
		{
			if (!key.id)
				return false;
			retained_.UpdateIf([&](const auto& identity) { return identity.id == key.id; },
				[&](auto& pinned) { pinned.Record(key, slot, sample); });
			auto& batch = batches_[(key.id - 1u) % Capacity];
			if (batch.key.id > key.id)
				return false;
			if (batch.key.id != key.id) {
				if (batch.key.id && !batch.Complete())
					++evictedIncomplete_;
				batch = {};
				batch.key = key;
			}
			return batch.Record(key, slot, sample);
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
		Retention retained_;
		std::uint64_t evictedIncomplete_ = 0;
	};
}
