#include "ProfilingRenderer.h"

#include <imgui.h>

void ProfilingRenderer::RenderStatistics(bool showTable, bool showModeToggle)
{
	(void)showTable;
	(void)showModeToggle;
	ImGui::TextDisabled("Profiling is not available in this build.");
}

void ProfilingRenderer::RenderFeatureTimers(const std::string& featurePrefix)
{
	(void)featurePrefix;
}
