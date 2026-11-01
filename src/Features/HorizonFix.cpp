#include "HorizonFix.h"

void HorizonFix::DrawSettings()
{
	Feature::DrawSettings();
}

void HorizonFix::PostPostLoad()
{
	// Fix the Water compatibility contract after plugins load and before cache admission.
	if (!loaded)
		return;
	pluginInstalled = GetModuleHandleW(L"HorizonFix.dll") != nullptr;
	pluginDetectionComplete = true;
	if (!pluginInstalled) {
		loadFailed = false;
		loaded = false;
		logger::info("[Horizon Fix] HorizonFix.dll not detected, compatibility disabled");
		return;
	}

	logger::info("[Horizon Fix] HorizonFix.dll detected, compatibility enabled");
}
