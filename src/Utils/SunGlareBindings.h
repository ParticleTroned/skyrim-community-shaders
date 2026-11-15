#pragma once

#include "ComIdentity.h"
#include <d3d11_1.h>
#include <winrt/base.h>

namespace SunGlare
{
	/** @brief Own the vertex depth and frame constants only for a sun-glare geometry draw. */
	class ScopedBindings
	{
	public:
		ScopedBindings(ID3D11DeviceContext* a_context, ID3D11ShaderResourceView* a_depth)
		{
			context.copy_from(a_context);
			if (FAILED(context->QueryInterface(IID_PPV_ARGS(context1.put()))))
				context1 = nullptr;
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
			if (a_depth) {
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
			if (context1)
				context1->VSSetConstantBuffers1(12, 1, &buffer, &frameFirst, &frameCount);
			else
				context->VSSetConstantBuffers(12, 1, &buffer);
			context->VSSetShaderResources(17, 1, &a_depth);
		}

		~ScopedBindings()
		{
			auto* depth = previousDepth.get();
			context->VSSetShaderResources(17, 1, &depth);
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
		UINT previousFirst = 0, previousCount = 0;
	};
}
