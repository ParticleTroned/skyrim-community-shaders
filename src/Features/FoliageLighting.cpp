#include "FoliageLighting.h"

#include "Util.h"

#include <algorithm>
#include <cmath>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	FoliageLighting::Settings,
	EnableFoliageScattering,
	EnableFoliageAmbientBoost,
	EnableFoliageAmbientFlip,
	FoliageAmbientAmount,
	EnableGrassScattering)

FoliageLighting::Settings FoliageLighting::GetDisabledSettings()
{
	Settings settings{};
	settings.EnableFoliageScattering = 0;
	settings.EnableFoliageAmbientBoost = 0;
	settings.EnableFoliageAmbientFlip = 0;
	settings.FoliageAmbientAmount = 0.0f;
	settings.EnableGrassScattering = 0;
	return settings;
}

void FoliageLighting::SanitizeSettings(Settings& a_settings)
{
	a_settings.EnableFoliageScattering = a_settings.EnableFoliageScattering != 0;
	a_settings.EnableFoliageAmbientBoost = a_settings.EnableFoliageAmbientBoost != 0;
	a_settings.EnableFoliageAmbientFlip = a_settings.EnableFoliageAmbientFlip != 0;
	a_settings.EnableGrassScattering = a_settings.EnableGrassScattering != 0;
	if (std::isfinite(a_settings.FoliageAmbientAmount)) {
		a_settings.FoliageAmbientAmount = std::clamp(
			a_settings.FoliageAmbientAmount,
			kAmbientAmountMin,
			kAmbientAmountMax);
	} else {
		a_settings.FoliageAmbientAmount = Settings{}.FoliageAmbientAmount;
	}
}

void FoliageLighting::DrawSettings()
{
	SanitizeSettings(settings);

	if (ImGui::TreeNodeEx("Tree Foliage")) {
		bool enableFoliageScattering = settings.EnableFoliageScattering != 0;
		if (ImGui::Checkbox("Foliage Scattering", &enableFoliageScattering))
			settings.EnableFoliageScattering = enableFoliageScattering ? 1u : 0u;
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(
				"Adds wrapped, view-dependent transmission to animated tree foliage. "
				"PBR foliage also receives a diffuse transmission term independent of texture thickness.");
		}

		bool enableFoliageAmbientBoost = settings.EnableFoliageAmbientBoost != 0;
		if (ImGui::Checkbox("Ambient Boost", &enableFoliageAmbientBoost))
			settings.EnableFoliageAmbientBoost = enableFoliageAmbientBoost ? 1u : 0u;
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextUnformatted("Adds indirect ambient response to animated PBR foliage after ambient occlusion.");

		ImGui::BeginDisabled(!enableFoliageAmbientBoost);
		ImGui::SliderFloat(
			"Ambient Amount",
			&settings.FoliageAmbientAmount,
			kAmbientAmountMin,
			kAmbientAmountMax,
			"%.2f",
			ImGuiSliderFlags_AlwaysClamp);
		ImGui::EndDisabled();
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextUnformatted("Strength of the additive indirect ambient response for animated PBR foliage.");

		bool enableFoliageAmbientFlip = settings.EnableFoliageAmbientFlip != 0;
		if (ImGui::Checkbox("Ambient Backface Flip", &enableFoliageAmbientFlip))
			settings.EnableFoliageAmbientFlip = enableFoliageAmbientFlip ? 1u : 0u;
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(
				"Mirrors the ambient sampling normal for visible backside tree foliage cards. "
				"VR uses one shared reference direction to keep both eyes consistent.");
		}

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Grass")) {
		bool enableGrassScattering = settings.EnableGrassScattering != 0;
		if (ImGui::Checkbox("Grass Scattering", &enableGrassScattering))
			settings.EnableGrassScattering = enableGrassScattering ? 1u : 0u;
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(
				"Adds wrapped, view-dependent transmission to non-PBR grass. "
				"Works in both the enhanced and fallback grass lighting paths.");
		}

		ImGui::TreePop();
	}

	SanitizeSettings(settings);
}

void FoliageLighting::LoadSettings(json& o_json)
{
	settings = o_json;
	SanitizeSettings(settings);
}

void FoliageLighting::SaveSettings(json& o_json)
{
	SanitizeSettings(settings);
	o_json = settings;
}

void FoliageLighting::RestoreDefaultSettings()
{
	settings = {};
}

FoliageLighting::Settings FoliageLighting::GetCommonBufferData() const
{
	if (!loaded)
		return GetDisabledSettings();

	auto data = settings;
	SanitizeSettings(data);
	return data;
}
