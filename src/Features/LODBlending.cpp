#include "LODBlending.h"

#include "WeatherVariableRegistry.h"

#include <algorithm>
#include <cmath>

namespace
{
	// Keep the weather variable key stable so existing per-weather overrides continue to work.
	constexpr const char* kWaterReflectionStrengthSetting = "WaterLODReflectionStrength";
	constexpr const char* kWaterReflectionStrengthConfigKey = "WaterReflectionStrength";
	constexpr const char* kWaterReflectionStrengthDisplay = "Water Reflection Strength";
	constexpr const char* kWaterReflectionStrengthTooltip =
		"Height-faded reflection amount for regular and LOD water.\n"
		"The same value is applied to all visible water, based on camera height above the current water level.\n"
		"1.00 blends toward the material reflection amount at high elevation, 0.00 blends toward only the reflection color.\n"
		"Higher values move high-elevation water back toward full sky/SSR.";
	constexpr float kWaterReflectionStrengthDefault = 1.0f;
	constexpr float kWaterReflectionStrengthMin = 0.0f;
	constexpr float kWaterReflectionStrengthMax = 4.0f;

	float ClampWaterReflectionStrength(float a_value)
	{
		if (!std::isfinite(a_value)) {
			return kWaterReflectionStrengthDefault;
		}

		return std::clamp(a_value, kWaterReflectionStrengthMin, kWaterReflectionStrengthMax);
	}

	bool TryGetWaterReflectionStrength(const json& a_json, const char* a_key, float& a_value)
	{
		if (!a_json.contains(a_key)) {
			return false;
		}

		try {
			a_value = a_json.at(a_key).get<float>();
		} catch (const json::exception&) {
			return false;
		}

		return true;
	}
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LODBlending::Settings,
	LODTerrainBrightness,
	LODObjectBrightness,
	LODObjectSnowBrightness,
	DisableTerrainVertexColors,
	LODTerrainGamma,
	LODObjectGamma,
	LODObjectSnowGamma,
	WaterReflectionStrength)

void LODBlending::DrawSettings()
{
	settings.WaterReflectionStrength = ClampWaterReflectionStrength(settings.WaterReflectionStrength);

	ImGui::SliderFloat("LOD Terrain Brightness", &settings.LODTerrainBrightness, 0.01f, 5.f, "%.2f");
	ImGui::SliderFloat("LOD Object Brightness", &settings.LODObjectBrightness, 0.01f, 5.f, "%.2f");
	ImGui::SliderFloat("LOD Object Snow Brightness", &settings.LODObjectSnowBrightness, 0.01f, 5.f, "%.2f");
	ImGui::SliderFloat("LOD Terrain Gamma", &settings.LODTerrainGamma, 0.1f, 3.f, "%.2f");
	ImGui::SliderFloat("LOD Object Gamma", &settings.LODObjectGamma, 0.1f, 3.f, "%.2f");
	ImGui::SliderFloat("LOD Object Snow Gamma", &settings.LODObjectSnowGamma, 0.1f, 3.f, "%.2f");
	const bool waterReflectionWeatherControlled =
		Util::WeatherUI::IsWeatherControlled(this, kWaterReflectionStrengthSetting);
	const bool waterReflectionChanged = Util::WeatherUI::SliderFloat(
		kWaterReflectionStrengthDisplay,
		this,
		kWaterReflectionStrengthSetting,
		&settings.WaterReflectionStrength,
		kWaterReflectionStrengthMin,
		kWaterReflectionStrengthMax,
		"%.2f");
	if (!waterReflectionWeatherControlled) {
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", kWaterReflectionStrengthTooltip);
		}
	}
	if (waterReflectionChanged) {
		WeatherVariables::GlobalWeatherRegistry::GetSingleton()->CaptureFeatureUserSettings(GetShortName());
	}
	ImGui::Checkbox("Disable Terrain Vertex Colors", (bool*)&settings.DisableTerrainVertexColors);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Disables vertex coloring on nearby terrain. "
			"Best combined with terrain LOD generated in xLODGen with Vertex Color Intensity set to 0. ");
	}
}

void LODBlending::LoadSettings(json& o_json)
{
	settings = o_json;
	if (!o_json.contains(kWaterReflectionStrengthConfigKey) && o_json.contains(kWaterReflectionStrengthSetting)) {
		try {
			settings.WaterReflectionStrength = o_json.at(kWaterReflectionStrengthSetting).get<float>();
		} catch (const json::exception& e) {
			logger::debug("Failed to load legacy LOD Blending water reflection strength: {}", e.what());
		}
	}
	settings.WaterReflectionStrength = ClampWaterReflectionStrength(settings.WaterReflectionStrength);
}

void LODBlending::SaveSettings(json& o_json)
{
	o_json = settings;
}

void LODBlending::RegisterWeatherVariables()
{
	auto* registry = WeatherVariables::GlobalWeatherRegistry::GetSingleton()
	                     ->GetOrCreateFeatureRegistry(GetShortName());

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		kWaterReflectionStrengthSetting,
		kWaterReflectionStrengthDisplay,
		kWaterReflectionStrengthTooltip,
		&settings.WaterReflectionStrength,
		kWaterReflectionStrengthDefault,
		kWaterReflectionStrengthMin, kWaterReflectionStrengthMax));
}

void LODBlending::NormalizeWeatherSettings(json& o_json)
{
	if (!o_json.is_object()) {
		return;
	}

	float waterReflectionStrength = kWaterReflectionStrengthDefault;
	const bool hasWaterReflectionStrength =
		TryGetWaterReflectionStrength(o_json, kWaterReflectionStrengthSetting, waterReflectionStrength) ||
		TryGetWaterReflectionStrength(o_json, kWaterReflectionStrengthConfigKey, waterReflectionStrength);

	if (hasWaterReflectionStrength) {
		o_json[kWaterReflectionStrengthSetting] = ClampWaterReflectionStrength(waterReflectionStrength);
	} else {
		o_json.erase(kWaterReflectionStrengthSetting);
		if (o_json.value("__enabled", false)) {
			// Enabled override without a valid value should not keep influencing runtime transitions.
			o_json["__enabled"] = false;
		}
	}
	o_json.erase(kWaterReflectionStrengthConfigKey);
}

LODBlending::Settings LODBlending::GetCommonBufferData() const
{
	auto data = settings;
	data.WaterReflectionStrength = ClampWaterReflectionStrength(data.WaterReflectionStrength);
	return data;
}

void LODBlending::RestoreDefaultSettings()
{
	settings = {};
}
