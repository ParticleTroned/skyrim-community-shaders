#include "GrassLighting.h"
#include "Menu/SettingsPage.h"
#include "Utils/UI.h"

#include <algorithm>
#include <cmath>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	GrassLighting::Settings,
	Glossiness,
	SpecularStrength,
	SubsurfaceScatteringAmount,
	OverrideComplexGrassSettings,
	BasicGrassBrightness,
	EnableWrappedLighting,
	ComplexGrassThreshold,
	Enabled)

float GrassLighting::ClampGlossiness(float glossiness, float fallback)
{
	if (!std::isfinite(glossiness)) {
		return fallback;
	}
	return std::clamp(glossiness, kGlossinessMin, kGlossinessMax);
}

float GrassLighting::ClampSpecularStrength(float specularStrength, float fallback)
{
	if (!std::isfinite(specularStrength)) {
		return fallback;
	}
	return std::clamp(specularStrength, kSpecularStrengthMin, kSpecularStrengthMax);
}

float GrassLighting::ClampSubsurfaceScatteringAmount(float subsurfaceScatteringAmount, float fallback)
{
	if (!std::isfinite(subsurfaceScatteringAmount)) {
		return fallback;
	}
	return std::clamp(subsurfaceScatteringAmount, kSubsurfaceScatteringAmountMin, kSubsurfaceScatteringAmountMax);
}

void GrassLighting::SanitizeSettings()
{
	settings.Glossiness = ClampGlossiness(settings.Glossiness, Settings{}.Glossiness);
	settings.SpecularStrength = ClampSpecularStrength(settings.SpecularStrength, Settings{}.SpecularStrength);
	settings.SubsurfaceScatteringAmount = ClampSubsurfaceScatteringAmount(
		settings.SubsurfaceScatteringAmount,
		Settings{}.SubsurfaceScatteringAmount);
	if (std::isfinite(settings.ComplexGrassThreshold)) {
		settings.ComplexGrassThreshold = std::clamp(
			settings.ComplexGrassThreshold,
			kComplexGrassThresholdMin,
			kComplexGrassThresholdMax);
	} else {
		settings.ComplexGrassThreshold = Settings{}.ComplexGrassThreshold;
	}
	settings.Enabled = settings.Enabled != 0;
}

bool GrassLighting::DrawEnabledCheckbox()
{
	SanitizeSettings();
	bool enabled = settings.Enabled != 0;
	if (Util::Widgets::Checkbox("Enabled", &enabled))
		settings.Enabled = enabled ? 1u : 0u;
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted("Enables enhanced grass lighting at runtime. Disable to use the basic grass lighting path without restarting.");
	return enabled;
}

void GrassLighting::DrawComplexGrassDetectionThreshold()
{
	Util::Widgets::SliderFloat(
		"Complex Grass Detection",
		&settings.ComplexGrassThreshold,
		kComplexGrassThresholdMin,
		kComplexGrassThresholdMax,
		"%.3f",
		ImGuiSliderFlags_AlwaysClamp);
	SanitizeSettings();
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted("Controls how strictly complex grass textures are detected. Lower values are more strict.");
		ImGui::TextUnformatted("Classifying fewer textures as complex can reduce enhanced normal and specular work, but may change their appearance.");
		ImGui::TextUnformatted("The performance effect depends on the grass textures currently visible.");
	}
}

void GrassLighting::DrawSettings()
{
	SanitizeSettings();

	MenuUI::SettingsPage page("GrassLighting", {
												   { "highlights", "Highlights", "Choose how grass reflects direct light.", "Direct-light shine", true, true, "Shape the shared look" },
												   { "lighting", "Lighting", "Refine grass colour and indirect lighting.", "Colour and indirect light", true, true, nullptr },
												   { "effects", "Effects", "Refine grass lighting effects.", "Additional lighting effects", true, true, "Refine grass effects" },
											   });
	const auto controlsDisabled = Util::DisableGuard(!(settings.Enabled != 0));
	if (page.Is("highlights")) {
		ImGui::TextWrapped("Specular highlights for complex grass");
		Util::Widgets::SliderFloat(
			"Glossiness",
			&settings.Glossiness,
			kGlossinessMin,
			kGlossinessMax,
			"%.0f",
			ImGuiSliderFlags_AlwaysClamp);
		SanitizeSettings();
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(
				"Specular highlight glossiness. This also defines the dry endpoint for Wetterness grass glossiness after rain and grass drying finish.");
		}

		Util::Widgets::SliderFloat(
			"Specular Strength",
			&settings.SpecularStrength,
			kSpecularStrengthMin,
			kSpecularStrengthMax,
			"%.2f",
			ImGuiSliderFlags_AlwaysClamp);
		SanitizeSettings();
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(
				"Specular highlight strength. This also defines the dry endpoint for Wetterness grass specular strength after rain and grass drying finish.");
		}

		ImGui::Spacing();
		DrawComplexGrassDetectionThreshold();

		ImGui::Spacing();
		ImGui::Spacing();
	}

	if (page.Is("effects")) {
		Util::Widgets::SliderFloat(
			"SSS Amount",
			&settings.SubsurfaceScatteringAmount,
			kSubsurfaceScatteringAmountMin,
			kSubsurfaceScatteringAmountMax,
			"%.2f",
			ImGuiSliderFlags_AlwaysClamp);
		SanitizeSettings();
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Light transmission through grass: Soft Lighting evens out illumination; Back Lighting lights the back face. Values above 1 strengthen compatibility tuning for grass that looks too harsh.");
		}

		ImGui::Spacing();
		ImGui::Spacing();
	}

	if (page.Is("lighting")) {
		Util::UIntCheckbox("Wrapped Lighting for Vanilla Grass", settings.EnableWrappedLighting);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Restore legacy wrapped diffuse on vanilla/basic grass to soften the lit/back-face boundary. The newer lighting model stays active; complex grass is unchanged.");
		}
		ImGui::Spacing();
		ImGui::Spacing();

		Util::UIntCheckbox("Override Complex Grass Lighting Settings", settings.OverrideComplexGrassSettings);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Override authored complex-grass brightness with basic-grass settings. Useful when authors did not account for CSX's extra lights. This was the default before Community Shaders 0.7.0.");
		}
		ImGui::Spacing();
		ImGui::Spacing();
		ImGui::TextWrapped("Basic Grass");
		Util::Widgets::SliderFloat("Brightness", &settings.BasicGrassBrightness, 0.0f, 1.0f);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Darkens the grass textures to look better with the new lighting");
		}
	}
}

void GrassLighting::DrawPerformanceSettings(bool)
{
	const bool enabled = DrawEnabledCheckbox();
	ImGui::BeginDisabled(!enabled);
	DrawComplexGrassDetectionThreshold();
	ImGui::EndDisabled();
}

json GrassLighting::CapturePerformanceSettingsState() const
{
	return settings;
}

void GrassLighting::LoadSettings(json& o_json)
{
	settings = o_json;
	SanitizeSettings();
}

void GrassLighting::SaveSettings(json& o_json)
{
	SanitizeSettings();
	o_json = settings;
}

void GrassLighting::RestoreDefaultSettings()
{
	settings = {};
	SanitizeSettings();
}
