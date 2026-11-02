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

void GrassLighting::SanitizeSettings(Settings& a_settings)
{
	a_settings.Glossiness = ClampGlossiness(a_settings.Glossiness, Settings{}.Glossiness);
	a_settings.SpecularStrength = ClampSpecularStrength(a_settings.SpecularStrength, Settings{}.SpecularStrength);
	a_settings.SubsurfaceScatteringAmount = ClampSubsurfaceScatteringAmount(
		a_settings.SubsurfaceScatteringAmount,
		Settings{}.SubsurfaceScatteringAmount);
	if (std::isfinite(a_settings.ComplexGrassThreshold)) {
		a_settings.ComplexGrassThreshold = std::clamp(
			a_settings.ComplexGrassThreshold,
			kComplexGrassThresholdMin,
			kComplexGrassThresholdMax);
	} else {
		a_settings.ComplexGrassThreshold = Settings{}.ComplexGrassThreshold;
	}
	a_settings.Enabled = a_settings.Enabled != 0;
}

void GrassLighting::SanitizeSettings()
{
	SanitizeSettings(settings);
}

bool GrassLighting::DrawEnabledCheckbox()
{
	SanitizeSettings();
	bool enabled = settings.Enabled != 0;
	if (ImGui::Checkbox("Enable", &enabled))
		settings.Enabled = enabled ? 1u : 0u;
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted("Enables enhanced grass lighting at runtime. Disable to use the basic grass lighting path without restarting.");
	return enabled;
}

void GrassLighting::DrawComplexGrassDetectionThreshold()
{
	ImGui::SliderFloat(
		"Complex Grass Detection",
		&settings.ComplexGrassThreshold,
		kComplexGrassThresholdMin,
		kComplexGrassThresholdMax,
		"%.3f",
		ImGuiSliderFlags_AlwaysClamp);
	SanitizeSettings();
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted("Controls how strictly non-PBR complex grass textures are detected. Lower values are more strict.");
		ImGui::TextUnformatted("Classifying fewer textures as complex can reduce enhanced normal and specular work, but may change their appearance.");
		ImGui::TextUnformatted("The performance effect depends on the grass textures currently visible.");
	}
}

