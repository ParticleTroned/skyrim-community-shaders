#pragma once

#include <sl_result.h>
#include <cstdint>
#include <optional>

namespace DLSSResultPolicy
{
	/** A budget warning still publishes a valid evaluated image. */
	[[nodiscard]] constexpr bool IsEvaluationSuccessful(sl::Result result) noexcept
	{
		return result == sl::Result::eOk || result == sl::Result::eWarnOutOfVRAM;
	}

	/** Bounded warning cadence owned by the evaluation thread. */
	class BudgetWarningThrottle
	{
	public:
		[[nodiscard]] bool ShouldLog(uint32_t frame) noexcept
		{
			if (lastFrame && frame - *lastFrame < 300u)
				return false;
			lastFrame = frame;
			return true;
		}
		void Reset() noexcept { lastFrame.reset(); }

	private:
		std::optional<uint32_t> lastFrame;
	};
}
