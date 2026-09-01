#pragma once

#include <Windows.h>
#include <cstddef>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;

namespace Util
{
	struct DetourAttachResult
	{
		long error = NO_ERROR;
		bool fallbackAllowed = false;
	};

	namespace detail
	{
		/** @brief Complete or abort only the transaction acquired by this call. */
		template <class Operations>
		DetourAttachResult AttachDetourTransaction(void** a_original, void* a_hook, const Operations& a_operations)
		{
			long result = a_operations.Begin();
			if (result != NO_ERROR)
				return { result, false };
			result = a_operations.UpdateThread();
			if (result == NO_ERROR)
				result = a_operations.Attach(a_original, a_hook);
			if (result != NO_ERROR) {
				const long abortResult = a_operations.Abort();
				return { abortResult == NO_ERROR ? result : abortResult, abortResult == NO_ERROR };
			}
			result = a_operations.Commit();
			return { result, result != NO_ERROR && result != ERROR_INVALID_OPERATION };
		}
	}

	/** @brief Attach a detour with checked transaction ownership and thread enlistment. */
	DetourAttachResult AttachDetour(void** a_original, void* a_hook);

	enum class VirtualHookMethod
	{
		Detour,
		VTable,
		Clone
	};

	struct VirtualHookResult
	{
		long error = NO_ERROR;
		long detourError = NO_ERROR;
		VirtualHookMethod method = VirtualHookMethod::Detour;
	};

	/**
	 * @brief Install a graphics hook, retaining the original call target before publication.
	 * The original storage must have process lifetime. Clones cover every known interface
	 * sharing the object's address and remain alive until process exit. Call during initialization.
	 */
	VirtualHookResult InstallVirtualFunctionHook(ID3D11Device* a_object, std::size_t a_slot, void** a_original, void* a_hook);
	/** @copydoc InstallVirtualFunctionHook(ID3D11Device*, std::size_t, void**, void*) */
	VirtualHookResult InstallVirtualFunctionHook(ID3D11DeviceContext* a_object, std::size_t a_slot, void** a_original, void* a_hook);
	/** @copydoc InstallVirtualFunctionHook(ID3D11Device*, std::size_t, void**, void*) */
	VirtualHookResult InstallVirtualFunctionHook(IDXGISwapChain* a_object, std::size_t a_slot, void** a_original, void* a_hook);
}
