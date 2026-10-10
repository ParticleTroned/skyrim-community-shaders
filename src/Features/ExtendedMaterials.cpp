#include "ExtendedMaterials.h"
#include "Menu/SettingsPage.h"
#include "Utils/UI.h"

#include "Utils/Finite.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	ExtendedMaterials::Settings,
	EnableComplexMaterial,
	EnableParallax,
	EnableTerrain,
	EnableHeightBlending,
	EnableShadows,
	EnableParallaxWarpingFix,
	ParallaxStrength)

void ExtendedMaterials::SanitizeSettings(Settings& a_settings)
{
	a_settings.EnableComplexMaterial = a_settings.EnableComplexMaterial != 0;
	a_settings.EnableParallax = a_settings.EnableParallax != 0;
	a_settings.EnableTerrain = a_settings.EnableTerrain != 0;
	a_settings.EnableHeightBlending = a_settings.EnableHeightBlending != 0;
	a_settings.EnableShadows = a_settings.EnableShadows != 0;
	a_settings.EnableParallaxWarpingFix = a_settings.EnableParallaxWarpingFix != 0;
	a_settings.ParallaxStrength = Util::ClampFinite(a_settings.ParallaxStrength,
		kMinParallaxStrength, kMaxParallaxStrength, Settings{}.ParallaxStrength);
}

void ExtendedMaterials::DrawParallaxStrength()
{
	if (Util::Widgets::SliderFloat("Parallax Strength", &settings.ParallaxStrength,
			kMinParallaxStrength, kMaxParallaxStrength, "%.2f", ImGuiSliderFlags_AlwaysClamp))
		SanitizeSettings(settings);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted(
			"Scale depth on supported meshes and terrain, including TruePBR. 1 keeps authored depth; 0 disables parallax and its shadows but keeps terrain height blending. Higher may stretch textures at shallow angles. Water has its own control.");
	}
}

void ExtendedMaterials::DataLoaded()
{
	if (settings.EnableTerrain && globals::game::iniSettingCollection) {
		if (auto bLandSpecular = globals::game::iniSettingCollection->GetSetting("bLandSpecular:Landscape"); bLandSpecular) {
			if (!bLandSpecular->data.b) {
				logger::info("[CPM] Changing bLandSpecular from {} to {} to support Terrain Parallax", bLandSpecular->data.b, true);
				bLandSpecular->data.b = true;
			}
		}
	}
}

void ExtendedMaterials::DrawSettings()
{
	SanitizeSettings(settings);

	MenuUI::SettingsPage page("ExtendedMaterials", {
													   { "materials", "Materials", "Choose which material features are enabled.", "Complex surfaces and reflections", true, true, "Choose surface detail" },
													   { "depth", "Depth", "Adjust surface depth and terrain blending.", "Parallax and terrain transitions", true, true, nullptr },
													   { "shadows", "Shadows", "Choose shadows for raised surface detail.", "Raised-detail shadows", true, true, "Refine surface lighting" },
												   });
	if (page.Is("materials")) {
		Util::UIntCheckbox("Enable Complex Material", settings.EnableComplexMaterial);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Enable Complex Material support through environment masks: parallax, metals and specular reflections. Invalid mask alpha on modded content may warp textures.");
		}

		ImGui::Spacing();
		ImGui::Spacing();
	}

	if (page.Is("depth")) {
		DrawParallaxStrength();
		Util::UIntCheckbox("Enable Parallax", settings.EnableParallax);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Enables parallax on standard meshes made for parallax.");
		}

		if (Util::UIntCheckbox("Enable Legacy Terrain", settings.EnableTerrain)) {
			if (settings.EnableTerrain) {
				DataLoaded();
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Enable terrain parallax from landscape texture alpha. All landscape textures must support parallax.");
		}
		Util::UIntCheckbox("Enable Terrain Height Blending", settings.EnableHeightBlending);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Enables landscape texture blending based on parallax. ");
		}
		Util::UIntCheckbox("Enable Parallax Warping Fix", settings.EnableParallaxWarpingFix);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Enables a fix reducing parallax scale on curved and smooth normal triangles.");
		}

		ImGui::Spacing();
		ImGui::Spacing();
	}

	if (page.Is("shadows")) {
		Util::UIntCheckbox("Enable Shadows", settings.EnableShadows);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Add inexpensive soft parallax shadows for all directional and point lights.");
		}
		ImGui::Spacing();
		ImGui::Spacing();
	}
}

