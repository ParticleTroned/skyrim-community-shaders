#include "GrassLighting.h"

#include <algorithm>
#include <cmath>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	GrassLighting::Settings,
	Glossiness,
	SpecularStrength,
	SubsurfaceScatteringAmount,
	OverrideComplexGrassSettings,
	BasicGrassBrightness,
	ComplexGrassThreshold)

float GrassLighting::ClampGlossiness(float glossiness, float fallback)
{
	if (!std::isfinite(glossiness)) {
		return fallback;
	}
	return std::clamp(glossiness, kGlossinessMin, kGlossinessMax);
}

void GrassLighting::SanitizeSettings()
{
	settings.Glossiness = ClampGlossiness(settings.Glossiness, Settings{}.Glossiness);
}

void GrassLighting::DrawSettings()
{
	SanitizeSettings();

	if (ImGui::TreeNodeEx("Complex Grass", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::TextWrapped("Specular highlights for complex grass");
		ImGui::SliderFloat(
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

		ImGui::SliderFloat("Specular Strength", &settings.SpecularStrength, 0.0f, 1.0f);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Specular highlight strength.");
		}

		ImGui::Spacing();
		ImGui::TextWrapped("Complex Grass Detection");
		ImGui::SliderFloat("Detection Threshold", &settings.ComplexGrassThreshold, 0.001f, 0.1f, "%.3f");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Threshold for detecting complex grass textures. Lower values are more strict.");
		}

		ImGui::Spacing();
		ImGui::Spacing();
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Effects", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderFloat("SSS Amount", &settings.SubsurfaceScatteringAmount, 0.0f, 1.0f);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Subsurface Scattering (SSS) amount. "
				"Soft lighting controls how evenly lit an object is. "
				"Back lighting illuminates the back face of an object. "
				"Combined to model the transport of light through the surface. ");
		}

		ImGui::Spacing();
		ImGui::Spacing();
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Override Complex Grass Lighting Settings", (bool*)&settings.OverrideComplexGrassSettings);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Override the settings set by the grass mesh author. "
				"Complex grass authors can define the brightness for their grass meshes. "
				"However, some authors may not account for the extra lights available from Community Shaders. "
				"This option will treat their grass settings like non-complex grass. "
				"This was the default in Community Shaders < 0.7.0");
		}
		ImGui::Spacing();
		ImGui::Spacing();
		ImGui::TextWrapped("Basic Grass");
		ImGui::SliderFloat("Brightness", &settings.BasicGrassBrightness, 0.0f, 1.0f);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Darkens the grass textures to look better with the new lighting");
		}

		ImGui::TreePop();
	}
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
