#pragma once

#include <cstdint>

namespace Util
{
	/** @brief Limits display history to one sample per frame after reopening warmup. */
	class GraphSampleGate
	{
	public:
		// Covers reopening work and delayed GPU query results.
		static constexpr std::uint32_t kWarmupFrames = 10;

		/** @brief Restart display warmup when the graph switches timing sources. */
		void Reset() { hasFrame = false; }

		/** @brief Reject duplicate frames and warm up after gaps or history resets. */
		[[nodiscard]] bool ShouldSample(std::uint32_t frame, std::uint64_t historyEpoch)
		{
			if (hasFrame && frame == lastFrame && historyEpoch == lastHistoryEpoch)
				return false;
			// Unsigned addition preserves continuity across natural frame-counter wrap.
			if (!hasFrame || historyEpoch != lastHistoryEpoch || frame != lastFrame + 1u)
				warmupFrames = kWarmupFrames;
			lastFrame = frame;
			lastHistoryEpoch = historyEpoch;
			hasFrame = true;
			if (warmupFrames == 0)
				return true;
			--warmupFrames;
			return false;
		}

	private:
		std::uint32_t lastFrame = 0;
		std::uint32_t warmupFrames = 0;
		std::uint64_t lastHistoryEpoch = 0;
		bool hasFrame = false;
	};
}
