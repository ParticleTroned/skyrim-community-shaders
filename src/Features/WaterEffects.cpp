#include "WaterEffects.h"

#include <DDSTextureLoader.h>

#include "Utils/D3D.h"
#include "CSUtility.h"
#include "FoveatedCommon.h"
#include "Upscaling.h"

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

WaterEffects::ParallaxFoveationData WaterEffects::GetParallaxFoveationData() const
{
	ParallaxFoveationData data{};
	if (!loaded || !globals::game::isVR || !globals::features::csUtility.settings.water.foveatedParallax)
		return data;
	const auto profile = globals::features::upscaling.foveatedRender.GetFoveationProfile();
	if (profile.available) {
		data.region = float4{ profile.coverageScale, FoveatedCommon::kCenterFeather,
			profile.centerHorizontalScale, FoveatedCommon::GetShaderMode(FoveatedCommon::DetailMode::Feathered) };
		data.centers = float4{ profile.centerOffsets[0].x, profile.centerOffsets[0].y,
			profile.centerOffsets[1].x, profile.centerOffsets[1].y };
	}
	return data;
}
