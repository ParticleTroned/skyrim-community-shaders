#include "VirtualFunctionHook.h"

#include <d3d11_4.h>
#include <detours/detours.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <system_error>
#include <vector>

namespace
{
	constexpr std::size_t kDeviceSlots = 43;
	constexpr std::size_t kContextSlots = 115;
	constexpr std::size_t kSwapChainSlots = 18;

	struct DetourOperations
	{
		long Begin() const { return DetourTransactionBegin(); }
		long UpdateThread() const { return DetourUpdateThread(GetCurrentThread()); }
		long Attach(void** a_original, void* a_hook) const { return DetourAttach(a_original, a_hook); }
		long Abort() const { return DetourTransactionAbort(); }
		long Commit() const { return DetourTransactionCommit(); }
	};

	struct HookRecord
	{
		void** originalStorage;
		void* target;
		void* originalCall;
		void* hook;
		Util::VirtualHookMethod method;
		long detourError;
	};

	struct HookState
	{
		std::mutex mutex;
		std::vector<HookRecord> hooks;
		std::vector<std::unique_ptr<void*[]>> clones;
	};

	bool ReadPointers(void* a_destination, const void* a_source, std::size_t a_bytes) noexcept
	{
		__try {
			std::memcpy(a_destination, a_source, a_bytes);
			return true;
		} __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION || GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR ?
						EXCEPTION_EXECUTE_HANDLER :
						EXCEPTION_CONTINUE_SEARCH) {
			return false;
		}
	}

	long ReplacePointer(void* volatile* a_destination, void* a_expected, void* a_replacement) noexcept
	{
		__try {
			return InterlockedCompareExchangePointer(a_destination, a_replacement, a_expected) == a_expected ? NO_ERROR : ERROR_RETRY;
		} __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION || GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR ?
						EXCEPTION_EXECUTE_HANDLER :
						EXCEPTION_CONTINUE_SEARCH) {
			return ERROR_NOACCESS;
		}
	}

	template <class Interface>
	bool IncludeInterfaceSlots(void* a_object, std::size_t& a_slots, std::size_t a_interfaceSlots)
	{
		Microsoft::WRL::ComPtr<Interface> extended;
		const auto result = static_cast<IUnknown*>(a_object)->QueryInterface(IID_PPV_ARGS(extended.GetAddressOf()));
		if (result == E_NOINTERFACE)
			return true;
		if (FAILED(result) || !extended)
			return false;
		if (static_cast<void*>(extended.Get()) == a_object)
			a_slots = (std::max)(a_slots, a_interfaceSlots);
		return true;
	}

	// Counts include inherited methods, as declared by the Windows SDK COM interfaces.
	std::size_t DeviceSlots(void* a_object)
	{
		std::size_t slots = kDeviceSlots;
		return IncludeInterfaceSlots<ID3D11Device1>(a_object, slots, 50) &&
		               IncludeInterfaceSlots<ID3D11Device2>(a_object, slots, 54) &&
		               IncludeInterfaceSlots<ID3D11Device3>(a_object, slots, 65) &&
		               IncludeInterfaceSlots<ID3D11Device4>(a_object, slots, 67) &&
		               IncludeInterfaceSlots<ID3D11Device5>(a_object, slots, 69) ?
		           slots :
		           0;
	}

	std::size_t ContextSlots(void* a_object)
	{
		std::size_t slots = kContextSlots;
		return IncludeInterfaceSlots<ID3D11DeviceContext1>(a_object, slots, 134) &&
		               IncludeInterfaceSlots<ID3D11DeviceContext2>(a_object, slots, 144) &&
		               IncludeInterfaceSlots<ID3D11DeviceContext3>(a_object, slots, 147) &&
		               IncludeInterfaceSlots<ID3D11DeviceContext4>(a_object, slots, 149) ?
		           slots :
		           0;
	}

	std::size_t SwapChainSlots(void* a_object)
	{
		std::size_t slots = kSwapChainSlots;
		return IncludeInterfaceSlots<IDXGISwapChain1>(a_object, slots, 29) &&
		               IncludeInterfaceSlots<IDXGISwapChain2>(a_object, slots, 36) &&
		               IncludeInterfaceSlots<IDXGISwapChain3>(a_object, slots, 40) &&
		               IncludeInterfaceSlots<IDXGISwapChain4>(a_object, slots, 41) ?
		           slots :
		           0;
	}

	using AttachFunction = Util::DetourAttachResult (*)(void**, void*);
	using SlotCountFunction = std::size_t (*)(void*);

	Util::VirtualHookResult InstallHook(HookState& a_state, void* a_object, std::size_t a_slot,
		std::size_t a_baseSlots, SlotCountFunction a_slotCount, void** a_original, void* a_hook,
		AttachFunction a_attach = Util::AttachDetour, decltype(&VirtualProtect) a_protect = VirtualProtect)
	{
		using enum Util::VirtualHookMethod;
		if (!a_object || !a_original || !a_hook || a_slot >= a_baseSlots)
			return { ERROR_INVALID_PARAMETER };
		std::scoped_lock lock(a_state.mutex);
		void** table = nullptr;
		void* target = nullptr;
		if (!ReadPointers(&table, a_object, sizeof(table)) || !table ||
			!ReadPointers(&target, table + a_slot, sizeof(target)) || !target)
			return { ERROR_NOACCESS };

		const auto previous = std::find_if(a_state.hooks.begin(), a_state.hooks.end(),
			[&](const auto& record) { return record.originalStorage == a_original; });
		const bool existingHook = previous != a_state.hooks.end();
		if (existingHook) {
			if (previous->hook != a_hook || previous->originalCall != *a_original)
				return { ERROR_INVALID_DATA };
			if (target == a_hook || (previous->method == Detour && target == previous->target))
				return { NO_ERROR, NO_ERROR, previous->method };
			if (target != previous->target)
				return { ERROR_INVALID_FUNCTION };
		} else if (target == a_hook || *a_original) {
			return { ERROR_INVALID_DATA };
		}

		const long previousDetourError = existingHook ? previous->detourError : NO_ERROR;
		// Reserve bookkeeping before changing dispatch; no allocation may fail after publication.
		a_state.hooks.reserve(a_state.hooks.size() + 1);
		Util::DetourAttachResult detour{ previousDetourError, true };
		if (!existingHook) {
			*a_original = target;
			detour = a_attach(a_original, a_hook);
			if (detour.error == NO_ERROR) {
				a_state.hooks.push_back({ a_original, target, *a_original, a_hook, Detour, NO_ERROR });
				return {};
			}
			*a_original = target;
			if (!detour.fallbackAllowed)
				return { detour.error, detour.error };
		}

		const auto remember = [&](Util::VirtualHookMethod a_method) {
			if (!existingHook)
				a_state.hooks.push_back({ a_original, target, *a_original, a_hook, a_method, detour.error });
		};
		const bool ownedTable = std::any_of(a_state.clones.begin(), a_state.clones.end(),
			[&](const auto& clone) { return clone.get() == table; });
		MEMORY_BASIC_INFORMATION info{};
		const bool queried = VirtualQuery(table + a_slot, &info, sizeof(info)) == sizeof(info);
		constexpr DWORD executable = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
		const DWORD writable = queried && (info.Protect & executable) ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE;
		DWORD oldProtection = 0;
		if (ownedTable || a_protect(table + a_slot, sizeof(void*), writable, &oldProtection)) {
			const long publishError = ReplacePointer(table + a_slot, target, a_hook);
			DWORD ignored = 0;
			const long restoreError = !ownedTable && !a_protect(table + a_slot, sizeof(void*), oldProtection, &ignored) ?
			                              static_cast<long>(GetLastError()) :
			                              NO_ERROR;
			if (publishError == NO_ERROR)
				remember(ownedTable ? Clone : VTable);
			return { restoreError != NO_ERROR ? restoreError : publishError,
				detour.error, ownedTable ? Clone : VTable };
		}

		const auto slots = a_slotCount(a_object);
		if (slots < a_baseSlots)
			return { ERROR_INVALID_DATA, detour.error };
		auto clone = std::make_unique<void*[]>(slots);
		if (!ReadPointers(clone.get(), table, slots * sizeof(void*)) ||
			std::any_of(clone.get(), clone.get() + slots, [](void* entry) { return !entry; }))
			return { ERROR_NOACCESS, detour.error };
		if (clone[a_slot] != target)
			return { ERROR_RETRY, detour.error };
		clone[a_slot] = a_hook;
		void* clonedTable = clone.get();
		a_state.clones.push_back(std::move(clone));
		const long publishError = ReplacePointer(static_cast<void* volatile*>(a_object), table, clonedTable);
		if (publishError != NO_ERROR) {
			a_state.clones.pop_back();
			return { publishError, detour.error };
		}
		remember(Clone);
		return { NO_ERROR, detour.error, Clone };
	}

	Util::VirtualHookResult InstallGraphicsHook(void* a_object, std::size_t a_slot, std::size_t a_baseSlots,
		SlotCountFunction a_slotCount, void** a_original, void* a_hook)
	{
		try {
			// Graphics objects can outlive DLL static destructors and retain pointers into these clones.
			static auto& state = *new HookState;
			return InstallHook(state, a_object, a_slot, a_baseSlots, a_slotCount, a_original, a_hook);
		} catch (const std::bad_alloc&) {
			return { ERROR_NOT_ENOUGH_MEMORY };
		} catch (const std::system_error&) {
			return { ERROR_LOCK_FAILED };
		}
	}
}

Util::DetourAttachResult Util::AttachDetour(void** a_original, void* a_hook)
{
	return detail::AttachDetourTransaction(a_original, a_hook, DetourOperations{});
}

Util::VirtualHookResult Util::InstallVirtualFunctionHook(ID3D11Device* a_object, std::size_t a_slot, void** a_original, void* a_hook)
{
	return InstallGraphicsHook(a_object, a_slot, kDeviceSlots, DeviceSlots, a_original, a_hook);
}

Util::VirtualHookResult Util::InstallVirtualFunctionHook(ID3D11DeviceContext* a_object, std::size_t a_slot, void** a_original, void* a_hook)
{
	return InstallGraphicsHook(a_object, a_slot, kContextSlots, ContextSlots, a_original, a_hook);
}

Util::VirtualHookResult Util::InstallVirtualFunctionHook(IDXGISwapChain* a_object, std::size_t a_slot, void** a_original, void* a_hook)
{
	return InstallGraphicsHook(a_object, a_slot, kSwapChainSlots, SwapChainSlots, a_original, a_hook);
}
