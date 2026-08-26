#pragma once

#include "../../Buffer.h"
#include "../../Globals.h"
#include "../../GpuPass.h"
#include "../../Profiler.h"
#include "../../Utils/D3D.h"

#include <cstdint>
#include <d3d11_4.h>

namespace UpscalingSharpener
{
	inline bool TryGetOutputDimensions(
		ID3D11UnorderedAccessView* outputUAV,
		uint32_t& outputWidth,
		uint32_t& outputHeight)
	{
		outputWidth = 0;
		outputHeight = 0;

		D3D11_TEXTURE2D_DESC textureDesc{};
		if (!Util::GetTexture2DDesc(outputUAV, textureDesc) ||
			!textureDesc.Width || !textureDesc.Height || !textureDesc.MipLevels) {
			return false;
		}

		D3D11_UNORDERED_ACCESS_VIEW_DESC viewDesc{};
		outputUAV->GetDesc(&viewDesc);

		// The shaders declare RWTexture2D, so other UAV dimensions are invalid.
		if (viewDesc.ViewDimension != D3D11_UAV_DIMENSION_TEXTURE2D)
			return false;

		const uint32_t mipSlice = viewDesc.Texture2D.MipSlice;
		if (mipSlice >= textureDesc.MipLevels)
			return false;

		outputWidth = textureDesc.Width >> mipSlice;
		outputHeight = textureDesc.Height >> mipSlice;
		if (!outputWidth)
			outputWidth = 1;
		if (!outputHeight)
			outputHeight = 1;
		return true;
	}

	enum class Pass
	{
		RCAS,
		LumaSharpen
	};

	template <class Config>
	inline bool DispatchComputePass(
		ID3D11ComputeShader* computeShader,
		ConstantBuffer* configCB,
		const Config& config,
		ID3D11ShaderResourceView* inputSRV,
		ID3D11UnorderedAccessView* outputUAV,
		Pass pass)
	{
		auto context = globals::d3d::context;
		if (!context || !computeShader || !configCB || !inputSRV || !outputUAV)
			return false;

		uint32_t outputWidth = 0;
		uint32_t outputHeight = 0;
		if (!TryGetOutputDimensions(outputUAV, outputWidth, outputHeight)) {
			return false;
		}

		configCB->Update(config);
		auto bufferArray = configCB->CB();

		context->CSSetShader(computeShader, nullptr, 0);
		context->CSSetConstantBuffers(0, 1, &bufferArray);

		ID3D11ShaderResourceView* srvs[] = { inputSRV };
		context->CSSetShaderResources(0, 1, srvs);

		ID3D11UnorderedAccessView* uavs[] = { outputUAV };
		context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

		const uint32_t dispatchX = (outputWidth + 7) / 8;
		const uint32_t dispatchY = (outputHeight + 7) / 8;
		{
			CS_GPU_PASS_SELECT(pass == Pass::RCAS, "Upscaling::RCAS", "Upscaling::LumaSharpen");
			context->Dispatch(dispatchX, dispatchY, 1);
		}

		ID3D11ShaderResourceView* nullSRVs[] = { nullptr };
		context->CSSetShaderResources(0, 1, nullSRVs);

		ID3D11UnorderedAccessView* nullUAVs[] = { nullptr };
		context->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);

		context->CSSetShader(nullptr, nullptr, 0);

		return true;
	}
}
