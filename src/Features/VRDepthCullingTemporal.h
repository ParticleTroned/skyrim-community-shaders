#pragma once

#include <cstdint>

namespace VRDepthCullingTemporal
{
	struct Status
	{
		bool installed = false;
		bool cullingEnabled = false;
		bool performanceMode = false;
		std::uint64_t envelopeMisses = 0;
		std::uint64_t totalPromoted = 0;
		std::uint32_t lastObjectCount = 0;
		std::uint32_t lastEligibleCount = 0;
		std::uint32_t lastPromotedCount = 0;
	};

	/** Install the Skyrim VR 1.4.15 producer and readback hooks. */
	void Install();
	/** Enable temporal work only while native depth culling is active. */
	void SetCullingEnabled(bool a_enabled);
	/** Select native-result Performance Mode when enabled. */
	void SetPerformanceMode(bool a_enabled);
	/** Return the mode currently observed by the render thread. */
	[[nodiscard]] bool IsPerformanceMode();
	/** Return thread-safe diagnostics for DevBench inspection. */
	[[nodiscard]] Status GetStatus();
}
