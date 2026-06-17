#pragma once

#include "../../Buffer.h"
#include "../../Globals.h"
#include "../../Profiler.h"
#include "../../State.h"

#include <cstdint>
#include <d3d11_4.h>

namespace UpscalingSharpener
{
	template <class Config>
	inline bool DispatchComputePass(
		ID3D11ComputeShader* computeShader,
		ConstantBuffer* configCB,
		const Config& config,
		ID3D11ShaderResourceView* inputSRV,
		ID3D11UnorderedAccessView* outputUAV,
		uint32_t width,
		uint32_t height,
		const char* perfEventName,
		const char* profileScopeName)
	{
		auto state = globals::state;
		auto context = globals::d3d::context;
		if (!state || !context || !computeShader || !configCB || !inputSRV || !outputUAV)
			return false;
		if (!width || !height)
			return false;

		state->BeginPerfEvent(perfEventName);

		configCB->Update(config);
		auto bufferArray = configCB->CB();

		context->CSSetShader(computeShader, nullptr, 0);
		context->CSSetConstantBuffers(0, 1, &bufferArray);

		ID3D11ShaderResourceView* srvs[] = { inputSRV };
		context->CSSetShaderResources(0, 1, srvs);

		ID3D11UnorderedAccessView* uavs[] = { outputUAV };
		context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

		const uint32_t dispatchX = (width + 7) / 8;
		const uint32_t dispatchY = (height + 7) / 8;
		{
			CS_PROFILE_SCOPE(profileScopeName);
			context->Dispatch(dispatchX, dispatchY, 1);
		}

		ID3D11ShaderResourceView* nullSRVs[] = { nullptr };
		context->CSSetShaderResources(0, 1, nullSRVs);

		ID3D11UnorderedAccessView* nullUAVs[] = { nullptr };
		context->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);

		context->CSSetShader(nullptr, nullptr, 0);

		state->EndPerfEvent();
		return true;
	}
}
