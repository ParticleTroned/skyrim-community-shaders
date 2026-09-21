#include "FoliageLighting.h"

#include "Globals.h"
#include "I18n/I18n.h"
#include "TruePBR.h"
#include "Util.h"

#include <algorithm>
#include <cmath>

#define I18N_KEY_PREFIX "feature.foliage_lighting."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	FoliageLighting::Settings,
	EnableFoliageScattering,
	EnableFoliageAmbientBoost,
	EnableFoliageAmbientFlip,
	FoliageAmbientAmount,
	EnableGrassScattering)

namespace
{
	bool IsTruePBRActive()
	{
		const auto& truePBR = globals::features::truePBR;
		return truePBR.loaded && truePBR.settings.Enabled != 0;
	}

	void DrawTruePBRDependentTooltip(bool a_truePBRActive, const char* a_description)
	{
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(a_description);
			if (!a_truePBRActive) {
				ImGui::Spacing();
				ImGui::TextUnformatted(
					T(TKEY("true_pbr_required"), "Requires True PBR to be loaded and enabled. The saved value is preserved."));
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

void FoliageLighting::DrawFoliageScatteringSetting()
{
	Util::UIntCheckbox(T(TKEY("foliage_scattering"), "Foliage Scattering"), settings.EnableFoliageScattering);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted(
			T(TKEY("foliage_scattering_tooltip"),
				"Adds wrapped, view-dependent transmission to animated tree foliage. "
				"PBR foliage also receives a diffuse transmission term independent of texture thickness."));
	}
}

void FoliageLighting::DrawFoliageAmbientBoostSetting(bool a_truePBRActive)
{
	ImGui::BeginDisabled(!a_truePBRActive);
	Util::UIntCheckbox(T(TKEY("ambient_boost"), "Ambient Boost"), settings.EnableFoliageAmbientBoost);
	ImGui::EndDisabled();
	DrawTruePBRDependentTooltip(
		a_truePBRActive,
		T(TKEY("ambient_boost_tooltip"), "Adds indirect ambient response to animated PBR foliage after ambient occlusion."));
}

void FoliageLighting::DrawFoliageAmbientFlipSetting()
{
	Util::UIntCheckbox(T(TKEY("ambient_backface_flip"), "Ambient Backface Flip"), settings.EnableFoliageAmbientFlip);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted(
			T(TKEY("ambient_backface_flip_tooltip"),
				"Mirrors the ambient sampling normal for visible backside tree foliage cards."));
	}
}

void FoliageLighting::DrawGrassScatteringSetting()
{
	Util::UIntCheckbox(T(TKEY("grass_scattering"), "Grass Scattering"), settings.EnableGrassScattering);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted(
			T(TKEY("grass_scattering_tooltip"),
				"Adds wrapped, view-dependent transmission to non-PBR grass. "
				"Works in both the enhanced and fallback grass lighting paths."));
	}
}

void FoliageLighting::DrawSettings()
{
	SanitizeSettings(settings);
	const bool truePBRActive = IsTruePBRActive();
	ImGui::BeginDisabled(!IsEnabled());

	if (ImGui::TreeNodeEx(T(TKEY("tree_foliage"), "Tree Foliage"))) {
		DrawFoliageScatteringSetting();
		DrawFoliageAmbientBoostSetting(truePBRActive);

		ImGui::BeginDisabled(!truePBRActive || settings.EnableFoliageAmbientBoost == 0);
		ImGui::SliderFloat(
			T(TKEY("ambient_amount"), "Ambient Amount"),
			&settings.FoliageAmbientAmount,
			kAmbientAmountMin,
			kAmbientAmountMax,
			"%.2f",
			ImGuiSliderFlags_AlwaysClamp);
		ImGui::EndDisabled();
		DrawTruePBRDependentTooltip(
			truePBRActive,
			T(TKEY("ambient_amount_tooltip"), "Strength of the additive indirect ambient response for animated PBR foliage."));

		DrawFoliageAmbientFlipSetting();

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx(T(TKEY("grass"), "Grass"))) {
		DrawGrassScatteringSetting();

		ImGui::TreePop();
	}

	ImGui::EndDisabled();
	SanitizeSettings(settings);
}

void FoliageLighting::DrawPerformanceSettings(bool)
{
	SanitizeSettings(settings);
	DrawSettingsHeaderControls();
	ImGui::BeginDisabled(!IsEnabled());
	const bool truePBRActive = IsTruePBRActive();
	DrawFoliageScatteringSetting();
	DrawFoliageAmbientBoostSetting(truePBRActive);
	DrawFoliageAmbientFlipSetting();
	DrawGrassScatteringSetting();
	ImGui::EndDisabled();
}

json FoliageLighting::CapturePerformanceSettingsState() const
{
	return {
		{ "Enabled", IsEnabled() },
		{ "EnableFoliageScattering", settings.EnableFoliageScattering != 0 },
		{ "EnableFoliageAmbientBoost", settings.EnableFoliageAmbientBoost != 0 },
		{ "EnableFoliageAmbientFlip", settings.EnableFoliageAmbientFlip != 0 },
		{ "EnableGrassScattering", settings.EnableGrassScattering != 0 }
	};
}

bool FoliageLighting::IsPerformanceCostMeasurementEnabled() const
{
	return IsRuntimeEnabled() && HasEnabledContribution();
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

bool FoliageLighting::HasEnabledContribution() const
{
	return settings.EnableFoliageScattering != 0 ||
	       (settings.EnableFoliageAmbientBoost != 0 && IsTruePBRActive()) ||
	       settings.EnableFoliageAmbientFlip != 0 ||
	       settings.EnableGrassScattering != 0;
}
