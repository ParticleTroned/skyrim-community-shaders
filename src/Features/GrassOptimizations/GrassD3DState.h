#pragma once

#include <array>
#include <d3d11_1.h>
#include <winrt/base.h>

namespace GrassD3D
{
	/** @brief Restore compute shader, resource bindings and constant-buffer windows. */
	class ComputeState
	{
	public:
		explicit ComputeState(ID3D11DeviceContext1* context) : ctx(context)
		{
			ctx->CSGetShader(shader.put(), classes.data(), &classCount);
			for (UINT i = 0; i < buffers.size(); ++i)
				ctx->CSGetConstantBuffers1(i, 1, buffers[i].put(), &first[i], &count[i]);
			for (UINT i = 0; i < srvs.size(); ++i)
				ctx->CSGetShaderResources(i, 1, srvs[i].put());
			for (UINT i = 0; i < uavs.size(); ++i)
				ctx->CSGetUnorderedAccessViews(i, 1, uavs[i].put());
		}
		~ComputeState()
		{
			std::array<ID3D11ShaderResourceView*, 3> emptySRVs{};
			std::array<ID3D11UnorderedAccessView*, 4> emptyUAVs{};
			ctx->CSSetShaderResources(0, 3, emptySRVs.data());
			ctx->CSSetUnorderedAccessViews(0, 4, emptyUAVs.data(), nullptr);
			for (UINT i = 0; i < buffers.size(); ++i) {
				auto cb = buffers[i].get();
				ctx->CSSetConstantBuffers1(i, 1, &cb, &first[i], &count[i]);
			}
			for (UINT i = 0; i < srvs.size(); ++i) {
				auto srv = srvs[i].get();
				ctx->CSSetShaderResources(i, 1, &srv);
			}
			for (UINT i = 0; i < uavs.size(); ++i) {
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
		winrt::com_ptr<ID3D11ComputeShader> shader;
		std::array<ID3D11ClassInstance*, D3D11_SHADER_MAX_INTERFACES> classes{};
		UINT classCount = static_cast<UINT>(classes.size());
		std::array<winrt::com_ptr<ID3D11Buffer>, 3> buffers;
		std::array<UINT, 3> first{}, count{};
		std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 3> srvs;
		std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, 4> uavs;
	};

	/** @brief Restore the native input assembler and grass vertex bindings. */
	class DrawState
	{
	public:
		explicit DrawState(ID3D11DeviceContext1* context) : ctx(context)
		{
			ctx->VSGetConstantBuffers1(9, 1, cb.put(), &first, &count);
			ctx->VSGetShaderResources(2, 1, srv.put());
			ctx->IAGetInputLayout(layout.put());
			for (UINT i = 0; i < vertices.size(); ++i)
				ctx->IAGetVertexBuffers(i, 1, vertices[i].put(), &strides[i], &offsets[i]);
			ctx->IAGetIndexBuffer(indices.put(), &format, &indexOffset);
			ctx->IAGetPrimitiveTopology(&topology);
		}
		~DrawState()
		{
			auto buffer = cb.get();
			auto resource = srv.get();
			ctx->VSSetConstantBuffers1(9, 1, &buffer, &first, &count);
			ctx->VSSetShaderResources(2, 1, &resource);
			ctx->IASetInputLayout(layout.get());
			for (UINT i = 0; i < vertices.size(); ++i) {
				auto vertex = vertices[i].get();
				ctx->IASetVertexBuffers(i, 1, &vertex, &strides[i], &offsets[i]);
			}
			ctx->IASetIndexBuffer(indices.get(), format, indexOffset);
			ctx->IASetPrimitiveTopology(topology);
		}
		DrawState(const DrawState&) = delete;
		DrawState& operator=(const DrawState&) = delete;

	private:
		ID3D11DeviceContext1* ctx;
		winrt::com_ptr<ID3D11Buffer> cb, indices;
		winrt::com_ptr<ID3D11ShaderResourceView> srv;
		winrt::com_ptr<ID3D11InputLayout> layout;
		std::array<winrt::com_ptr<ID3D11Buffer>, 2> vertices;
		std::array<UINT, 2> strides{}, offsets{};
		UINT first = 0, count = 0, indexOffset = 0;
		DXGI_FORMAT format{};
		D3D11_PRIMITIVE_TOPOLOGY topology{};
	};
}
