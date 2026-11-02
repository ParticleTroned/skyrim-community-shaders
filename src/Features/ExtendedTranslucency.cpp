#include "ExtendedTranslucency.h"
#include "Menu/SettingsPage.h"
#include "Utils/UI.h"

#include "../I18n/I18n.h"
#include "../ShaderCache.h"
#include "../State.h"
#include "../Util.h"

#define I18N_KEY_PREFIX "feature.extended_translucency."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	ExtendedTranslucency::Settings,
	Enabled,
	AlphaMode,
	AlphaReduction,
	AlphaSoftness,
	AlphaStrength,
	SkinnedOnly);

const RE::BSFixedString ExtendedTranslucency::NiExtraDataName_AnisotropicAlphaMaterial = "AnisotropicAlphaMaterial";

namespace
{
	void DrawEnabledCheckbox(ExtendedTranslucency::Settings& a_settings)
	{
		ImGui::Checkbox(T("feature.extended_translucency.enable", "Enable"), &a_settings.Enabled);
	}
}

void ExtendedTranslucency::BSLightingShader_SetupGeometry(RE::BSRenderPass* pass)
{
	auto SetFeatureDescriptor = [](int material) {
		auto& descriptor = globals::state->permutationData.ExtraFeatureDescriptor;
		static constexpr int mask = ExtraFeatureDescriptorMask << ExtraFeatureDescriptorShift;
		static constexpr int shift = ExtraFeatureDescriptorShift;
		descriptor = (descriptor & ~mask) | (material << shift);
	};

	// Clear the ExtraFeatureDescriptor to disable this effect on default
	SetFeatureDescriptor(MaterialModel::DescriptorDisabled);
	if (!globals::features::extendedTranslucency.settings.Enabled)
		return;

	if (!pass || !pass->geometry) {
		return;
	}

	auto* geometry = pass->geometry;
	auto& runtimeData = geometry->GetGeometryRuntimeData();
	auto& property0 = runtimeData.alphaProperty;
	auto& property1 = runtimeData.shaderProperty;
	auto alphaProperty = property0 && property0->GetRTTI() == globals::rtti::NiAlphaPropertyRTTI.get() ? static_cast<RE::NiAlphaProperty*>(property0.get()) : nullptr;
	auto lightProperty = property1 && property1->GetRTTI() == globals::rtti::BSLightingShaderPropertyRTTI.get() ? static_cast<RE::BSLightingShaderProperty*>(property1.get()) : nullptr;

	// This effect only matters when alpha property exists and blending is enabled
	// Geometries with alpha < 1 have an implicit alpha blend property
	if (!(lightProperty && lightProperty->alpha < 0.999f) && (!alphaProperty || !alphaProperty->GetAlphaBlending())) {
		return;
	}

	const auto* data = geometry->GetExtraData(NiExtraDataName_AnisotropicAlphaMaterial);
	if (!data) {
		// If there is no extra data for explicit settings, use the default material model from global user settings
		// And respect the SkinnedOnly setting
		const auto& feature = globals::features::extendedTranslucency;
		if (!feature.settings.SkinnedOnly || runtimeData.skinInstance != nullptr) {
			SetFeatureDescriptor(MaterialModel::DescriptorUseDefault);
		}
	} else {
		// Read explicit material model from extra data
		if (data->GetRTTI() == globals::rtti::NiIntegerExtraDataRTTI.get()) {
			uint32_t material = static_cast<uint32_t>(static_cast<const RE::NiIntegerExtraData*>(data)->value) & ExtraFeatureDescriptorMask;
			// Promote `Disabled` in settings to `DescriptorDisabled` in shader
			material = material == MaterialModel::Disabled ? MaterialModel::DescriptorDisabled : material;
			SetFeatureDescriptor(material);
		} else {
			// logging is too expensive here, treat type error as disable, should only happen for modders
		}
	}
}

