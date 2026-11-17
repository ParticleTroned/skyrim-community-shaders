#pragma once

#include <Unknwn.h>
#include <Windows.h>

#include <atomic>
#include <new>
#include <type_traits>
#include <utility>

namespace Util
{
	/** @brief Run diagnostic cleanup when D3D releases its private-data reference.
	 * The callback must not retain or dereference the D3D owner. Attachment failure
	 * releases the unarmed callback without invoking it.
	 */
	template <class DeviceChild, class Callback>
	HRESULT AttachD3DPrivateDataLifetime(
		DeviceChild& a_owner, REFGUID a_guid, Callback a_callback) noexcept
	{
		static_assert(std::is_nothrow_invocable_v<Callback&>);
		static_assert(std::is_nothrow_move_constructible_v<Callback>);
		class Sentinel final : public IUnknown
		{
		public:
			explicit Sentinel(Callback a_cleanup) noexcept : cleanup(std::move(a_cleanup)) {}
			void Arm() noexcept { armed = true; }
			HRESULT STDMETHODCALLTYPE QueryInterface(REFIID a_id, void** a_result) override
			{
				if (!a_result)
					return E_POINTER;
				*a_result = nullptr;
				if (a_id != __uuidof(IUnknown))
					return E_NOINTERFACE;
				*a_result = static_cast<IUnknown*>(this);
				AddRef();
				return S_OK;
			}
			ULONG STDMETHODCALLTYPE AddRef() override
			{
				return references.fetch_add(1, std::memory_order_relaxed) + 1;
			}
			ULONG STDMETHODCALLTYPE Release() override
			{
				const auto remaining = references.fetch_sub(1, std::memory_order_acq_rel) - 1;
				if (remaining == 0) {
					if (armed)
						cleanup();
					delete this;
				}
				return remaining;
			}

		private:
			std::atomic_ulong references{ 1 };
			bool armed{ false };
			Callback cleanup;
		};

		auto* sentinel = new (std::nothrow) Sentinel(std::move(a_callback));
		if (!sentinel)
			return E_OUTOFMEMORY;
		const auto result = a_owner.SetPrivateDataInterface(a_guid, sentinel);
		if (SUCCEEDED(result))
			sentinel->Arm();
		sentinel->Release();
		return result;
	}
}
