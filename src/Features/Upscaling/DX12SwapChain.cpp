#include "DX12SwapChain.h"

#include <FidelityFX/api/include/dx12/ffx_api_dx12.hpp>
#include <dxgi1_6.h>

#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

#include "../Upscaling.h"
#include "FidelityFX.h"
#include "Streamline.h"

void DX12SwapChain::CreateD3D12Device(IDXGIAdapter* a_adapter)
{
	DX::ThrowIfFailed(D3D12CreateDevice(a_adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&d3d12Device)));

	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	queueDesc.NodeMask = 0;

	DX::ThrowIfFailed(d3d12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue)));

	for (int i = 0; i < 2; i++) {
		DX::ThrowIfFailed(d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocators[i])));
		DX::ThrowIfFailed(d3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocators[i].get(), nullptr, IID_PPV_ARGS(&commandLists[i])));
		commandLists[i]->Close();
	}
}

void DX12SwapChain::CreateSwapChain(IDXGIAdapter* adapter, DXGI_SWAP_CHAIN_DESC a_swapChainDesc)
{
	CreateD3D12Device(adapter);

	winrt::com_ptr<IDXGIFactory4> dxgiFactory;
	DX::ThrowIfFailed(adapter->GetParent(IID_PPV_ARGS(dxgiFactory.put())));

	swapChainDesc = {};
	swapChainDesc.Width = a_swapChainDesc.BufferDesc.Width;
	swapChainDesc.Height = a_swapChainDesc.BufferDesc.Height;
	swapChainDesc.Format = a_swapChainDesc.BufferDesc.Format;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = 2;
	swapChainDesc.SwapEffect = a_swapChainDesc.SwapEffect;
	swapChainDesc.Flags = a_swapChainDesc.Flags;

	ffx::CreateContextDescFrameGenerationSwapChainForHwndDX12 ffxSwapChainDesc{};

	ffxSwapChainDesc.desc = &swapChainDesc;
	ffxSwapChainDesc.dxgiFactory = dxgiFactory.get();
	ffxSwapChainDesc.fullscreenDesc = nullptr;
	ffxSwapChainDesc.gameQueue = commandQueue.get();
	ffxSwapChainDesc.hwnd = a_swapChainDesc.OutputWindow;
	ffxSwapChainDesc.swapchain = &swapChain;

	auto& fidelityFX = globals::features::upscaling.fidelityFX;

	ffxSwapChainDesc.header.pNext = nullptr;
	fidelityFX.swapChainContextValid = fidelityFX.CreateFrameGenerationContext(
		fidelityFX.swapChainContext, &ffxSwapChainDesc.header);
	if (!fidelityFX.swapChainContextValid) {
		throw std::runtime_error("FidelityFX swap-chain context creation failed");
	}

	DX::ThrowIfFailed(swapChain->GetBuffer(0, IID_PPV_ARGS(&swapChainBuffers[0])));
	DX::ThrowIfFailed(swapChain->GetBuffer(1, IID_PPV_ARGS(&swapChainBuffers[1])));

	frameIndex = swapChain->GetCurrentBackBufferIndex();

	if (!fidelityFX.SetupFrameGeneration())
		throw std::runtime_error("FidelityFX frame-generation context creation failed");
}

void DX12SwapChain::CreateInterop()
{
	winrt::handle sharedFenceHandle;
	DX::ThrowIfFailed(d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&d3d12Fence)));
	DX::ThrowIfFailed(d3d12Device->CreateSharedHandle(d3d12Fence.get(), nullptr, GENERIC_ALL, nullptr, sharedFenceHandle.put()));
	DX::ThrowIfFailed(d3d11Device->OpenSharedFence(sharedFenceHandle.get(), IID_PPV_ARGS(&d3d11Fence)));

	swapChainProxy = new DXGISwapChainProxy(*this, swapChain);

	RecreateWrappedResources(swapChainDesc);
}

