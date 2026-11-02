#pragma once

#include "RegionCapacity.h"
#include <array>
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

	/** Aggregate provider accounting; allocation deltas are observations, never a residency budget. */
	struct NgxAllocationStatistics
	{
		std::optional<std::uint64_t> allocatedBytes;
		std::optional<std::uint32_t> callbackQuery, callbackResult, sizeQuery;
		std::uint64_t queryMicroseconds = 0;
		bool queryThrew = false;
	};

	/** Lifecycle-only provider totals and requested scratch sizes; residency remains an OS observation. */
	struct NativeMemorySnapshot
	{
		NgxAllocationStatistics provider;
		std::array<std::uint64_t, kLogicalFeatureSlotCount> scratchBytes{};
		std::uint64_t sampledAtMs = 0;
		std::uint32_t residentMask = 0;
		bool valid = false;
	};
}
