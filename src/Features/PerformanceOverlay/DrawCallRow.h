#pragma once

#include <optional>
#include <string>

/** One profiler row shared by the overlay and A/B measurement aggregation. */
struct DrawCallRow
{
	std::string label;
	int shaderType;  // Use int for consistency with the rest of the codebase
	int drawCalls;
	float frameTime;
	float percent;
	float costPerCall;
	std::string tooltip;
	bool enabled;
	std::optional<float> testFrameTime;
	std::optional<float> testCostPerCall;
};
