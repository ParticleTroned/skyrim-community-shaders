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
			for (UINT i = 0; i < buffers.size(); ++i)
				ctx->CSGetConstantBuffers1(i, 1, buffers[i].put(), &first[i], &count[i]);
			for (UINT i = 0; i < srvs.size(); ++i)
				ctx->CSGetShaderResources(i, 1, srvs[i].put());
			for (UINT i = 0; i < uavCount; ++i)
				ctx->CSGetUnorderedAccessViews(i, 1, uavs[i].put());
		}
		~ComputeState()
		{
			std::array<ID3D11ShaderResourceView*, 3> emptySRVs{};
			std::array<ID3D11UnorderedAccessView*, 14> emptyUAVs{};
			ctx->CSSetShaderResources(0, 3, emptySRVs.data());
			ctx->CSSetUnorderedAccessViews(0, uavCount, emptyUAVs.data(), nullptr);
			for (UINT i = 0; i < buffers.size(); ++i) {
				auto cb = buffers[i].get();
				ctx->CSSetConstantBuffers1(i, 1, &cb, &first[i], &count[i]);
			}
			for (UINT i = 0; i < srvs.size(); ++i) {
				auto srv = srvs[i].get();
				ctx->CSSetShaderResources(i, 1, &srv);
			}
			for (UINT i = 0; i < uavCount; ++i) {
				auto uav = uavs[i].get();
				ctx->CSSetUnorderedAccessViews(i, 1, &uav, nullptr);
			}
			ctx->CSSetShader(shader.get(), classes.data(), classCount);
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
	};

}
