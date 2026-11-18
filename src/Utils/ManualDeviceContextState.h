#pragma once

#include <array>
#include <d3d11_1.h>
#include <memory>
#include <utility>
#include <winrt/base.h>

namespace Util
{
	namespace detail
	{
		template <class T, size_t Count>
		struct StateReferences
		{
			std::array<T*, Count> values{};
			StateReferences() = default;
			StateReferences(const StateReferences&) = delete;
			StateReferences& operator=(const StateReferences&) = delete;
			~StateReferences() { Reset(); }
			void Reset() noexcept
			{
				for (auto*& value : values)
					if (const auto old = std::exchange(value, nullptr))
						old->Release();
			}
			T** Put() noexcept
			{
				Reset();
				return values.data();
			}
			T* const* Data() const noexcept { return values.data(); }
		};

		template <class Shader>
		struct ShaderStageState
		{
			winrt::com_ptr<Shader> shader;
			StateReferences<ID3D11ClassInstance, D3D11_SHADER_MAX_INTERFACES> classes;
			StateReferences<ID3D11Buffer, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> buffers;
			StateReferences<ID3D11ShaderResourceView, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> resources;
			StateReferences<ID3D11SamplerState, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT> samplers;
			std::array<UINT, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> first{}, count{};
			UINT classCount = 0;

			template <auto GetShader, auto GetBuffers, auto GetResources, auto GetSamplers>
			void Capture(ID3D11DeviceContext1* context)
			{
				shader = nullptr;
				classCount = D3D11_SHADER_MAX_INTERFACES;
				(context->*GetShader)(shader.put(), classes.Put(), &classCount);
				(context->*GetBuffers)(0, static_cast<UINT>(first.size()), buffers.Put(), first.data(), count.data());
				(context->*GetResources)(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, resources.Put());
				(context->*GetSamplers)(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT, samplers.Put());
			}

			template <auto SetShader, auto SetBuffers, auto SetBuffers1, auto SetResources, auto SetSamplers>
			void Apply(ID3D11DeviceContext1* context) const
			{
				(context->*SetShader)(shader.get(), classes.Data(), classCount);
				(context->*SetBuffers)(0, static_cast<UINT>(first.size()), buffers.Data());
				for (UINT slot = 0; slot < first.size(); ++slot) {
					auto* buffer = buffers.values[slot];
					if (buffer && (first[slot] != 0 || count[slot] != D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT))
						(context->*SetBuffers1)(slot, 1, &buffer, &first[slot], &count[slot]);
				}
				(context->*SetResources)(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, resources.Data());
				(context->*SetSamplers)(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT, samplers.Data());
			}
		};
	}

	/** Owns a complete binding snapshot for wrappers that cannot swap native context states. */
	class ManualDeviceContextState
	{
	public:
		ManualDeviceContextState() = default;
		ManualDeviceContextState(const ManualDeviceContextState&) = delete;
		ManualDeviceContextState& operator=(const ManualDeviceContextState&) = delete;

