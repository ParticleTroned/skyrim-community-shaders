#include "TerrainVariation.h"
#include "../Util.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	TerrainVariation::Settings,
	enableLODTerrainTilingFix)

void TerrainVariation::DrawSettings()
{
	ImGui::TextWrapped(
		"Terrain Variation is always enabled when installed. "
		"To turn it off, use Disable at Boot.");

	ImGui::Spacing();

	bool lodTilingFix = settings.enableLODTerrainTilingFix != 0;
	if (ImGui::Checkbox("Apply to LOD Terrain", &lodTilingFix)) {
		settings.enableLODTerrainTilingFix = lodTilingFix ? 1u : 0u;
		logger::info("TerrainVariation LOD setting changed to: {}", settings.enableLODTerrainTilingFix != 0);
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Applies the tiling fix to LOD terrain objects.\n"
			"This helps reduce the visible tiling effect on distant terrain.");
	}
}

void TerrainVariation::DrawEssentialSettings()
{
	DrawSettings();
}

void TerrainVariation::PostPostLoad()
{
	logger::info("TerrainVariation: Feature initialized");
}

void TerrainVariation::LoadSettings(json& o_json)
{
	settings = o_json;
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
