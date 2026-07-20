#include "LODBlending.h"

#include "../I18n/I18n.h"

#include <algorithm>
#include <cmath>

#define I18N_KEY_PREFIX "feature.lod_blending."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LODBlending::Settings,
	Enabled,
	LODTerrainBrightness,
	LODObjectBrightness,
	LODObjectSnowBrightness,
	DisableTerrainVertexColors,
	LODTerrainGamma,
	LODObjectGamma,
	LODObjectSnowGamma,
	WaterReflectionStrength)

namespace
{
	constexpr const char* kWaterReflectionStrengthConfigKey = "WaterReflectionStrength";
	constexpr const char* kEnableWaterReflectionStrengthConfigKey = "EnableWaterReflectionStrength";
	constexpr float kWaterReflectionStrengthDefault = 1.0f;
	constexpr float kWaterReflectionStrengthMin = 0.0f;
	constexpr float kWaterReflectionStrengthMax = 4.0f;

	const char* GetWaterReflectionStrengthDisplay()
	{
		return T(TKEY("water_reflection_strength"), "Water Reflection Strength");
	}

	const char* GetWaterReflectionStrengthTooltip()
	{
		return T(TKEY("water_reflection_strength_tooltip"),
			"Height-faded reflection amount for regular and LOD water.\n"
			"The same value is applied to all visible water, based on camera height above the current water level.\n"
			"1.00 blends toward the material reflection amount at high elevation, 0.00 blends toward only the reflection color.\n"
			"Higher values move high-elevation water back toward full sky/SSR.");
	}

	float ClampWaterReflectionStrength(float a_value)
	{
		if (!std::isfinite(a_value))
			return kWaterReflectionStrengthDefault;

		return std::clamp(a_value, kWaterReflectionStrengthMin, kWaterReflectionStrengthMax);
	}

	bool GetBooleanOrDefault(const json& a_json, const char* a_key, bool a_default)
	{
		if (!a_json.is_object() || !a_json.contains(a_key))
			return a_default;

		try {
			return a_json.at(a_key).get<bool>();
		} catch (const json::exception&) {
			logger::debug("Failed to load LOD Blending {} setting as a boolean", a_key);
			return a_default;
		}
	}

	bool DrawEnabledCheckbox(LODBlending::Settings& a_settings)
	{
		bool enabled = a_settings.Enabled != 0;
		if (ImGui::Checkbox(T(TKEY("enabled"), "Enabled"), &enabled))
			a_settings.Enabled = enabled ? 1u : 0u;
		return enabled;
	}

	void DrawWaterReflectionSettings(LODBlending& a_feature)
	{
		auto& settings = a_feature.settings;
		auto& applyWaterReflectionStrength = a_feature.EnableWaterReflectionStrength;

		ImGui::Checkbox(T(TKEY("apply_water_reflection_strength"), "Apply Water Reflection Strength"), &applyWaterReflectionStrength);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("apply_water_reflection_strength_tooltip"),
								  "Toggle the height-faded water reflection strength blend at runtime.\n"
								  "Disable this to use the original reflection path while leaving the slider value intact."));
		}

		ImGui::BeginDisabled(!applyWaterReflectionStrength);
		ImGui::SliderFloat(
			GetWaterReflectionStrengthDisplay(),
			&settings.WaterReflectionStrength,
			kWaterReflectionStrengthMin,
			kWaterReflectionStrengthMax,
			"%.2f",
			ImGuiSliderFlags_AlwaysClamp);
		ImGui::EndDisabled();
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", GetWaterReflectionStrengthTooltip());
	}
}

