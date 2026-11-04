#include "FoliageLighting.h"
#include "Menu/SettingsPage.h"

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

void FoliageLighting::DrawFoliageScatteringSetting()
{
	Util::UIntCheckbox("Foliage Scattering", settings.EnableFoliageScattering);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted(
			"Lets light glow through animated tree leaves, making backlit foliage softer and brighter. Off keeps ordinary leaf lighting.");
	}
}

void FoliageLighting::DrawFoliageAmbientBoostSetting(bool a_truePBRActive)
{
	ImGui::BeginDisabled(!a_truePBRActive);
	Util::UIntCheckbox("Ambient Boost", settings.EnableFoliageAmbientBoost);
	ImGui::EndDisabled();
	DrawTruePBRDependentTooltip(
		a_truePBRActive,
		"Brightens animated PBR foliage in shaded areas. Off keeps its normal ambient brightness.");
}

void FoliageLighting::DrawFoliageAmbientFlipSetting()
{
	Util::UIntCheckbox("Ambient Backface Flip", settings.EnableFoliageAmbientFlip);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted(
			"Lights the backs of leaves from the side you can see, helping avoid unnaturally dark leaf undersides. Off uses their original lighting direction.");
	}
}

void FoliageLighting::DrawGrassScatteringSetting()
{
	Util::UIntCheckbox("Grass Scattering", settings.EnableGrassScattering);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted(
			"Lets light glow through grass blades. On gives softer, brighter backlit grass; off keeps ordinary grass lighting.");
	}
}

void FoliageLighting::DrawSettings()
{
	SanitizeSettings(settings);
	const bool truePBRActive = IsTruePBRActive();

	MenuUI::SettingsPage page("FoliageLighting", {
													 { "trees", "Trees", "Refine light passing through tree leaves.", "Leaf translucency", true, true, "Balance vegetation lighting" },
													 { "grass", "Grass", "Refine light passing through grass.", "Grass translucency", true, true, nullptr },
												 });
	const auto controlsDisabled = Util::DisableGuard(!(IsEnabled()));
	if (page.Is("trees")) {
		DrawFoliageScatteringSetting();
		DrawFoliageAmbientBoostSetting(truePBRActive);

		ImGui::BeginDisabled(!truePBRActive || settings.EnableFoliageAmbientBoost == 0);
		Util::Widgets::SliderFloat(
			"Ambient Amount",
			&settings.FoliageAmbientAmount,
			kAmbientAmountMin,
			kAmbientAmountMax,
			"%.2f",
			ImGuiSliderFlags_AlwaysClamp);
		ImGui::EndDisabled();
		DrawTruePBRDependentTooltip(
			truePBRActive,
			"How much shaded PBR foliage is brightened. Higher values give a lighter appearance.");

		DrawFoliageAmbientFlipSetting();
	}

	if (page.Is("grass")) {
		DrawGrassScatteringSetting();
	}

	SanitizeSettings(settings);
}

void FoliageLighting::DrawPerformanceSettings(bool)
{
	SanitizeSettings(settings);
	DrawSettingsEnabledControl();
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
