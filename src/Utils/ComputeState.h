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
		explicit ComputeState(ID3D11DeviceContext1* context, UINT usedUAVs = 4) : ctx(context), uavCount(std::min(usedUAVs, 14u))
		{
			ctx->CSGetShader(shader.put(), classes.data(), &classCount);
			std::array<ID3D11Buffer*, 4> rawBuffers{};
			ctx->CSGetConstantBuffers1(0, static_cast<UINT>(rawBuffers.size()), rawBuffers.data(), first.data(), count.data());
			for (UINT i = 0; i < buffers.size(); ++i)
				buffers[i].attach(rawBuffers[i]);
			std::array<ID3D11ShaderResourceView*, 3> rawSRVs{};
			ctx->CSGetShaderResources(0, static_cast<UINT>(rawSRVs.size()), rawSRVs.data());
			for (UINT i = 0; i < srvs.size(); ++i)
				srvs[i].attach(rawSRVs[i]);
			std::array<ID3D11UnorderedAccessView*, 14> rawUAVs{};
			ctx->CSGetUnorderedAccessViews(0, uavCount, rawUAVs.data());
			for (UINT i = 0; i < uavCount; ++i)
				uavs[i].attach(rawUAVs[i]);
			ctx->GetPredication(predicate.put(), &predicateValue);
			ctx->SetPredication(nullptr, FALSE);
		}
		~ComputeState()
		{
			std::array<ID3D11ShaderResourceView*, 3> emptySRVs{};
			std::array<ID3D11UnorderedAccessView*, 14> emptyUAVs{};
			ctx->CSSetShaderResources(0, 3, emptySRVs.data());
			ctx->CSSetUnorderedAccessViews(0, uavCount, emptyUAVs.data(), nullptr);
			std::array<ID3D11Buffer*, 4> rawBuffers{};
			for (UINT i = 0; i < buffers.size(); ++i)
				rawBuffers[i] = buffers[i].get();
			for (UINT i = 0; i < rawBuffers.size(); ++i) {
				if (rawBuffers[i] && (first[i] != 0 || count[i] != D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT))
					ctx->CSSetConstantBuffers1(i, 1, &rawBuffers[i], &first[i], &count[i]);
				else
					ctx->CSSetConstantBuffers(i, 1, &rawBuffers[i]);
			}
			std::array<ID3D11ShaderResourceView*, 3> rawSRVs{};
			for (UINT i = 0; i < srvs.size(); ++i)
				rawSRVs[i] = srvs[i].get();
			ctx->CSSetShaderResources(0, static_cast<UINT>(rawSRVs.size()), rawSRVs.data());
			std::array<ID3D11UnorderedAccessView*, 14> rawUAVs{};
			for (UINT i = 0; i < uavCount; ++i)
				rawUAVs[i] = uavs[i].get();
			ctx->CSSetUnorderedAccessViews(0, uavCount, rawUAVs.data(), nullptr);
			ctx->CSSetShader(shader.get(), classes.data(), classCount);
			ctx->SetPredication(predicate.get(), predicateValue);
			for (UINT i = 0; i < classCount; ++i)
				classes[i]->Release();
		}
		ComputeState(const ComputeState&) = delete;
		ComputeState& operator=(const ComputeState&) = delete;

	private:
		ID3D11DeviceContext1* ctx;
		UINT uavCount;
		winrt::com_ptr<ID3D11ComputeShader> shader;
		std::array<ID3D11ClassInstance*, D3D11_SHADER_MAX_INTERFACES> classes{};
		UINT classCount = static_cast<UINT>(classes.size());
		std::array<winrt::com_ptr<ID3D11Buffer>, 4> buffers;
		std::array<UINT, 4> first{}, count{};
		std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 3> srvs;
		std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, 14> uavs;
		winrt::com_ptr<ID3D11Predicate> predicate;
		BOOL predicateValue = FALSE;
	};

}