void DX12SwapChain::ResetUnpublished() noexcept
{
	if (swapChainProxy) {
		auto* unpublishedProxy = std::exchange(swapChainProxy, nullptr);
		unpublishedProxy->Release();
	}
	swapChain = nullptr;
	swapChainBufferWrapped.reset();
	uiBufferWrapped.reset();
	depthBufferShared12.reset();
	motionVectorBufferShared12.reset();
	for (auto& buffer : swapChainBuffers)
		buffer = nullptr;
	d3d11Fence = nullptr;
	d3d12Fence = nullptr;
	d3d11Context = nullptr;
	d3d11Device = nullptr;
	for (auto& list : commandLists)
		list = nullptr;
	for (auto& allocator : commandAllocators)
		allocator = nullptr;
	commandQueue = nullptr;
	d3d12Device = nullptr;
	swapChainDesc = {};
	frameIndex = 0;
	fenceValue = 0;
	runtimeQuarantined = false;
}

void DX12SwapChain::RecreateWrappedResources(const DXGI_SWAP_CHAIN_DESC1& desc)
{
	D3D11_TEXTURE2D_DESC texDesc11{};
	texDesc11.Width = desc.Width;
	texDesc11.Height = desc.Height;
	texDesc11.MipLevels = 1;
	texDesc11.ArraySize = 1;
	texDesc11.Format = desc.Format;
	texDesc11.SampleDesc.Count = 1;
	texDesc11.SampleDesc.Quality = 0;
	texDesc11.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

	// Build both replacements before releasing the active resources so a failed
	// allocation cannot leave the proxy with only half of its interop textures.
	auto newSwapChainBuffer = std::make_unique<WrappedResource>(texDesc11, d3d11Device.get(), d3d12Device.get());

	texDesc11.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	auto newUiBuffer = std::make_unique<WrappedResource>(texDesc11, d3d11Device.get(), d3d12Device.get());

	swapChainBufferWrapped = std::move(newSwapChainBuffer);
	uiBufferWrapped = std::move(newUiBuffer);
}

DXGISwapChainProxy* DX12SwapChain::TakeSwapChainProxy()
{
	return std::exchange(swapChainProxy, nullptr);
}

void DX12SwapChain::OnProxyDestroyed(IDXGISwapChain4* a_swapChain) noexcept
{
	if (swapChain == a_swapChain)
		swapChain = nullptr;
}

void DX12SwapChain::SetD3D11Device(ID3D11Device* a_d3d11Device)
{
	DX::ThrowIfFailed(a_d3d11Device->QueryInterface(IID_PPV_ARGS(&d3d11Device)));
}

void DX12SwapChain::SetD3D11DeviceContext(ID3D11DeviceContext* a_d3d11Context)
{
	DX::ThrowIfFailed(a_d3d11Context->QueryInterface(IID_PPV_ARGS(&d3d11Context)));
}

HRESULT DX12SwapChain::GetBuffer(UINT buffer, REFIID riid, void** ppSurface)
{
	if (!ppSurface)
		return E_POINTER;

	*ppSurface = nullptr;
	if (buffer != 0 || !swapChainBufferWrapped || !swapChainBufferWrapped->resource11)
		return DXGI_ERROR_INVALID_CALL;

	// IDXGISwapChain::GetBuffer returns an owned COM reference. Returning the raw
	// pointer here let the caller's Release destroy the shared texture while the
	// D3D12 side still retained and submitted its corresponding resource.
	return swapChainBufferWrapped->resource11->QueryInterface(riid, ppSurface);
}

