#pragma once

#include <d3d11.h>

namespace UpscalingSharpener
{
	/** Replaces owned compute views without letting the previous output suppress an input binding. */
	inline void BindComputeViews(ID3D11DeviceContext* context, ID3D11ShaderResourceView* inputSRV,
		ID3D11UnorderedAccessView* outputUAV, ID3D11ShaderResourceView* motionSRV = nullptr)
	{
		// D3D11 binds a null SRV while an overlapping UAV remains bound.
		ID3D11UnorderedAccessView* nullUAV = nullptr;
		context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
		ID3D11ShaderResourceView* srvs[] = { inputSRV, motionSRV };
		context->CSSetShaderResources(0, motionSRV ? 2u : 1u, srvs);
		context->CSSetUnorderedAccessViews(0, 1, &outputUAV, nullptr);
	}
}
