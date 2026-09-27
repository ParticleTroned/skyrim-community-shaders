#pragma once

#include <sl_result.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>

namespace DLSSResultPolicy
{
	/** Streamline reports budget exhaustion after successful evaluation; other errors retain failure semantics. */
	[[nodiscard]] constexpr bool IsEvaluationSuccessful(sl::Result a_result) noexcept
	{
		return a_result == sl::Result::eOk || a_result == sl::Result::eWarnOutOfVRAM;
	}

	/** Per-instance budget warning cadence, owned by the dispatch thread. */
	class BudgetWarningThrottle
	{
	public:
		static constexpr uint32_t kIntervalFrames = 300;

		/** Emits the first warning and repeats at the interval, independently for each bounded eye. */
		[[nodiscard]] bool ShouldLog(uint32_t a_eye, uint32_t a_frame) noexcept
		{
			auto& lastFrame = lastFrames[std::min(a_eye, 1u)];
			if (lastFrame && a_frame - *lastFrame < kIntervalFrames)
				return false;
			lastFrame = a_frame;
			return true;
		}

		/** Starts a fresh warning cadence after Streamline initialization. */
		void Reset() noexcept { lastFrames = {}; }

	private:
		std::array<std::optional<uint32_t>, 2> lastFrames{};
	};
}
