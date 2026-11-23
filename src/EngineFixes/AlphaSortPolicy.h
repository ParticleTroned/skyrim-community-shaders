#pragma once

#include "Utils/Finite.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace AlphaSort
{
	struct Position
	{
		float x, y, z;
	};

	inline bool IsFinite(Position position) noexcept
	{
		return Util::IsFinite(position.x) && Util::IsFinite(position.y) && Util::IsFinite(position.z);
	}

	/** Rotation-independent, finite key; signed squaring preserves nearest-bound ordering. */
	inline float DistanceKey(Position centre, float radius, Position origin, float closest) noexcept
	{
		if (!IsFinite(centre) || !IsFinite(origin) || !Util::IsFinite(radius) || radius < 0 || !Util::IsFinite(closest))
			return 0;
		const double x = double(centre.x) - origin.x;
		const double y = double(centre.y) - origin.y;
		const double z = double(centre.z) - origin.z;
		double key = x * x + y * y + z * z;
		if (closest != 0) {
			const double distance = std::sqrt(key) - double(closest) * radius;
			key = std::copysign(distance * distance, distance);
		}
		// Keep every key above the native empty-queue sentinel, including extreme bounds.
		constexpr double limit = std::numeric_limits<float>::max() / 4.0;
		return static_cast<float>(std::clamp(key, -limit, limit));
	}

	namespace detail
	{
		inline constexpr std::size_t kMaximumCachedPasses = 65536;

		inline std::uint32_t DescendingKey(float value) noexcept
		{
			// Equal signed zeros must retain their native submission order.
			const auto bits = std::bit_cast<std::uint32_t>(value == 0 ? 0.0f : value);
			return ~(bits ^ (bits & 0x80000000u ? 0xFFFFFFFFu : 0x80000000u));
		}

		template <class Pass, class Key>
		void RadixSort(Pass*& head, Key key) noexcept
		{
			struct Bucket
			{
				Pass* head{};
				Pass* tail{};
			};
			for (unsigned shift = 0; shift < 32; shift += 8) {
				std::array<Bucket, 256> buckets{};
				for (auto* pass = head; pass;) {
					auto* next = pass->passGroupNext;
					auto& bucket = buckets[(key(pass) >> shift) & 0xFFu];
					if (bucket.tail)
						bucket.tail->passGroupNext = pass;
					else
						bucket.head = pass;
					bucket.tail = pass;
					pass = next;
				}
				Pass** link = &head;
				for (const auto& bucket : buckets) {
					if (!bucket.head)
						continue;
					*link = bucket.head;
					link = &bucket.tail->passGroupNext;
				}
				*link = nullptr;
			}
		}

		template <class Pass>
		struct Scratch
		{
			struct Entry
			{
				Entry* passGroupNext;
				Pass* pass;
				std::uint32_t key;
			};
			std::unique_ptr<Entry[]> entries;
			std::size_t capacity = 0;
		};

		template <class Pass>
		Scratch<Pass>& GetScratch() noexcept
		{
			static thread_local Scratch<Pass> scratch;
			return scratch;
		}

		template <class Pass, class Key>
		bool SortCached(Pass*& head, Key key) noexcept
		{
			std::size_t count = 0;
			auto* remaining = head;
			while (remaining && count < kMaximumCachedPasses) {
				++count;
				remaining = remaining->passGroupNext;
			}
			if (remaining)
				return false;
			// Bound retained scratch; oversized queues keep the allocation-free sort.
			auto& scratch = GetScratch<Pass>();
			using Entry = typename Scratch<Pass>::Entry;
			if (!scratch.entries || scratch.capacity < count) {
				const auto capacity = std::bit_ceil(count);
				std::unique_ptr<Entry[]> replacement(new (std::nothrow) Entry[capacity]);
				if (!replacement)
					return false;
				scratch.entries = std::move(replacement);
				scratch.capacity = capacity;
			}
			const auto capacity = scratch.capacity;
			// A nested key callback must never borrow the enclosing sort's live entries.
			auto entries = std::move(scratch.entries);
			auto* pass = head;
			for (std::size_t i = 0; i < count; ++i, pass = pass->passGroupNext)
				entries[i] = { i + 1 < count ? &entries[i + 1] : nullptr, pass, DescendingKey(key(pass)) };
			auto* ordered = entries.get();
			RadixSort(ordered, [](const Entry* entry) { return entry->key; });
			Pass** link = &head;
			for (auto* entry = ordered; entry; entry = entry->passGroupNext) {
				*link = entry->pass;
				link = &entry->pass->passGroupNext;
			}
			*link = nullptr;
			if (!scratch.entries || capacity >= scratch.capacity) {
				scratch.entries = std::move(entries);
				scratch.capacity = capacity;
			}
			return true;
		}
	}

	/** Stable descending finite-key sort; bounded small-list work, otherwise linear radix sorting.
	 * The caller owns an acyclic list and keeps keys unchanged throughout sorting.
	 */
	template <class Pass, class Key>
	void Sort(Pass*& head, Key key) noexcept
	{
		if (!head || !head->passGroupNext)
			return;
		// Small groups should not pay for four 256-bucket scans. Larger lists remain unbounded.
		constexpr std::size_t smallCapacity = 32;
		struct Entry
		{
			Pass* pass;
			float depth;
		};
		std::array<Entry, smallCapacity> entries;
		std::size_t count = 0;
		auto* remaining = head;
		while (remaining && count < entries.size()) {
			entries[count++].pass = remaining;
			remaining = remaining->passGroupNext;
		}
		if (!remaining) {
			for (std::size_t i = 0; i < count; ++i) {
				const Entry entry{ entries[i].pass, key(entries[i].pass) };
				auto position = i;
				while (position && entry.depth > entries[position - 1].depth) {
					entries[position] = entries[position - 1];
					--position;
				}
				entries[position] = entry;
			}
			head = entries[0].pass;
			for (std::size_t i = 1; i < count; ++i)
				entries[i - 1].pass->passGroupNext = entries[i].pass;
			entries[count - 1].pass->passGroupNext = nullptr;
			return;
		}
		if (!detail::SortCached(head, key))
			detail::RadixSort(head, [&](Pass* pass) { return detail::DescendingKey(key(pass)); });
	}
}