		void Capture(ID3D11DeviceContext1* context)
		{
			Reset();
			winrt::com_ptr<ID3D11Device> device;
			context->GetDevice(device.put());
			uavCount = device->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_1 ? D3D11_1_UAV_SLOT_COUNT : D3D11_PS_CS_UAV_REGISTER_COUNT;
			vs.Capture<&ID3D11DeviceContext1::VSGetShader, &ID3D11DeviceContext1::VSGetConstantBuffers1, &ID3D11DeviceContext1::VSGetShaderResources, &ID3D11DeviceContext1::VSGetSamplers>(context);
			hs.Capture<&ID3D11DeviceContext1::HSGetShader, &ID3D11DeviceContext1::HSGetConstantBuffers1, &ID3D11DeviceContext1::HSGetShaderResources, &ID3D11DeviceContext1::HSGetSamplers>(context);
			ds.Capture<&ID3D11DeviceContext1::DSGetShader, &ID3D11DeviceContext1::DSGetConstantBuffers1, &ID3D11DeviceContext1::DSGetShaderResources, &ID3D11DeviceContext1::DSGetSamplers>(context);
			gs.Capture<&ID3D11DeviceContext1::GSGetShader, &ID3D11DeviceContext1::GSGetConstantBuffers1, &ID3D11DeviceContext1::GSGetShaderResources, &ID3D11DeviceContext1::GSGetSamplers>(context);
			ps.Capture<&ID3D11DeviceContext1::PSGetShader, &ID3D11DeviceContext1::PSGetConstantBuffers1, &ID3D11DeviceContext1::PSGetShaderResources, &ID3D11DeviceContext1::PSGetSamplers>(context);
			cs.Capture<&ID3D11DeviceContext1::CSGetShader, &ID3D11DeviceContext1::CSGetConstantBuffers1, &ID3D11DeviceContext1::CSGetShaderResources, &ID3D11DeviceContext1::CSGetSamplers>(context);
			context->CSGetUnorderedAccessViews(0, uavCount, computeOutputs.Put());
			context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, targets.Put(), depthTarget.put());
			context->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0, uavCount, pixelOutputs.Put());
			context->OMGetBlendState(blend.put(), blendFactor.data(), &sampleMask);
			context->OMGetDepthStencilState(depthState.put(), &stencilRef);
			context->RSGetState(rasterizer.put());
			context->RSGetViewports(&viewportCount, viewports.data());
			context->RSGetScissorRects(&scissorCount, scissors.data());
			context->IAGetInputLayout(layout.put());
			context->IAGetPrimitiveTopology(&topology);
			context->IAGetVertexBuffers(0, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT, vertices.Put(), strides.data(), offsets.data());
			context->IAGetIndexBuffer(indexBuffer.put(), &indexFormat, &indexOffset);
			context->SOGetTargets(D3D11_SO_BUFFER_SLOT_COUNT, streamOutputs.Put());
			context->GetPredication(predicate.put(), &predicateValue);
			valid = true;
		}

		void Apply(ID3D11DeviceContext1* context) const
		{
			// Clear transient outputs before restoring inputs to avoid alias-driven unbinding.
			context->ClearState();
			if (!valid)
				return;
			UINT targetCount = 0;
			for (UINT slot = 0; slot < targets.values.size(); ++slot)
				if (targets.values[slot])
					targetCount = slot + 1;
			context->OMSetRenderTargetsAndUnorderedAccessViews(targetCount, targets.Data(), depthTarget.get(), targetCount,
				uavCount - targetCount, uavCount > targetCount ? pixelOutputs.Data() + targetCount : nullptr, nullptr);
			context->CSSetUnorderedAccessViews(0, uavCount, computeOutputs.Data(), nullptr);
			vs.Apply<&ID3D11DeviceContext1::VSSetShader, &ID3D11DeviceContext1::VSSetConstantBuffers, &ID3D11DeviceContext1::VSSetConstantBuffers1, &ID3D11DeviceContext1::VSSetShaderResources, &ID3D11DeviceContext1::VSSetSamplers>(context);
			hs.Apply<&ID3D11DeviceContext1::HSSetShader, &ID3D11DeviceContext1::HSSetConstantBuffers, &ID3D11DeviceContext1::HSSetConstantBuffers1, &ID3D11DeviceContext1::HSSetShaderResources, &ID3D11DeviceContext1::HSSetSamplers>(context);
			ds.Apply<&ID3D11DeviceContext1::DSSetShader, &ID3D11DeviceContext1::DSSetConstantBuffers, &ID3D11DeviceContext1::DSSetConstantBuffers1, &ID3D11DeviceContext1::DSSetShaderResources, &ID3D11DeviceContext1::DSSetSamplers>(context);
			gs.Apply<&ID3D11DeviceContext1::GSSetShader, &ID3D11DeviceContext1::GSSetConstantBuffers, &ID3D11DeviceContext1::GSSetConstantBuffers1, &ID3D11DeviceContext1::GSSetShaderResources, &ID3D11DeviceContext1::GSSetSamplers>(context);
			ps.Apply<&ID3D11DeviceContext1::PSSetShader, &ID3D11DeviceContext1::PSSetConstantBuffers, &ID3D11DeviceContext1::PSSetConstantBuffers1, &ID3D11DeviceContext1::PSSetShaderResources, &ID3D11DeviceContext1::PSSetSamplers>(context);
			cs.Apply<&ID3D11DeviceContext1::CSSetShader, &ID3D11DeviceContext1::CSSetConstantBuffers, &ID3D11DeviceContext1::CSSetConstantBuffers1, &ID3D11DeviceContext1::CSSetShaderResources, &ID3D11DeviceContext1::CSSetSamplers>(context);
			context->OMSetBlendState(blend.get(), blendFactor.data(), sampleMask);
			context->OMSetDepthStencilState(depthState.get(), stencilRef);
			context->RSSetState(rasterizer.get());
			context->RSSetViewports(viewportCount, viewports.data());
			context->RSSetScissorRects(scissorCount, scissors.data());
			context->IASetInputLayout(layout.get());
			context->IASetPrimitiveTopology(topology);
			context->IASetVertexBuffers(0, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT, vertices.Data(), strides.data(), offsets.data());
			context->IASetIndexBuffer(indexBuffer.get(), indexFormat, indexOffset);
			// Append preserves each buffer's hidden stream-output cursor.
			constexpr std::array<UINT, D3D11_SO_BUFFER_SLOT_COUNT> append{ ~0u, ~0u, ~0u, ~0u };
			context->SOSetTargets(D3D11_SO_BUFFER_SLOT_COUNT, streamOutputs.Data(), append.data());
			context->SetPredication(predicate.get(), predicateValue);
		}

		void Reset() noexcept
		{
			std::destroy_at(this);
			std::construct_at(this);
		}

	private:
		detail::ShaderStageState<ID3D11VertexShader> vs;
		detail::ShaderStageState<ID3D11HullShader> hs;
		detail::ShaderStageState<ID3D11DomainShader> ds;
		detail::ShaderStageState<ID3D11GeometryShader> gs;
		detail::ShaderStageState<ID3D11PixelShader> ps;
		detail::ShaderStageState<ID3D11ComputeShader> cs;
		detail::StateReferences<ID3D11UnorderedAccessView, D3D11_1_UAV_SLOT_COUNT> computeOutputs, pixelOutputs;
		detail::StateReferences<ID3D11RenderTargetView, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> targets;
		detail::StateReferences<ID3D11Buffer, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> vertices;
		detail::StateReferences<ID3D11Buffer, D3D11_SO_BUFFER_SLOT_COUNT> streamOutputs;
		winrt::com_ptr<ID3D11DepthStencilView> depthTarget;
		winrt::com_ptr<ID3D11BlendState> blend;
		winrt::com_ptr<ID3D11DepthStencilState> depthState;
		winrt::com_ptr<ID3D11RasterizerState> rasterizer;
		winrt::com_ptr<ID3D11InputLayout> layout;
		winrt::com_ptr<ID3D11Buffer> indexBuffer;
		winrt::com_ptr<ID3D11Predicate> predicate;
		std::array<FLOAT, 4> blendFactor{};
		std::array<UINT, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> strides{}, offsets{};
		std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports{};
		std::array<D3D11_RECT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> scissors{};
		UINT viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
		UINT scissorCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
		UINT uavCount = 0, sampleMask = 0, stencilRef = 0, indexOffset = 0;
		DXGI_FORMAT indexFormat = DXGI_FORMAT_UNKNOWN;
		D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
		BOOL predicateValue = FALSE;
		bool valid = false;
	};
}
