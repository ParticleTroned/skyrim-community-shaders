#pragma once

#include <d3d11_1.h>
#include <winrt/base.h>

namespace UpscalingDepth
{
	enum class Stage
	{
		Compute,
		Pixel
	};

	/** Binds the scene camera for depth helpers and restores the previous buffer window. */
	class ScopedFrameBuffer
	{
	public:
		ScopedFrameBuffer(ID3D11DeviceContext* a_context, ID3D11Buffer* a_frameBuffer, Stage a_stage) :
			context(a_context), stage(a_stage)
		{
			if (!context)
				return;
			(void)context->QueryInterface(IID_PPV_ARGS(context1.put()));
			if (context1) {
				if (stage == Stage::Compute)
					context1->CSGetConstantBuffers1(kSlot, 1, previous.put(), &first, &count);
				else
					context1->PSGetConstantBuffers1(kSlot, 1, previous.put(), &first, &count);
			} else if (stage == Stage::Compute) {
				context->CSGetConstantBuffers(kSlot, 1, previous.put());
			} else {
				context->PSGetConstantBuffers(kSlot, 1, previous.put());
			}
			Bind(a_frameBuffer);
		}

		~ScopedFrameBuffer()
		{
			if (!context)
				return;
			auto* buffer = previous.get();
			if (context1 && buffer && (first != 0 || count != D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT)) {
				if (stage == Stage::Compute)
					context1->CSSetConstantBuffers1(kSlot, 1, &buffer, &first, &count);
				else
					context1->PSSetConstantBuffers1(kSlot, 1, &buffer, &first, &count);
			} else {
				Bind(buffer);
			}
		}

		ScopedFrameBuffer(const ScopedFrameBuffer&) = delete;
		ScopedFrameBuffer& operator=(const ScopedFrameBuffer&) = delete;

	private:
		void Bind(ID3D11Buffer* buffer)
		{
			if (stage == Stage::Compute)
				context->CSSetConstantBuffers(kSlot, 1, &buffer);
			else
				context->PSSetConstantBuffers(kSlot, 1, &buffer);
		}

		static constexpr UINT kSlot = 12;
		ID3D11DeviceContext* context;
		Stage stage;
		winrt::com_ptr<ID3D11DeviceContext1> context1;
		winrt::com_ptr<ID3D11Buffer> previous;
		UINT first = 0, count = D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT;
	};
}