void ExtendedMaterials::DrawPerformanceSettings(bool)
{
	SanitizeSettings(settings);
	Util::UIntCheckbox("Enable Complex Material", settings.EnableComplexMaterial);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted("Controls complex-material texture sampling and shading.");
	}

	Util::UIntCheckbox("Enable Parallax", settings.EnableParallax);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted("Controls parallax occlusion mapping on supported meshes.");
	}
	DrawParallaxStrength();

	if (Util::UIntCheckbox("Enable Legacy Terrain", settings.EnableTerrain)) {
		if (settings.EnableTerrain)
			DataLoaded();
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted("Controls parallax sampling on compatible landscape textures.");
	}

	const bool terrainPathEnabled = settings.EnableParallax != 0 || settings.EnableTerrain != 0;
	ImGui::BeginDisabled(!terrainPathEnabled);
	Util::UIntCheckbox("Enable Terrain Height Blending", settings.EnableHeightBlending);
	ImGui::EndDisabled();
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted("Controls additional height sampling and blending on parallax-enabled terrain.");
	}

	const bool parallaxPathEnabled =
		settings.EnableComplexMaterial != 0 || settings.EnableParallax != 0 || settings.EnableTerrain != 0;
	ImGui::BeginDisabled(!parallaxPathEnabled);
	Util::UIntCheckbox("Enable Parallax Shadows", settings.EnableShadows);
	ImGui::EndDisabled();
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted("Controls the additional soft-shadow work performed by parallax materials.");
	}

	Util::UIntCheckbox("Enable Parallax Warping Fix", settings.EnableParallaxWarpingFix);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted("Controls derivative-based curvature correction on extended-material geometry.");
	}
}

json ExtendedMaterials::CapturePerformanceSettingsState() const
{
	return {
		{ "EnableComplexMaterial", settings.EnableComplexMaterial != 0 },
		{ "EnableParallax", settings.EnableParallax != 0 },
		{ "EnableTerrain", settings.EnableTerrain != 0 },
		{ "EnableHeightBlending", settings.EnableHeightBlending != 0 },
		{ "EnableShadows", settings.EnableShadows != 0 },
		{ "EnableParallaxWarpingFix", settings.EnableParallaxWarpingFix != 0 },
		{ "ParallaxStrength", settings.ParallaxStrength }
	};
}

bool ExtendedMaterials::IsPerformanceCostMeasurementEnabled() const
{
	return settings.EnableComplexMaterial != 0 ||
	       settings.EnableParallax != 0 ||
	       settings.EnableTerrain != 0 ||
	       settings.EnableParallaxWarpingFix != 0;
}

void ExtendedMaterials::SetPerformanceCostMeasurementEnabled(bool a_enabled)
{
	const Settings defaults{};
	if (a_enabled) {
		settings.EnableComplexMaterial = defaults.EnableComplexMaterial;
		settings.EnableParallax = defaults.EnableParallax;
		settings.EnableTerrain = defaults.EnableTerrain;
		settings.EnableHeightBlending = defaults.EnableHeightBlending;
		settings.EnableShadows = defaults.EnableShadows;
		settings.EnableParallaxWarpingFix = defaults.EnableParallaxWarpingFix;
		SanitizeSettings(settings);
		return;
	}

	settings.EnableComplexMaterial = 0;
	settings.EnableParallax = 0;
	settings.EnableTerrain = 0;
	settings.EnableHeightBlending = 0;
	settings.EnableShadows = 0;
	settings.EnableParallaxWarpingFix = 0;
}

json ExtendedMaterials::CapturePerformanceCostMeasurementState() const
{
	return settings;
}

void ExtendedMaterials::RestorePerformanceCostMeasurementState(const json& a_state)
{
	if (!a_state.is_object())
		return;

	settings = a_state.get<Settings>();
	SanitizeSettings(settings);
	if (settings.EnableTerrain)
		DataLoaded();
}

void ExtendedMaterials::LoadSettings(json& o_json)
{
	settings = o_json;
	SanitizeSettings(settings);
}

void ExtendedMaterials::SaveSettings(json& o_json)
{
	SanitizeSettings(settings);
	o_json = settings;
}

void ExtendedMaterials::RestoreDefaultSettings()
{
	settings = {};
}

bool ExtendedMaterials::HasShaderDefine(RE::BSShader::Type shaderType)
{
	switch (shaderType) {
	case RE::BSShader::Type::Lighting:
		return true;
	default:
		return false;
	}
}
