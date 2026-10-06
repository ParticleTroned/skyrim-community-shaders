#pragma once

#include "Utils/ComputeState.h"

#include <array>
#include <d3d11_1.h>
#include <winrt/base.h>

namespace GrassD3D
{
	using ComputeState = D3DState::ComputeState;

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