HRESULT DX12SwapChain::ResizeBuffers(UINT bufferCount, UINT width, UINT height, DXGI_FORMAT format, UINT flags)
{
	if (!swapChain || runtimeQuarantined)
		return DXGI_ERROR_INVALID_CALL;

	// DXGI defines zero as "preserve the current buffer count". FidelityFX's
	// frame-generation swap-chain stores the supplied value verbatim and uses it
	// as its replacement-buffer count, so forwarding zero leaves it with no valid
	// source resource at the next Present.
	const UINT effectiveBufferCount = bufferCount ? bufferCount : swapChainDesc.BufferCount;
	if (!bufferCount)
		logger::warn("[FidelityFX] Normalized ResizeBuffers count from 0 to {} to preserve replacement buffers", effectiveBufferCount);
	if (effectiveBufferCount != 2) {
		logger::error("[DX12SwapChain] Rejected unsupported resize buffer count {} (CS requires 2)", effectiveBufferCount);
		return DXGI_ERROR_UNSUPPORTED;
	}

	// These references are to FidelityFX replacement buffers. They must not keep
	// the old generation alive across the provider's resize, and must be refreshed
	// before CS records another copy.
	swapChainBuffers[0] = nullptr;
	swapChainBuffers[1] = nullptr;
	const HRESULT result = swapChain->ResizeBuffers(effectiveBufferCount, width, height, format, flags);
	if (FAILED(result)) {
		(void)RefreshAfterResize();
		return result;
	}

	return RefreshAfterResize();
}

HRESULT DX12SwapChain::ResizeBuffers1(
	UINT bufferCount,
	UINT width,
	UINT height,
	DXGI_FORMAT format,
	UINT flags,
	const UINT* creationNodeMask,
	IUnknown* const* presentQueue)
{
	if (!swapChain || runtimeQuarantined)
		return DXGI_ERROR_INVALID_CALL;

	const UINT effectiveBufferCount = bufferCount ? bufferCount : swapChainDesc.BufferCount;
	if (effectiveBufferCount != 2)
		return DXGI_ERROR_UNSUPPORTED;

	swapChainBuffers[0] = nullptr;
	swapChainBuffers[1] = nullptr;
	const HRESULT result = swapChain->ResizeBuffers1(
		effectiveBufferCount, width, height, format, flags, creationNodeMask, presentQueue);
	if (FAILED(result)) {
		(void)RefreshAfterResize();
		return result;
	}
	return RefreshAfterResize();
}

HRESULT DX12SwapChain::RefreshAfterResize() noexcept
{
	try {
		DXGI_SWAP_CHAIN_DESC1 resizedDesc{};
		HRESULT result = swapChain->GetDesc1(&resizedDesc);
		if (FAILED(result)) {
			runtimeQuarantined = true;
			return result;
		}

		std::unique_ptr<WrappedResource> newSwapChainBuffer;
		std::unique_ptr<WrappedResource> newUiBuffer;
		const bool resourcesChanged = resizedDesc.Width != swapChainDesc.Width ||
		                              resizedDesc.Height != swapChainDesc.Height ||
		                              resizedDesc.Format != swapChainDesc.Format;
		if (resourcesChanged) {
			D3D11_TEXTURE2D_DESC textureDesc{};
			textureDesc.Width = resizedDesc.Width;
			textureDesc.Height = resizedDesc.Height;
			textureDesc.MipLevels = 1;
			textureDesc.ArraySize = 1;
			textureDesc.Format = resizedDesc.Format;
			textureDesc.SampleDesc.Count = 1;
			textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
			newSwapChainBuffer = std::make_unique<WrappedResource>(textureDesc, d3d11Device.get(), d3d12Device.get());
			textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			newUiBuffer = std::make_unique<WrappedResource>(textureDesc, d3d11Device.get(), d3d12Device.get());
		}

		winrt::com_ptr<ID3D12Resource> newBuffers[2];
		result = swapChain->GetBuffer(0, IID_PPV_ARGS(newBuffers[0].put()));
		if (SUCCEEDED(result))
			result = swapChain->GetBuffer(1, IID_PPV_ARGS(newBuffers[1].put()));
		if (FAILED(result)) {
			runtimeQuarantined = true;
			return result;
		}

		if (resourcesChanged) {
			swapChainBufferWrapped = std::move(newSwapChainBuffer);
			uiBufferWrapped = std::move(newUiBuffer);
		}
		swapChainBuffers[0] = std::move(newBuffers[0]);
		swapChainBuffers[1] = std::move(newBuffers[1]);
		swapChainDesc = resizedDesc;
		frameIndex = swapChain->GetCurrentBackBufferIndex();
		return S_OK;
	} catch (const std::exception& error) {
		logger::error("[DX12SwapChain] Resize refresh failed; quarantining the proxy: {}", error.what());
	} catch (...) {
		logger::error("[DX12SwapChain] Resize refresh failed; quarantining the proxy");
	}
	runtimeQuarantined = true;
	return E_FAIL;
}

