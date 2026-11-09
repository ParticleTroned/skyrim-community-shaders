#pragma once

#include "ResourceName.h"
#include <cstddef>
#include <d3d11_1.h>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <winrt/base.h>

namespace Util
{
	/** Owns renderer bookkeeping inline so native-draw recovery cannot allocate. */
	class DeviceContextStateSnapshot
	{
	public:
		DeviceContextStateSnapshot() noexcept = default;
		DeviceContextStateSnapshot(const DeviceContextStateSnapshot&) = delete;
		DeviceContextStateSnapshot& operator=(const DeviceContextStateSnapshot&) = delete;
		~DeviceContextStateSnapshot() { Restore(); }
		explicit operator bool() const noexcept { return restoreState != nullptr; }

		/** Captures a bounded, nonthrowing state whose destructor restores its bookkeeping. */
		template <class State, class... Args>
		void Emplace(Args&&... args) noexcept
		{
			static_assert(sizeof(State) <= kCapacity && alignof(State) <= alignof(std::max_align_t));
			static_assert(std::is_nothrow_constructible_v<State, Args...> && std::is_nothrow_destructible_v<State>);
			Restore();
			std::construct_at(reinterpret_cast<State*>(storage), std::forward<Args>(args)...);
			restoreState = [](void* state) noexcept { std::destroy_at(std::launder(static_cast<State*>(state))); };
		}

		/** Restores once, after the matching native context state has been restored. */
		void Restore() noexcept
		{
			if (const auto restore = std::exchange(restoreState, nullptr))
				restore(storage);
		}

	private:
		static constexpr std::size_t kCapacity = 1024;
		alignas(std::max_align_t) std::byte storage[kCapacity];
		void (*restoreState)(void*) noexcept = nullptr;
	};

	using CaptureDeviceContextState = void (*)(ID3D11DeviceContext*, DeviceContextStateSnapshot&) noexcept;
	/** Registered during renderer initialization; a null observer adds no capture work. */
	inline CaptureDeviceContextState captureDeviceContextState = nullptr;

	namespace detail
	{
		inline winrt::com_ptr<ID3D11DeviceContext> registeredStateContext;
		inline bool contextStateIsolationAvailable = false;

		inline bool IsContextStateAvailable(ID3D11DeviceContext* context)
		{
			if (contextStateIsolationAvailable || !registeredStateContext)
				return true;
			if (context == registeredStateContext.get())
				return false;
			winrt::com_ptr<ID3D11DeviceContext> canonical;
			return SUCCEEDED(context->QueryInterface(__uuidof(ID3D11DeviceContext), canonical.put_void())) &&
			       canonical.get() != registeredStateContext.get();
		}
	}

	/** Creates an isolated D3D11 state without changing the immediate context. */
	inline bool CreateIsolatedDeviceContextState(ID3D11Device* device, ID3D11DeviceContext* context,
		winrt::com_ptr<ID3D11DeviceContext1>& context1, winrt::com_ptr<ID3DDeviceContextState>& state, const char* name)
	{
		if (!device || !context || !detail::IsContextStateAvailable(context))
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
			if (!context || !isolatedState || !detail::IsContextStateAvailable(context))
				return;
			if (captureDeviceContextState)
				captureDeviceContextState(context, logicalState);
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
			logicalState.Restore();
		}

	private:
		ID3D11DeviceContext1* context = nullptr;
		winrt::com_ptr<ID3DDeviceContextState> previousState;
		DeviceContextStateSnapshot logicalState;
		bool active = false;
	};

	/** Registers the renderer context before hooks; repeats preserve a disabled isolation capability. */
	inline void RegisterDeviceContextStateForHooks(ID3D11DeviceContext* context)
	{
		if (!context || detail::registeredStateContext.get() == context)
			return;
		detail::registeredStateContext.copy_from(context);
		detail::contextStateIsolationAvailable = true;
	}

	/** Disables swaps that would invalidate native table hooks; existing isolated states also become unusable. */
	inline void DisableDeviceContextStateIsolation(ID3D11DeviceContext* context)
	{
		if (context == detail::registeredStateContext.get())
			detail::contextStateIsolationAvailable = false;
	}
}
