#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace StreamingTextures
{
	/** Rank preview, required and opportunistic restores without starving older requests. */
	struct RestorationPriority
	{
		unsigned kind = 0;
		double urgency = 0;
		std::uint64_t waitingSince = 0, saving = 0, serial = 0;
		static constexpr std::uint64_t maximumWaitMilliseconds = 5000;

		[[nodiscard]] bool BetterThan(const RestorationPriority& other, std::uint64_t now) const noexcept
		{
			if (kind != other.kind)
				return kind > other.kind;
			if (kind) {
				const auto overdue = [&](std::uint64_t since) { return since && now >= since && now - since >= maximumWaitMilliseconds; };
				if (overdue(waitingSince) != overdue(other.waitingSince))
					return overdue(waitingSince);
				if (overdue(waitingSince) && waitingSince != other.waitingSince)
					return waitingSince < other.waitingSince;
				const auto score = [](double value) { return std::isfinite(value) ? std::clamp(value, 0.0, 64.0) : 64.0; };
				if (score(urgency) != score(other.urgency))
					return score(urgency) > score(other.urgency);
				if (waitingSince != other.waitingSince)
					return waitingSince < other.waitingSince;
			} else if (saving != other.saving) {
				return saving > other.saving;
			}
			return serial < other.serial;
		}
	};
}
