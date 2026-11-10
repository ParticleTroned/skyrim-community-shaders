#pragma once

#include "Utils/ComputeState.h"

#include <array>
#include <d3d11_1.h>
#include <winrt/base.h>

namespace GrassD3D
{
	using ComputeState = D3DState::ComputeState;

	/** @brief Restore native shaders, geometry constants and input bindings after an owned grass draw. */
	class DrawState
	{
	public:
		explicit DrawState(ID3D11DeviceContext1* context) : ctx(context)
		{
			ctx->VSGetShader(vertexShader.put(), vertexClasses.data(), &vertexClassCount);
			ctx->PSGetShader(pixelShader.put(), pixelClasses.data(), &pixelClassCount);
			ctx->VSGetConstantBuffers1(2, 1, vertexGeometry.put(), &vertexFirst, &vertexCount);
			ctx->VSGetConstantBuffers1(9, 1, cb.put(), &first, &count);
			std::array<ID3D11ShaderResourceView*, 2> rawSRVs{};
			ctx->VSGetShaderResources(2, static_cast<UINT>(rawSRVs.size()), rawSRVs.data());
			for (UINT i = 0; i < srvs.size(); ++i)
				srvs[i].attach(rawSRVs[i]);
			ctx->IAGetInputLayout(layout.put());
			std::array<ID3D11Buffer*, 2> rawVertices{};
			ctx->IAGetVertexBuffers(0, static_cast<UINT>(rawVertices.size()), rawVertices.data(), strides.data(), offsets.data());
			for (UINT i = 0; i < vertices.size(); ++i)
				vertices[i].attach(rawVertices[i]);
			ctx->IAGetIndexBuffer(indices.put(), &format, &indexOffset);
			ctx->IAGetPrimitiveTopology(&topology);
		}
		~DrawState()
		{
			ctx->VSSetShader(vertexShader.get(), vertexClasses.data(), vertexClassCount);
			ctx->PSSetShader(pixelShader.get(), pixelClasses.data(), pixelClassCount);
			for (UINT i = 0; i < vertexClassCount; ++i)
				vertexClasses[i]->Release();
			for (UINT i = 0; i < pixelClassCount; ++i)
				pixelClasses[i]->Release();
			auto vertexCB = vertexGeometry.get();
			ctx->VSSetConstantBuffers1(2, 1, &vertexCB, &vertexFirst, &vertexCount);
			auto buffer = cb.get();
			ctx->VSSetConstantBuffers1(9, 1, &buffer, &first, &count);
			std::array<ID3D11ShaderResourceView*, 2> rawSRVs{};
			for (UINT i = 0; i < srvs.size(); ++i)
				rawSRVs[i] = srvs[i].get();
			ctx->VSSetShaderResources(2, static_cast<UINT>(rawSRVs.size()), rawSRVs.data());
			ctx->IASetInputLayout(layout.get());
			std::array<ID3D11Buffer*, 2> rawVertices{};
			for (UINT i = 0; i < vertices.size(); ++i)
				rawVertices[i] = vertices[i].get();
			ctx->IASetVertexBuffers(0, static_cast<UINT>(rawVertices.size()), rawVertices.data(), strides.data(), offsets.data());
			ctx->IASetIndexBuffer(indices.get(), format, indexOffset);
			ctx->IASetPrimitiveTopology(topology);
		}
		DrawState(const DrawState&) = delete;
		DrawState& operator=(const DrawState&) = delete;

	private:
		ID3D11DeviceContext1* ctx;
		winrt::com_ptr<ID3D11VertexShader> vertexShader;
		winrt::com_ptr<ID3D11PixelShader> pixelShader;
		std::array<ID3D11ClassInstance*, D3D11_SHADER_MAX_INTERFACES> vertexClasses{}, pixelClasses{};
		UINT vertexClassCount = UINT(vertexClasses.size()), pixelClassCount = UINT(pixelClasses.size());
		winrt::com_ptr<ID3D11Buffer> vertexGeometry;
		UINT vertexFirst = 0, vertexCount = 0;
		winrt::com_ptr<ID3D11Buffer> cb, indices;
		std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 2> srvs;
		winrt::com_ptr<ID3D11InputLayout> layout;
		std::array<winrt::com_ptr<ID3D11Buffer>, 2> vertices;
		std::array<UINT, 2> strides{}, offsets{};
		UINT first = 0, count = 0, indexOffset = 0;
		DXGI_FORMAT format{};
		D3D11_PRIMITIVE_TOPOLOGY topology{};
	};
}
