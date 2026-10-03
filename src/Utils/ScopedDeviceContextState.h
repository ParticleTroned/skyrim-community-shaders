#pragma once

#include <d3d11_1.h>
#include <winrt/base.h>

namespace Util
{
	/** Isolates the complete pipeline, including bindings displaced by resource hazards. */
	class ScopedDeviceContextState
	{
	public:
		ScopedDeviceContextState(ID3D11DeviceContext1* a_context, ID3DDeviceContextState* a_state) : context(a_context)
		{
			if (context && a_state) {
				context->SwapDeviceContextState(a_state, previous.put());
				active = true;
			}
		}
		ScopedDeviceContextState(const ScopedDeviceContextState&) = delete;
		ScopedDeviceContextState& operator=(const ScopedDeviceContextState&) = delete;
		~ScopedDeviceContextState() { Restore(); }
		explicit operator bool() const noexcept { return active; }
		void Restore() noexcept
		{
			if (active) {
				active = false;
				context->SwapDeviceContextState(previous.get(), nullptr);
			}
		}

	private:
		ID3D11DeviceContext1* context = nullptr;
		winrt::com_ptr<ID3DDeviceContextState> previous;
		bool active = false;
	};
}
