#pragma once

#include <d3d11.h>
#include <utility>

#include "RendererOwnership.h"

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

	/** @brief Validate shared-context eligibility without changing another owner's API protection flag. */
	HRESULT ValidateImmediateContext(ID3D11DeviceContext* a_context) noexcept;

	/**
	 * @brief Validate successful creation outputs before exposing them to callers.
	 * Failed creation and success without objects retain their original result.
	 * Validation failure releases and clears every supplied output object.
	 */
	HRESULT ValidateDeviceCreation(
		HRESULT a_result,
		ID3D11Device** a_device,
		ID3D11DeviceContext** a_context,
		IDXGISwapChain** a_swapChain) noexcept;

	/** @brief Try one staging read under renderer ownership; release before any caller retry or encoding. */
	template <class Callback>
	HRESULT TryReadbackWithRendererOwnership(ID3D11DeviceContext* a_context, ID3D11Resource* a_staging,
		CRITICAL_SECTION* a_lock, Callback&& a_copy)
	{
		if (!a_context || !a_staging || !a_lock)
			return E_POINTER;
		const RendererOwnership ownership(a_lock);
		if (!ownership)
			return DXGI_ERROR_WAS_STILL_DRAWING;
		D3D11_MAPPED_SUBRESOURCE mapped{};
		const auto result = a_context->Map(a_staging, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
		if (FAILED(result))
			return result;
		struct Unmap
		{
			ID3D11DeviceContext* context;
			ID3D11Resource* staging;
			~Unmap() { context->Unmap(staging, 0); }
		} unmap{ a_context, a_staging };
		return std::forward<Callback>(a_copy)(mapped);
	}
}
