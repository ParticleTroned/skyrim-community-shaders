#pragma once

#include "../LifetimeDiagnostics.h"

#include <cstdint>
#include <stdexcept>

namespace NeuralRendering::KernelFrameFence
{
	inline bool Valid(const LifetimeFenceSnapshot& snapshot) noexcept
	{
		return snapshot.initialized && snapshot.device && snapshot.queue && snapshot.fence &&
		       snapshot.completedKnown && !snapshot.deviceRemoved && snapshot.completed != UINT64_MAX;
	}
	inline bool Same(const LifetimeFenceSnapshot& left, const LifetimeFenceSnapshot& right) noexcept
	{
		return left.device == right.device && left.queue == right.queue && left.fence == right.fence;
	}
	/** Readiness and idle signals cannot retire frame storage; only the changed command context can. */
	inline std::uint64_t Submission(const LifetimeFenceSnapshot& before, const LifetimeFenceSnapshot& after)
	{
		if (!Valid(before) || !Valid(after) || !Same(before, after) || !before.recording || after.recording ||
			after.issued <= before.issued)
			throw std::runtime_error("experimental kernel submission fence identity or recording boundary changed");
		unsigned changed = 0;
		std::uint64_t value = 0;
		for (std::size_t index = 0; index < before.contexts.size(); ++index) {
			if (before.contexts[index] == after.contexts[index])
				continue;
			++changed;
			value = after.contexts[index];
			if (!value || value <= before.issued || value != after.issued)
				throw std::runtime_error("experimental kernel frame lacks its exact context completion signal");
		}
		if (changed != 1)
			throw std::runtime_error("experimental kernel submission changed an unexpected number of contexts");
		return value;
	}
}
