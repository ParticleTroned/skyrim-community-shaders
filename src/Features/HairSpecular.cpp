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
		static constexpr MenuUI::Choice models[]{
			{ "kajiya-kay", "Kajiya-Kay", "", "Uses simple highlights along hair strands." },
			{ "marschner", "Marschner", "", "Adds richer shine and light passing through hair. Enable self-shadowing if it looks too bright." }
		};
		int mode = static_cast<int>(settings.HairMode);
		if (MenuUI::ChoiceSetting("Hair Mode", &mode, models))
			settings.HairMode = static_cast<uint>(mode);
		ImGui::Spacing();
	}
	if (page.Is("look")) {
		Util::Widgets::SliderFloat("Glossiness", &settings.HairGlossiness, 0.0f, settings.HairMode == 0 ? 256.0f : 100.0f, "%.0f");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Higher values give tighter, sharper hair highlights. Lower values spread them into a softer shine.");
		}
		Util::Widgets::SliderFloat("Specular Multiplier", &settings.SpecularMult, 0.0f, 10.0f, "%.2f");
		Util::AddTooltip("Brightness of highlights from direct lights. Higher values make hair shine more.");
		Util::Widgets::SliderFloat("Diffuse Multiplier", &settings.DiffuseMult, 0.0f, 10.0f, "%.2f");
		Util::AddTooltip("Brightness of the hair beneath its highlights under direct light.");
		Util::Widgets::SliderFloat("Indirect Specular Multiplier", &settings.SpecularIndirectMult, 0.0f, 10.0f, "%.2f");
		Util::AddTooltip("Strength of hair shine from its surroundings, rather than direct lights.");
		Util::Widgets::SliderFloat("Indirect Diffuse Multiplier", &settings.DiffuseIndirectMult, 0.0f, 10.0f, "%.2f");
		Util::AddTooltip("Brightness of hair in ambient light. Higher values lighten shaded hair.");
		Util::Widgets::SliderFloat("Hair Base Color Multiplier", &settings.BaseColorMult, 0.0f, 10.0f, "%.2f");
		Util::AddTooltip("Brightness of the underlying hair color. 1 keeps the texture color; lower values darken it.");
		Util::Widgets::SliderFloat("Hair Saturation", &settings.HairSaturation, 0.0f, 5.0f, "%.2f");
		Util::AddTooltip("Strength of hair color. 0 removes color; values above 1 make it more vivid.");
		Util::Widgets::SliderFloat("Transmission", &settings.Transmission, 0.0f, 1.0f, "%.2f");
		Util::AddTooltip("Amount of light that passes through hair. Higher values make backlit strands brighter.");
		ImGui::Spacing();
	}
	if (page.Is("highlights")) {
		Util::UIntCheckbox("Enable Tangent Shift", settings.EnableTangentShift);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Varies highlights along strands using detail supplied by the hair mod. Off keeps a more uniform highlight direction.");
		}
		if (settings.HairMode == 0) {
			Util::Widgets::SliderFloat("Primary Specular Tangent Shift", &settings.PrimaryTangentShift, -1.0f, 1.0f, "%.2f");
			Util::AddTooltip("Moves the main highlight along the hair strands. Negative and positive values move it in opposite directions.");
			Util::Widgets::SliderFloat("Secondary Specular Tangent Shift", &settings.SecondaryTangentShift, -1.0f, 1.0f, "%.2f");
			Util::AddTooltip("Moves the second highlight along the strands, changing its separation from the main shine.");
		}
		ImGui::Spacing();
	}
	if (page.Is("shadows")) {
		Util::UIntCheckbox("Enable Screen-Space Self Shadow", settings.EnableSelfShadow);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Adds small shadows where visible hair overlaps, helping prevent overly bright hair. Off leaves those extra shadows out, reducing rendering work.");
		}
		const auto shadowsDisabled = Util::DisableGuard(settings.EnableSelfShadow == 0);
		Util::Widgets::SliderFloat("Self Shadow Strength", &settings.SelfShadowStrength, 0.0f, 1.0f, "%.2f");
		Util::AddTooltip("How dark overlapping hair becomes. 0 removes this darkening; 1 applies it fully.");
		Util::Widgets::SliderFloat("Self Shadow Exponent", &settings.SelfShadowExponent, 0.0f, 10.0f, "%.2f");
		Util::AddTooltip("Higher values lighten partially shaded hair while keeping fully covered areas dark.");
		Util::Widgets::SliderFloat("Self Shadow Scale", &settings.SelfShadowScale, 0.0f, 10.0f, "%.2f");
		Util::AddTooltip("How far nearby hair and surfaces can cast these small shadows. Higher values extend their reach.");
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
