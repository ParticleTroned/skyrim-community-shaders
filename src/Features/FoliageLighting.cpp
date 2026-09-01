#include "FoliageLighting.h"

#include "Globals.h"
#include "TruePBR.h"
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

namespace
{
	void DrawTruePBRDependentTooltip(bool a_truePBRActive, const char* a_description)
	{
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(a_description);
			if (!a_truePBRActive) {
				ImGui::Spacing();
				ImGui::TextUnformatted("Requires True PBR to be loaded and enabled. The saved value is preserved.");
			}
		}
	}
}

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

void FoliageLighting::DrawSettingsHeaderControls()
{
	bool foliageLightingEnabled = IsEnabled();
	if (ImGui::Checkbox("Enable", &foliageLightingEnabled))
		SetEnabled(foliageLightingEnabled);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted("Controls all tree foliage and grass lighting additions while preserving their saved tuning.");
}

void FoliageLighting::DrawSettings()
{
	SanitizeSettings(settings);
	const auto& truePBR = globals::features::truePBR;
	const bool truePBRActive = truePBR.loaded && truePBR.settings.Enabled != 0;
	ImGui::BeginDisabled(!IsEnabled());

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
		ImGui::BeginDisabled(!truePBRActive);
		if (ImGui::Checkbox("Ambient Boost", &enableFoliageAmbientBoost))
			settings.EnableFoliageAmbientBoost = enableFoliageAmbientBoost ? 1u : 0u;
		ImGui::EndDisabled();
		DrawTruePBRDependentTooltip(
			truePBRActive,
			"Adds indirect ambient response to animated PBR foliage after ambient occlusion.");

		ImGui::BeginDisabled(!truePBRActive || !enableFoliageAmbientBoost);
		ImGui::SliderFloat(
			"Ambient Amount",
			&settings.FoliageAmbientAmount,
			kAmbientAmountMin,
			kAmbientAmountMax,
			"%.2f",
			ImGuiSliderFlags_AlwaysClamp);
		ImGui::EndDisabled();
		DrawTruePBRDependentTooltip(
			truePBRActive,
			"Strength of the additive indirect ambient response for animated PBR foliage.");

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

	ImGui::EndDisabled();
	SanitizeSettings(settings);
}

void FoliageLighting::LoadSettings(json& o_json)
{
	settings = o_json;
	SetEnabled(o_json.value("Enabled", true));
	SanitizeSettings(settings);
}

void FoliageLighting::SaveSettings(json& o_json)
{
	SanitizeSettings(settings);
	o_json = settings;
	o_json["Enabled"] = IsEnabled();
}

void FoliageLighting::RestoreDefaultSettings()
{
	settings = {};
	SetEnabled(true);
}

FoliageLighting::Settings FoliageLighting::GetCommonBufferData() const
{
	if (!IsRuntimeEnabled())
		return GetDisabledSettings();

	auto data = settings;
	SanitizeSettings(data);
	return data;
}
