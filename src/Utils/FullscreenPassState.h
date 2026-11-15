#pragma once

#include "DeviceContextState.h"

#include <array>
#include <cstddef>
#include <d3d11_1.h>
#include <winrt/base.h>

namespace Util
{
	/** Restores fullscreen raster state and only the pixel binding slots the pass owns. */
	template <UINT PixelBufferSlot, UINT PixelResourceCount, bool SaveSampler = true>
	class FullscreenPassState
	{
		static_assert(PixelBufferSlot < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT);
		static_assert(PixelResourceCount > 0 && PixelResourceCount <= D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT);

	public:
		explicit FullscreenPassState(ID3D11DeviceContext* context) : ctx(context)
		{
			if (captureDeviceContextState)
				captureDeviceContextState(ctx, logicalState);
			ctx->QueryInterface(__uuidof(ID3D11DeviceContext1), ctx1.put_void());
			ctx->VSGetShader(vs.put(), vsClasses.data(), &vsClassCount);
			ctx->PSGetShader(ps.put(), psClasses.data(), &psClassCount);
			ctx->HSGetShader(hs.put(), hsClasses.data(), &hsClassCount);
			ctx->DSGetShader(ds.put(), dsClasses.data(), &dsClassCount);
			ctx->GSGetShader(gs.put(), gsClasses.data(), &gsClassCount);
			if (ctx1)
				ctx1->PSGetConstantBuffers1(PixelBufferSlot, 1, pixelBuffer.put(), &firstConstant, &constantCount);
			else
				ctx->PSGetConstantBuffers(PixelBufferSlot, 1, pixelBuffer.put());
			ctx->PSGetShaderResources(0, PixelResourceCount, resources.data());
			if constexpr (SaveSampler)
				ctx->PSGetSamplers(0, 1, sampler.put());
			ctx->OMGetRenderTargets(static_cast<UINT>(targets.size()), targets.data(), depthTarget.put());
			winrt::com_ptr<ID3D11Device> device;
			ctx->GetDevice(device.put());
			unorderedCount = device->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_1 ? D3D11_1_UAV_SLOT_COUNT : D3D11_PS_CS_UAV_REGISTER_COUNT;
			ctx->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0, unorderedCount, unorderedOutputs.data());
			ctx->OMGetBlendState(blend.put(), blendFactor.data(), &sampleMask);
			ctx->OMGetDepthStencilState(depthState.put(), &stencilRef);
			ctx->RSGetState(rasterizer.put());
			ctx->RSGetViewports(&viewportCount, viewports.data());
			ctx->IAGetPrimitiveTopology(&topology);
			ctx->IAGetInputLayout(layout.put());
			ctx->IAGetVertexBuffers(0, 1, vertexBuffer.put(), &vertexStride, &vertexOffset);
			ctx->IAGetIndexBuffer(indexBuffer.put(), &indexFormat, &indexOffset);
			ctx->GetPredication(predicate.put(), &predicateValue);
			// Engine occlusion predicates must not suppress fullscreen repairs.
			ctx->SetPredication(nullptr, FALSE);
		}

