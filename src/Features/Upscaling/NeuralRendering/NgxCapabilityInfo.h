#pragma once

#include <cstdint>
#include <optional>

namespace NeuralRendering
{
	/** Provider capability queries distinguish unknown information from a reported requirement. */
	struct NgxDriverRequirements
	{
		std::optional<bool> needsUpdate;
		std::optional<std::uint32_t> minimumMajor, minimumMinor;
		std::optional<std::uint32_t> updateQuery, majorQuery, minorQuery;
	};

#ifdef DEVBENCH_BRIDGE_ENABLED
	/** Aggregate provider accounting; never an exact per-eye cost or an admission budget. */
	struct NgxAllocationStatistics
	{
		std::optional<std::uint64_t> allocatedBytes;
		std::optional<std::uint32_t> callbackQuery, callbackResult, sizeQuery;
	};
#endif
}
