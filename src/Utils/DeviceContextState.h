#pragma once

#include "ComIdentity.h"
#include "ManualDeviceContextState.h"
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
		inline Microsoft::WRL::ComPtr<IUnknown> registeredStateIdentity;
		inline bool contextStateIsolationAvailable = false;
		inline bool useManualContextState = false;

		inline bool IsContextStateAvailable(ID3D11DeviceContext* context)
		{
			if (contextStateIsolationAvailable || !registeredStateContext)
				return true;
			if (context == registeredStateContext.get())
				return false;
			const auto identity = GetComIdentity(context);
			return identity && registeredStateIdentity && identity.Get() != registeredStateIdentity.Get();
		}
	}

	/** Holds native state or preallocated binding snapshots, including a prepared pass between scopes. */
	struct IsolatedDeviceContextState
	{
		winrt::com_ptr<ID3DDeviceContextState> native;
		std::unique_ptr<ManualDeviceContextState> bindings, previousBindings;
		bool active = false;
	};

	/** Selects binding restoration before any isolated state is created. */
	inline void UseManualDeviceContextState(bool enabled) noexcept
	{
		detail::useManualContextState = enabled;
	}

	/** Creates isolated state without changing the immediate context; failure retains prior resources. */
	inline bool CreateIsolatedDeviceContextState(ID3D11Device* device, ID3D11DeviceContext* context,
		winrt::com_ptr<ID3D11DeviceContext1>& context1, std::unique_ptr<IsolatedDeviceContextState>& state, const char* name)
	{
		if (!device || !context || (!detail::useManualContextState && !detail::IsContextStateAvailable(context)))
			return false;
		winrt::com_ptr<ID3D11DeviceContext1> candidateContext;
		if (FAILED(context->QueryInterface(__uuidof(ID3D11DeviceContext1), candidateContext.put_void())))
			return false;
		auto candidate = std::unique_ptr<IsolatedDeviceContextState>(new (std::nothrow) IsolatedDeviceContextState);
		if (!candidate)
			return false;
		if (detail::useManualContextState) {
			candidate->bindings.reset(new (std::nothrow) ManualDeviceContextState);
			candidate->previousBindings.reset(new (std::nothrow) ManualDeviceContextState);
			if (!candidate->bindings || !candidate->previousBindings)
				return false;
		} else {
			winrt::com_ptr<ID3D11Device1> device1;
			if (FAILED(device->QueryInterface(__uuidof(ID3D11Device1), device1.put_void())))
				return false;
			const auto level = device->GetFeatureLevel();
			const UINT flags = device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED;
			if (FAILED(device1->CreateDeviceContextState(flags, &level, 1, D3D11_SDK_VERSION,
					__uuidof(ID3D11Device), nullptr, candidate->native.put())))
				return false;
			SetResourceName(candidate->native.get(), "%s", name);
		}
		context1 = std::move(candidateContext);
		state = std::move(candidate);
		return true;
	}

	/** Restores the complete prior pipeline and retains the isolated pass for its next scope. */
	class ScopedDeviceContextState
	{
	public:
		ScopedDeviceContextState(ID3D11DeviceContext1* a_context, IsolatedDeviceContextState* isolatedState) : context(a_context), isolated(isolatedState)
		{
			if (!context || !isolated || isolated->active || (!isolated->bindings && !detail::IsContextStateAvailable(context)))
				return;
			if (captureDeviceContextState)
				captureDeviceContextState(context, logicalState);
			if (isolated->bindings) {
				isolated->previousBindings->Capture(context);
				isolated->bindings->Apply(context);
			} else {
				context->SwapDeviceContextState(isolated->native.get(), previousState.put());
			}
			isolated->active = active = true;
		}
		ScopedDeviceContextState(const ScopedDeviceContextState&) = delete;
		ScopedDeviceContextState& operator=(const ScopedDeviceContextState&) = delete;
		~ScopedDeviceContextState() { Restore(); }
		explicit operator bool() const noexcept { return active; }
		/** Restores once, including on early return or exception. */
		void Restore() noexcept
		{
			if (!active)
				return;
			isolated->active = active = false;
			if (isolated->bindings) {
				isolated->bindings->Capture(context);
				isolated->previousBindings->Apply(context);
				isolated->previousBindings->Reset();
			} else {
				context->SwapDeviceContextState(previousState.get(), nullptr);
			}
			logicalState.Restore();
		}

	private:
		ID3D11DeviceContext1* context = nullptr;
		IsolatedDeviceContextState* isolated = nullptr;
		winrt::com_ptr<ID3DDeviceContextState> previousState;
		DeviceContextStateSnapshot logicalState;
		bool active = false;
	};

	/** Registers the renderer context before hooks; repeats preserve a disabled isolation capability. */
	inline void RegisterDeviceContextStateForHooks(ID3D11DeviceContext* context)
	{
		if (!context || detail::registeredStateContext.get() == context)
			return;
		auto identity = GetComIdentity(context);
		const bool sameContext = identity && identity.Get() == detail::registeredStateIdentity.Get();
		detail::registeredStateContext.copy_from(context);
		detail::registeredStateIdentity = std::move(identity);
		if (!sameContext)
			detail::contextStateIsolationAvailable = true;
	}

	/** Disables swaps that would invalidate native table hooks; existing isolated states also become unusable. */
	inline void DisableDeviceContextStateIsolation(ID3D11DeviceContext* context)
	{
		if (!context || !detail::registeredStateContext || !detail::contextStateIsolationAvailable)
			return;
		if (context == detail::registeredStateContext.get()) {
			detail::contextStateIsolationAvailable = false;
			return;
		}
		// Only IUnknown identity is shared by every interface view of the context.
		const auto identity = GetComIdentity(context);
		if (identity && identity.Get() == detail::registeredStateIdentity.Get())
			detail::contextStateIsolationAvailable = false;
	}
}
