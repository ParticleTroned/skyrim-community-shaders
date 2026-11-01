#pragma once

#include "ResourceName.h"
#include <d3d11_1.h>
#include <utility>
#include <winrt/base.h>

namespace Util
{
	/** Creates an isolated D3D11 state without changing the immediate context. */
	inline bool CreateIsolatedDeviceContextState(ID3D11Device* device, ID3D11DeviceContext* context,
		winrt::com_ptr<ID3D11DeviceContext1>& context1, winrt::com_ptr<ID3DDeviceContextState>& state, const char* name)
	{
		if (!device || !context)
			return false;
		winrt::com_ptr<ID3D11Device1> device1;
		winrt::com_ptr<ID3D11DeviceContext1> candidateContext;
		winrt::com_ptr<ID3DDeviceContextState> candidateState;
		if (FAILED(device->QueryInterface(__uuidof(ID3D11Device1), device1.put_void())) ||
			FAILED(context->QueryInterface(__uuidof(ID3D11DeviceContext1), candidateContext.put_void())))
			return false;
		const auto level = device->GetFeatureLevel();
		const UINT flags = device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED;
		if (FAILED(device1->CreateDeviceContextState(flags, &level, 1, D3D11_SDK_VERSION,
				__uuidof(ID3D11Device), nullptr, candidateState.put())))
			return false;
		SetResourceName(candidateState.get(), "%s", name);
		context1 = std::move(candidateContext);
		state = std::move(candidateState);
		return true;
	}

	/** Restores the complete prior pipeline after rendering into isolated state. */
	class ScopedDeviceContextState
	{
	public:
		ScopedDeviceContextState(ID3D11DeviceContext1* a_context, ID3DDeviceContextState* isolatedState) : context(a_context)
		{
			if (!context || !isolatedState)
				return;
			context->SwapDeviceContextState(isolatedState, previousState.put());
			active = true;
		}
		ScopedDeviceContextState(const ScopedDeviceContextState&) = delete;
		ScopedDeviceContextState& operator=(const ScopedDeviceContextState&) = delete;
		~ScopedDeviceContextState() { Restore(); }
		explicit operator bool() const noexcept { return active; }
		/** Restores once; subsequent calls and destruction are harmless. */
		void Restore() noexcept
		{
			if (!active)
				return;
			active = false;
			context->SwapDeviceContextState(previousState.get(), nullptr);
		}

	private:
		ID3D11DeviceContext1* context = nullptr;
		winrt::com_ptr<ID3DDeviceContextState> previousState;
		bool active = false;
	};
}
