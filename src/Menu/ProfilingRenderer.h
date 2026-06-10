#pragma once

#include <string>

class ProfilingRenderer
{
public:
	static void RenderStatistics(bool showTable = true, bool showModeToggle = true);
	static void RenderFeatureTimers(const std::string& featurePrefix);
};