void LODBlending::DrawSettings()
{
	settings.WaterReflectionStrength = ClampWaterReflectionStrength(settings.WaterReflectionStrength);
	const bool enabled = DrawEnabledCheckbox(settings);

	ImGui::BeginDisabled(!enabled);
	ImGui::SliderFloat(T(TKEY("lod_terrain_brightness"), "LOD Terrain Brightness"), &settings.LODTerrainBrightness, 0.01f, 5.f, "%.2f");
	ImGui::SliderFloat(T(TKEY("lod_object_brightness"), "LOD Object Brightness"), &settings.LODObjectBrightness, 0.01f, 5.f, "%.2f");
	ImGui::SliderFloat(T(TKEY("lod_object_snow_brightness"), "LOD Object Snow Brightness"), &settings.LODObjectSnowBrightness, 0.01f, 5.f, "%.2f");
	ImGui::SliderFloat(T(TKEY("lod_terrain_gamma"), "LOD Terrain Gamma"), &settings.LODTerrainGamma, 0.1f, 3.f, "%.2f");
	ImGui::SliderFloat(T(TKEY("lod_object_gamma"), "LOD Object Gamma"), &settings.LODObjectGamma, 0.1f, 3.f, "%.2f");
	ImGui::SliderFloat(T(TKEY("lod_object_snow_gamma"), "LOD Object Snow Gamma"), &settings.LODObjectSnowGamma, 0.1f, 3.f, "%.2f");

	DrawWaterReflectionSettings(*this);

	bool disableTerrainVertexColors = settings.DisableTerrainVertexColors != 0;
	if (ImGui::Checkbox(T(TKEY("disable_terrain_vertex_colors"), "Disable Terrain Vertex Colors"), &disableTerrainVertexColors))
		settings.DisableTerrainVertexColors = disableTerrainVertexColors ? 1u : 0u;
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("disable_terrain_vertex_colors_tooltip"),
							  "Disables vertex coloring on nearby terrain. Best combined with terrain LOD generated in xLODGen with Vertex Color Intensity set to 0."));
	}
	ImGui::EndDisabled();
}

void LODBlending::DrawEssentialSettings()
{
	DrawEnabledCheckbox(settings);
}

void LODBlending::LoadSettings(json& o_json)
{
	if (!o_json.is_object()) {
		logger::debug("Ignoring invalid LOD Blending settings object");
		RestoreDefaultSettings();
		return;
	}

	json settingsJson = o_json;
	auto strengthIt = settingsJson.find(kWaterReflectionStrengthConfigKey);
	if (strengthIt != settingsJson.end() && !strengthIt->is_number()) {
		logger::debug("Ignoring invalid LOD Blending water reflection strength setting");
		settingsJson.erase(strengthIt);
	}

	try {
		settings = settingsJson;
	} catch (const json::exception& e) {
		logger::debug("Ignoring invalid LOD Blending settings: {}", e.what());
		RestoreDefaultSettings();
		return;
	}

	EnableWaterReflectionStrength = GetBooleanOrDefault(o_json, kEnableWaterReflectionStrengthConfigKey, true);
	settings.WaterReflectionStrength = ClampWaterReflectionStrength(settings.WaterReflectionStrength);
	settings.Enabled = settings.Enabled ? 1u : 0u;
	settings.DisableTerrainVertexColors = settings.DisableTerrainVertexColors ? 1u : 0u;
}

void LODBlending::SaveSettings(json& o_json)
{
	settings.WaterReflectionStrength = ClampWaterReflectionStrength(settings.WaterReflectionStrength);
	o_json = settings;
	o_json[kEnableWaterReflectionStrengthConfigKey] = EnableWaterReflectionStrength;
}

LODBlending::Settings LODBlending::GetCommonBufferData() const
{
	auto data = settings;
	data.WaterReflectionStrength = data.Enabled && EnableWaterReflectionStrength ?
	                                   ClampWaterReflectionStrength(data.WaterReflectionStrength) :
	                                   -1.0f;
	return data;
}

void LODBlending::RestoreDefaultSettings()
{
	settings = {};
	EnableWaterReflectionStrength = true;
}

#undef I18N_KEY_PREFIX
