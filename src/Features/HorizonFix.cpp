#include "HorizonFix.h"

#include <imgui.h>

#define I18N_KEY_PREFIX "feature.horizon_fix."

void HorizonFix::DrawSettings()
{
	ImGui::TextWrapped("%s", T(TKEY("compatibility_description"),
								 "This feature provides compatibility with the Horizon Fix SKSE plugin, which extends the water far clip plane so water can render beyond the vanilla far clip distance. It is active only while HorizonFix.dll is installed."));
}

void HorizonFix::PostPostLoad()
{
	// Every SKSE plugin is loaded now, while shader-cache validation has not run.
	// Toggling the DLL therefore changes the ordinary feature-cache state before
	// Water shaders are selected or rebuilt.
	const bool pluginDetected = GetModuleHandleW(L"HorizonFix.dll") != nullptr;
	if (!pluginDetected) {
		loaded = false;
		logger::info("[Horizon Fix] HorizonFix.dll not detected, compatibility disabled");
		return;
	}

	logger::info("[Horizon Fix] HorizonFix.dll detected, compatibility enabled");
}

bool HorizonFix::IsInMenu() const
{
	// Keep optional compatibility out of the feature list when its provider is
	// absent, while still exposing a boot-disabled feature when the DLL exists.
	return GetModuleHandleW(L"HorizonFix.dll") != nullptr;
}

#undef I18N_KEY_PREFIX
