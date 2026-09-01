#include "HorizonFix.h"

#include <imgui.h>

void HorizonFix::DrawSettings()
{
	ImGui::TextWrapped(
		"This feature provides compatibility with the Horizon Fix SKSE plugin, which extends the water far clip plane to allow water to be rendered beyond the vanilla far clip distance. This feature is only active when the Horizon Fix plugin is installed.");
}

void HorizonFix::PostPostLoad()
{
	// Probe after all SKSE plugins load and before cache admission so Water uses
	// the compatibility record matching the installed companion plugin.
	if (!loaded)
		return;

	pluginInstalled = GetModuleHandleW(L"HorizonFix.dll") != nullptr;
	pluginDetectionComplete = true;
	if (!pluginInstalled) {
		loaded = false;
		logger::info("[Horizon Fix] HorizonFix plugin not detected, compatibility disabled");
		return;
	}

	logger::info("[Horizon Fix] HorizonFix plugin detected, compatibility enabled");
}
