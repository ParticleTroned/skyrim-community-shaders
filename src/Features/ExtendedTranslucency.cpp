#include "ExtendedTranslucency.h"
#include "Menu/SettingsPage.h"
#include "Utils/UI.h"

#include "../ShaderCache.h"
#include "../State.h"
#include "../Util.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	ExtendedTranslucency::Settings,
	Enabled,
	AlphaMode,
	AlphaReduction,
	AlphaSoftness,
	AlphaStrength,
	SkinnedOnly);

const RE::BSFixedString ExtendedTranslucency::NiExtraDataName_AnisotropicAlphaMaterial = "AnisotropicAlphaMaterial";

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

	auto& property0 = pass->geometry->GetGeometryRuntimeData().alphaProperty;
	auto& property1 = pass->geometry->GetGeometryRuntimeData().shaderProperty;
	auto alphaProperty = property0 && property0->GetRTTI() == globals::rtti::NiAlphaPropertyRTTI.get() ? static_cast<RE::NiAlphaProperty*>(property0.get()) : nullptr;
	auto lightProperty = property1 && property1->GetRTTI() == globals::rtti::BSLightingShaderPropertyRTTI.get() ? static_cast<RE::BSLightingShaderProperty*>(property1.get()) : nullptr;

	// This effect only matters when alpha property exists and blending is enabled
	// Geometries with alpha < 1 have an implicit alpha blend property
	if (!(lightProperty && lightProperty->alpha < 0.999f) && (!alphaProperty || !alphaProperty->GetAlphaBlending())) {
		return;
	}

	const auto* data = pass->geometry->GetExtraData(NiExtraDataName_AnisotropicAlphaMaterial);
	if (!data) {
		// If there is no extra data for explicit settings, use the default material model from global user settings
		// And respect the SkinnedOnly setting
		const auto& feature = globals::features::extendedTranslucency;
		if (!feature.settings.SkinnedOnly || pass->geometry->GetGeometryRuntimeData().skinInstance != nullptr) {
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
														  { "appearance", "Material", "Choose the material response, then refine its appearance.", "Fabric, glass and edge response", true, true, "Choose the material response" },
													  });
	if (!page.Is("appearance"))
		return;

	ImGui::BeginDisabled(!settings.Enabled);

	{
		MenuUI::SectionHeading("Translucent Material");
		ImGui::PushID("Translucent Material");
		static constexpr MenuUI::Choice materialModels[]{
			{ "disabled", "0 - Disabled", "", "Keeps the original transparency without this angle-dependent material effect." },
			{ "rim", "1 - Rim Edge", "", "Makes edges more opaque when viewed at an angle, even on very transparent surfaces." },
			{ "fabric-glass", "2 - Fabric / Glass", "", "Varies transparency with viewing angle to suggest fabric threads or layered glass." },
			{ "woven", "3 - Woven Fabric", "", "Varies transparency across two crossing thread directions to suggest woven fabric." },
		};

		bool changed = false;
		int model = static_cast<int>(settings.AlphaMode);
		if (MenuUI::ChoiceSetting("Default Material Model", &model, materialModels)) {
			settings.AlphaMode = static_cast<uint32_t>(model);
			changed = true;
		}
		if (Util::Widgets::Checkbox("Animated bodies and clothing only", &settings.SkinnedOnly)) {
			changed = true;
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("On: limits the effect to animated bodies and clothing. Off: also affects other compatible objects. Enable if unrelated objects look wrong.");
		}

		if (Util::Widgets::SliderFloat("Transparency Increase", &settings.AlphaReduction, 0, 1.f)) {
			changed = true;
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Makes the material easier to see through. Increase this if the chosen material style looks too opaque.");
		}

		if (Util::Widgets::SliderFloat("Softness", &settings.AlphaSoftness, 0.0f, 1.0f)) {
			changed = true;
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Softens the added opacity from the selected material style. Higher values keep the material more transparent.");
		}

		if (Util::Widgets::SliderFloat("Blend Weight", &settings.AlphaStrength, 0.0f, 1.0f)) {
			changed = true;
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Set how strongly the effect blends into the final image.");
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

std::pair<std::string, std::vector<std::string>> ExtendedTranslucency::GetFeatureSummary()
{
	return {
		"Lets light pass through thin fabrics and other translucent materials.",
		{ "Multiple translucency material models (rim edge, isotropic/anisotropic fabric)",
			"Realistic fabric translucency with directional light transmission",
			"Per-material override support via NIF extra data",
			"Configurable transparency and softness controls",
			"Performance-optimized translucency calculations" }
	};
}
