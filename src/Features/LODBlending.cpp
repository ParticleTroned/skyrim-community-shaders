#include "LODBlending.h"

#include "../I18n/I18n.h"

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
	LODObjectSnowGamma)

namespace
{
	bool DrawEnabledCheckbox(LODBlending::Settings& a_settings)
	{
		bool enabled = a_settings.Enabled != 0;
		if (ImGui::Checkbox(T(TKEY("enabled"), "Enabled"), &enabled))
			a_settings.Enabled = enabled ? 1u : 0u;
		return enabled;
	}
}

void LODBlending::DrawSettings()
{
	const bool enabled = DrawEnabledCheckbox(settings);

	ImGui::BeginDisabled(!enabled);
	ImGui::SliderFloat(T(TKEY("lod_terrain_brightness"), "LOD Terrain Brightness"), &settings.LODTerrainBrightness, 0.01f, 5.f, "%.2f");
	ImGui::SliderFloat(T(TKEY("lod_object_brightness"), "LOD Object Brightness"), &settings.LODObjectBrightness, 0.01f, 5.f, "%.2f");
	ImGui::SliderFloat(T(TKEY("lod_object_snow_brightness"), "LOD Object Snow Brightness"), &settings.LODObjectSnowBrightness, 0.01f, 5.f, "%.2f");
	ImGui::SliderFloat(T(TKEY("lod_terrain_gamma"), "LOD Terrain Gamma"), &settings.LODTerrainGamma, 0.1f, 3.f, "%.2f");
	ImGui::SliderFloat(T(TKEY("lod_object_gamma"), "LOD Object Gamma"), &settings.LODObjectGamma, 0.1f, 3.f, "%.2f");
	ImGui::SliderFloat(T(TKEY("lod_object_snow_gamma"), "LOD Object Snow Gamma"), &settings.LODObjectSnowGamma, 0.1f, 3.f, "%.2f");
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

#undef I18N_KEY_PREFIX

void LODBlending::LoadSettings(json& o_json)
{
	settings = o_json;
	settings.Enabled = settings.Enabled ? 1u : 0u;
	settings.DisableTerrainVertexColors = settings.DisableTerrainVertexColors ? 1u : 0u;
}

void LODBlending::SaveSettings(json& o_json)
{
	o_json = settings;
}

void LODBlending::RestoreDefaultSettings()
{
	settings = {};
}