HRESULT DX12SwapChain::Present(UINT syncInterval, UINT flags)
{
	return PresentInternal(syncInterval, flags, nullptr);
}

HRESULT DX12SwapChain::Present1(UINT syncInterval, UINT flags, const DXGI_PRESENT_PARAMETERS* presentParameters)
{
	if (!presentParameters)
		return E_POINTER;
	return PresentInternal(syncInterval, flags, presentParameters);
}

HRESULT DX12SwapChain::PresentInternal(
	UINT syncInterval,
	UINT flags,
	const DXGI_PRESENT_PARAMETERS* presentParameters) noexcept
{
	if (runtimeQuarantined || !swapChain || frameIndex >= std::size(commandAllocators) ||
		!d3d11Context || !d3d11Fence || !d3d12Fence || !commandQueue ||
		!commandAllocators[frameIndex] || !commandLists[frameIndex] ||
		!swapChainBufferWrapped || !swapChainBufferWrapped->resource ||
		!uiBufferWrapped || !uiBufferWrapped->rtv || !swapChainBuffers[frameIndex]) {
		return DXGI_ERROR_INVALID_CALL;
	}

	auto fail = [this](HRESULT a_result, const char* a_operation) {
		runtimeQuarantined = true;
		logger::error("[DX12SwapChain] {} failed with 0x{:08X}; quarantining the proxy", a_operation, static_cast<uint32_t>(a_result));
		return a_result;
	};
	auto check = [&](HRESULT a_result, const char* a_operation) -> std::optional<HRESULT> {
		if (FAILED(a_result))
			return fail(a_result, a_operation);
		return std::nullopt;
	};

	try {
		auto& upscaling = globals::features::upscaling;

		// Wait for D3D11 to finish
		if (auto result = check(d3d11Context->Signal(d3d11Fence.get(), fenceValue), "D3D11 fence signal"))
			return *result;
		if (auto result = check(commandQueue->Wait(d3d12Fence.get(), fenceValue), "D3D12 queue wait"))
			return *result;
		fenceValue++;

		// New frame, reset
		if (auto result = check(commandAllocators[frameIndex]->Reset(), "command allocator reset"))
			return *result;
		if (auto result = check(commandLists[frameIndex]->Reset(commandAllocators[frameIndex].get(), nullptr), "command list reset"))
			return *result;

		// Copy shared texture to swap chain buffer
		{
			auto fakeSwapChain = swapChainBufferWrapped->resource.get();
			auto realSwapChain = swapChainBuffers[frameIndex].get();
			const std::array beforeCopy{
				CD3DX12_RESOURCE_BARRIER::Transition(fakeSwapChain, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(realSwapChain, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST)
			};
			commandLists[frameIndex]->ResourceBarrier(static_cast<UINT>(beforeCopy.size()), beforeCopy.data());

			commandLists[frameIndex]->CopyResource(realSwapChain, fakeSwapChain);

			const std::array afterCopy{
				CD3DX12_RESOURCE_BARRIER::Transition(fakeSwapChain, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON),
				CD3DX12_RESOURCE_BARRIER::Transition(realSwapChain, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT)
			};
			commandLists[frameIndex]->ResourceBarrier(static_cast<UINT>(afterCopy.size()), afterCopy.data());
		}

		const bool useFrameGeneration = upscaling.ShouldUseFrameGenerationThisFrame() &&
		                                upscaling.streamline.EnsureReflexDisabledForFrameGeneration();
		(void)upscaling.fidelityFX.Present(useFrameGeneration);

		if (auto result = check(commandLists[frameIndex]->Close(), "command list close"))
			return *result;

		ID3D12CommandList* commandListsToExecute[] = { commandLists[frameIndex].get() };
		commandQueue->ExecuteCommandLists(1, commandListsToExecute);

		// Present the frame
		const HRESULT presentResult = presentParameters ?
		                                  swapChain->Present1(syncInterval, flags, presentParameters) :
		                                  swapChain->Present(syncInterval, flags);
		if (auto result = check(presentResult, "swap-chain present"))
			return *result;

		// Wait for D3D12 to finish
		if (auto result = check(commandQueue->Signal(d3d12Fence.get(), fenceValue), "D3D12 fence signal"))
			return *result;
		if (auto result = check(d3d11Context->Wait(d3d11Fence.get(), fenceValue), "D3D11 fence wait"))
			return *result;
		fenceValue++;

		// Update the frame index
		frameIndex = swapChain->GetCurrentBackBufferIndex();

		float clearColor[4]{ 0, 0, 0, 0 };
		d3d11Context->ClearRenderTargetView(uiBufferWrapped->rtv.get(), clearColor);

		// If VSync is disabled, use frame limiter to prevent tearing and optimise pacing
		if (syncInterval == 0)
			upscaling.FrameLimiter();

		return S_OK;
	} catch (const std::exception& error) {
		logger::error("[DX12SwapChain] Present raised an exception; quarantining the proxy: {}", error.what());
	} catch (...) {
		logger::error("[DX12SwapChain] Present raised an unknown exception; quarantining the proxy");
	}
	runtimeQuarantined = true;
	return E_FAIL;
}

HRESULT DX12SwapChain::GetDevice(REFIID uuid, void** ppDevice)
{
	if (!ppDevice)
		return E_POINTER;
	*ppDevice = nullptr;
	if (uuid == __uuidof(ID3D11Device) || uuid == __uuidof(ID3D11Device1) || uuid == __uuidof(ID3D11Device2) || uuid == __uuidof(ID3D11Device3) || uuid == __uuidof(ID3D11Device4) || uuid == __uuidof(ID3D11Device5)) {
		return d3d11Device ? d3d11Device->QueryInterface(uuid, ppDevice) : E_NOINTERFACE;
	}

	return swapChain ? swapChain->GetDevice(uuid, ppDevice) : DXGI_ERROR_INVALID_CALL;
}

HANDLE DX12SwapChain::GetFrameLatencyWaitableObject()
{
	return swapChain->GetFrameLatencyWaitableObject();
}

float DX12SwapChain::GetFrameTime() const
{
	// Calculate frame time based on swap chain presentation
	static float lastPresentTime = 0.0f;
	static float frameTime = 1.0f / 60.0f;  // Default to 60 fps
	static LARGE_INTEGER frequency = {};
	static LARGE_INTEGER currentTime = {};

	if (frequency.QuadPart == 0) {
		QueryPerformanceFrequency(&frequency);
	}

	QueryPerformanceCounter(&currentTime);
	float time = static_cast<float>(currentTime.QuadPart) / static_cast<float>(frequency.QuadPart);

	if (lastPresentTime > 0.0f) {
		frameTime = time - lastPresentTime;
	}
	lastPresentTime = time;

	return frameTime;
}

WrappedResource::WrappedResource(D3D11_TEXTURE2D_DESC a_texDesc, ID3D11Device5* a_d3d11Device, ID3D12Device* a_d3d12Device)
{
	// Create D3D11 shared texture directly instead of wrapping D3D12 resource
	a_texDesc.MiscFlags |= D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
	winrt::com_ptr<ID3D11Texture2D> newResource11;
	winrt::com_ptr<ID3D11ShaderResourceView> newSRV;
	winrt::com_ptr<ID3D11UnorderedAccessView> newUAV;
	winrt::com_ptr<ID3D11RenderTargetView> newRTV;
	winrt::com_ptr<ID3D12Resource> newResource12;
	DX::ThrowIfFailed(a_d3d11Device->CreateTexture2D(&a_texDesc, nullptr, newResource11.put()));

	// Get shared handle from D3D11 texture to enable D3D12 access
	winrt::com_ptr<IDXGIResource1> dxgiResource;
	DX::ThrowIfFailed(newResource11->QueryInterface(IID_PPV_ARGS(dxgiResource.put())));
	winrt::handle sharedHandle;
	DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, sharedHandle.put()));

	// Open the shared D3D11 texture as D3D12 resource
	DX::ThrowIfFailed(a_d3d12Device->OpenSharedHandle(sharedHandle.get(), IID_PPV_ARGS(newResource12.put())));

	if (a_texDesc.BindFlags & D3D11_BIND_SHADER_RESOURCE) {
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = a_texDesc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;

		DX::ThrowIfFailed(a_d3d11Device->CreateShaderResourceView(newResource11.get(), &srvDesc, newSRV.put()));
	}

	if (a_texDesc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) {
		if (a_texDesc.ArraySize > 1) {
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format = a_texDesc.Format;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
			uavDesc.Texture2DArray.FirstArraySlice = 0;
			uavDesc.Texture2DArray.ArraySize = a_texDesc.ArraySize;

			DX::ThrowIfFailed(a_d3d11Device->CreateUnorderedAccessView(newResource11.get(), &uavDesc, newUAV.put()));
		} else {
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format = a_texDesc.Format;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Texture2D.MipSlice = 0;

			DX::ThrowIfFailed(a_d3d11Device->CreateUnorderedAccessView(newResource11.get(), &uavDesc, newUAV.put()));
		}
	}

	if (a_texDesc.BindFlags & D3D11_BIND_RENDER_TARGET) {
		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		rtvDesc.Format = a_texDesc.Format;
		rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		rtvDesc.Texture2D.MipSlice = 0;
		DX::ThrowIfFailed(a_d3d11Device->CreateRenderTargetView(newResource11.get(), &rtvDesc, newRTV.put()));
	}

	// Publish members only after every requested view and cross-API resource has
	// been created. Constructor failure therefore releases the complete candidate
	// through the local RAII owners and leaves no partially published wrapper.
	resource11 = std::move(newResource11);
	srv = std::move(newSRV);
	uav = std::move(newUAV);
	rtv = std::move(newRTV);
	resource = std::move(newResource12);
}