		~FullscreenPassState()
		{
			const std::array<ID3D11ShaderResourceView*, PixelResourceCount> empty{};
			ctx->PSSetShaderResources(0, PixelResourceCount, empty.data());
			// Restore outputs before inputs so a pass output cannot suppress a saved SRV.
			UINT targetCount = 0;
			for (UINT i = 0; i < targets.size(); ++i)
				if (targets[i])
					targetCount = i + 1;
			// RTV and pixel UAV slots overlap; preserve their bindings and hidden counters together.
			ctx->OMSetRenderTargetsAndUnorderedAccessViews(targetCount, targets.data(), depthTarget.get(), targetCount,
				unorderedCount - targetCount, unorderedCount > targetCount ? unorderedOutputs.data() + targetCount : nullptr, nullptr);
			ctx->PSSetShaderResources(0, PixelResourceCount, resources.data());
			auto* buffer = pixelBuffer.get();
			if (ctx1)
				ctx1->PSSetConstantBuffers1(PixelBufferSlot, 1, &buffer, &firstConstant, &constantCount);
			else
				ctx->PSSetConstantBuffers(PixelBufferSlot, 1, &buffer);
			if constexpr (SaveSampler) {
				auto* savedSampler = sampler.get();
				ctx->PSSetSamplers(0, 1, &savedSampler);
			}
			ctx->VSSetShader(vs.get(), vsClasses.data(), vsClassCount);
			ctx->PSSetShader(ps.get(), psClasses.data(), psClassCount);
			ctx->HSSetShader(hs.get(), hsClasses.data(), hsClassCount);
			ctx->DSSetShader(ds.get(), dsClasses.data(), dsClassCount);
			ctx->GSSetShader(gs.get(), gsClasses.data(), gsClassCount);
			ctx->OMSetBlendState(blend.get(), blendFactor.data(), sampleMask);
			ctx->OMSetDepthStencilState(depthState.get(), stencilRef);
			ctx->RSSetState(rasterizer.get());
			ctx->RSSetViewports(viewportCount, viewports.data());
			ctx->IASetPrimitiveTopology(topology);
			ctx->IASetInputLayout(layout.get());
			auto* vertices = vertexBuffer.get();
			ctx->IASetVertexBuffers(0, 1, &vertices, &vertexStride, &vertexOffset);
			ctx->IASetIndexBuffer(indexBuffer.get(), indexFormat, indexOffset);
			ctx->SetPredication(predicate.get(), predicateValue);
			Release(resources, PixelResourceCount);
			Release(targets, static_cast<UINT>(targets.size()));
			Release(unorderedOutputs, unorderedCount);
			Release(vsClasses, vsClassCount);
			Release(psClasses, psClassCount);
			Release(hsClasses, hsClassCount);
			Release(dsClasses, dsClassCount);
			Release(gsClasses, gsClassCount);
			logicalState.Restore();
		}
		FullscreenPassState(const FullscreenPassState&) = delete;
		FullscreenPassState& operator=(const FullscreenPassState&) = delete;

	private:
		template <class T, std::size_t Count>
		static void Release(const std::array<T*, Count>& values, UINT count)
		{
			for (UINT i = 0; i < count; ++i)
				if (values[i])
					values[i]->Release();
		}

		ID3D11DeviceContext* ctx;
		winrt::com_ptr<ID3D11DeviceContext1> ctx1;
		DeviceContextStateSnapshot logicalState;
		winrt::com_ptr<ID3D11VertexShader> vs;
		winrt::com_ptr<ID3D11PixelShader> ps;
		winrt::com_ptr<ID3D11HullShader> hs;
		winrt::com_ptr<ID3D11DomainShader> ds;
		winrt::com_ptr<ID3D11GeometryShader> gs;
		std::array<ID3D11ClassInstance*, D3D11_SHADER_MAX_INTERFACES> vsClasses{}, psClasses{}, hsClasses{}, dsClasses{}, gsClasses{};
		UINT vsClassCount = D3D11_SHADER_MAX_INTERFACES, psClassCount = D3D11_SHADER_MAX_INTERFACES;
		UINT hsClassCount = D3D11_SHADER_MAX_INTERFACES, dsClassCount = D3D11_SHADER_MAX_INTERFACES, gsClassCount = D3D11_SHADER_MAX_INTERFACES;
		winrt::com_ptr<ID3D11Buffer> pixelBuffer, vertexBuffer, indexBuffer;
		UINT firstConstant = 0, constantCount = 0, vertexStride = 0, vertexOffset = 0, indexOffset = 0;
		DXGI_FORMAT indexFormat = DXGI_FORMAT_UNKNOWN;
		std::array<ID3D11ShaderResourceView*, PixelResourceCount> resources{};
		winrt::com_ptr<ID3D11SamplerState> sampler;
		std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> targets{};
		std::array<ID3D11UnorderedAccessView*, D3D11_1_UAV_SLOT_COUNT> unorderedOutputs{};
		UINT unorderedCount = 0;
		winrt::com_ptr<ID3D11DepthStencilView> depthTarget;
		winrt::com_ptr<ID3D11BlendState> blend;
		std::array<FLOAT, 4> blendFactor{};
		UINT sampleMask = 0, stencilRef = 0;
		winrt::com_ptr<ID3D11DepthStencilState> depthState;
		winrt::com_ptr<ID3D11RasterizerState> rasterizer;
		UINT viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
		std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports{};
		D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
		winrt::com_ptr<ID3D11InputLayout> layout;
		winrt::com_ptr<ID3D11Predicate> predicate;
		BOOL predicateValue = FALSE;
	};
}
