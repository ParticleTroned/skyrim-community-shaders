#pragma once

#include "ComIdentity.h"
#include <d3d11_1.h>
#include <winrt/base.h>

namespace SkyShader
{
	/** Retains one render-thread context and its optional D3D11.1 interface, including unsupported results. */
	class ContextCache
	{
	public:
		/** Reuses resolved interface support, retries transient failures, and clears on null. */
		[[nodiscard]] ID3D11DeviceContext1* GetExtended(ID3D11DeviceContext* a_context) noexcept
		{
			if (context.get() != a_context) {
				context1 = nullptr;
				context.copy_from(a_context);
				if (context) {
					const auto result = context->QueryInterface(IID_PPV_ARGS(context1.put()));
					if (FAILED(result)) {
						context1 = nullptr;
						if (result != E_NOINTERFACE)
							context = nullptr;
					}
				}
			}
			return context1.get();
		}

	private:
		winrt::com_ptr<ID3D11DeviceContext> context;
		winrt::com_ptr<ID3D11DeviceContext1> context1;
	};

	/** @brief Scope sky vertex constants and optional sun-glare depth without disturbing native bindings. */
	class ScopedBindings
	{
	public:
		ScopedBindings(ContextCache& a_cache, ID3D11DeviceContext* a_context, ID3D11ShaderResourceView* a_depth,
			ID3D11Buffer* a_permutation = nullptr, bool a_bindDepth = true) :
			depthBound(a_bindDepth), permutationBound(a_permutation != nullptr)
		{
			context.copy_from(a_context);
			context1.copy_from(a_cache.GetExtended(a_context));
			if (depthBound)
				context->VSGetShaderResources(17, 1, previousDepth.put());
			winrt::com_ptr<ID3D11Buffer> frame;
			UINT frameFirst = 0, frameCount = 0;
			if (context1) {
				context1->VSGetConstantBuffers1(12, 1, previousFrame.put(), &previousFirst, &previousCount);
				context1->PSGetConstantBuffers1(12, 1, frame.put(), &frameFirst, &frameCount);
			} else {
				context->VSGetConstantBuffers(12, 1, previousFrame.put());
				context->PSGetConstantBuffers(12, 1, frame.put());
			}

			if (!frame)
				a_depth = nullptr;
			if (depthBound && a_depth) {
				winrt::com_ptr<ID3D11DepthStencilView> dsv;
				context->OMGetRenderTargets(0, nullptr, dsv.put());
				if (dsv) {
					D3D11_DEPTH_STENCIL_VIEW_DESC desc{};
					dsv->GetDesc(&desc);
					if (!(desc.Flags & D3D11_DSV_READ_ONLY_DEPTH)) {
						winrt::com_ptr<ID3D11Resource> input, output;
						a_depth->GetResource(input.put());
						dsv->GetResource(output.put());
						// Read-only depth attachments may also supply the visibility samples.
						const auto inputIdentity = Util::GetComIdentity(input.get());
						const auto outputIdentity = Util::GetComIdentity(output.get());
						if (!inputIdentity || !outputIdentity || inputIdentity == outputIdentity)
							a_depth = nullptr;
					}
				}
			}
			auto* buffer = frame.get();
			if (context1) {
				if (buffer != previousFrame.get() || frameFirst != previousFirst || frameCount != previousCount)
					context1->VSSetConstantBuffers1(12, 1, &buffer, &frameFirst, &frameCount);
			} else {
				// Without range queries, retain the whole-buffer setter's native behavior.
				context->VSSetConstantBuffers(12, 1, &buffer);
			}
			if (depthBound && a_depth != previousDepth.get())
				context->VSSetShaderResources(17, 1, &a_depth);
			if (permutationBound) {
				if (context1)
					context1->VSGetConstantBuffers1(4, 1, previousPermutation.put(), &previousPermutationFirst, &previousPermutationCount);
				else
					context->VSGetConstantBuffers(4, 1, previousPermutation.put());
				if (!context1 || previousPermutation.get() != a_permutation ||
					previousPermutationFirst != 0 || previousPermutationCount != D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT)
					context->VSSetConstantBuffers(4, 1, &a_permutation);
			}
		}

		~ScopedBindings()
		{
			// Draw work can change a slot even when its entry binding already matched.
			if (depthBound) {
				auto* depth = previousDepth.get();
				context->VSSetShaderResources(17, 1, &depth);
			}
			if (permutationBound) {
				auto* permutation = previousPermutation.get();
				if (context1)
					context1->VSSetConstantBuffers1(4, 1, &permutation, &previousPermutationFirst, &previousPermutationCount);
				else
					context->VSSetConstantBuffers(4, 1, &permutation);
			}
			auto* frame = previousFrame.get();
			if (context1)
				context1->VSSetConstantBuffers1(12, 1, &frame, &previousFirst, &previousCount);
			else
				context->VSSetConstantBuffers(12, 1, &frame);
		}

		ScopedBindings(const ScopedBindings&) = delete;
		ScopedBindings& operator=(const ScopedBindings&) = delete;

	private:
		winrt::com_ptr<ID3D11DeviceContext> context;
		winrt::com_ptr<ID3D11DeviceContext1> context1;
		winrt::com_ptr<ID3D11ShaderResourceView> previousDepth;
		winrt::com_ptr<ID3D11Buffer> previousFrame;
		winrt::com_ptr<ID3D11Buffer> previousPermutation;
		UINT previousFirst = 0, previousCount = 0;
		UINT previousPermutationFirst = 0, previousPermutationCount = 0;
		bool depthBound, permutationBound;
	};
}
