#pragma once

#include "Utils/Finite.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

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

	/** Stable descending finite-key sort; bounded small-list work, otherwise four linear walks.
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
		struct Bucket
		{
			Pass* head{};
			Pass* tail{};
		};
		for (unsigned shift = 0; shift < 32; shift += 8) {
			std::array<Bucket, 256> buckets{};
			for (auto* pass = head; pass;) {
				auto* next = pass->passGroupNext;
				const float value = key(pass);
				// Canonicalize signed zero so equal depths retain native submission order.
				const auto bits = std::bit_cast<std::uint32_t>(value == 0 ? 0.0f : value);
				const auto ordered = bits ^ (bits & 0x80000000u ? 0xFFFFFFFFu : 0x80000000u);
				auto& bucket = buckets[(~ordered >> shift) & 0xFFu];
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
}
