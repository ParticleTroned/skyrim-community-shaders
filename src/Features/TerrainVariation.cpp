#include "TerrainVariation.h"
#include "../FeatureBuffer.h"
#include "../Globals.h"
#include "../I18n/I18n.h"
#include "../State.h"
#include "../Util.h"

#define I18N_KEY_PREFIX "feature.terrain_variation."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	TerrainVariation::Settings,
	enableTilingFix,
	enableLODTerrainTilingFix)

void TerrainVariation::DrawSettings()
{
	bool oldEnabled = settings.enableTilingFix;
	ImGui::Checkbox("Enable", (bool*)&settings.enableTilingFix);
	if (oldEnabled != (bool)settings.enableTilingFix) {
		// Update the shader settings when the checkbox is toggled
		UpdateShaderSettings();
		logger::info("TerrainVariation setting changed to: {}", settings.enableTilingFix);
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("enable_tiling_fix_tooltip"),
							  "Reduces the repeating pattern effect on terrain textures.\nThis technique creates more natural-looking terrain by adding variation to texture sampling."));
	}

	ImGui::Separator();

	bool oldLODEnabled = settings.enableLODTerrainTilingFix;
	ImGui::Checkbox(T(TKEY("apply_to_lod_terrain"), "Apply to LOD Terrain"), (bool*)&settings.enableLODTerrainTilingFix);
	if (oldLODEnabled != (bool)settings.enableLODTerrainTilingFix) {
		UpdateShaderSettings();
		logger::info("TerrainVariation LOD setting changed to: {}", settings.enableLODTerrainTilingFix);
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("apply_to_lod_terrain_tooltip"),
							  "Applies the tiling fix to LOD terrain objects.\nThis helps reduce the visible tiling effect on distant terrain."));
	}
}

#undef I18N_KEY_PREFIX

void TerrainVariation::UpdateShaderSettings()
{
	if (!globals::state) {
		return;
	}

	// Mark the vertex descriptor as dirty to trigger an update
	if (globals::game::stateUpdateFlags) {
		globals::game::stateUpdateFlags->set(RE::BSGraphics::DIRTY_VERTEX_DESC);
	}
}

void TerrainVariation::PostPostLoad()
{
	logger::info("TerrainVariation: Feature initialized");
	UpdateShaderSettings();
}

void TerrainVariation::DrawEssentialSettings()
{
	const bool oldEnabled = settings.enableTilingFix != 0;
	ImGui::Checkbox("Enable", reinterpret_cast<bool*>(&settings.enableTilingFix));
	if (oldEnabled != (settings.enableTilingFix != 0)) {
		UpdateShaderSettings();
		logger::info("TerrainVariation setting changed to: {}", settings.enableTilingFix);
	}
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted("Reduces repeating patterns in terrain textures by varying texture sampling.");
}

void TerrainVariation::LoadSettings(json& o_json)
{
	settings = o_json;
	UpdateShaderSettings();
}

void TerrainVariation::SaveSettings(json& o_json)
{
	o_json = settings;
}

void TerrainVariation::RestoreDefaultSettings()
{
	settings = {};
}

bool TerrainVariation::DrawFailLoadMessage() const
{
	return false;
}
