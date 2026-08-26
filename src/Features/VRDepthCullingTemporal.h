#pragma once

#include <cstdint>

namespace VRDepthCullingTemporal
{
	struct Status
	{
		bool installed = false;
		bool performanceMode = false;
		std::uint64_t envelopeMisses = 0;
		std::uint64_t totalPromoted = 0;
		std::uint32_t lastObjectCount = 0;
		std::uint32_t lastEligibleCount = 0;
		std::uint32_t lastPromotedCount = 0;
	};

	void Install();
	void SetPerformanceMode(bool a_enabled);
	[[nodiscard]] bool IsPerformanceMode();
	[[nodiscard]] Status GetStatus();
}
