#include "HairSpecular.h"
#include "Menu/SettingsPage.h"
#include "Utils/UI.h"

#include "../I18n/I18n.h"
#include "Utils/D3D.h"
#include <DirectXTex.h>

#define I18N_KEY_PREFIX "feature.hair_specular."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	HairSpecular::Settings,
	Enabled,
	HairGlossiness,
	SpecularMult,
	DiffuseMult,
	EnableTangentShift,
	PrimaryTangentShift,
	SecondaryTangentShift,
	HairSaturation,
	SpecularIndirectMult,
	DiffuseIndirectMult,
	BaseColorMult,
	Transmission,
	EnableSelfShadow,
	SelfShadowStrength,
	SelfShadowExponent,
	SelfShadowScale,
	HairMode)

void HairSpecular::DrawSettings()
{
	MenuUI::SettingsPage page("HairSpecular", {
												  { "model", T("feature.hair_specular.ui_model", "Model"), T("feature.hair_specular.ui_choose_the_hair_lighting_model_first", "Choose the hair lighting model first."), T("feature.hair_specular.ui_kajiya_kay_or_marschner", "Kajiya-Kay or Marschner"), true, true, T("feature.hair_specular.ui_choose_model_and_appearance", "Choose model and appearance") },
												  { "look", T("feature.hair_specular.ui_look", "Look"), T("feature.hair_specular.ui_balance_brightness_colour_and_shine", "Balance brightness, colour and shine."), T("feature.hair_specular.ui_brightness_colour_and_shine", "Brightness, colour and shine"), true, true, nullptr },
												  { "highlights", T("feature.grass_lighting.settings.highlights", "Highlights"), T("feature.hair_specular.ui_refine_the_highlights_along_hair_strands", "Refine the highlights along hair strands."), T("feature.hair_specular.ui_strand_highlight_direction", "Strand highlight direction"), true, true, T("feature.hair_specular.ui_refine_highlights_and_shadows", "Refine highlights and shadows") },
												  { "shadows", T("feature.hair_specular.ui_shadows", "Shadows"), T("feature.hair_specular.ui_refine_how_hair_shadows_itself", "Refine how hair shadows itself."), T("feature.hair_specular.ui_screen_space_self_shadowing", "Screen-space self-shadowing"), true, true, nullptr },
											  });

	if (page.Is("model")) {
		const char* hairModels[] = {
			T("feature.hair_specular.kajiya_kay", "Kajiya-Kay"),
			T("feature.hair_specular.marschner", "Marschner")
		};
		int hairMode = static_cast<int>(settings.HairMode);
		if (MenuUI::ChoiceSetting(T("feature.hair_specular.hair_mode", "Hair Mode"), &hairMode, hairModels, static_cast<int>(std::size(hairModels))))
			settings.HairMode = static_cast<uint>(std::clamp(hairMode, 0, 1));
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T("feature.hair_specular.ui_choose_hair_shading_kajiya_kay_empirical_specular_high_2d4604f9", "Choose hair shading:\nKajiya-Kay: empirical specular highlights.\nMarschner: more physical light interaction; transmission can look too bright without self-shadowing.\nBoth use anisotropic, tangent-based shading."));
		}
		ImGui::Spacing();
	}
	if (page.Is("look")) {
		Util::Widgets::SliderFloat(T("feature.hair_specular.glossiness", "Glossiness"), &settings.HairGlossiness, 0.0f, settings.HairMode == 0 ? 256.0f : 100.0f, "%.0f");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T("feature.hair_specular.ui_hair_glossiness_specular_exponent_in_kajiya_kay_surfac_4e4575a8", "Hair glossiness: specular exponent in Kajiya-Kay; surface roughness in Marschner."));
		}
		Util::Widgets::SliderFloat(T("feature.hair_specular.specular_multiplier", "Specular Multiplier"), &settings.SpecularMult, 0.0f, 10.0f, "%.2f");
		Util::Widgets::SliderFloat(T("feature.hair_specular.diffuse_multiplier", "Diffuse Multiplier"), &settings.DiffuseMult, 0.0f, 10.0f, "%.2f");
		Util::Widgets::SliderFloat(T("feature.hair_specular.indirect_specular_multiplier", "Indirect Specular Multiplier"), &settings.SpecularIndirectMult, 0.0f, 10.0f, "%.2f");
		Util::Widgets::SliderFloat(T("feature.hair_specular.indirect_diffuse_multiplier", "Indirect Diffuse Multiplier"), &settings.DiffuseIndirectMult, 0.0f, 10.0f, "%.2f");
		Util::Widgets::SliderFloat(T("feature.hair_specular.hair_base_color_multiplier", "Hair Base Color Multiplier"), &settings.BaseColorMult, 0.0f, 10.0f, "%.2f");
		Util::Widgets::SliderFloat(T("feature.hair_specular.hair_saturation", "Hair Saturation"), &settings.HairSaturation, 0.0f, 5.0f, "%.2f");
		Util::Widgets::SliderFloat(T("feature.hair_specular.transmission", "Transmission"), &settings.Transmission, 0.0f, 1.0f, "%.2f");
		ImGui::Spacing();
	}
	if (page.Is("highlights")) {
		Util::UIntCheckbox(T("feature.hair_specular.enable_tangent_shift", "Enable Tangent Shift"), settings.EnableTangentShift);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T("feature.hair_specular.ui_vary_strand_highlights_with_a_tangent_shift_texture_re_12ab14f0", "Vary strand highlights with a tangent-shift texture. Results depend on the hair model."));
		}
		if (settings.HairMode == 0) {
			Util::Widgets::SliderFloat(T("feature.hair_specular.primary_tangent_shift", "Primary Specular Tangent Shift"), &settings.PrimaryTangentShift, -1.0f, 1.0f, "%.2f");
			Util::Widgets::SliderFloat(T("feature.hair_specular.secondary_tangent_shift", "Secondary Specular Tangent Shift"), &settings.SecondaryTangentShift, -1.0f, 1.0f, "%.2f");
		}
		ImGui::Spacing();
	}
	if (page.Is("shadows")) {
		Util::UIntCheckbox(T("feature.hair_specular.enable_self_shadow", "Enable Screen-Space Self Shadow"), settings.EnableSelfShadow);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T("feature.hair_specular.ui_enable_screen_space_hair_self_shadowing_without_it_mar_23b2677d", "Enable screen-space hair self-shadowing. Without it, Marschner transmission can look too bright."));
		}
		const auto shadowsDisabled = Util::DisableGuard(settings.EnableSelfShadow == 0);
		Util::Widgets::SliderFloat(T("feature.hair_specular.self_shadow_strength", "Self Shadow Strength"), &settings.SelfShadowStrength, 0.0f, 1.0f, "%.2f");
		Util::Widgets::SliderFloat(T("feature.hair_specular.self_shadow_exponent", "Self Shadow Exponent"), &settings.SelfShadowExponent, 0.0f, 10.0f, "%.2f");
		Util::Widgets::SliderFloat(T("feature.hair_specular.self_shadow_scale", "Self Shadow Scale"), &settings.SelfShadowScale, 0.0f, 10.0f, "%.2f");
	}
}

