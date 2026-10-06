#pragma once

#include <array>
#include <cstddef>

namespace NeuralRendering
{
	/** Deduplicate only identical native resources with identical required states. */
	template <class Transition, std::size_t Capacity>
	[[nodiscard]] bool AddSourceTransition(std::array<Transition, Capacity>& entries,
		std::size_t& count, Transition value) noexcept
	{
		if (!value.resource || count > entries.size())
			return false;
		for (std::size_t index = 0; index < count; ++index)
			if (entries[index].resource == value.resource)
				return entries[index].featureState == value.featureState;
		if (count == entries.size())
			return false;
		entries[count++] = value;
		return true;
	}
}
