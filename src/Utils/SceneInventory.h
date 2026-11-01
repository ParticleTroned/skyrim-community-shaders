#pragma once

#include <cstddef>
#include <cstdint>

namespace Util
{
	/** Validate used NiTArray slots before retaining children; callers own scene mutation. */
	template <class Children, class ValidPointer, class Visitor>
	bool VisitSceneInventoryChildren(
		const Children& a_children, std::size_t a_available,
		ValidPointer&& a_valid, Visitor&& a_visit)
	{
		const auto capacity = a_children.capacity();
		const auto slots = a_children.free_idx();
		const auto count = a_children.size();
		const auto* data = a_children.begin();
		if (slots > capacity || count > slots || count > a_available ||
			(capacity && !a_valid(data)))
			return false;
		std::size_t observed = 0;
		// end() includes unused capacity; free_idx() includes active slots and holes.
		for (std::uint32_t index = 0; index < slots; ++index) {
			auto* child = data[index].get();
			if (!child)
				continue;
			if (!a_valid(child) || observed >= count)
				return false;
			++observed;
			a_visit(child);
		}
		return observed == count;
	}
}
