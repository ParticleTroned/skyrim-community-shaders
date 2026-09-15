#pragma once

#include <d3d11.h>

namespace Util
{
	/** @brief Read-only eligibility and protection state for one D3D11 context. */
	struct ImmediateContextProtectionStatus
	{
		bool contextAvailable = false;
		bool immediateContext = false;
		UINT deviceFlags = 0;
		bool multithreadAvailable = false;
		bool multithreadProtected = false;
		HRESULT status = E_POINTER;
	};

	/** @brief Preserve device options while allowing its immediate context to serve multiple threads. */
	UINT ThreadSafeDeviceFlags(UINT a_flags) noexcept;

	/** @brief Inspect without mutation; S_OK means eligible, not necessarily protected. */
	ImmediateContextProtectionStatus InspectImmediateContextProtection(ID3D11DeviceContext* a_context) noexcept;

	/** @brief Enable and verify durable protection; never disable it or retain context ownership. */
	HRESULT ProtectImmediateContext(ID3D11DeviceContext* a_context) noexcept;

	/**
	 * @brief Protect successful creation outputs before exposing them to callers.
	 * Failed creation and success without objects retain their original result.
	 * Protection failure releases and clears every supplied output object.
	 */
	HRESULT ProtectDeviceCreation(
		HRESULT a_result,
		ID3D11Device** a_device,
		ID3D11DeviceContext** a_context,
		IDXGISwapChain** a_swapChain) noexcept;
}
