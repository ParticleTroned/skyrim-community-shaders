#include "ExtendedMaterials.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	ExtendedMaterials::Settings,
	EnableComplexMaterial,
	EnableParallax,
	EnableTerrain,
	EnableHeightBlending,
	EnableShadows,
	EnableParallaxWarpingFix)

void ExtendedMaterials::SanitizeSettings(Settings& a_settings)
{
	a_settings.EnableComplexMaterial = a_settings.EnableComplexMaterial != 0;
	a_settings.EnableParallax = a_settings.EnableParallax != 0;
	a_settings.EnableTerrain = a_settings.EnableTerrain != 0;
	a_settings.EnableHeightBlending = a_settings.EnableHeightBlending != 0;
	a_settings.EnableShadows = a_settings.EnableShadows != 0;
	a_settings.EnableParallaxWarpingFix = a_settings.EnableParallaxWarpingFix != 0;
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
	if (ImGui::TreeNodeEx("Complex Material")) {
		Util::UIntCheckbox("Enable Complex Material", settings.EnableComplexMaterial);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Enables support for the Complex Material specification which makes use of the environment mask. "
				"This includes parallax, as well as more realistic metals and specular reflections. "
				"May lead to some warped textures on modded content which have an invalid alpha channel in their environment mask. ");
		}

		ImGui::Spacing();
		ImGui::Spacing();
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Parallax")) {
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
				"Enables terrain parallax using the alpha channel of each landscape texture. "
				"Therefore, all landscape textures must support parallax for this effect to work properly. ");
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
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Approximate Soft Shadows")) {
		Util::UIntCheckbox("Enable Shadows", settings.EnableShadows);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Enables cheap soft shadows when using parallax. "
				"This applies to all directional and point lights. ");
		}
		ImGui::Spacing();
		ImGui::Spacing();
		ImGui::TreePop();
	}
}

void ExtendedMaterials::DrawEssentialSettings()
{
	SanitizeSettings(settings);
	Util::UIntCheckbox("Enable Complex Material", settings.EnableComplexMaterial);
	Util::UIntCheckbox("Enable Parallax", settings.EnableParallax);
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
		{ "EnableParallaxWarpingFix", settings.EnableParallaxWarpingFix != 0 }
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
