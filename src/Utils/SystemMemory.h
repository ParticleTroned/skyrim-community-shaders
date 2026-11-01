#pragma once

#include <cstdint>
#include <string_view>

namespace Util
{
	/** System-wide commit and usable physical RAM from one Windows sample. */
	struct SystemMemorySample
	{
		bool valid = false;
		std::uint32_t error = 0;
		std::uint64_t commitUsedBytes = 0;
		std::uint64_t commitLimitBytes = 0;
		std::uint64_t physicalTotalBytes = 0;
		std::uint64_t physicalAvailableBytes = 0;
	};

	/** Query the same commit counters used by the menu; failure remains explicit. */
	[[nodiscard]] SystemMemorySample QuerySystemMemory() noexcept;

	/** Log RAM, current pagefile allocation and commit capacity, without pagefile paths. */
	void LogSystemMemory(std::string_view a_context);
}
