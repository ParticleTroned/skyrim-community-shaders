#pragma once

#include <array>
#include <cstdint>

namespace NeuralRendering
{
	/** Read-only ownership observations; a snapshot never signals or waits on the GPU. */
	struct LifetimeFenceSnapshot
	{
		std::uint64_t device = 0, queue = 0, fence = 0, device11 = 0, context11 = 0;
		std::uint64_t issued = 0, completed = 0;
		std::array<std::uint64_t, 3> contexts{};
		bool initialized = false, recording = false, completedKnown = false, deviceRemoved = false;
	};
}