void HairSpecular::LoadSettings(json& o_json)
{
	settings = o_json;
}

void HairSpecular::SaveSettings(json& o_json)
{
	o_json = settings;
}

void HairSpecular::RestoreDefaultSettings()
{
	settings = {};
}

void HairSpecular::SetupResources()
{
	auto device = globals::d3d::device;

	logger::debug("Loading Hair Tangent Shift Texture...");
	{
		DirectX::ScratchImage image;
		try {
			std::filesystem::path path = "Data\\Shaders\\Hair\\TangentShift.dds";

			DX::ThrowIfFailed(LoadFromDDSFile(path.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image));
		} catch (const DX::com_exception& e) {
			logger::error("{}", e.what());
			return;
		}

		ID3D11Resource* pResource = nullptr;
		try {
			DX::ThrowIfFailed(CreateTexture(device,
				image.GetImages(), image.GetImageCount(),
				image.GetMetadata(), &pResource));
		} catch (const DX::com_exception& e) {
			logger::error("{}", e.what());
			return;
		}

		texTangentShift = eastl::make_unique<Texture2D>(reinterpret_cast<ID3D11Texture2D*>(pResource), "HairSpecular::TangentShift");

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texTangentShift->desc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = 10 }
		};
		texTangentShift->CreateSRV(srvDesc);
	}
}

void HairSpecular::Prepass()
{
	auto context = globals::d3d::context;

	if (texTangentShift) {
		ID3D11ShaderResourceView* srv = texTangentShift->srv.get();
		context->PSSetShaderResources(73, 1, &srv);
	}
}

#undef I18N_KEY_PREFIX
