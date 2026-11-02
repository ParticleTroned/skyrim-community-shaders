#include "ExtendedMaterials.h"
#include "../I18n/I18n.h"
#include "Menu/SettingsPage.h"
#include "Utils/Finite.h"
#include "Utils/UI.h"

#define I18N_KEY_PREFIX "feature.extended_materials."

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

void ExtendedMaterials::DrawParallaxStrength()
{
	if (Util::Widgets::SliderFloat(T("feature.extended_materials.parallax_strength", "Parallax Strength"), &settings.ParallaxStrength,
			kMinParallaxStrength, kMaxParallaxStrength, "%.2f", ImGuiSliderFlags_AlwaysClamp))
		SanitizeSettings(settings);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted(
			T("feature.extended_materials.scale_depth_on_supported_meshes_and_terrain_i_17d8c030", "Scale depth on supported meshes and terrain, including TruePBR. 1 keeps authored depth; 0 disables parallax and its shadows but keeps terrain height blending. Higher may stretch textures at shallow angles. Water has its own control."));
	}
}

void ExtendedMaterials::DrawSettings()
{
	SanitizeSettings(settings);

	MenuUI::SettingsPage page("ExtendedMaterials", {
													   { "materials", T("feature.category.materials", "Materials"), T("feature.extended_materials.ui_choose_which_material_features_are_enabled", "Choose which material features are enabled."), T("feature.extended_materials.ui_complex_surfaces_and_reflections", "Complex surfaces and reflections"), true, true, T("feature.extended_materials.ui_choose_surface_detail", "Choose surface detail") },
													   { "depth", T("feature.extended_materials.ui_depth", "Depth"), T("feature.extended_materials.ui_adjust_surface_depth_and_terrain_blending", "Adjust surface depth and terrain blending."), T("feature.extended_materials.ui_parallax_and_terrain_transitions", "Parallax and terrain transitions"), true, true, nullptr },
													   { "shadows", T("feature.extended_materials.ui_shadows", "Shadows"), T("feature.extended_materials.ui_choose_shadows_for_raised_surface_detail", "Choose shadows for raised surface detail."), T("feature.extended_materials.ui_raised_detail_shadows", "Raised-detail shadows"), true, true, T("feature.extended_materials.ui_refine_surface_lighting", "Refine surface lighting") },
												   });
	if (page.Is("materials")) {
		Util::UIntCheckbox(T("feature.extended_materials.enable_complex_material", "Enable Complex Material"), settings.EnableComplexMaterial);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T("feature.extended_materials.ui_enable_complex_material_support_through_environment_ma_0278cbb5", "Enable Complex Material support through environment masks: parallax, metals and specular reflections. Invalid mask alpha on modded content may warp textures."));
		}

		ImGui::Spacing();
		ImGui::Spacing();
	}

	if (page.Is("depth")) {
		DrawParallaxStrength();
		Util::UIntCheckbox(T("feature.extended_materials.enable_parallax", "Enable Parallax"), settings.EnableParallax);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T("feature.extended_materials.enable_parallax_tooltip", "Enables parallax on standard meshes made for parallax."));
		}

		if (Util::UIntCheckbox(T("feature.extended_materials.enable_legacy_terrain", "Enable Legacy Terrain"), settings.EnableTerrain)) {
			if (settings.EnableTerrain) {
				DataLoaded();
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T("feature.extended_materials.ui_enable_terrain_parallax_from_landscape_texture_alpha_a_832955d0", "Enable terrain parallax from landscape texture alpha. All landscape textures must support parallax."));
		}
		Util::UIntCheckbox(T("feature.extended_materials.enable_height_blending", "Enable Terrain Height Blending"), settings.EnableHeightBlending);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T("feature.extended_materials.enable_height_blending_tooltip", "Enables landscape texture blending based on parallax. "));
		}
		Util::UIntCheckbox(T("feature.extended_materials.enable_parallax_warping_fix", "Enable Parallax Warping Fix"), settings.EnableParallaxWarpingFix);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T("feature.extended_materials.enable_parallax_warping_fix_tooltip", "Enables a fix reducing parallax scale on curved and smooth normal triangles."));
		}

		ImGui::Spacing();
		ImGui::Spacing();
	}

	if (page.Is("shadows")) {
		Util::UIntCheckbox(T("feature.extended_materials.enable_shadows", "Enable Shadows"), settings.EnableShadows);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T("feature.extended_materials.ui_add_inexpensive_soft_parallax_shadows_for_all_directio_867fdf8a", "Add inexpensive soft parallax shadows for all directional and point lights."));
		}
		ImGui::Spacing();
		ImGui::Spacing();
	}
}

#undef I18N_KEY_PREFIX

void ExtendedMaterials::DrawPerformanceSettings(bool)
{
	SanitizeSettings(settings);
	Util::UIntCheckbox(T("feature.extended_materials.enable_complex_material", "Enable Complex Material"), settings.EnableComplexMaterial);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted(T("feature.extended_materials.controls_complex_material_texture_sampling_and_shading", "Controls complex-material texture sampling and shading."));
	}

	Util::UIntCheckbox(T("feature.extended_materials.enable_parallax", "Enable Parallax"), settings.EnableParallax);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted(T("feature.extended_materials.controls_parallax_occlusion_mapping_on_supported_meshes", "Controls parallax occlusion mapping on supported meshes."));
	}

	if (Util::UIntCheckbox(T("feature.extended_materials.enable_legacy_terrain", "Enable Legacy Terrain"), settings.EnableTerrain)) {
		if (settings.EnableTerrain)
			DataLoaded();
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted(T("feature.extended_materials.controls_parallax_sampling_on_compatible_land_582e7059", "Controls parallax sampling on compatible landscape textures."));
	}

	const bool terrainPathEnabled = settings.EnableParallax != 0 || settings.EnableTerrain != 0;
	ImGui::BeginDisabled(!terrainPathEnabled);
	Util::UIntCheckbox(T("feature.extended_materials.enable_height_blending", "Enable Terrain Height Blending"), settings.EnableHeightBlending);
	ImGui::EndDisabled();
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted(T("feature.extended_materials.controls_additional_height_sampling_and_blend_9f603518", "Controls additional height sampling and blending on parallax-enabled terrain."));
	}

	const bool parallaxPathEnabled =
		settings.EnableComplexMaterial != 0 || settings.EnableParallax != 0 || settings.EnableTerrain != 0;
	ImGui::BeginDisabled(!parallaxPathEnabled);
	Util::UIntCheckbox(T("feature.extended_materials.enable_parallax_shadows", "Enable Parallax Shadows"), settings.EnableShadows);
	ImGui::EndDisabled();
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted(T("feature.extended_materials.controls_the_additional_soft_shadow_work_perf_479297bb", "Controls the additional soft-shadow work performed by parallax materials."));
	}

	Util::UIntCheckbox(T("feature.extended_materials.enable_parallax_warping_fix", "Enable Parallax Warping Fix"), settings.EnableParallaxWarpingFix);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted(T("feature.extended_materials.controls_derivative_based_curvature_correctio_6561ac2f", "Controls derivative-based curvature correction on extended-material geometry."));
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
