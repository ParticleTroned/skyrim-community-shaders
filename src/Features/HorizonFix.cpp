#include "HorizonFix.h"
#include "Menu/SettingsPage.h"

#include <imgui.h>

void HorizonFix::DrawSettings()
{
	Feature::DrawSettings();
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
