#pragma once

#include <algorithm>
#include <array>
#include <d3d11_1.h>
#include <winrt/base.h>

namespace D3DState
{
	/** @brief Restore compute shader, resource bindings and constant-buffer windows. */
	class ComputeState
	{
	public:
		explicit ComputeState(ID3D11DeviceContext1* context, UINT usedUAVs = 4, UINT usedSRVs = 3) : ctx(context), uavCount(std::min(usedUAVs, 14u)), srvCount(std::min(usedSRVs, 4u))
		{
			ctx->CSGetShader(shader.put(), classes.data(), &classCount);
			std::array<ID3D11Buffer*, 4> rawBuffers{};
			ctx->CSGetConstantBuffers1(0, static_cast<UINT>(rawBuffers.size()), rawBuffers.data(), first.data(), count.data());
			for (UINT i = 0; i < buffers.size(); ++i)
				buffers[i].attach(rawBuffers[i]);
			std::array<ID3D11ShaderResourceView*, 4> rawSRVs{};
			ctx->CSGetShaderResources(0, srvCount, rawSRVs.data());
			for (UINT i = 0; i < srvCount; ++i)
				srvs[i].attach(rawSRVs[i]);
			std::array<ID3D11UnorderedAccessView*, 14> rawUAVs{};
			ctx->CSGetUnorderedAccessViews(0, uavCount, rawUAVs.data());
			for (UINT i = 0; i < uavCount; ++i)
				uavs[i].attach(rawUAVs[i]);
		}
		~ComputeState()
		{
			std::array<ID3D11ShaderResourceView*, 4> emptySRVs{};
			std::array<ID3D11UnorderedAccessView*, 14> emptyUAVs{};
			ctx->CSSetShaderResources(0, srvCount, emptySRVs.data());
			ctx->CSSetUnorderedAccessViews(0, uavCount, emptyUAVs.data(), nullptr);
			std::array<ID3D11Buffer*, 4> rawBuffers{};
			for (UINT i = 0; i < buffers.size(); ++i)
				rawBuffers[i] = buffers[i].get();
			ctx->CSSetConstantBuffers1(0, static_cast<UINT>(rawBuffers.size()), rawBuffers.data(), first.data(), count.data());
			std::array<ID3D11ShaderResourceView*, 4> rawSRVs{};
			for (UINT i = 0; i < srvCount; ++i)
				rawSRVs[i] = srvs[i].get();
			ctx->CSSetShaderResources(0, srvCount, rawSRVs.data());
			std::array<ID3D11UnorderedAccessView*, 14> rawUAVs{};
			for (UINT i = 0; i < uavCount; ++i)
				rawUAVs[i] = uavs[i].get();
			ctx->CSSetUnorderedAccessViews(0, uavCount, rawUAVs.data(), nullptr);
			ctx->CSSetShader(shader.get(), classes.data(), classCount);
			for (UINT i = 0; i < classCount; ++i)
				classes[i]->Release();
		}
		ComputeState(const ComputeState&) = delete;
		ComputeState& operator=(const ComputeState&) = delete;

	private:
		ID3D11DeviceContext1* ctx;
		UINT uavCount, srvCount;
		winrt::com_ptr<ID3D11ComputeShader> shader;
		std::array<ID3D11ClassInstance*, D3D11_SHADER_MAX_INTERFACES> classes{};
		UINT classCount = static_cast<UINT>(classes.size());
		std::array<winrt::com_ptr<ID3D11Buffer>, 4> buffers;
		std::array<UINT, 4> first{}, count{};
		std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 4> srvs;
		std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, 14> uavs;
	};

}