DXGISwapChainProxy::DXGISwapChainProxy(DX12SwapChain& a_owner, IDXGISwapChain4* a_swapChain) :
	owner(a_owner)
{
	// The FidelityFX creation API returns an owned swap-chain reference. The
	// proxy adopts that reference and exposes a separate COM identity.
	swapChain.attach(a_swapChain);
}

/****IUknown****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::QueryInterface(REFIID riid, void** ppvObj)
{
	if (!ppvObj)
		return E_POINTER;
	*ppvObj = nullptr;
	if (riid != __uuidof(IUnknown) &&
		riid != __uuidof(IDXGIObject) &&
		riid != __uuidof(IDXGIDeviceSubObject) &&
		riid != __uuidof(IDXGISwapChain) &&
		riid != __uuidof(IDXGISwapChain1) &&
		riid != __uuidof(IDXGISwapChain2) &&
		riid != __uuidof(IDXGISwapChain3) &&
		riid != __uuidof(IDXGISwapChain4)) {
		return E_NOINTERFACE;
	}

	*ppvObj = static_cast<IDXGISwapChain4*>(this);
	AddRef();
	return S_OK;
}

ULONG STDMETHODCALLTYPE DXGISwapChainProxy::AddRef()
{
	return ++referenceCount;
}

ULONG STDMETHODCALLTYPE DXGISwapChainProxy::Release()
{
	const ULONG remaining = --referenceCount;
	if (remaining == 0) {
		auto* underlying = swapChain.get();
		owner.OnProxyDestroyed(underlying);
		delete this;
	}
	return remaining;
}

/****IDXGIObject****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetPrivateData(_In_ REFGUID Name, UINT DataSize, _In_reads_bytes_(DataSize) const void* pData)
{
	return swapChain->SetPrivateData(Name, DataSize, pData);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetPrivateDataInterface(_In_ REFGUID Name, _In_opt_ const IUnknown* pUnknown)
{
	return swapChain->SetPrivateDataInterface(Name, pUnknown);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetPrivateData(_In_ REFGUID Name, _Inout_ UINT* pDataSize, _Out_writes_bytes_(*pDataSize) void* pData)
{
	return swapChain->GetPrivateData(Name, pDataSize, pData);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetParent(_In_ REFIID riid, _COM_Outptr_ void** ppParent)
{
	return swapChain->GetParent(riid, ppParent);
}

/****IDXGIDeviceSubObject****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetDevice(_In_ REFIID riid, _COM_Outptr_ void** ppDevice)
{
	return owner.GetDevice(riid, ppDevice);
}

/****IDXGISwapChain****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::Present(UINT SyncInterval, UINT Flags)
{
	return owner.Present(SyncInterval, Flags);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetBuffer(UINT buffer, _In_ REFIID riid, _COM_Outptr_ void** ppSurface)
{
	return owner.GetBuffer(buffer, riid, ppSurface);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetFullscreenState(BOOL Fullscreen, _In_opt_ IDXGIOutput* pTarget)
{
	return swapChain->SetFullscreenState(Fullscreen, pTarget);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetFullscreenState(_Out_opt_ BOOL* pFullscreen, _COM_Outptr_opt_result_maybenull_ IDXGIOutput** ppTarget)
{
	return swapChain->GetFullscreenState(pFullscreen, ppTarget);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetDesc(_Out_ DXGI_SWAP_CHAIN_DESC* pDesc)
{
	return swapChain->GetDesc(pDesc);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::ResizeBuffers(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
	return owner.ResizeBuffers(BufferCount, Width, Height, NewFormat, SwapChainFlags);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::ResizeTarget(_In_ const DXGI_MODE_DESC* pNewTargetParameters)
{
	return swapChain->ResizeTarget(pNewTargetParameters);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetContainingOutput(_COM_Outptr_ IDXGIOutput** ppOutput)
{
	return swapChain->GetContainingOutput(ppOutput);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetFrameStatistics(_Out_ DXGI_FRAME_STATISTICS* pStats)
{
	return swapChain->GetFrameStatistics(pStats);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetLastPresentCount(_Out_ UINT* pLastPresentCount)
{
	return swapChain->GetLastPresentCount(pLastPresentCount);
}

/****IDXGISwapChain1****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetDesc1(DXGI_SWAP_CHAIN_DESC1* pDesc)
{
	return swapChain->GetDesc1(pDesc);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pDesc)
{
	return swapChain->GetFullscreenDesc(pDesc);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetHwnd(HWND* pHwnd)
{
	return swapChain->GetHwnd(pHwnd);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetCoreWindow(REFIID refiid, void** ppUnk)
{
	return swapChain->GetCoreWindow(refiid, ppUnk);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::Present1(UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS* pPresentParameters)
{
	return owner.Present1(SyncInterval, PresentFlags, pPresentParameters);
}

BOOL STDMETHODCALLTYPE DXGISwapChainProxy::IsTemporaryMonoSupported()
{
	return swapChain->IsTemporaryMonoSupported();
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetRestrictToOutput(IDXGIOutput** ppRestrictToOutput)
{
	return swapChain->GetRestrictToOutput(ppRestrictToOutput);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetBackgroundColor(const DXGI_RGBA* pColor)
{
	return swapChain->SetBackgroundColor(pColor);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetBackgroundColor(DXGI_RGBA* pColor)
{
	return swapChain->GetBackgroundColor(pColor);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetRotation(DXGI_MODE_ROTATION Rotation)
{
	return swapChain->SetRotation(Rotation);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetRotation(DXGI_MODE_ROTATION* pRotation)
{
	return swapChain->GetRotation(pRotation);
}

/****IDXGISwapChain2****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetSourceSize(UINT Width, UINT Height)
{
	return swapChain->SetSourceSize(Width, Height);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetSourceSize(UINT* pWidth, UINT* pHeight)
{
	return swapChain->GetSourceSize(pWidth, pHeight);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetMaximumFrameLatency(UINT MaxLatency)
{
	return swapChain->SetMaximumFrameLatency(MaxLatency);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetMaximumFrameLatency(UINT* pMaxLatency)
{
	return swapChain->GetMaximumFrameLatency(pMaxLatency);
}

HANDLE STDMETHODCALLTYPE DXGISwapChainProxy::GetFrameLatencyWaitableObject()
{
	return swapChain->GetFrameLatencyWaitableObject();
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetMatrixTransform(const DXGI_MATRIX_3X2_F* pMatrix)
{
	return swapChain->SetMatrixTransform(pMatrix);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetMatrixTransform(DXGI_MATRIX_3X2_F* pMatrix)
{
	return swapChain->GetMatrixTransform(pMatrix);
}

/****IDXGISwapChain3****/
UINT STDMETHODCALLTYPE DXGISwapChainProxy::GetCurrentBackBufferIndex()
{
	return swapChain->GetCurrentBackBufferIndex();
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE ColorSpace, UINT* pColorSpaceSupport)
{
	return swapChain->CheckColorSpaceSupport(ColorSpace, pColorSpaceSupport);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetColorSpace1(DXGI_COLOR_SPACE_TYPE ColorSpace)
{
	return swapChain->SetColorSpace1(ColorSpace);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::ResizeBuffers1(
	UINT BufferCount,
	UINT Width,
	UINT Height,
	DXGI_FORMAT Format,
	UINT SwapChainFlags,
	const UINT* pCreationNodeMask,
	IUnknown* const* ppPresentQueue)
{
	return owner.ResizeBuffers1(
		BufferCount, Width, Height, Format, SwapChainFlags, pCreationNodeMask, ppPresentQueue);
}

/****IDXGISwapChain4****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetHDRMetaData(DXGI_HDR_METADATA_TYPE Type, UINT Size, void* pMetaData)
{
	return swapChain->SetHDRMetaData(Type, Size, pMetaData);
}

void DX12SwapChain::SetUIBuffer()
{
	if (!globals::game::ui->GameIsPaused()) {
		auto& data = globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kFRAMEBUFFER];
		data.RTV = uiBufferWrapped->rtv.get();
		d3d11Context->OMSetRenderTargets(1, &data.RTV, nullptr);
	}
}

void DX12SwapChain::CreateSharedResources()
{
	auto renderer = globals::game::renderer;

	// Create depth buffer
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	D3D11_TEXTURE2D_DESC texDesc{};
	main.texture->GetDesc(&texDesc);
	texDesc.Format = DXGI_FORMAT_R32_FLOAT;
	auto newDepthBuffer = std::make_unique<WrappedResource>(texDesc, d3d11Device.get(), d3d12Device.get());

	// Create motion vector buffer
	auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
	motionVector.texture->GetDesc(&texDesc);
	auto newMotionVectorBuffer = std::make_unique<WrappedResource>(texDesc, d3d11Device.get(), d3d12Device.get());

	depthBufferShared12 = std::move(newDepthBuffer);
	motionVectorBufferShared12 = std::move(newMotionVectorBuffer);
}