struct ExtendedTranslucency::Hooks
{
	struct BSLightingShader_SetupGeometry
	{
		static void thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
		{
			ExtendedTranslucency::BSLightingShader_SetupGeometry(Pass);
			func(This, Pass, RenderFlags);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	static void Install()
	{
		stl::write_vfunc<0x6, BSLightingShader_SetupGeometry>(RE::VTABLE_BSLightingShader[0]);
		logger::info("[ExtendedTranslucency] Installed hooks - BSLightingShader_SetupGeometry");
	}
};

void ExtendedTranslucency::PostPostLoad()
{
	Hooks::Install();
}

void ExtendedTranslucency::DrawSettings()
{
	MenuUI::SettingsPage page("ExtendedTranslucency", {
														  { "appearance", T("feature.extended_translucency.ui_material", "Material"), T("feature.extended_translucency.ui_choose_the_material_response_then_refine_its_appearance", "Choose the material response, then refine its appearance."), T("feature.extended_translucency.ui_fabric_glass_and_edge_response", "Fabric, glass and edge response"), true, true, T("feature.extended_translucency.ui_choose_the_material_response", "Choose the material response") },
													  });
	if (!page.Is("appearance"))
		return;

	ImGui::BeginDisabled(!settings.Enabled);

	{
		MenuUI::SectionHeading(T("feature.extended_translucency.translucent_material", "Translucent Material"));
		ImGui::PushID("Translucent Material");
		const char* AlphaModeNames[] = {
			T("feature.extended_translucency.alpha_mode_disabled", "0 - Disabled"),
			T("feature.extended_translucency.alpha_mode_rim_edge", "1 - Rim Edge"),
			T("feature.extended_translucency.alpha_mode_isotropic_fabric", "2 - Isotropic Fabric, Glass, ..."),
			T("feature.extended_translucency.alpha_mode_anisotropic_fabric", "3 - Anisotropic Fabric"),
		};

		static constexpr int AlphaModeSize = static_cast<int>(std::size(AlphaModeNames));

		MenuUI::ChoiceSetting(T("feature.extended_translucency.default_material_model", "Default Material Model"), (int*)&settings.AlphaMode, AlphaModeNames, AlphaModeSize);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T("feature.extended_translucency.ui_opacity_varies_with_viewing_angle_disabled_flat_alpha_a3f5a1fb", "Opacity varies with viewing angle:\n- Disabled: flat alpha.\n- Rim Edge: nonphysical rim; edges stay opaque even at full transparency.\n- Isotropic Fabric: single-direction threads using the normal map; also suits layered glass.\n- Anisotropic Fabric: tangent/bitangent weave; ignores the normal map."));
		}
		Util::Widgets::Checkbox(T("feature.extended_translucency.skinned_mesh_only", "Skinned Mesh Only"), &settings.SkinnedOnly);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T("feature.extended_translucency.ui_apply_only_to_skinned_meshes_enable_if_unrelated_objec_8d3d34e3", "Apply only to skinned meshes. Enable if unrelated objects show unwanted effects."));
		}

		Util::Widgets::SliderFloat(T("feature.extended_translucency.transparency_increase", "Transparency Increase"), &settings.AlphaReduction, 0, 1.f);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T("feature.extended_translucency.ui_translucency_can_make_materials_more_opaque_than_inten_59b67b73", "Translucency can make materials more opaque than intended. Reduce alpha to compensate and widen the output's dynamic range."));
		}

		Util::Widgets::SliderFloat(T("feature.extended_translucency.softness", "Softness"), &settings.AlphaSoftness, 0.0f, 1.0f);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T("feature.extended_translucency.ui_soften_the_alpha_increase_higher_softness_reduces_the_936589dc", "Soften the alpha increase. Higher softness reduces the added opacity."));
		}

		Util::Widgets::SliderFloat(T("feature.extended_translucency.blend_weight", "Blend Weight"), &settings.AlphaStrength, 0.0f, 1.0f);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T("feature.extended_translucency.ui_set_how_strongly_the_effect_blends_into_the_final_image", "Set how strongly the effect blends into the final image."));
		}

		ImGui::Spacing();
		ImGui::Spacing();
		ImGui::PopID();
	}

	ImGui::EndDisabled();
}

void ExtendedTranslucency::LoadSettings(json& o_json)
{
	settings = o_json;
}

void ExtendedTranslucency::SaveSettings(json& o_json)
{
	o_json = settings;
}

void ExtendedTranslucency::RestoreDefaultSettings()
{
	settings = {};
}

#undef I18N_KEY_PREFIX
