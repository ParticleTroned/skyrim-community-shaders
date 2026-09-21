#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace VRShadowBatch::NativeLayout
{
	static_assert(sizeof(std::uintptr_t) == 8);

	template <class T>
	T Read(const void* a_base, std::size_t a_offset)
	{
		T value;
		std::memcpy(&value, static_cast<const std::byte*>(a_base) + a_offset, sizeof(value));
		return value;
	}

	struct TechniqueEntry
	{
		std::uint32_t technique;
		std::uint32_t group;
		std::uintptr_t next;
	};
	static_assert(sizeof(TechniqueEntry) == 0x10 && offsetof(TechniqueEntry, next) == 8);

	/** Read only the VR 1.4.15 batch layout; uncertainty must not release native owners. */
	inline bool BucketEmpty(const void* a_renderer, std::uint32_t a_technique, std::uint32_t a_bucket)
	{
		if (!a_renderer || a_bucket >= 5)
			return false;
		// The VR map has the standard parent and identity hash, not CommonLib's fixed/CRC map.
		const auto entries = Read<std::uintptr_t>(a_renderer, 0x48);
		const auto capacity = Read<std::uint32_t>(a_renderer, 0x2C);
		if (!entries)
			return capacity == 0;
		const auto sentinel = Read<std::uintptr_t>(a_renderer, 0x38);
		if (entries <= 0x10000 || entries % alignof(std::uintptr_t) || !std::has_single_bit(capacity) || !sentinel)
			return false;
		const auto bytes = static_cast<std::uintptr_t>(capacity) * sizeof(TechniqueEntry);
		if (entries > std::numeric_limits<std::uintptr_t>::max() - bytes ||
			(sentinel >= entries && sentinel - entries < bytes))
			return false;
		const auto head = entries + (a_technique & (capacity - 1)) * sizeof(TechniqueEntry);
		auto current = head;
		// Excessive or corrupt chains retain ownership until native reset instead of stalling.
		constexpr std::uint32_t maxChain = 1024;
		for (auto remaining = std::min(capacity, maxChain); remaining; --remaining) {
			const auto entry = Read<TechniqueEntry>(reinterpret_cast<const void*>(current), 0);
			if (!entry.next)
				return current == head;
			if (entry.technique == a_technique) {
				const auto groups = Read<std::uintptr_t>(a_renderer, 0x08);
				const auto count = Read<std::uint32_t>(a_renderer, 0x18);
				const auto offset = static_cast<std::uintptr_t>(entry.group) * 0x30 + a_bucket * sizeof(std::uintptr_t);
				if (entry.group >= count || groups <= 0x10000 || groups % alignof(std::uintptr_t) ||
					groups > std::numeric_limits<std::uintptr_t>::max() - offset - sizeof(std::uintptr_t))
					return false;
				return Read<std::uintptr_t>(reinterpret_cast<const void*>(groups), offset) == 0;
			}
			if (entry.next == sentinel)
				return true;
			if (entry.next < entries || entry.next - entries >= bytes || (entry.next - entries) % sizeof(TechniqueEntry))
				return false;
			current = entry.next;
		}
		return false;
	}
}
