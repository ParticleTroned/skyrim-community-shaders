#include "AdaptiveBrightness.h"
#include "Menu/SettingsPage.h"
#include "Presets/LocationSkyLighting.h"
#include "Presets/LookProfiles.h"
#include "Presets/LookSceneRuntime.h"
#include "Presets/LookScenes.h"
#include "SkySync.h"
#include "Utils/UI.h"

#include <exception>
#include <map>
#include <utility>

void AdaptiveBrightness::SyncLocationSkyLighting()
{
	std::map<std::string, json> layers;
	if (settings.enabled && performanceCostMeasurementEnabled) {
		for (const auto& location : settings.locationOverrides) {
			if (!location.skyLighting.empty()) {
				auto values = location.skyLighting;
				for (auto& value : values) value = value.get<float>();
				layers[Presets::SceneFormKey(location.key)] = { { "SkySync", std::move(values) } };
			}
		}
	}
	Presets::LookSceneRuntime::SetLocationOverrides(std::move(layers));
}

void AdaptiveBrightness::DrawLocationSkyLighting(json& values, bool allowEdits)
{
	MenuUI::SectionHeading("Sky lighting", "Override only the values this place needs. Unchecked values inherit the broader location or profile. Sky Sync must be active; ordinary interiors without sunlight shadows are unaffected.");
	auto& sky = globals::features::skySync;
	const auto disabled = Util::DisableGuard(!allowEdits || !sky.loaded);
	if (!sky.loaded)
		MenuUI::AvailabilityNotice("Sky Sync is unavailable.");
	else if (!sky.settings.Enabled)
		ImGui::TextDisabled("Sky Sync is disabled; saved overrides take effect when it is enabled.");
	if (!sky.settings.DimSunlightUnderHorizon)
		ImGui::TextDisabled("Horizon fading is disabled in Sky Sync.");
	const auto draw = [&](const char* key, const char* label, float current, float maximum, const char* format, const char* help) {
		ImGui::PushID(key);
		const SKSE::stl::scope_exit restoreId([] { ImGui::PopID(); });
		bool overrideValue = values.contains(key);
		if (Util::Widgets::Checkbox(label, &overrideValue)) {
			if (overrideValue)
				values[key] = current;
			else
				values.erase(key);
		}
		Util::AddTooltip(help);
		if (overrideValue) {
			float value = values.at(key).get<float>();
			if (Util::Widgets::SliderFloat("Value", &value, 0.f, maximum, format, ImGuiSliderFlags_AlwaysClamp))
				values[key] = value;
		} else
			ImGui::TextDisabled("Inherited");
	};
	draw("MinShadowElevation", "Override shadow-length limit", sky.settings.MinShadowElevation,
		SkySync::MaxShadowElevation, "%.1f deg", "Minimum light elevation. Higher values shorten sunrise and sunset shadows. Enabling starts from the current Sky Sync value.");
	draw("HorizonFadeHours", "Override horizon fade duration", sky.settings.HorizonFadeHours,
		SkySync::MaxHorizonFadeHours, "%.1f h", "Duration of direct sunlight fading around sunrise and sunset. Requires Dim Sunlight Under Horizon in Sky Sync; this does not change that setting.");
	if (ImGui::Button("Inherit all sky lighting"))
		values = json::object();
}

#ifdef DEVBENCH_BRIDGE_ENABLED
json AdaptiveBrightness::LocationSkyLightingControls(const json& request)
{
	if (!request.contains("locationKey") || !request["locationKey"].is_string())
		return { { "error", "locationKey must name an existing saved location" } };
	const auto key = request["locationKey"].get<std::string>();
	auto* location = FindLocationOverride(key);
	if (!location)
		return { { "error", "Unknown saved location key" } };
	if (const auto values = request.find("skyLighting"); values != request.end()) {
		if (const auto* reason = Presets::LookProfiles::Get().MutationBlockReason())
			return { { "error", reason } };
		std::string error;
		if (!Presets::LocationSkyLighting::Validate(*values, error))
			return { { "error", error } };
		try {
			Presets::SceneFormKey(key);
		} catch (const std::exception& failure) {
			return { { "error", failure.what() } };
		}
		location->skyLighting = *values;
		if (locationOverrideEditKey == key)
			ResetLocationOverrideEdit();
		SyncLocationSkyLighting();
	}
	const auto& sky = globals::features::skySync;
	return { { "locationKey", key }, { "skyLighting", location->skyLighting }, { "persisted", false },
		{ "visualsEnabled", settings.enabled }, { "skySyncAvailable", sky.loaded }, { "skySyncEnabled", sky.settings.Enabled },
		{ "horizonDimmingEnabled", sky.settings.DimSunlightUnderHorizon } };
}
#endif
