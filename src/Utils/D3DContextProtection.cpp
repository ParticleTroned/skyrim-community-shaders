#include "D3DContextProtection.h"

#include <d3d11_4.h>
#include <wrl/client.h>

namespace
{
	using Microsoft::WRL::ComPtr;

	Util::ImmediateContextProtectionStatus InspectContext(
		ID3D11DeviceContext* a_context,
		ComPtr<ID3D11Multithread>& a_multithread) noexcept
	{
		Util::ImmediateContextProtectionStatus result;
		if (!a_context)
			return result;
		result.contextAvailable = true;
		result.immediateContext = a_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE;
		ComPtr<ID3D11Device> device;
		a_context->GetDevice(device.GetAddressOf());
		if (!device) {
			result.status = E_NOINTERFACE;
			return result;
		}
		result.deviceFlags = device->GetCreationFlags();
		if (!result.immediateContext) {
			result.status = E_INVALIDARG;
			return result;
		}
		if ((result.deviceFlags & D3D11_CREATE_DEVICE_SINGLETHREADED) != 0) {
			result.status = DXGI_ERROR_INVALID_CALL;
			return result;
		}
		result.status = a_context->QueryInterface(IID_PPV_ARGS(a_multithread.GetAddressOf()));
		if (FAILED(result.status))
			return result;
		if (!a_multithread) {
			result.status = E_NOINTERFACE;
			return result;
		}
		result.multithreadAvailable = true;
		result.multithreadProtected = a_multithread->GetMultithreadProtected() != FALSE;
		return result;
	}

	template <class T>
	void ReleaseOutput(T** a_output) noexcept
	{
		if (a_output && *a_output) {
			auto* object = *a_output;
			*a_output = nullptr;
			object->Release();
		}
	}
}

UINT Util::ThreadSafeDeviceFlags(UINT a_flags) noexcept
{
	return a_flags & ~static_cast<UINT>(D3D11_CREATE_DEVICE_SINGLETHREADED);
}

Util::ImmediateContextProtectionStatus Util::InspectImmediateContextProtection(ID3D11DeviceContext* a_context) noexcept
{
	ComPtr<ID3D11Multithread> multithread;
	return InspectContext(a_context, multithread);
}

HRESULT Util::ProtectImmediateContext(ID3D11DeviceContext* a_context) noexcept
{
	ComPtr<ID3D11Multithread> multithread;
	const auto observation = InspectContext(a_context, multithread);
	if (FAILED(observation.status))
		return observation.status;
	if (!observation.multithreadProtected)
		multithread->SetMultithreadProtected(TRUE);
	return multithread->GetMultithreadProtected() != FALSE ? S_OK : E_FAIL;
}

HRESULT Util::ProtectDeviceCreation(
	HRESULT a_result,
	ID3D11Device** a_device,
	ID3D11DeviceContext** a_context,
	IDXGISwapChain** a_swapChain) noexcept
{
	if (FAILED(a_result))
		return a_result;
	if ((!a_device || !*a_device) && (!a_context || !*a_context) && (!a_swapChain || !*a_swapChain))
		return a_result;

	ComPtr<ID3D11DeviceContext> context;
	ComPtr<ID3D11Device> device;
	HRESULT protectionResult = S_OK;
	if (a_context && *a_context) {
		context = *a_context;
	} else {
		if (a_device && *a_device)
			device = *a_device;
		else
			protectionResult = (*a_swapChain)->GetDevice(IID_PPV_ARGS(device.GetAddressOf()));
		if (SUCCEEDED(protectionResult) && device)
			device->GetImmediateContext(context.GetAddressOf());
	}
	if (SUCCEEDED(protectionResult))
		protectionResult = ProtectImmediateContext(context.Get());
	if (SUCCEEDED(protectionResult))
		return a_result;

	ReleaseOutput(a_swapChain);
	ReleaseOutput(a_context);
	ReleaseOutput(a_device);
	return protectionResult;
}