void GrassLighting::DrawSettings()
{
	SanitizeSettings();

	MenuUI::SettingsPage page("GrassLighting", {
												   { "highlights", T("feature.grass_lighting.settings.highlights", "Highlights"), T("feature.grass_lighting.settings.choose_how_grass_reflects_direct_light", "Choose how grass reflects direct light."), T("feature.grass_lighting.settings.direct_light_shine", "Direct-light shine"), true, true, T("feature.grass_lighting.settings.shape_the_shared_look", "Shape the shared look") },
												   { "lighting", T("feature.grass_lighting.settings.lighting", "Lighting"), T("feature.grass_lighting.settings.refine_grass_colour_and_indirect_lighting", "Refine grass colour and indirect lighting."), T("feature.grass_lighting.settings.colour_and_indirect_light", "Colour and indirect light"), true, true, nullptr },
												   { "effects", T("feature.grass_lighting.settings.effects", "Effects"), T("feature.grass_lighting.settings.refine_grass_lighting_effects", "Refine grass lighting effects."), T("feature.grass_lighting.settings.additional_lighting_effects", "Additional lighting effects"), true, true, T("feature.grass_lighting.settings.refine_grass_effects", "Refine grass effects") },
											   });
	const auto controlsDisabled = Util::DisableGuard(!(settings.Enabled != 0));
	if (page.Is("highlights")) {
		ImGui::TextWrapped(T("feature.grass_lighting.settings.specular_highlights_for_complex_grass", "Specular highlights for complex grass"));
		Util::Widgets::SliderFloat(
			T("feature.grass_lighting.settings.glossiness", "Glossiness"),
			&settings.Glossiness,
			kGlossinessMin,
			kGlossinessMax,
			"%.0f",
			ImGuiSliderFlags_AlwaysClamp);
		SanitizeSettings();
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(
				T("feature.grass_lighting.settings.specular_highlight_glossiness_this_also_defines_the_dry_endpoint_for", "Specular highlight glossiness. This also defines the dry endpoint for Wetterness grass glossiness after rain and grass drying finish."));
		}

		Util::Widgets::SliderFloat(
			T("feature.grass_lighting.settings.specular_strength", "Specular Strength"),
			&settings.SpecularStrength,
			kSpecularStrengthMin,
			kSpecularStrengthMax,
			"%.2f",
			ImGuiSliderFlags_AlwaysClamp);
		SanitizeSettings();
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(
				T("feature.grass_lighting.settings.specular_highlight_strength_this_also_defines_the_dry_endpoint_for_w", "Specular highlight strength. This also defines the dry endpoint for Wetterness grass specular strength after rain and grass drying finish."));
		}

		ImGui::Spacing();
		DrawComplexGrassDetectionThreshold();

		ImGui::Spacing();
		ImGui::Spacing();
	}

	if (page.Is("effects")) {
		Util::Widgets::SliderFloat(
			T("feature.grass_lighting.settings.sss_amount", "SSS Amount"),
			&settings.SubsurfaceScatteringAmount,
			kSubsurfaceScatteringAmountMin,
			kSubsurfaceScatteringAmountMax,
			"%.2f",
			ImGuiSliderFlags_AlwaysClamp);
		SanitizeSettings();
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				T("feature.grass_lighting.settings.light_transmission_through_grass_soft_lighting_evens_out_illuminatio", "Light transmission through grass: Soft Lighting evens out illumination; Back Lighting lights the back face. Values above 1 strengthen compatibility tuning for grass that looks too harsh."));
		}

		ImGui::Spacing();
		ImGui::Spacing();
	}

	if (page.Is("lighting")) {
		Util::UIntCheckbox(T("feature.grass_lighting.settings.wrapped_lighting_for_vanilla_grass", "Wrapped Lighting for Vanilla Grass"), settings.EnableWrappedLighting);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				T("feature.grass_lighting.settings.restore_legacy_wrapped_diffuse_on_vanilla_basic_grass_to_soften_the_", "Restore legacy wrapped diffuse on vanilla/basic grass to soften the lit/back-face boundary. The newer lighting model stays active; complex grass is unchanged."));
		}
		ImGui::Spacing();
		ImGui::Spacing();

		Util::UIntCheckbox(T("feature.grass_lighting.settings.override_complex_grass_lighting_settings", "Override Complex Grass Lighting Settings"), settings.OverrideComplexGrassSettings);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				T("feature.grass_lighting.settings.override_authored_complex_grass_brightness_with_basic_grass_settings", "Override authored complex-grass brightness with basic-grass settings. Useful when authors did not account for CSX's extra lights. This was the default before Community Shaders 0.7.0."));
		}
		ImGui::Spacing();
		ImGui::Spacing();
		ImGui::TextWrapped(T("feature.grass_lighting.settings.basic_grass", "Basic Grass"));
		Util::Widgets::SliderFloat(T("feature.grass_lighting.settings.brightness", "Brightness"), &settings.BasicGrassBrightness, 0.0f, 1.0f);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(T("feature.grass_lighting.settings.darkens_the_grass_textures_to_look_better_with_the_new_lighting", "Darkens the grass textures to look better with the new lighting"));
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
	auto snapshot = settings;
	SanitizeSettings(snapshot);
	return snapshot;
}

void GrassLighting::LoadSettings(json& o_json)
{
	settings = o_json;
	SanitizeSettings();
}

void GrassLighting::SaveSettings(json& o_json)
{
	auto savedSettings = settings;
	SanitizeSettings(savedSettings);
	o_json = savedSettings;
}

void GrassLighting::RestoreDefaultSettings()
{
	settings = {};
	SanitizeSettings();
}
