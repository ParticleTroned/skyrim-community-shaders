#include "RCAS.h"

#include "../../../State.h"
#include "../../../Util.h"

namespace
{
	bool TryGetOutputDimensions(ID3D11UnorderedAccessView* a_uav, uint32_t& o_width, uint32_t& o_height)
	{
		o_width = 0;
		o_height = 0;

		if (!a_uav)
			return false;

		ID3D11Resource* resource = nullptr;
		a_uav->GetResource(&resource);
		if (!resource)
			return false;

		ID3D11Texture2D* texture = nullptr;
		const HRESULT hr = resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture));
		resource->Release();
		if (FAILED(hr) || !texture)
			return false;

		D3D11_TEXTURE2D_DESC textureDesc{};
		texture->GetDesc(&textureDesc);
		texture->Release();

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		a_uav->GetDesc(&uavDesc);

		uint32_t mipSlice = 0;
		switch (uavDesc.ViewDimension) {
		case D3D11_UAV_DIMENSION_TEXTURE2D:
			mipSlice = uavDesc.Texture2D.MipSlice;
			break;
		case D3D11_UAV_DIMENSION_TEXTURE2DARRAY:
			mipSlice = uavDesc.Texture2DArray.MipSlice;
			break;
		default:
			return false;
		}

		o_width = textureDesc.Width >> mipSlice;
		o_height = textureDesc.Height >> mipSlice;
		if (!o_width)
			o_width = 1;
		if (!o_height)
			o_height = 1;

		return true;
	}
}

struct RCASConfig
{
	float sharpness;
	float3 pad;
};

RCAS::~RCAS()
{
	delete rcasConfigCB;
	rcasConfigCB = nullptr;
}

void RCAS::Initialize()
{
	if (rcasConfigCB)
		return;

	logger::info("[RCAS] Creating resources");
	CreateComputeShader();
	rcasConfigCB = new ConstantBuffer(ConstantBufferDesc<RCASConfig>());
}

void RCAS::CreateComputeShader()
{
	std::vector<std::pair<const char*, const char*>> defines;
	rcasComputeShader.attach((ID3D11ComputeShader*)Util::CompileShader(L"Data\\Shaders\\Upscaling\\RCAS\\RCAS.hlsl", defines, "cs_5_0"));
}

void RCAS::ApplySharpen(ID3D11ShaderResourceView* inputSRV, ID3D11UnorderedAccessView* outputUAV, float sharpness)
{
	auto state = globals::state;
	auto context = globals::d3d::context;
	if (!state || !context || !inputSRV || !outputUAV)
		return;

	ZoneScoped;
	TracyD3D11Zone(state->tracyCtx, "RCAS Sharpening");

	if (!rcasComputeShader) {
		logger::warn("[RCAS] Compute shader not compiled");
		return;
	}
	if (!rcasConfigCB) {
		logger::warn("[RCAS] Constant buffer not initialized");
		return;
	}
	globals::profiler->BeginPass("Upscaling::RCAS");
	state->BeginPerfEvent("RCAS Sharpening");

	uint32_t screenWidth = 0;
	uint32_t screenHeight = 0;
	if (!TryGetOutputDimensions(outputUAV, screenWidth, screenHeight)) {
		logger::warn("[RCAS] Unable to determine output UAV dimensions");
		globals::profiler->EndPass();
		state->EndPerfEvent();
		return;
	}

	RCASConfig config{};
	config.sharpness = sharpness;

	rcasConfigCB->Update(config);
	auto bufferArray = rcasConfigCB->CB();

	context->CSSetShader(rcasComputeShader.get(), nullptr, 0);
	context->CSSetConstantBuffers(0, 1, &bufferArray);

	ID3D11ShaderResourceView* srvs[] = { inputSRV };
	context->CSSetShaderResources(0, 1, srvs);

	ID3D11UnorderedAccessView* uavs[] = { outputUAV };
	context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

	uint32_t dispatchX = (screenWidth + 7) / 8;
	uint32_t dispatchY = (screenHeight + 7) / 8;
	context->Dispatch(dispatchX, dispatchY, 1);

	ID3D11ShaderResourceView* nullSRVs[] = { nullptr };
	context->CSSetShaderResources(0, 1, nullSRVs);

	ID3D11UnorderedAccessView* nullUAVs[] = { nullptr };
	context->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);

	context->CSSetShader(nullptr, nullptr, 0);

	globals::profiler->EndPass();
	state->EndPerfEvent();
}
