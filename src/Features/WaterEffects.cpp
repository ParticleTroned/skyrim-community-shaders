#include "WaterEffects.h"

#include "Menu.h"
#include "Menu/SettingsPage.h"
#include "Utils/ResourceName.h"
#include "Utils/UI.h"

#include <DDSTextureLoader.h>

void WaterEffects::DrawSettings()
{
	MenuUI::SettingsPage page("WaterEffects", {
												  { "about", "How it works", "Understand underwater caustics and wave depth, and find their appearance controls.", "Caustics, wave depth and tuning" },
											  });
	if (!page.Is("about"))
		return;

	MenuUI::SectionHeading("Underwater caustics");
	MenuUI::DetailText("Moving patches of light fall on submerged surfaces. Their pattern changes with depth and follows the scene's directional lighting.");
	Util::AddTooltip("Caustics Strength changes contrast; Tiling changes pattern size; Speed controls animation; Color Dispersion controls colour separation. Zero strength removes the patterns, and zero speed freezes them.");

	MenuUI::SectionHeading("Wave depth");
	MenuUI::DetailText("Parallax makes compatible water waves appear raised and recessed as your viewpoint moves.");
	Util::AddTooltip("Parallax Strength changes apparent depth; zero disables it. Parallax Quality controls sampling detail and GPU cost. This changes shading, not the water mesh or swimming height.");

	MenuUI::SectionHeading("Appearance controls");
	MenuUI::DetailText("Visuals > Global > Water > Show Detailed Water Controls");
	Util::AddTooltip("Use Global for the shared look. Visuals profiles, weather and location adjustments can refine water for specific conditions. Enable Visuals for these adjustments to take effect.");
	if (ImGui::Button("Open Visuals")) {
		MenuUI::SettingsPage::Select("AdaptiveBrightness", "global");
		globals::menu->SelectFeatureMenu("AdaptiveBrightness");
	}
	Util::AddTooltip("Open Visuals > Global, then choose Water and Show Detailed Water Controls to adjust caustics and parallax.");
}

void WaterEffects::SetupResources()
{
	auto device = globals::d3d::device;
	auto context = globals::d3d::context;

	winrt::com_ptr<ID3D11Resource> causticsTexture;
	DirectX::CreateDDSTextureFromFile(device, context, L"Data\\Shaders\\WaterEffects\\watercaustics.dds", causticsTexture.put(), causticsView.put());
	Util::SetResourceName(causticsTexture.get(), "WaterEffects::Caustics");
	Util::SetResourceName(causticsView.get(), "WaterEffects::Caustics SRV");
}

void WaterEffects::Prepass()
{
	auto context = globals::d3d::context;
	auto srv = causticsView.get();
	context->PSSetShaderResources(65, 1, &srv);
}

bool WaterEffects::HasShaderDefine(RE::BSShader::Type)
{
	return true;
}
