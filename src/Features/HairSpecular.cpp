#include "HairSpecular.h"
#include "Menu/SettingsPage.h"
#include "Utils/UI.h"

#include "Utils/D3D.h"
#include <DirectXTex.h>

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
												  { "model", "Model", "Choose the hair lighting model first.", "Kajiya-Kay or Marschner", true, true, "Choose model and appearance" },
												  { "look", "Look", "Balance brightness, colour and shine.", "Brightness, colour and shine", true, true, nullptr },
												  { "highlights", "Highlights", "Refine the highlights along hair strands.", "Strand highlight direction", true, true, "Refine highlights and shadows" },
												  { "shadows", "Shadows", "Refine how hair shadows itself.", "Screen-space self-shadowing", true, true, nullptr },
											  });

	if (page.Is("model")) {
		MenuUI::ChoiceSetting("Hair Mode", (int*)&settings.HairMode, "Kajiya-Kay\0Marschner\0");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Choose hair shading:\nKajiya-Kay: empirical specular highlights.\nMarschner: more physical light interaction; transmission can look too bright without self-shadowing.\nBoth use anisotropic, tangent-based shading.");
		}
		ImGui::Spacing();
	}
	if (page.Is("look")) {
		Util::Widgets::SliderFloat("Glossiness", &settings.HairGlossiness, 0.0f, settings.HairMode == 0 ? 256.0f : 100.0f, "%.0f");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Hair glossiness: specular exponent in Kajiya-Kay; surface roughness in Marschner.");
		}
		Util::Widgets::SliderFloat("Specular Multiplier", &settings.SpecularMult, 0.0f, 10.0f, "%.2f");
		Util::Widgets::SliderFloat("Diffuse Multiplier", &settings.DiffuseMult, 0.0f, 10.0f, "%.2f");
		Util::Widgets::SliderFloat("Indirect Specular Multiplier", &settings.SpecularIndirectMult, 0.0f, 10.0f, "%.2f");
		Util::Widgets::SliderFloat("Indirect Diffuse Multiplier", &settings.DiffuseIndirectMult, 0.0f, 10.0f, "%.2f");
		Util::Widgets::SliderFloat("Hair Base Color Multiplier", &settings.BaseColorMult, 0.0f, 10.0f, "%.2f");
		Util::Widgets::SliderFloat("Hair Saturation", &settings.HairSaturation, 0.0f, 5.0f, "%.2f");
		Util::Widgets::SliderFloat("Transmission", &settings.Transmission, 0.0f, 1.0f, "%.2f");
		ImGui::Spacing();
	}
	if (page.Is("highlights")) {
		Util::UIntCheckbox("Enable Tangent Shift", settings.EnableTangentShift);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Vary strand highlights with a tangent-shift texture. Results depend on the hair model.");
		}
		if (settings.HairMode == 0) {
			Util::Widgets::SliderFloat("Primary Specular Tangent Shift", &settings.PrimaryTangentShift, -1.0f, 1.0f, "%.2f");
			Util::Widgets::SliderFloat("Secondary Specular Tangent Shift", &settings.SecondaryTangentShift, -1.0f, 1.0f, "%.2f");
		}
		ImGui::Spacing();
	}
	if (page.Is("shadows")) {
		Util::UIntCheckbox("Enable Screen-Space Self Shadow", settings.EnableSelfShadow);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Enable screen-space hair self-shadowing. Without it, Marschner transmission can look too bright.");
		}
		const auto shadowsDisabled = Util::DisableGuard(settings.EnableSelfShadow == 0);
		Util::Widgets::SliderFloat("Self Shadow Strength", &settings.SelfShadowStrength, 0.0f, 1.0f, "%.2f");
		Util::Widgets::SliderFloat("Self Shadow Exponent", &settings.SelfShadowExponent, 0.0f, 10.0f, "%.2f");
		Util::Widgets::SliderFloat("Self Shadow Scale", &settings.SelfShadowScale, 0.0f, 10.0f, "%.2f");
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
