#include "DX12SwapChain.h"

#include <FidelityFX/api/include/dx12/ffx_api_dx12.hpp>
#include <algorithm>
#include <cmath>
#include <dxgi1_6.h>
#include <format>
#include <iterator>
#include <limits>
#include <string>

#include "../../Hooks.h"
#include "../../Utils/D3D.h"
#include "../HDRDisplay.h"
#include "../Upscaling.h"
#include "FidelityFX.h"
#include "Streamline.h"

namespace
{
	constexpr double kMaximumOutputSampleDurationMs = 1000.0;
	constexpr double kOutputTimingStaleSeconds = 2.0;
	constexpr DWORD kFenceWaitTimeoutMs = 5000;

	bool NormalizeHostBackBufferCount(
		DXGI_SWAP_CHAIN_DESC1& a_desc,
		UINT a_hostBufferCount,
		bool a_providerOwnsNativeBuffers)
	{
		const bool validCount = a_providerOwnsNativeBuffers ?
		                            a_desc.BufferCount != 0 :
		                            a_desc.BufferCount == a_hostBufferCount;
		if (validCount)
			a_desc.BufferCount = a_hostBufferCount;
		return validCount;
	}

	struct ScopedHandle
	{
		~ScopedHandle()
		{
			if (handle && handle != INVALID_HANDLE_VALUE)
				CloseHandle(handle);
		}

		HANDLE handle = nullptr;
	};
}

void DX12SwapChain::CreateD3D12Device(IDXGIAdapter* a_adapter)
{
	if (d3d12Device)
		return;

	DX::ThrowIfFailed(D3D12CreateDevice(a_adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&d3d12Device)));
}

void DX12SwapChain::CreateD3D12CommandResources()
{
	if (!d3d12Device)
		DX::ThrowIfFailed(E_POINTER);

	for (UINT i = 0; i < kMaximumBackBufferCount; ++i) {
		if (commandAllocators[i] && commandLists[i])
			continue;
		DX::ThrowIfFailed(d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocators[i])));
		DX::ThrowIfFailed(d3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocators[i].get(), nullptr, IID_PPV_ARGS(&commandLists[i])));
		commandAllocators[i]->SetName(
			std::format(L"Upscaling::Frame Generation Command Allocator[{}]", i).c_str());
		commandLists[i]->SetName(
			std::format(L"Upscaling::Frame Generation Command List[{}]", i).c_str());
		DX::ThrowIfFailed(commandLists[i]->Close());
	}
}

void DX12SwapChain::CreateSwapChain(IDXGIAdapter* adapter, DXGI_SWAP_CHAIN_DESC a_swapChainDesc)
{
	if (!adapter)
		DX::ThrowIfFailed(E_POINTER);

	CreateD3D12Device(adapter);
	CreateD3D12CommandResources();
	if (!commandQueue) {
		D3D12_COMMAND_QUEUE_DESC queueDesc{};
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
		DX::ThrowIfFailed(d3d12Device->CreateCommandQueue(
			&queueDesc,
			IID_PPV_ARGS(commandQueue.put())));
		commandQueue->SetName(L"Upscaling::FidelityFX Command Queue");
	}

	dxgiFactory = nullptr;
	DX::ThrowIfFailed(adapter->GetParent(IID_PPV_ARGS(dxgiFactory.put())));

	// Runtime format negotiation for swap chain
	DXGI_FORMAT attemptedFormat = DXGI_FORMAT_R10G10B10A2_UNORM;
	DXGI_FORMAT negotiatedFormat = DXGI_FORMAT_R10G10B10A2_UNORM;
	bool fallbackUsed = false;

	// Test R10G10B10A2 support for HDR capability
	D3D12_FEATURE_DATA_FORMAT_SUPPORT formatSupport = { DXGI_FORMAT_R10G10B10A2_UNORM, D3D12_FORMAT_SUPPORT1_RENDER_TARGET, D3D12_FORMAT_SUPPORT2_NONE };
	if (SUCCEEDED(d3d12Device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &formatSupport, sizeof(formatSupport)))) {
		if ((formatSupport.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) == 0) {
			logger::warn("[DX12SwapChain] R10G10B10A2_UNORM not supported as render target, falling back to R8G8B8A8_UNORM");
			negotiatedFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
			fallbackUsed = true;
		}
	} else {
		logger::warn("[DX12SwapChain] CheckFeatureSupport failed for R10G10B10A2_UNORM, falling back to R8G8B8A8_UNORM");
		negotiatedFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
		fallbackUsed = true;
	}

	logger::info("[DX12SwapChain] Swap chain format negotiation: attempted={}, negotiated={}, fallback={}",
		static_cast<uint32_t>(attemptedFormat),
		static_cast<uint32_t>(negotiatedFormat),
		fallbackUsed ? "true" : "false");

	swapChainDesc = {};
	swapChainDesc.Width = a_swapChainDesc.BufferDesc.Width;
	swapChainDesc.Height = a_swapChainDesc.BufferDesc.Height;
	swapChainDesc.Format = negotiatedFormat;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = kFidelityFXBackBufferCount;
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

	if (ffx::CreateContext(fidelityFX.swapChainContext, nullptr, ffxSwapChainDesc) != ffx::ReturnCode::Ok) {
		logger::critical("[FidelityFX] Failed to create swap chain context!");
		DX::ThrowIfFailed(E_FAIL);
	}
	if (!swapChain)
		DX::ThrowIfFailed(E_POINTER);
	nativeSwapChain.copy_from(swapChain);
	frameGenerationBackend = FrameGenerationBackend::kFidelityFX;
	activeBackBufferCount = kFidelityFXBackBufferCount;
	QueryPerformanceFrequency(&qpf);
	ResetOutputPresentationTiming(true);

	DX::ThrowIfFailed(AcquireSwapChainBuffers(swapChain, activeBackBufferCount, swapChainBuffers));
	for (UINT i = 0; i < activeBackBufferCount; ++i) {
		swapChainBuffers[i]->SetName(
			std::format(L"Upscaling::FidelityFX Back Buffer[{}]", i).c_str());
	}

	frameIndex = swapChain->GetCurrentBackBufferIndex();
	if (frameIndex >= activeBackBufferCount) {
		logger::critical(
			"[FidelityFX] Swap chain returned invalid back-buffer index {} for {} buffers.",
			frameIndex,
			activeBackBufferCount);
		DX::ThrowIfFailed(DXGI_ERROR_INVALID_CALL);
	}

	// Set color space based on HDR Display feature state and negotiated format
	auto* hdr = globals::features::hdrDisplay.loaded ? &globals::features::hdrDisplay : nullptr;
	bool enableHDR = hdr && hdr->settings.enableHDR;
	// Only set HDR color space if not falling back to SDR format
	SetColorSpace(enableHDR && !fallbackUsed);

	fidelityFX.SetupFrameGeneration();
}

bool DX12SwapChain::UpgradeD3D12DeviceForDLSSG()
{
	if (!d3d12Device)
		return false;

	auto& streamline = globals::features::upscaling.streamlineDX12;
	winrt::com_ptr<ID3D12Device> deviceReference;
	deviceReference.copy_from(d3d12Device.get());
	auto* nativeDevice = deviceReference.detach();
	void* upgradedDevice = nativeDevice;
	const bool deviceUpgradeSucceeded =
		streamline.UpgradeInterface(&upgradedDevice, "ID3D12Device");
	if (!deviceUpgradeSucceeded || !upgradedDevice ||
		!Util::HaveDistinctCOMIdentity(
			nativeDevice,
			static_cast<IUnknown*>(upgradedDevice))) {
		logger::error(
			"[DX12SwapChain] Streamline did not provide a distinct D3D12 device proxy.");
		if (upgradedDevice && upgradedDevice != nativeDevice)
			static_cast<IUnknown*>(upgradedDevice)->Release();
		nativeDevice->Release();
		return false;
	}
	// The proxy owns its own base reference; retire the temporary caller copy.
	nativeDevice->Release();
	d3d12ProxyDevice.attach(static_cast<ID3D12Device*>(upgradedDevice));
	streamlineProxyGraphActive = true;
	// Manual hooking requires the native device to remain untouched until its
	// Streamline proxy exists. Non-hook command resources may now use the native pair.
	CreateD3D12CommandResources();

	commandQueue = nullptr;
	commandQueueProxy = nullptr;
	D3D12_COMMAND_QUEUE_DESC queueDesc{};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	const HRESULT queueResult = d3d12ProxyDevice->CreateCommandQueue(
		&queueDesc,
		IID_PPV_ARGS(commandQueueProxy.put()));
	if (FAILED(queueResult)) {
		logger::error(
			"[DX12SwapChain] Streamline-proxied CreateCommandQueue failed: 0x{:08X}",
			static_cast<unsigned>(queueResult));
		return false;
	}
	void* nativeQueue = nullptr;
	if (!streamline.GetNativeInterface(
			commandQueueProxy.get(),
			&nativeQueue,
			"ID3D12CommandQueue")) {
		commandQueueProxy = nullptr;
		return false;
	}
	if (!Util::HaveDistinctCOMIdentity(
			commandQueueProxy.get(),
			static_cast<IUnknown*>(nativeQueue))) {
		logger::error(
			"[DX12SwapChain] Streamline did not return a distinct native command queue for its proxy.");
		static_cast<IUnknown*>(nativeQueue)->Release();
		commandQueueProxy = nullptr;
		return false;
	}
	commandQueue.attach(static_cast<ID3D12CommandQueue*>(nativeQueue));
	commandQueue->SetName(L"Upscaling::DLSS-G Command Queue");
	return true;
}

HRESULT DX12SwapChain::CreateDLSSGSwapChain(
	IDXGIAdapter* a_adapter,
	DXGI_SWAP_CHAIN_DESC a_swapChainDesc)
{
	if (!a_adapter || !d3d12Device || !commandQueue || !commandQueueProxy)
		return E_POINTER;

	winrt::com_ptr<IDXGIFactory4> nativeFactory;
	HRESULT result = a_adapter->GetParent(IID_PPV_ARGS(nativeFactory.put()));
	if (FAILED(result))
		return result;

	winrt::com_ptr<IDXGIFactory4> factoryReference;
	factoryReference.copy_from(nativeFactory.get());
	auto* nativeFactoryReference = factoryReference.detach();
	void* upgradedFactory = nativeFactoryReference;
	const bool factoryUpgradeSucceeded =
		globals::features::upscaling.streamlineDX12.UpgradeInterface(
			&upgradedFactory,
			"IDXGIFactory4");
	if (!factoryUpgradeSucceeded || !upgradedFactory ||
		!Util::HaveDistinctCOMIdentity(
			nativeFactoryReference,
			static_cast<IUnknown*>(upgradedFactory))) {
		logger::error(
			"[DX12SwapChain] Streamline did not provide a distinct DXGI factory proxy.");
		if (upgradedFactory && upgradedFactory != nativeFactoryReference)
			static_cast<IUnknown*>(upgradedFactory)->Release();
		nativeFactoryReference->Release();
		return E_NOINTERFACE;
	}
	nativeFactoryReference->Release();
	winrt::com_ptr<IDXGIFactory4> factoryProxy;
	factoryProxy.attach(static_cast<IDXGIFactory4*>(upgradedFactory));

	DXGI_FORMAT negotiatedFormat = DXGI_FORMAT_R10G10B10A2_UNORM;
	D3D12_FEATURE_DATA_FORMAT_SUPPORT formatSupport{
		negotiatedFormat,
		D3D12_FORMAT_SUPPORT1_RENDER_TARGET,
		D3D12_FORMAT_SUPPORT2_NONE
	};
	if (FAILED(d3d12Device->CheckFeatureSupport(
			D3D12_FEATURE_FORMAT_SUPPORT,
			&formatSupport,
			sizeof(formatSupport))) ||
		(formatSupport.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) == 0) {
		negotiatedFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	}

	DXGI_SWAP_CHAIN_DESC1 directDesc{};
	directDesc.Width = a_swapChainDesc.BufferDesc.Width;
	directDesc.Height = a_swapChainDesc.BufferDesc.Height;
	directDesc.Format = negotiatedFormat;
	directDesc.SampleDesc.Count = 1;
	directDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	directDesc.BufferCount = kDLSSGBackBufferCount;
	directDesc.SwapEffect = a_swapChainDesc.SwapEffect;
	// DLSS-G owns presentation pacing; the host must not wait on this handle.
	directDesc.Flags = a_swapChainDesc.Flags &
	                   ~DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

	winrt::com_ptr<IDXGISwapChain1> createdSwapChain;
	result = factoryProxy->CreateSwapChainForHwnd(
		commandQueueProxy.get(),
		a_swapChainDesc.OutputWindow,
		&directDesc,
		nullptr,
		nullptr,
		createdSwapChain.put());
	if (FAILED(result))
		return result;

	winrt::com_ptr<IDXGISwapChain4> candidateSwapChain;
	result = createdSwapChain->QueryInterface(IID_PPV_ARGS(candidateSwapChain.put()));
	if (FAILED(result))
		return result;
	void* candidateNativeInterface = nullptr;
	if (!globals::features::upscaling.streamlineDX12.GetNativeInterface(
			candidateSwapChain.get(),
			&candidateNativeInterface,
			"IDXGISwapChain4")) {
		return E_NOINTERFACE;
	}
	if (!Util::HaveDistinctCOMIdentity(
			candidateSwapChain.get(),
			static_cast<IUnknown*>(candidateNativeInterface))) {
		logger::error(
			"[DX12SwapChain] Streamline did not create a distinct swap-chain proxy.");
		static_cast<IUnknown*>(candidateNativeInterface)->Release();
		return E_NOINTERFACE;
	}
	winrt::com_ptr<IDXGISwapChain4> candidateNativeSwapChain;
	candidateNativeSwapChain.attach(
		static_cast<IDXGISwapChain4*>(candidateNativeInterface));
	// The upgraded factory creates the swap-chain proxy. Calling
	// slUpgradeInterface on that proxy again is an invalid manual integration.
	DXGI_SWAP_CHAIN_DESC1 actualDesc{};
	result = candidateNativeSwapChain->GetDesc1(&actualDesc);
	if (FAILED(result))
		return result;
	const UINT providerBufferCount = actualDesc.BufferCount;
	// DLSS-G owns a native presentation chain whose buffers are distinct from
	// the host-facing proxy's requested off-screen buffers.
	if (!NormalizeHostBackBufferCount(
			actualDesc,
			kDLSSGBackBufferCount,
			true) ||
		actualDesc.Width == 0 || actualDesc.Height == 0) {
		logger::error(
			"[DX12SwapChain] Streamline swap-chain description is invalid: buffers={}, size={}x{}",
			providerBufferCount,
			actualDesc.Width,
			actualDesc.Height);
		return E_FAIL;
	}
	if (providerBufferCount != kDLSSGBackBufferCount) {
		logger::debug(
			"[DX12SwapChain] Streamline owns {} native presentation buffers; the host proxy exposes {} off-screen buffers.",
			providerBufferCount,
			kDLSSGBackBufferCount);
	}

	winrt::com_ptr<ID3D12Resource> candidateBuffers[kMaximumBackBufferCount];
	result = AcquireSwapChainBuffers(
		candidateSwapChain.get(),
		kDLSSGBackBufferCount,
		candidateBuffers);
	if (FAILED(result))
		return result;
	for (UINT i = 0; i < kDLSSGBackBufferCount; ++i) {
		candidateBuffers[i]->SetName(
			std::format(L"Upscaling::DLSS-G Back Buffer[{}]", i).c_str());
	}

	dxgiFactory = std::move(nativeFactory);
	nativeSwapChain = std::move(candidateNativeSwapChain);
	streamlineSwapChainOwner = std::move(candidateSwapChain);
	swapChain = streamlineSwapChainOwner.get();
	swapChainDesc = actualDesc;
	frameGenerationBackend = FrameGenerationBackend::kDLSSG;
	activeBackBufferCount = kDLSSGBackBufferCount;
	for (UINT i = 0; i < activeBackBufferCount; ++i)
		swapChainBuffers[i] = std::move(candidateBuffers[i]);
	frameIndex = swapChain->GetCurrentBackBufferIndex();
	if (frameIndex >= activeBackBufferCount) {
		logger::error(
			"[DX12SwapChain] Streamline returned invalid initial back-buffer index {} for {} buffers.",
			frameIndex,
			activeBackBufferCount);
		return DXGI_ERROR_INVALID_CALL;
	}
	QueryPerformanceFrequency(&qpf);
	ResetOutputPresentationTiming(true);

	const auto* hdr = globals::features::hdrDisplay.loaded ?
	                      &globals::features::hdrDisplay :
	                      nullptr;
	SetColorSpace(
		hdr && hdr->settings.enableHDR &&
		negotiatedFormat == DXGI_FORMAT_R10G10B10A2_UNORM);
	logger::info(
		"[DX12SwapChain] Created Streamline DLSS-G swap chain with {} buffers at {}x{}",
		activeBackBufferCount,
		swapChainDesc.Width,
		swapChainDesc.Height);
	return S_OK;
}

bool DX12SwapChain::DisableDLSSGCandidate()
{
	// Streamline must release its hooks while all proxied DXGI/D3D objects are alive.
	if (!globals::features::upscaling.streamlineDX12.Shutdown()) {
		logger::critical(
			"[DX12SwapChain] Keeping the failed DLSS-G proxy graph alive because Streamline shutdown was not confirmed.");
		return false;
	}
	RetireStreamlineProxyGraph();
	swapChainProxy = nullptr;
	swapChainBufferWrapped.reset();
	uiBufferWrapped.reset();
	wrappedResourceDesc = {};
	wrappedResourceDescValid = false;
	depthBufferShared12.reset();
	motionVectorBufferShared12.reset();
	d3d11Fence = nullptr;
	d3d12Fence = nullptr;
	d3d11Context = nullptr;
	d3d11Device = nullptr;
	ReleaseSwapChainBuffers();
	dxgiFactory = nullptr;
	commandQueue = nullptr;
	for (UINT i = 0; i < kMaximumBackBufferCount; ++i) {
		commandLists[i] = nullptr;
		commandAllocators[i] = nullptr;
	}
	d3d12Device = nullptr;
	nativeSwapChain = nullptr;
	frameGenerationBackend = FrameGenerationBackend::kNone;
	activeBackBufferCount = 0;
	frameIndex = 0;
	fenceValue = 1;
	std::fill(
		std::begin(commandAllocatorFenceValues),
		std::end(commandAllocatorFenceValues),
		0);
	presentInteropFailure = S_OK;
	colorSpaceChangePending = false;
	++interopGeneration;
	swapChainDesc = {};
	ResetOutputPresentationTiming(true);
	return true;
}

void DX12SwapChain::RetireStreamlineProxyGraph()
{
	if (frameGenerationBackend != FrameGenerationBackend::kDLSSG &&
		!streamlineProxyGraphActive) {
		return;
	}
	if (swapChainProxy)
		swapChainProxy->RetireStreamlineSwapChain();
	streamlineSwapChainOwner = nullptr;
	swapChain = nullptr;
	commandQueueProxy = nullptr;
	d3d12ProxyDevice = nullptr;
	streamlineProxyGraphActive = false;
}

HRESULT DX12SwapChain::AcquireSwapChainBuffers(
	IDXGISwapChain4* a_swapChain,
	UINT a_count,
	winrt::com_ptr<ID3D12Resource> (&a_buffers)[kMaximumBackBufferCount])
{
	if (!a_swapChain || a_count == 0 || a_count > kMaximumBackBufferCount)
		return E_INVALIDARG;
	for (auto& buffer : a_buffers)
		buffer = nullptr;
	for (UINT i = 0; i < a_count; ++i) {
		const HRESULT result = a_swapChain->GetBuffer(i, IID_PPV_ARGS(a_buffers[i].put()));
		if (FAILED(result)) {
			for (auto& buffer : a_buffers)
				buffer = nullptr;
			return result;
		}
	}
	return S_OK;
}

void DX12SwapChain::ReleaseSwapChainBuffers()
{
	for (auto& buffer : swapChainBuffers)
		buffer = nullptr;
}

HRESULT DX12SwapChain::WaitForFenceValue(
	ID3D12Fence* a_fence,
	UINT64 a_value,
	std::string_view a_context)
{
	if (!a_fence)
		return E_POINTER;
	if (a_value == 0)
		return S_OK;
	const UINT64 completedValue = a_fence->GetCompletedValue();
	if (completedValue == UINT64_MAX) {
		return d3d12Device ? d3d12Device->GetDeviceRemovedReason() :
		                     DXGI_ERROR_DEVICE_REMOVED;
	}
	if (completedValue >= a_value)
		return S_OK;

	ScopedHandle completionEvent;
	completionEvent.handle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	if (!completionEvent.handle)
		return HRESULT_FROM_WIN32(GetLastError());

	HRESULT result = a_fence->SetEventOnCompletion(
		a_value,
		completionEvent.handle);
	if (FAILED(result))
		return result;

	const DWORD waitResult = WaitForSingleObject(
		completionEvent.handle,
		kFenceWaitTimeoutMs);
	if (waitResult == WAIT_OBJECT_0)
		return S_OK;

	result = waitResult == WAIT_TIMEOUT ?
	             HRESULT_FROM_WIN32(ERROR_TIMEOUT) :
	             HRESULT_FROM_WIN32(GetLastError());
	logger::error(
		"[DX12SwapChain] Timed out or failed while waiting for {} at fence {}: 0x{:08X}",
		a_context,
		a_value,
		static_cast<unsigned>(result));
	return result;
}

HRESULT DX12SwapChain::WaitForFenceValue(
	UINT64 a_value,
	std::string_view a_context)
{
	return WaitForFenceValue(d3d12Fence.get(), a_value, a_context);
}

HRESULT DX12SwapChain::WaitForCommandAllocator(UINT a_index)
{
	if (a_index >= kMaximumBackBufferCount)
		return E_INVALIDARG;
	return WaitForFenceValue(
		commandAllocatorFenceValues[a_index],
		"command-allocator reuse");
}

HRESULT DX12SwapChain::WaitForInteropIdle()
{
	if (!d3d11Context || !d3d11Fence || !d3d12Fence || !commandQueue)
		return E_POINTER;

	const UINT64 d3d11CompleteValue = fenceValue++;
	HRESULT result = d3d11Context->Signal(d3d11Fence.get(), d3d11CompleteValue);
	if (FAILED(result))
		return result;
	result = commandQueue->Wait(d3d12Fence.get(), d3d11CompleteValue);
	if (FAILED(result))
		return result;

	const UINT64 d3d12CompleteValue = fenceValue++;
	result = commandQueue->Signal(d3d12Fence.get(), d3d12CompleteValue);
	if (FAILED(result))
		return result;
	return WaitForFenceValue(d3d12CompleteValue, "interop idle");
}

HRESULT DX12SwapChain::SynchronizeAfterPresent(
	UINT a_submittedAllocator,
	ID3D12Fence* a_providerCompletionFence,
	UINT64 a_providerCompletionValue)
{
	HRESULT providerOrderingResult = S_OK;
	if (a_providerCompletionFence && a_providerCompletionValue != 0) {
		providerOrderingResult = commandQueue ?
		                             commandQueue->Wait(
										 a_providerCompletionFence,
										 a_providerCompletionValue) :
		                             E_POINTER;
		if (FAILED(providerOrderingResult)) {
			logger::error(
				"[DX12SwapChain] Could not order the presenting queue after DLSS-G input processing: 0x{:08X}",
				static_cast<unsigned>(providerOrderingResult));
			const HRESULT providerRecoveryResult = WaitForFenceValue(
				a_providerCompletionFence,
				a_providerCompletionValue,
				"DLSS-G input completion recovery");
			if (FAILED(providerRecoveryResult))
				return providerRecoveryResult;
		}
	}

	HRESULT hostSyncResult = E_POINTER;
	if (commandQueue && d3d12Fence) {
		const UINT64 postPresentFenceValue = fenceValue++;
		hostSyncResult = commandQueue->Signal(
			d3d12Fence.get(),
			postPresentFenceValue);
		if (SUCCEEDED(hostSyncResult)) {
			if (a_submittedAllocator < kMaximumBackBufferCount) {
				commandAllocatorFenceValues[a_submittedAllocator] =
					postPresentFenceValue;
			}

			if (d3d11Context && d3d11Fence) {
				hostSyncResult = d3d11Context->Wait(
					d3d11Fence.get(),
					postPresentFenceValue);
				if (SUCCEEDED(hostSyncResult))
					return providerOrderingResult;
				logger::error(
					"[DX12SwapChain] D3D11 post-Present fence wait failed: 0x{:08X}",
					static_cast<unsigned>(hostSyncResult));
			}

			hostSyncResult = WaitForFenceValue(
				postPresentFenceValue,
				"post-Present CPU recovery");
			if (SUCCEEDED(hostSyncResult))
				return providerOrderingResult;
		} else {
			logger::error(
				"[DX12SwapChain] D3D12 post-Present fence signal failed: 0x{:08X}",
				static_cast<unsigned>(hostSyncResult));
		}
	}

	// A final CPU wait preserves provider-owned input lifetime when the host
	// signal path failed after the provider queue wait was submitted.
	if (a_providerCompletionFence && a_providerCompletionValue != 0) {
		const HRESULT providerSyncResult = WaitForFenceValue(
			a_providerCompletionFence,
			a_providerCompletionValue,
			"DLSS-G input completion recovery");
		if (FAILED(providerSyncResult))
			return providerSyncResult;
	}

	// Plugin input completion cannot prove completion of CSX's command list or
	// allocator. Preserve the host failure even when the independent input wait
	// succeeded so this interop generation remains fail-closed.
	return hostSyncResult;
}

void DX12SwapChain::CreateInterop()
{
	if (!d3d12Device || !d3d11Device || !swapChain)
		DX::ThrowIfFailed(E_POINTER);

	ScopedHandle sharedFenceHandle;
	DX::ThrowIfFailed(d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&d3d12Fence)));
	d3d12Fence->SetName(L"Upscaling::Frame Generation Shared Fence");
	DX::ThrowIfFailed(d3d12Device->CreateSharedHandle(d3d12Fence.get(), nullptr, GENERIC_ALL, nullptr, &sharedFenceHandle.handle));
	DX::ThrowIfFailed(d3d11Device->OpenSharedFence(sharedFenceHandle.handle, IID_PPV_ARGS(&d3d11Fence)));
	Util::SetResourceName(d3d11Fence.get(), "Upscaling::Frame Generation Shared Fence");

	swapChainProxy.attach(new DXGISwapChainProxy(
		swapChain,
		nativeSwapChain ? nativeSwapChain.get() : swapChain));
	DX::ThrowIfFailed(RecreateWrappedResources(swapChainDesc));

	// A fully recreated shared-fence path is the recovery boundary for a
	// previously latched post-Present interop failure.
	fenceValue = 1;
	std::fill(
		std::begin(commandAllocatorFenceValues),
		std::end(commandAllocatorFenceValues),
		0);
	presentInteropFailure = S_OK;
	++interopGeneration;
}

HRESULT DX12SwapChain::RecreateWrappedResources(const DXGI_SWAP_CHAIN_DESC1& a_desc)
{
	try {
		if (!d3d11Device || !d3d11Context || !d3d12Device)
			return E_POINTER;

		D3D11_TEXTURE2D_DESC texDesc11{};
		texDesc11.Width = a_desc.Width;
		texDesc11.Height = a_desc.Height;
		texDesc11.MipLevels = 1;
		texDesc11.ArraySize = 1;
		texDesc11.Format = a_desc.Format;
		texDesc11.SampleDesc.Count = 1;
		texDesc11.SampleDesc.Quality = 0;
		texDesc11.BindFlags = D3D11_BIND_SHADER_RESOURCE |
		                      D3D11_BIND_RENDER_TARGET |
		                      D3D11_BIND_UNORDERED_ACCESS;

		// Build both replacements before releasing either active wrapper. A failed
		// allocation therefore leaves the proxy's current scene/UI pair intact.
		auto newSwapChainBuffer = std::make_unique<WrappedResource>(
			texDesc11,
			d3d11Device.get(),
			d3d12Device.get(),
			"Swap-chain scene interop");

		// Vanilla UI is SDR and uses an 8-bit interop target.
		texDesc11.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		auto newUiBuffer = std::make_unique<WrappedResource>(
			texDesc11,
			d3d11Device.get(),
			d3d12Device.get(),
			"Swap-chain UI interop");

		swapChainBufferWrapped = std::move(newSwapChainBuffer);
		uiBufferWrapped = std::move(newUiBuffer);
		wrappedResourceDesc = a_desc;
		wrappedResourceDescValid = true;

		const float clearColor[4]{};
		d3d11Context->ClearRenderTargetView(swapChainBufferWrapped->rtv.get(), clearColor);
		d3d11Context->ClearRenderTargetView(uiBufferWrapped->rtv.get(), clearColor);
		return S_OK;
	} catch (const std::exception& error) {
		logger::error(
			"[DX12SwapChain] Could not recreate wrapped resources: {}",
			error.what());
	} catch (...) {
		logger::error("[DX12SwapChain] Could not recreate wrapped resources: unknown exception");
	}
	return E_FAIL;
}

IDXGISwapChain4* DX12SwapChain::GetSwapChainProxy()
{
	if (!swapChainProxy)
		return nullptr;
	swapChainProxy->AddRef();
	return swapChainProxy.get();
}

IDXGISwapChain4* DX12SwapChain::GetManualHookSwapChain() const
{
	if (frameGenerationBackend == FrameGenerationBackend::kDLSSG &&
		globals::features::upscaling.AreBackendsRetiringOrShutdown()) {
		return nativeSwapChain.get();
	}
	return swapChain ? swapChain : nativeSwapChain.get();
}

void DX12SwapChain::SetD3D11Device(ID3D11Device* a_d3d11Device)
{
	if (!a_d3d11Device)
		DX::ThrowIfFailed(E_POINTER);

	DX::ThrowIfFailed(a_d3d11Device->QueryInterface(IID_PPV_ARGS(&d3d11Device)));
}

void DX12SwapChain::SetD3D11DeviceContext(ID3D11DeviceContext* a_d3d11Context)
{
	if (!a_d3d11Context)
		DX::ThrowIfFailed(E_POINTER);

	DX::ThrowIfFailed(a_d3d11Context->QueryInterface(IID_PPV_ARGS(&d3d11Context)));
}

HRESULT DX12SwapChain::GetBuffer(UINT Buffer, REFIID riid, void** ppSurface)
{
	if (!ppSurface)
		return E_POINTER;

	*ppSurface = nullptr;
	if (FAILED(presentInteropFailure))
		return presentInteropFailure;

	// The game consumes a D3D11 replacement here. Every real D3D12 buffer is
	// acquired through the Streamline proxy when the interop generation is built.
	if (Buffer != 0 || !swapChainBufferWrapped || !swapChainBufferWrapped->resource11)
		return DXGI_ERROR_INVALID_CALL;

	return swapChainBufferWrapped->resource11->QueryInterface(riid, ppSurface);
}

HRESULT DX12SwapChain::ResizeBuffers(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
	return ResizeBuffersInternal(
		BufferCount,
		Width,
		Height,
		NewFormat,
		SwapChainFlags,
		nullptr,
		nullptr,
		false);
}

HRESULT DX12SwapChain::ResizeBuffers1(
	UINT a_bufferCount,
	UINT a_width,
	UINT a_height,
	DXGI_FORMAT a_format,
	UINT a_flags,
	const UINT* a_creationNodeMask,
	IUnknown* const* a_presentQueues)
{
	return ResizeBuffersInternal(
		a_bufferCount,
		a_width,
		a_height,
		a_format,
		a_flags,
		a_creationNodeMask,
		a_presentQueues,
		true);
}

HRESULT DX12SwapChain::ResizeBuffersInternal(
	UINT BufferCount,
	UINT Width,
	UINT Height,
	DXGI_FORMAT NewFormat,
	UINT SwapChainFlags,
	const UINT* a_creationNodeMask,
	IUnknown* const* a_presentQueues,
	bool a_useResizeBuffers1)
{
	if (a_useResizeBuffers1 &&
		frameGenerationBackend != FrameGenerationBackend::kNone) {
		// Neither active FG swap-chain provider implements ResizeBuffers1.
		logger::error(
			"[DX12SwapChain] ResizeBuffers1 is not supported by the active frame-generation swap chain.");
		return DXGI_ERROR_UNSUPPORTED;
	}

	IDXGISwapChain4* const resizingSwapChain = GetManualHookSwapChain();
	if (!resizingSwapChain)
		return DXGI_ERROR_INVALID_CALL;
	const bool usesStreamlineResizeHooks =
		frameGenerationBackend == FrameGenerationBackend::kDLSSG &&
		streamlineProxyGraphActive &&
		!globals::features::upscaling.AreBackendsRetiringOrShutdown();
	// DXGI defines zero as preserving the current count. Normalize it for host
	// validation while preserving the caller's value for ResizeBuffers1 arrays.
	UINT effectiveBufferCount = BufferCount ? BufferCount : swapChainDesc.BufferCount;
	if (!BufferCount)
		logger::warn("[DX12SwapChain] Normalized ResizeBuffers count from 0 to {}", effectiveBufferCount);
	if (!a_useResizeBuffers1 &&
		frameGenerationBackend == FrameGenerationBackend::kDLSSG &&
		effectiveBufferCount == kFidelityFXBackBufferCount) {
		logger::info(
			"[DX12SwapChain] Normalized the cached D3D11 resize count from {} to the DLSS-G count {}",
			effectiveBufferCount,
			activeBackBufferCount);
		effectiveBufferCount = activeBackBufferCount;
	}
	if (effectiveBufferCount != activeBackBufferCount) {
		logger::error(
			"[DX12SwapChain] Rejected resize buffer count {} (active backend owns {})",
			effectiveBufferCount,
			activeBackBufferCount);
		return DXGI_ERROR_UNSUPPORTED;
	}
	if (a_useResizeBuffers1 && BufferCount > 0) {
		if (!a_creationNodeMask || !a_presentQueues)
			return E_INVALIDARG;
		for (UINT i = 0; i < BufferCount; ++i) {
			if (!a_presentQueues[i])
				return E_INVALIDARG;
		}
	}
	UINT effectiveFlags = SwapChainFlags;
	if (usesStreamlineResizeHooks) {
		effectiveFlags &= ~DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
	}
	if (const HRESULT prepareResult = PrepareForSwapChainChange();
		FAILED(prepareResult))
		return prepareResult;

	// Release all cached references to the active replacement buffers before
	// asking the provider to retire that generation.
	ReleaseSwapChainBuffers();

	// ResizeBuffers1 sizes its queue/mask arrays with the caller's BufferCount.
	// Preserve a legal zero count so null or zero-length arrays stay valid.
	const UINT forwardedBufferCount =
		a_useResizeBuffers1 ? BufferCount : effectiveBufferCount;
	IUnknown* const* forwardedPresentQueues = a_presentQueues;
	const HRESULT resizeResult = a_useResizeBuffers1 ?
	                                 resizingSwapChain->ResizeBuffers1(
										 forwardedBufferCount,
										 Width,
										 Height,
										 NewFormat,
										 effectiveFlags,
										 a_creationNodeMask,
										 forwardedPresentQueues) :
	                                 resizingSwapChain->ResizeBuffers(
										 effectiveBufferCount,
										 Width,
										 Height,
										 NewFormat,
										 effectiveFlags);

	DXGI_SWAP_CHAIN_DESC1 resizedDesc{};
	const HRESULT descResult = nativeSwapChain ?
	                               nativeSwapChain->GetDesc1(&resizedDesc) :
	                               E_NOINTERFACE;
	if (FAILED(descResult)) {
		const HRESULT failure = FAILED(resizeResult) ? resizeResult : descResult;
		logger::error(
			"[DX12SwapChain] GetDesc1 after ResizeBuffers failed: HRESULT=0x{:08X}; resize HRESULT=0x{:08X}",
			static_cast<unsigned>(descResult),
			static_cast<unsigned>(resizeResult));
		RecordResizeGeneration();
		presentInteropFailure = failure;
		return failure;
	}
	const UINT providerBufferCount = resizedDesc.BufferCount;
	const bool providerBufferCountValid = NormalizeHostBackBufferCount(
		resizedDesc,
		activeBackBufferCount,
		frameGenerationBackend == FrameGenerationBackend::kDLSSG);

	if (FAILED(resizeResult)) {
		logger::error(
			"[DX12SwapChain] ResizeBuffers failed: HRESULT=0x{:08X}, count={}, size={}x{}, format={}, flags=0x{:X}",
			static_cast<unsigned>(resizeResult),
			effectiveBufferCount,
			Width,
			Height,
			static_cast<unsigned>(NewFormat),
			effectiveFlags);

		// A Streamline after-hook may fail after DXGI has committed the resize.
		// The mandatory GetBuffer hook must not be bypassed to inspect that state,
		// so keep the retained D3D11 wrappers alive and fail this generation closed.
		RecordResizeGeneration(&resizedDesc);
		presentInteropFailure = resizeResult;
		return resizeResult;
	}
	const bool resizePostconditionsMet =
		providerBufferCountValid &&
		resizedDesc.Width != 0 && resizedDesc.Height != 0 &&
		(Width == 0 || resizedDesc.Width == Width) &&
		(Height == 0 || resizedDesc.Height == Height) &&
		(NewFormat == DXGI_FORMAT_UNKNOWN || resizedDesc.Format == NewFormat) &&
		resizedDesc.Flags == effectiveFlags;
	if (!resizePostconditionsMet) {
		logger::error(
			"[DX12SwapChain] Resize postconditions failed: buffers={}, size={}x{}, format={}, flags=0x{:X}",
			providerBufferCount,
			resizedDesc.Width,
			resizedDesc.Height,
			static_cast<unsigned>(resizedDesc.Format),
			resizedDesc.Flags);
		const HRESULT invalidDescription = DXGI_ERROR_INVALID_CALL;
		RecordResizeGeneration(&resizedDesc);
		presentInteropFailure = invalidDescription;
		return invalidDescription;
	}
	// A successful, validated resize establishes a new generation even when its
	// dimensions and format are unchanged.
	RecordResizeGeneration(&resizedDesc);

	winrt::com_ptr<ID3D12Resource> resizedBuffers[kMaximumBackBufferCount];
	const HRESULT buffersResult = AcquireSwapChainBuffers(
		resizingSwapChain,
		activeBackBufferCount,
		resizedBuffers);
	if (FAILED(buffersResult)) {
		logger::error(
			"[DX12SwapChain] Could not acquire resized back buffers: HRESULT=0x{:08X}",
			static_cast<unsigned>(buffersResult));
		presentInteropFailure = buffersResult;
		return buffersResult;
	}
	const wchar_t* backendName =
		frameGenerationBackend == FrameGenerationBackend::kDLSSG ?
			L"DLSS-G" :
			L"FidelityFX";
	for (UINT i = 0; i < activeBackBufferCount; ++i) {
		resizedBuffers[i]->SetName(
			std::format(L"Upscaling::{} Back Buffer[{}]", backendName, i).c_str());
	}

	const bool wrappedResourcesChanged =
		!wrappedResourceDescValid ||
		resizedDesc.Width != wrappedResourceDesc.Width ||
		resizedDesc.Height != wrappedResourceDesc.Height ||
		resizedDesc.Format != wrappedResourceDesc.Format ||
		!swapChainBufferWrapped || !uiBufferWrapped;
	const HRESULT wrappedResourcesResult = wrappedResourcesChanged ?
	                                           RecreateWrappedResources(resizedDesc) :
	                                           S_OK;
	// Cache the provider's committed generation even if rebuilding the host
	// wrappers failed. Their independent description keeps the retry path honest.
	for (UINT i = 0; i < activeBackBufferCount; ++i)
		swapChainBuffers[i] = std::move(resizedBuffers[i]);
	frameIndex = resizingSwapChain->GetCurrentBackBufferIndex();
	if (frameIndex >= activeBackBufferCount) {
		logger::error(
			"[DX12SwapChain] Resized provider returned invalid back-buffer index {} for {} buffers.",
			frameIndex,
			activeBackBufferCount);
		presentInteropFailure = DXGI_ERROR_INVALID_CALL;
		return presentInteropFailure;
	}
	if (FAILED(wrappedResourcesResult)) {
		presentInteropFailure = wrappedResourcesResult;
		return wrappedResourcesResult;
	}
	presentInteropFailure = S_OK;
	if (frameGenerationBackend == FrameGenerationBackend::kDLSSG) {
		auto& streamline = globals::features::upscaling.streamlineDX12;
		streamline.ResetFrameTracking();
		streamline.ResetDLSSGState();
	}
	const auto* hdr = globals::features::hdrDisplay.loaded ?
	                      &globals::features::hdrDisplay :
	                      nullptr;
	(void)SetColorSpace(
		hdr && hdr->settings.enableHDR &&
		resizedDesc.Format == DXGI_FORMAT_R10G10B10A2_UNORM);

	logger::info(
		"[DX12SwapChain] Resized interop buffers to {}x{} format={} (wrappers recreated={})",
		resizedDesc.Width,
		resizedDesc.Height,
		static_cast<unsigned>(resizedDesc.Format),
		wrappedResourcesChanged);
	return S_OK;
}

bool DX12SwapChain::DisableDLSSGBeforeSwapChainChange()
{
	if (frameGenerationBackend != FrameGenerationBackend::kDLSSG ||
		globals::features::upscaling.AreBackendsShutdown())
		return true;

	auto& streamline = globals::features::upscaling.streamlineDX12;
	if (streamline.RequiresDLSSGPresentBoundary()) {
		streamline.RequestDLSSGDisable();
		logger::debug(
			"[DX12SwapChain] Deferring swap-chain mutation until a mode-off Present; the caller must retry.");
		return false;
	}
	return true;
}

HRESULT DX12SwapChain::PrepareForSwapChainChange()
{
	if (!DisableDLSSGBeforeSwapChainChange())
		return DXGI_ERROR_WAS_STILL_DRAWING;
	if (!d3d11Fence && !d3d12Fence)
		return S_OK;
	if (!d3d11Fence || !d3d12Fence || !d3d11Context || !commandQueue)
		return DXGI_ERROR_INVALID_CALL;

	const HRESULT result = WaitForInteropIdle();
	if (FAILED(result)) {
		logger::error(
			"[DX12SwapChain] Could not drain frame-generation interop before a swap-chain change: 0x{:08X}",
			static_cast<unsigned>(result));
		presentInteropFailure = result;
		++interopGeneration;
		ResetOutputPresentationTiming(true);
		return result;
	}
	return S_OK;
}

bool DX12SwapChain::PrepareForShutdown()
{
	return SUCCEEDED(PrepareForSwapChainChange());
}

void DX12SwapChain::RecordExternalSwapChainMutation(HRESULT a_failure)
{
	++interopGeneration;
	ResetOutputPresentationTiming(true);
	if (FAILED(a_failure) && SUCCEEDED(presentInteropFailure))
		presentInteropFailure = a_failure;
}

void DX12SwapChain::RecordResizeGeneration(
	const DXGI_SWAP_CHAIN_DESC1* a_desc)
{
	if (a_desc)
		swapChainDesc = *a_desc;
	++interopGeneration;
	ResetOutputPresentationTiming(true);
	std::fill(
		std::begin(commandAllocatorFenceValues),
		std::end(commandAllocatorFenceValues),
		0);
	hdrColorSpaceActive = false;
}

HRESULT DX12SwapChain::Present(UINT SyncInterval, UINT Flags)
{
	return PresentInternal(SyncInterval, Flags, nullptr);
}

HRESULT DX12SwapChain::Present1(
	UINT a_syncInterval,
	UINT a_flags,
	const DXGI_PRESENT_PARAMETERS* a_parameters)
{
	if (!a_parameters)
		return E_POINTER;
	return PresentInternal(a_syncInterval, a_flags, a_parameters);
}

HRESULT DX12SwapChain::PresentInternal(
	UINT SyncInterval,
	UINT Flags,
	const DXGI_PRESENT_PARAMETERS* a_parameters)
{
	auto& upscaling = globals::features::upscaling;
	const bool shutdownRequestedAtEntry =
		upscaling.IsBackendShutdownRequested();
	IDXGISwapChain4* const presentingSwapChain = GetManualHookSwapChain();
	if ((Flags & DXGI_PRESENT_TEST) != 0)
		return presentingSwapChain ?
		           (a_parameters ?
						   presentingSwapChain->Present1(SyncInterval, Flags, a_parameters) :
						   presentingSwapChain->Present(SyncInterval, Flags)) :
		           DXGI_ERROR_INVALID_CALL;

	auto* hdr = globals::features::hdrDisplay.loaded ?
	                &globals::features::hdrDisplay :
	                nullptr;
	const bool isHDR = hdr && hdr->IsHDROutputActive();
	const auto frame = upscaling.ConsumeFrameGenerationInputsForPresent();
	bool uiPreparedForOutput = frame.uiPreparedForOutput;
	bool providerPresentInvoked = false;
	bool providerMayUseSeparatedUI = false;
	bool uiFallbackApplied = false;
	FidelityFX::FrameGenerationPresentResult fidelityFXResult{};
	const SKSE::stl::scope_exit restoreSeparatedUI([&]() noexcept {
		if (!providerPresentInvoked && !providerMayUseSeparatedUI &&
			frame.uiSeparated && !uiFallbackApplied &&
			!upscaling.CompositeFrameGenerationUIFallback(
				uiPreparedForOutput)) {
			logger::error(
				"[DX12SwapChain] Failed to restore separated UI while abandoning Present.");
		}
	});
	upscaling.fidelityFX.isFrameGenActive = false;
	upscaling.streamlineDX12.dlssgState.active = false;
	const bool usesDLSSG =
		frameGenerationBackend == FrameGenerationBackend::kDLSSG &&
		streamlineProxyGraphActive && upscaling.streamlineDX12.initialized &&
		!upscaling.AreBackendsShutdown();
	uint32_t pclFrame = std::numeric_limits<uint32_t>::max();
	bool emitDLSSGMarkers = false;
	if (usesDLSSG) {
		auto& streamline = upscaling.streamlineDX12;
		pclFrame = frame.valid ? frame.frame :
		                         streamline.GetLatestFrameTokenFrame();
		// Shutdown can begin after the last tagged Present. Acquire a cleanup-only
		// token so the next Present can submit the null tags that release SL's refs.
		if (shutdownRequestedAtEntry && !frame.valid &&
			!streamline.dlssgState.tagsCleared &&
			streamline.EnsureFrameToken()) {
			pclFrame = streamline.GetLatestFrameTokenFrame();
		}
		emitDLSSGMarkers =
			frame.valid && frame.dlssgFrameReady;
	}
	struct DLSSGModeOffPreparation
	{
		bool nullTagsSubmitted = false;
		bool tagsSafe = false;
		bool modeOffQueued = false;
		bool providerMayUseSeparatedUI = false;
	};
	const auto prepareDLSSGModeOff = [&](Streamline::DLSSGTagResult a_tagResult) {
		DLSSGModeOffPreparation result{};
		if (!usesDLSSG)
			return result;

		auto& streamline = upscaling.streamlineDX12;
		result.nullTagsSubmitted =
			a_tagResult == Streamline::DLSSGTagResult::kCleared;
		if (!result.nullTagsSubmitted) {
			result.nullTagsSubmitted =
				streamline.ClearDLSSGResourceTags(pclFrame) ==
				Streamline::DLSSGTagResult::kCleared;
		}
		result.tagsSafe =
			result.nullTagsSubmitted || streamline.dlssgState.tagsCleared;

		const uint32_t safeRenderWidth =
			frame.renderWidth ? frame.renderWidth : swapChainDesc.Width;
		const uint32_t safeRenderHeight =
			frame.renderHeight ? frame.renderHeight : swapChainDesc.Height;
		result.modeOffQueued = streamline.ConfigureDLSSG(
			false,
			swapChainDesc.Width,
			swapChainDesc.Height,
			safeRenderWidth,
			safeRenderHeight);
		if (!result.modeOffQueued || !result.tagsSafe)
			streamline.RequestDLSSGDisable();
		// Do not mutate a separated UI resource until a null tag or an earlier
		// completed Present proves that the provider no longer owns it.
		result.providerMayUseSeparatedUI = !result.tagsSafe;
		return result;
	};
	const auto invokeProviderPresent = [&]() {
		if (!presentingSwapChain)
			return DXGI_ERROR_INVALID_CALL;
		providerPresentInvoked = true;
		return a_parameters ?
		           presentingSwapChain->Present1(SyncInterval, Flags, a_parameters) :
		           presentingSwapChain->Present(SyncInterval, Flags);
	};
	const auto latchInteropFailure = [&](HRESULT a_failure) {
		if (FAILED(a_failure) && SUCCEEDED(presentInteropFailure)) {
			presentInteropFailure = a_failure;
			++interopGeneration;
		}
	};
	struct FinalizedPresent
	{
		HRESULT result = DXGI_ERROR_INVALID_CALL;
		bool accepted = false;
		bool frameGenerationActive = false;
		bool providerResourcesReusable = false;
	};
	const auto finalizeProviderPresent = [&](bool a_dlssgGenerationRequested,
											 bool a_dlssgNullTagsSubmitted,
											 bool a_markerCycleHealthy,
											 UINT a_submittedAllocator,
											 bool a_fidelityFXInputsUsed,
											 HRESULT a_preparationResult) {
		FinalizedPresent finalized{};
		bool providerBoundaryCompleted = !usesDLSSG;
		bool asynchronousOutputStatusObserved = false;
		if (emitDLSSGMarkers) {
			a_markerCycleHealthy &=
				upscaling.streamlineDX12.EmitPCLMarkerForFrame(
					sl::PCLMarker::eRenderSubmitEnd,
					"RenderSubmitEnd",
					pclFrame);
			a_markerCycleHealthy &=
				upscaling.streamlineDX12.EmitPCLMarkerForFrame(
					sl::PCLMarker::ePresentStart,
					"PresentStart",
					pclFrame);
			if (!a_markerCycleHealthy)
				upscaling.streamlineDX12.RequestDLSSGDisable();
		}

		const HRESULT proxyResult = invokeProviderPresent();
		finalized.result = proxyResult;
		if (usesDLSSG) {
			const HRESULT apiError =
				upscaling.streamlineDX12.ConsumeDLSSGAPIError();
			asynchronousOutputStatusObserved = apiError != S_OK;
			// The host proxy returns before the underlying asynchronous Present.
			// Only a non-failing proxy result proves that its before-hook reached
			// a boundary capable of consuming the ordered options and tags.
			providerBoundaryCompleted =
				providerPresentInvoked && SUCCEEDED(proxyResult);
			if (!providerBoundaryCompleted) {
				latchInteropFailure(
					FAILED(proxyResult) ? proxyResult : E_FAIL);
			}
			if (FAILED(apiError)) {
				logger::error(
					"[DX12SwapChain] DLSS-G reported an asynchronous backend failure: 0x{:08X}",
					static_cast<unsigned>(apiError));
				upscaling.streamlineDX12.RequestDLSSGDisable();
			}
		}
		if (emitDLSSGMarkers) {
			a_markerCycleHealthy &=
				upscaling.streamlineDX12.EmitPCLMarkerForFrame(
					sl::PCLMarker::ePresentEnd,
					"PresentEnd",
					pclFrame);
			if (!a_markerCycleHealthy)
				upscaling.streamlineDX12.RequestDLSSGDisable();
		}

		finalized.accepted =
			SUCCEEDED(finalized.result) &&
			finalized.result != DXGI_STATUS_OCCLUDED;
		if (usesDLSSG && FAILED(finalized.result))
			upscaling.streamlineDX12.RequestDLSSGDisable();
		Streamline::DLSSGPresentSync dlssgSync{};
		if (usesDLSSG && providerPresentInvoked) {
			dlssgSync =
				upscaling.streamlineDX12.UpdateDLSSGStateAfterPresent(
					a_dlssgGenerationRequested,
					providerBoundaryCompleted,
					finalized.accepted,
					a_dlssgNullTagsSubmitted);
			finalized.frameGenerationActive =
				finalized.accepted && a_markerCycleHealthy &&
				upscaling.streamlineDX12.dlssgState.active;
		} else if (frameGenerationBackend ==
				   FrameGenerationBackend::kFidelityFX) {
			finalized.frameGenerationActive =
				finalized.accepted && a_fidelityFXInputsUsed &&
				fidelityFXResult.active;
			upscaling.fidelityFX.isFrameGenActive =
				finalized.frameGenerationActive;
		}

		if (providerPresentInvoked && presentingSwapChain) {
			const UINT nextFrameIndex =
				presentingSwapChain->GetCurrentBackBufferIndex();
			if (nextFrameIndex < activeBackBufferCount) {
				frameIndex = nextFrameIndex;
			} else {
				logger::error(
					"[DX12SwapChain] Provider returned invalid back-buffer index {} for {} buffers.",
					nextFrameIndex,
					activeBackBufferCount);
				latchInteropFailure(DXGI_ERROR_INVALID_CALL);
				if (SUCCEEDED(finalized.result))
					finalized.result = DXGI_ERROR_INVALID_CALL;
			}
		}

		if (providerPresentInvoked) {
			const HRESULT syncResult = SynchronizeAfterPresent(
				a_submittedAllocator,
				dlssgSync.inputsCompletionFence.get(),
				dlssgSync.inputsCompletionValue);
			if (FAILED(syncResult) && usesDLSSG)
				upscaling.streamlineDX12.RequestDLSSGDisable();
			latchInteropFailure(syncResult);
			if (SUCCEEDED(finalized.result) && FAILED(syncResult))
				finalized.result = syncResult;
		}
		latchInteropFailure(a_preparationResult);
		if (SUCCEEDED(finalized.result) && FAILED(a_preparationResult))
			finalized.result = a_preparationResult;
		if (FAILED(finalized.result)) {
			finalized.accepted = false;
			finalized.frameGenerationActive = false;
			upscaling.streamlineDX12.dlssgState.active = false;
			upscaling.fidelityFX.isFrameGenActive = false;
		}
		finalized.providerResourcesReusable =
			!usesDLSSG || providerBoundaryCompleted;
		// The callback is asynchronous and cannot be attributed to this Present.
		// Invalidate telemetry without rewriting this call's result or acceptance.
		if (finalized.accepted)
			RecordAcceptedGamePresent();
		if (asynchronousOutputStatusObserved)
			ResetOutputPresentationTiming();
		else if (finalized.accepted)
			UpdateOutputPresentationTiming();
		else
			ResetOutputPresentationTiming();
		return finalized;
	};
	const auto presentAfterInteropFailure = [&](HRESULT a_failure) {
		latchInteropFailure(a_failure);
		const auto modeOff = prepareDLSSGModeOff(
			Streamline::DLSSGTagResult::kFailed);
		providerMayUseSeparatedUI = modeOff.providerMayUseSeparatedUI;
		if (frameGenerationBackend == FrameGenerationBackend::kFidelityFX) {
			fidelityFXResult = upscaling.fidelityFX.Present(false, isHDR);
			providerMayUseSeparatedUI =
				fidelityFXResult.providerMayUseSeparatedUI;
		}
		if (frame.uiSeparated && !providerMayUseSeparatedUI) {
			uiFallbackApplied =
				upscaling.CompositeFrameGenerationUIFallback(
					uiPreparedForOutput);
			if (!uiFallbackApplied) {
				logger::error(
					"[DX12SwapChain] Failed to restore separated UI during fail-closed Present.");
			}
		}
		const auto finalized = finalizeProviderPresent(
			providerMayUseSeparatedUI,
			modeOff.nullTagsSubmitted,
			true,
			kMaximumBackBufferCount,
			false,
			a_failure);
		return FAILED(finalized.result) ? finalized.result : a_failure;
	};
	if (FAILED(presentInteropFailure))
		return presentAfterInteropFailure(presentInteropFailure);
	static bool loggedIncompletePresentResources = false;

	const bool hasPresentResources =
		presentingSwapChain &&
		d3d11Context &&
		d3d11Fence &&
		d3d12Fence &&
		commandQueue &&
		activeBackBufferCount > 0 &&
		activeBackBufferCount <= kMaximumBackBufferCount &&
		frameIndex < activeBackBufferCount &&
		commandAllocators[frameIndex] &&
		commandLists[frameIndex] &&
		swapChainBuffers[frameIndex] &&
		swapChainBufferWrapped &&
		swapChainBufferWrapped->resource &&
		uiBufferWrapped &&
		uiBufferWrapped->rtv;
	if (!hasPresentResources) {
		if (!loggedIncompletePresentResources) {
			logger::error("[DX12SwapChain] Cannot present because D3D12 interop resources are incomplete.");
			loggedIncompletePresentResources = true;
		}
		return presentAfterInteropFailure(DXGI_ERROR_INVALID_CALL);
	}
	loggedIncompletePresentResources = false;
	const UINT recordingFrameIndex = frameIndex;
	auto* const recordingAllocator =
		commandAllocators[recordingFrameIndex].get();
	auto* const recordingCommandList =
		commandLists[recordingFrameIndex].get();

	const UINT64 prePresentFenceValue = fenceValue++;
	if (const HRESULT result = d3d11Context->Signal(d3d11Fence.get(), prePresentFenceValue); FAILED(result)) {
		logger::error("[DX12SwapChain] D3D11 pre-Present fence signal failed: 0x{:08X}", static_cast<unsigned>(result));
		return presentAfterInteropFailure(result);
	}
	if (const HRESULT result = commandQueue->Wait(d3d12Fence.get(), prePresentFenceValue); FAILED(result)) {
		logger::error("[DX12SwapChain] D3D12 pre-Present fence wait failed: 0x{:08X}", static_cast<unsigned>(result));
		return presentAfterInteropFailure(result);
	}

	if (const HRESULT result = WaitForCommandAllocator(recordingFrameIndex); FAILED(result)) {
		logger::error(
			"[DX12SwapChain] Command allocator {} is still in use: 0x{:08X}",
			recordingFrameIndex,
			static_cast<unsigned>(result));
		return presentAfterInteropFailure(result);
	}

	if (const HRESULT result = recordingAllocator->Reset(); FAILED(result)) {
		logger::error("[DX12SwapChain] Command allocator reset failed: 0x{:08X}", static_cast<unsigned>(result));
		return presentAfterInteropFailure(result);
	}
	if (const HRESULT result = recordingCommandList->Reset(recordingAllocator, nullptr); FAILED(result)) {
		logger::error("[DX12SwapChain] Command list reset failed: 0x{:08X}", static_cast<unsigned>(result));
		return presentAfterInteropFailure(result);
	}
	bool commandListRecording = true;
	const SKSE::stl::scope_exit closeRecordingList([&]() noexcept {
		if (!commandListRecording)
			return;
		commandListRecording = false;
		const HRESULT closeResult = recordingCommandList->Close();
		if (FAILED(closeResult)) {
			logger::error(
				"[DX12SwapChain] Failed to close an abandoned command list: 0x{:08X}",
				static_cast<unsigned>(closeResult));
		}
	});

	{
		auto fakeSwapChain = swapChainBufferWrapped->resource.get();
		auto realSwapChain = swapChainBuffers[recordingFrameIndex].get();
		const D3D12_RESOURCE_BARRIER beginCopyBarriers[] = {
			CD3DX12_RESOURCE_BARRIER::Transition(
				fakeSwapChain,
				D3D12_RESOURCE_STATE_COMMON,
				D3D12_RESOURCE_STATE_COPY_SOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(
				realSwapChain,
				D3D12_RESOURCE_STATE_PRESENT,
				D3D12_RESOURCE_STATE_COPY_DEST)
		};
		recordingCommandList->ResourceBarrier(
			_countof(beginCopyBarriers),
			beginCopyBarriers);

		recordingCommandList->CopyResource(realSwapChain, fakeSwapChain);

		const D3D12_RESOURCE_BARRIER endCopyBarriers[] = {
			CD3DX12_RESOURCE_BARRIER::Transition(
				fakeSwapChain,
				D3D12_RESOURCE_STATE_COPY_SOURCE,
				D3D12_RESOURCE_STATE_COMMON),
			CD3DX12_RESOURCE_BARRIER::Transition(
				realSwapChain,
				D3D12_RESOURCE_STATE_COPY_DEST,
				D3D12_RESOURCE_STATE_PRESENT)
		};
		recordingCommandList->ResourceBarrier(
			_countof(endCopyBarriers),
			endCopyBarriers);
	}

	// FSR owns a separate UI surface. DLSS-G follows Streamline's working
	// integration contract: the HUD remains baked into the tagged color input.
	const bool backendUIReady = usesDLSSG || frame.uiSeparated;
	const bool useFrameGenerationInputs =
		frame.valid && frame.requested && frame.inputsReady &&
		backendUIReady && !shutdownRequestedAtEntry &&
		!upscaling.IsBackendShutdownRequested();
	bool dlssgConfigured = false;
	HRESULT preparationFailure = S_OK;
	bool executeCommands = true;
	bool markerCycleHealthy = true;
	auto dlssgTagResult = Streamline::DLSSGTagResult::kFailed;
	if (usesDLSSG) {
		auto& streamline = upscaling.streamlineDX12;
		if (emitDLSSGMarkers) {
			const bool simulationEnded = streamline.EmitPCLMarkerForFrame(
				sl::PCLMarker::eSimulationEnd,
				"SimulationEnd",
				pclFrame);
			const bool renderStarted = streamline.EmitPCLMarkerForFrame(
				sl::PCLMarker::eRenderSubmitStart,
				"RenderSubmitStart",
				pclFrame);
			markerCycleHealthy = simulationEnded && renderStarted;
			if (!markerCycleHealthy)
				streamline.RequestDLSSGDisable();
		}
		const bool requestDLSSG =
			useFrameGenerationInputs &&
			emitDLSSGMarkers &&
			markerCycleHealthy && !streamline.dlssgState.disablePending &&
			!upscaling.IsBackendShutdownRequested();
		if (requestDLSSG) {
			dlssgTagResult = streamline.TagDLSSGResources(
				recordingCommandList,
				depthBufferShared12 ? depthBufferShared12->resource.get() : nullptr,
				motionVectorBufferShared12 ? motionVectorBufferShared12->resource.get() : nullptr,
				swapChainBufferWrapped ? swapChainBufferWrapped->resource.get() : nullptr,
				uiBufferWrapped ? uiBufferWrapped->resource.get() : nullptr,
				frame.renderWidth,
				frame.renderHeight,
				pclFrame);
		} else {
			dlssgTagResult = streamline.TagDLSSGResources(
				recordingCommandList,
				nullptr,
				nullptr,
				nullptr,
				nullptr,
				frame.renderWidth,
				frame.renderHeight,
				pclFrame);
		}

		if (dlssgTagResult == Streamline::DLSSGTagResult::kTagged) {
			dlssgConfigured = streamline.ConfigureDLSSG(
				true,
				swapChainDesc.Width,
				swapChainDesc.Height,
				frame.renderWidth,
				frame.renderHeight);
			if (!dlssgConfigured) {
				dlssgTagResult = streamline.TagDLSSGResources(
					recordingCommandList,
					nullptr,
					nullptr,
					nullptr,
					nullptr,
					frame.renderWidth,
					frame.renderHeight,
					pclFrame);
			}
		}
		providerMayUseSeparatedUI = dlssgConfigured;

		if (!dlssgConfigured) {
			const auto modeOff = prepareDLSSGModeOff(dlssgTagResult);
			if (modeOff.nullTagsSubmitted)
				dlssgTagResult = Streamline::DLSSGTagResult::kCleared;
			if (!modeOff.modeOffQueued || !modeOff.tagsSafe) {
				logger::error(
					"[DX12SwapChain] DLSS-G disable or null tagging failed; presenting through Streamline and retrying mode-off next frame.");
				providerMayUseSeparatedUI =
					modeOff.providerMayUseSeparatedUI;
				if (providerMayUseSeparatedUI)
					preparationFailure = E_FAIL;
			}
		}
	} else if (frameGenerationBackend == FrameGenerationBackend::kFidelityFX) {
		fidelityFXResult = upscaling.fidelityFX.Present(
			useFrameGenerationInputs,
			isHDR);
		providerMayUseSeparatedUI =
			fidelityFXResult.providerMayUseSeparatedUI;
		if (providerMayUseSeparatedUI && !fidelityFXResult.active)
			preparationFailure = E_FAIL;
	}

	const bool frameGenerationSubmissionSucceeded =
		usesDLSSG ? dlssgConfigured :
					(useFrameGenerationInputs && fidelityFXResult.active);
	if (frame.uiSeparated && !frameGenerationSubmissionSucceeded &&
		!providerMayUseSeparatedUI) {
		if (!upscaling.CompositeFrameGenerationUIFallback(
				uiPreparedForOutput)) {
			logger::error("[DX12SwapChain] Failed to restore the separated UI after frame-generation submission failed.");
			preparationFailure = E_FAIL;
			executeCommands = false;
		} else {
			uiFallbackApplied = true;
		}

		// The fallback dispatch is issued after the normal pre-Present handshake.
		// Gate the queued D3D12 copy on this later D3D11 write as well.
		if (uiFallbackApplied) {
			const UINT64 uiFallbackFenceValue = fenceValue++;
			const HRESULT signalResult =
				d3d11Context->Signal(d3d11Fence.get(), uiFallbackFenceValue);
			if (FAILED(signalResult)) {
				logger::error(
					"[DX12SwapChain] D3D11 UI-fallback fence signal failed: 0x{:08X}",
					static_cast<unsigned>(signalResult));
				preparationFailure = signalResult;
				executeCommands = false;
			} else {
				const HRESULT waitResult =
					commandQueue->Wait(d3d12Fence.get(), uiFallbackFenceValue);
				if (FAILED(waitResult)) {
					logger::error(
						"[DX12SwapChain] D3D12 UI-fallback fence wait failed: 0x{:08X}",
						static_cast<unsigned>(waitResult));
					preparationFailure = waitResult;
					executeCommands = false;
				}
			}
		}
	}

	const HRESULT closeResult = recordingCommandList->Close();
	commandListRecording = false;
	if (FAILED(closeResult)) {
		logger::error("[DX12SwapChain] Command list close failed: 0x{:08X}", static_cast<unsigned>(closeResult));
		preparationFailure = closeResult;
		executeCommands = false;
	}

	if (!executeCommands && usesDLSSG && dlssgConfigured) {
		const auto modeOff = prepareDLSSGModeOff(dlssgTagResult);
		if (modeOff.nullTagsSubmitted)
			dlssgTagResult = Streamline::DLSSGTagResult::kCleared;
		providerMayUseSeparatedUI =
			modeOff.providerMayUseSeparatedUI;
		dlssgConfigured = providerMayUseSeparatedUI;
		if (frame.uiSeparated && !providerMayUseSeparatedUI &&
			!uiFallbackApplied) {
			uiFallbackApplied =
				upscaling.CompositeFrameGenerationUIFallback(
					uiPreparedForOutput);
		}
	} else if (!executeCommands &&
			   frameGenerationBackend == FrameGenerationBackend::kFidelityFX &&
			   fidelityFXResult.active) {
		fidelityFXResult = upscaling.fidelityFX.Present(false, isHDR);
		providerMayUseSeparatedUI =
			fidelityFXResult.providerMayUseSeparatedUI;
		if (frame.uiSeparated && !providerMayUseSeparatedUI &&
			!uiFallbackApplied) {
			uiFallbackApplied =
				upscaling.CompositeFrameGenerationUIFallback(
					uiPreparedForOutput);
		}
	}

	const UINT submittedFrameIndex = recordingFrameIndex;
	ID3D12CommandList* commandListsToExecute[] = {
		commandLists[submittedFrameIndex].get()
	};
	if (executeCommands)
		commandQueue->ExecuteCommandLists(1, commandListsToExecute);
	auto finalizedPresent = finalizeProviderPresent(
		dlssgConfigured,
		dlssgTagResult == Streamline::DLSSGTagResult::kCleared,
		markerCycleHealthy,
		executeCommands ? submittedFrameIndex : kMaximumBackBufferCount,
		useFrameGenerationInputs,
		preparationFailure);
	if (SUCCEEDED(presentInteropFailure) &&
		finalizedPresent.providerResourcesReusable) {
		float clearColor[4]{ 0, 0, 0, 0 };
		d3d11Context->ClearRenderTargetView(swapChainBufferWrapped->rtv.get(), clearColor);
		d3d11Context->ClearRenderTargetView(uiBufferWrapped->rtv.get(), clearColor);
	}

	if (finalizedPresent.accepted && SyncInterval == 0)
		upscaling.FrameLimiter(finalizedPresent.frameGenerationActive);
	if (colorSpaceChangePending && SUCCEEDED(presentInteropFailure)) {
		const HRESULT colorSpaceResult = SetColorSpace1(pendingColorSpace);
		if (FAILED(colorSpaceResult) &&
			colorSpaceResult != DXGI_ERROR_WAS_STILL_DRAWING) {
			logger::error(
				"[DX12SwapChain] Deferred color-space change failed: 0x{:08X}",
				static_cast<unsigned>(colorSpaceResult));
		}
	}

	return finalizedPresent.result;
}

HRESULT DX12SwapChain::GetDevice(REFIID uuid, void** ppDevice)
{
	if (!ppDevice)
		return E_POINTER;

	*ppDevice = nullptr;
	if (!d3d11Device)
		return E_NOINTERFACE;

	// This object is the D3D11-facing facade even when D3D12 owns presentation.
	return d3d11Device->QueryInterface(uuid, ppDevice);
}

HANDLE DX12SwapChain::GetFrameLatencyWaitableObject()
{
	if (!nativeSwapChain)
		return nullptr;

	return nativeSwapChain->GetFrameLatencyWaitableObject();
}

DX12SwapChain::OutputPresentationTiming DX12SwapChain::GetOutputPresentationTiming() const
{
	std::lock_guard lock(outputPresentationTimingMutex);
	auto result = outputPresentationTiming;
	if (!result.valid || qpf.QuadPart <= 0)
		return result;

	LARGE_INTEGER now{};
	if (!QueryPerformanceCounter(&now) ||
		now.QuadPart < previousOutputFrameStatistics.SyncQPCTime.QuadPart ||
		static_cast<double>(
			now.QuadPart - previousOutputFrameStatistics.SyncQPCTime.QuadPart) /
				static_cast<double>(qpf.QuadPart) >
			kOutputTimingStaleSeconds) {
		result.valid = false;
		result.averageFrameTimeMs = 0.0;
		result.fps = 0.0;
		result.sampledDurationMs = 0.0;
		result.presentedFrameCount = 0;
	}
	return result;
}

DX12SwapChain::PresentationTelemetry DX12SwapChain::GetPresentationTelemetry() const
{
	std::lock_guard lock(outputPresentationTimingMutex);
	return {
		.acceptedGamePresentCount = acceptedGamePresentCount,
		.outputPresentedFrameCount = outputPresentedFrameCount,
		.outputSampledDurationMs = outputSampledDurationMs,
		.sampleId = outputPresentationTiming.sampleId,
		.discontinuityEpoch = outputPresentationTiming.discontinuityEpoch,
		.outputValid = outputPresentationTiming.valid,
	};
}

void DX12SwapChain::RecordAcceptedGamePresent()
{
	std::lock_guard lock(outputPresentationTimingMutex);
	++acceptedGamePresentCount;
}

void DX12SwapChain::UpdateOutputPresentationTiming()
{
	if (!nativeSwapChain || qpf.QuadPart <= 0) {
		ResetOutputPresentationTiming();
		return;
	}

	DXGI_FRAME_STATISTICS statistics{};
	// The active frame-generation proxy forwards statistics from the real swap
	// chain, where both interpolated and game frames cross the display boundary.
	if (FAILED(nativeSwapChain->GetFrameStatistics(&statistics))) {
		ResetOutputPresentationTiming();
		return;
	}

	std::lock_guard lock(outputPresentationTimingMutex);
	if (!hasOutputFrameStatisticsBaseline) {
		previousOutputFrameStatistics = statistics;
		hasOutputFrameStatisticsBaseline = true;
		outputPresentationTiming.valid = false;
		return;
	}

	const auto previousPresentCount =
		previousOutputFrameStatistics.PresentCount;
	const auto previousSyncQpc =
		previousOutputFrameStatistics.SyncQPCTime.QuadPart;
	if (statistics.PresentCount < previousPresentCount ||
		statistics.SyncQPCTime.QuadPart < previousSyncQpc) {
		previousOutputFrameStatistics = statistics;
		InvalidateOutputPresentationTimingLocked();
		return;
	}

	const uint32_t presentedFrameCount =
		statistics.PresentCount - previousPresentCount;
	const int64_t elapsedQpc =
		statistics.SyncQPCTime.QuadPart - previousSyncQpc;
	if (presentedFrameCount == 0 || elapsedQpc <= 0)
		return;

	const double sampledDurationMs =
		1000.0 * static_cast<double>(elapsedQpc) /
		static_cast<double>(qpf.QuadPart);
	const double frameTimeMs =
		sampledDurationMs / static_cast<double>(presentedFrameCount);
	if (!std::isfinite(sampledDurationMs) ||
		!std::isfinite(frameTimeMs) ||
		sampledDurationMs > kMaximumOutputSampleDurationMs ||
		frameTimeMs <= 0.0) {
		previousOutputFrameStatistics = statistics;
		InvalidateOutputPresentationTimingLocked();
		return;
	}

	previousOutputFrameStatistics = statistics;
	outputPresentationTiming.averageFrameTimeMs = frameTimeMs;
	outputPresentationTiming.fps = 1000.0 / frameTimeMs;
	outputPresentationTiming.sampledDurationMs = sampledDurationMs;
	outputPresentationTiming.presentedFrameCount = presentedFrameCount;
	outputPresentedFrameCount += presentedFrameCount;
	outputSampledDurationMs += sampledDurationMs;
	++outputPresentationTiming.sampleId;
	outputPresentationTiming.valid = true;
}

void DX12SwapChain::ResetOutputPresentationTiming(bool force)
{
	std::lock_guard lock(outputPresentationTimingMutex);
	if (!force &&
		!hasOutputFrameStatisticsBaseline &&
		!outputPresentationTiming.valid) {
		return;
	}

	previousOutputFrameStatistics = {};
	hasOutputFrameStatisticsBaseline = false;
	InvalidateOutputPresentationTimingLocked();
}

void DX12SwapChain::InvalidateOutputPresentationTimingLocked()
{
	const auto sampleId = outputPresentationTiming.sampleId;
	const auto discontinuityEpoch =
		outputPresentationTiming.discontinuityEpoch + 1;
	outputPresentationTiming = {};
	outputPresentationTiming.sampleId = sampleId;
	outputPresentationTiming.discontinuityEpoch =
		discontinuityEpoch;
	outputPresentedFrameCount = 0;
	outputSampledDurationMs = 0.0;
}

WrappedResource::WrappedResource(
	D3D11_TEXTURE2D_DESC a_texDesc,
	ID3D11Device5* a_d3d11Device,
	ID3D12Device* a_d3d12Device,
	std::string_view a_debugName)
{
	const std::string_view debugName = a_debugName.empty() ? "Unnamed D3D11/D3D12 interop resource" : a_debugName;
	if (!a_d3d11Device || !a_d3d12Device) {
		logger::error(
			"[DX12SwapChain] Cannot construct '{}': D3D11 device present={}, D3D12 device present={}",
			debugName,
			a_d3d11Device != nullptr,
			a_d3d12Device != nullptr);
		DX::ThrowIfFailed(E_POINTER);
	}

	const auto throwIfResourceOperationFailed = [&](HRESULT a_result, std::string_view a_operation) {
		if (FAILED(a_result)) {
			logger::error(
				"[DX12SwapChain] {} failed for '{}': HRESULT=0x{:08X}, size={}x{}, array={}, format={}, bind=0x{:X}, misc=0x{:X}",
				a_operation,
				debugName,
				static_cast<unsigned>(a_result),
				a_texDesc.Width,
				a_texDesc.Height,
				a_texDesc.ArraySize,
				static_cast<unsigned>(a_texDesc.Format),
				a_texDesc.BindFlags,
				a_texDesc.MiscFlags);
			DX::ThrowIfFailed(a_result);
		}
	};

	// Create D3D11 shared texture directly instead of wrapping D3D12 resource
	a_texDesc.MiscFlags |= D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
	throwIfResourceOperationFailed(
		a_d3d11Device->CreateTexture2D(&a_texDesc, nullptr, resource11.put()),
		"CreateTexture2D");
	const std::string resourceName = std::format("Upscaling::{}", debugName);
	Util::SetResourceName(resource11.get(), "%s", resourceName.c_str());

	// Get shared handle from D3D11 texture to enable D3D12 access
	winrt::com_ptr<IDXGIResource1> dxgiResource;
	throwIfResourceOperationFailed(
		resource11->QueryInterface(IID_PPV_ARGS(dxgiResource.put())),
		"QueryInterface(IDXGIResource1)");
	ScopedHandle sharedHandle;
	throwIfResourceOperationFailed(
		dxgiResource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &sharedHandle.handle),
		"CreateSharedHandle");

	// Open the shared D3D11 texture as D3D12 resource
	throwIfResourceOperationFailed(
		a_d3d12Device->OpenSharedHandle(sharedHandle.handle, IID_PPV_ARGS(resource.put())),
		"OpenSharedHandle(ID3D12Resource)");
	const std::wstring resourceNameWide(resourceName.begin(), resourceName.end());
	resource->SetName(resourceNameWide.c_str());

	if (a_texDesc.BindFlags & D3D11_BIND_SHADER_RESOURCE) {
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = a_texDesc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;

		throwIfResourceOperationFailed(
			a_d3d11Device->CreateShaderResourceView(resource11.get(), &srvDesc, srv.put()),
			"CreateShaderResourceView");
		Util::SetResourceName(srv.get(), "%s SRV", resourceName.c_str());
	}

	if (a_texDesc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) {
		if (a_texDesc.ArraySize > 1) {
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format = a_texDesc.Format;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
			uavDesc.Texture2DArray.FirstArraySlice = 0;
			uavDesc.Texture2DArray.ArraySize = a_texDesc.ArraySize;

			throwIfResourceOperationFailed(
				a_d3d11Device->CreateUnorderedAccessView(resource11.get(), &uavDesc, uav.put()),
				"CreateUnorderedAccessView(Texture2DArray)");
		} else {
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format = a_texDesc.Format;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Texture2D.MipSlice = 0;

			throwIfResourceOperationFailed(
				a_d3d11Device->CreateUnorderedAccessView(resource11.get(), &uavDesc, uav.put()),
				"CreateUnorderedAccessView(Texture2D)");
		}
		Util::SetResourceName(uav.get(), "%s UAV", resourceName.c_str());
	}

	if (a_texDesc.BindFlags & D3D11_BIND_RENDER_TARGET) {
		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		rtvDesc.Format = a_texDesc.Format;
		rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		rtvDesc.Texture2D.MipSlice = 0;
		throwIfResourceOperationFailed(
			a_d3d11Device->CreateRenderTargetView(resource11.get(), &rtvDesc, rtv.put()),
			"CreateRenderTargetView");
		Util::SetResourceName(rtv.get(), "%s RTV", resourceName.c_str());
	}
}

DXGISwapChainProxy::DXGISwapChainProxy(
	IDXGISwapChain4* a_streamlineSwapChain,
	IDXGISwapChain4* a_nativeSwapChain)
{
	streamlineSwapChain.copy_from(a_streamlineSwapChain);
	nativeSwapChain.copy_from(a_nativeSwapChain);
}

void DXGISwapChainProxy::RetireStreamlineSwapChain()
{
	streamlineSwapChain = nullptr;
}

/****IUknown****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::QueryInterface(REFIID riid, void** ppvObj)
{
	if (!ppvObj)
		return E_POINTER;

	*ppvObj = nullptr;

	if (riid == __uuidof(IUnknown) ||
		riid == __uuidof(IDXGIObject) ||
		riid == __uuidof(IDXGIDeviceSubObject) ||
		riid == __uuidof(IDXGISwapChain) ||
		riid == __uuidof(IDXGISwapChain1) ||
		riid == __uuidof(IDXGISwapChain2) ||
		riid == __uuidof(IDXGISwapChain3) ||
		riid == __uuidof(IDXGISwapChain4)) {
		*ppvObj = static_cast<IDXGISwapChain4*>(this);
		AddRef();
		return S_OK;
	}

	return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE DXGISwapChainProxy::AddRef()
{
	return referenceCount.fetch_add(1, std::memory_order_relaxed) + 1;
}

ULONG STDMETHODCALLTYPE DXGISwapChainProxy::Release()
{
	const ULONG remaining = referenceCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
	if (remaining == 0)
		delete this;
	return remaining;
}

/****IDXGIObject****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetPrivateData(_In_ REFGUID Name, UINT DataSize, _In_reads_bytes_(DataSize) const void* pData)
{
	return nativeSwapChain->SetPrivateData(Name, DataSize, pData);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetPrivateDataInterface(_In_ REFGUID Name, _In_opt_ const IUnknown* pUnknown)
{
	return nativeSwapChain->SetPrivateDataInterface(Name, pUnknown);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetPrivateData(_In_ REFGUID Name, _Inout_ UINT* pDataSize, _Out_writes_bytes_(*pDataSize) void* pData)
{
	return nativeSwapChain->GetPrivateData(Name, pDataSize, pData);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetParent(_In_ REFIID riid, _COM_Outptr_ void** ppParent)
{
	return nativeSwapChain->GetParent(riid, ppParent);
}

/****IDXGIDeviceSubObject****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetDevice(_In_ REFIID riid, _COM_Outptr_ void** ppDevice)
{
	return globals::features::upscaling.dx12SwapChain.GetDevice(riid, ppDevice);
}

/****IDXGISwapChain****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::Present(UINT SyncInterval, UINT Flags)
{
	const HRESULT result =
		globals::features::upscaling.dx12SwapChain.Present(SyncInterval, Flags);
	Hooks::RecordSwapChainBasePresentResult(result);
	return result;
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetBuffer(UINT Buffer, _In_ REFIID riid, _COM_Outptr_ void** ppSurface)
{
	return globals::features::upscaling.dx12SwapChain.GetBuffer(Buffer, riid, ppSurface);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetFullscreenState(BOOL Fullscreen, _In_opt_ IDXGIOutput* pTarget)
{
	auto& chain = globals::features::upscaling.dx12SwapChain;
	IDXGISwapChain4* const innerSwapChain = chain.GetManualHookSwapChain();
	if (!innerSwapChain)
		return DXGI_ERROR_INVALID_CALL;
	if (const HRESULT prepareResult = chain.PrepareForSwapChainChange();
		FAILED(prepareResult))
		return prepareResult;
	const HRESULT result = innerSwapChain->SetFullscreenState(Fullscreen, pTarget);
	BOOL fullscreenAfter = FALSE;
	winrt::com_ptr<IDXGIOutput> targetAfter;
	const HRESULT afterResult =
		nativeSwapChain->GetFullscreenState(
			&fullscreenAfter,
			targetAfter.put());
	if (FAILED(afterResult)) {
		const HRESULT failure = FAILED(result) ? result : afterResult;
		chain.RecordExternalSwapChainMutation(failure);
		return failure;
	}
	if (FAILED(result)) {
		chain.RecordExternalSwapChainMutation(result);
		return result;
	}

	const bool requestedFullscreen = Fullscreen != FALSE;
	const bool appliedFullscreen = fullscreenAfter != FALSE;
	const bool requestedTargetApplied =
		!requestedFullscreen || !pTarget ||
		Util::HaveSameCOMIdentity(pTarget, targetAfter.get());
	if (appliedFullscreen != requestedFullscreen ||
		!requestedTargetApplied) {
		const HRESULT postconditionFailure = DXGI_ERROR_INVALID_CALL;
		chain.RecordExternalSwapChainMutation(postconditionFailure);
		return postconditionFailure;
	}

	chain.RecordExternalSwapChainMutation();
	return result;
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetFullscreenState(_Out_opt_ BOOL* pFullscreen, _COM_Outptr_opt_result_maybenull_ IDXGIOutput** ppTarget)
{
	return nativeSwapChain->GetFullscreenState(pFullscreen, ppTarget);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetDesc(_Out_ DXGI_SWAP_CHAIN_DESC* pDesc)
{
	return nativeSwapChain->GetDesc(pDesc);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::ResizeBuffers(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
	return globals::features::upscaling.dx12SwapChain.ResizeBuffers(BufferCount, Width, Height, NewFormat, SwapChainFlags);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::ResizeTarget(_In_ const DXGI_MODE_DESC* pNewTargetParameters)
{
	auto& chain = globals::features::upscaling.dx12SwapChain;
	if (const HRESULT prepareResult = chain.PrepareForSwapChainChange();
		FAILED(prepareResult))
		return prepareResult;
	const HRESULT result = nativeSwapChain->ResizeTarget(pNewTargetParameters);
	if (SUCCEEDED(result))
		chain.RecordExternalSwapChainMutation();
	return result;
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetContainingOutput(_COM_Outptr_ IDXGIOutput** ppOutput)
{
	return nativeSwapChain->GetContainingOutput(ppOutput);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetFrameStatistics(_Out_ DXGI_FRAME_STATISTICS* pStats)
{
	return nativeSwapChain->GetFrameStatistics(pStats);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetLastPresentCount(_Out_ UINT* pLastPresentCount)
{
	return nativeSwapChain->GetLastPresentCount(pLastPresentCount);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetDesc1(DXGI_SWAP_CHAIN_DESC1* pDesc)
{
	return nativeSwapChain->GetDesc1(pDesc);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pDesc)
{
	return nativeSwapChain->GetFullscreenDesc(pDesc);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetHwnd(HWND* pHwnd)
{
	return nativeSwapChain->GetHwnd(pHwnd);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetCoreWindow(REFIID refiid, void** ppUnk)
{
	return nativeSwapChain->GetCoreWindow(refiid, ppUnk);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::Present1(
	UINT SyncInterval,
	UINT PresentFlags,
	const DXGI_PRESENT_PARAMETERS* pPresentParameters)
{
	const HRESULT result = globals::features::upscaling.dx12SwapChain.Present1(
		SyncInterval,
		PresentFlags,
		pPresentParameters);
	Hooks::RecordSwapChainBasePresentResult(result);
	return result;
}

BOOL STDMETHODCALLTYPE DXGISwapChainProxy::IsTemporaryMonoSupported()
{
	return nativeSwapChain->IsTemporaryMonoSupported();
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetRestrictToOutput(IDXGIOutput** ppRestrictToOutput)
{
	return nativeSwapChain->GetRestrictToOutput(ppRestrictToOutput);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetBackgroundColor(const DXGI_RGBA* pColor)
{
	return nativeSwapChain->SetBackgroundColor(pColor);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetBackgroundColor(DXGI_RGBA* pColor)
{
	return nativeSwapChain->GetBackgroundColor(pColor);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetRotation(DXGI_MODE_ROTATION Rotation)
{
	auto& chain = globals::features::upscaling.dx12SwapChain;
	if (const HRESULT prepareResult = chain.PrepareForSwapChainChange();
		FAILED(prepareResult))
		return prepareResult;
	const HRESULT result = nativeSwapChain->SetRotation(Rotation);
	if (SUCCEEDED(result))
		chain.RecordExternalSwapChainMutation();
	return result;
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetRotation(DXGI_MODE_ROTATION* pRotation)
{
	return nativeSwapChain->GetRotation(pRotation);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetSourceSize(UINT Width, UINT Height)
{
	auto& chain = globals::features::upscaling.dx12SwapChain;
	if (const HRESULT prepareResult = chain.PrepareForSwapChainChange();
		FAILED(prepareResult))
		return prepareResult;
	const HRESULT result = nativeSwapChain->SetSourceSize(Width, Height);
	if (SUCCEEDED(result))
		chain.RecordExternalSwapChainMutation();
	return result;
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetSourceSize(UINT* pWidth, UINT* pHeight)
{
	return nativeSwapChain->GetSourceSize(pWidth, pHeight);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetMaximumFrameLatency(UINT MaxLatency)
{
	return nativeSwapChain->SetMaximumFrameLatency(MaxLatency);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetMaximumFrameLatency(UINT* pMaxLatency)
{
	return nativeSwapChain->GetMaximumFrameLatency(pMaxLatency);
}

HANDLE STDMETHODCALLTYPE DXGISwapChainProxy::GetFrameLatencyWaitableObject()
{
	return globals::features::upscaling.dx12SwapChain.GetFrameLatencyWaitableObject();
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetMatrixTransform(const DXGI_MATRIX_3X2_F* pMatrix)
{
	return nativeSwapChain->SetMatrixTransform(pMatrix);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetMatrixTransform(DXGI_MATRIX_3X2_F* pMatrix)
{
	return nativeSwapChain->GetMatrixTransform(pMatrix);
}

UINT STDMETHODCALLTYPE DXGISwapChainProxy::GetCurrentBackBufferIndex()
{
	auto* const innerSwapChain =
		globals::features::upscaling.dx12SwapChain.GetManualHookSwapChain();
	return innerSwapChain ? innerSwapChain->GetCurrentBackBufferIndex() : 0;
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::CheckColorSpaceSupport(
	DXGI_COLOR_SPACE_TYPE ColorSpace,
	UINT* pColorSpaceSupport)
{
	return nativeSwapChain->CheckColorSpaceSupport(ColorSpace, pColorSpaceSupport);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetColorSpace1(DXGI_COLOR_SPACE_TYPE ColorSpace)
{
	return globals::features::upscaling.dx12SwapChain.SetColorSpace1(ColorSpace);
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
	return globals::features::upscaling.dx12SwapChain.ResizeBuffers1(
		BufferCount,
		Width,
		Height,
		Format,
		SwapChainFlags,
		pCreationNodeMask,
		ppPresentQueue);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetHDRMetaData(
	DXGI_HDR_METADATA_TYPE Type,
	UINT Size,
	void* pMetaData)
{
	return nativeSwapChain->SetHDRMetaData(Type, Size, pMetaData);
}

bool DX12SwapChain::SetColorSpace(bool enableHDR)
{
	const auto colorSpace = enableHDR ?
	                            DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 :
	                            DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
	const HRESULT result = SetColorSpace1(colorSpace);
	if (result == DXGI_ERROR_WAS_STILL_DRAWING) {
		pendingColorSpace = colorSpace;
		colorSpaceChangePending = true;
		logger::info(
			"[DX12SwapChain] Deferred the {} color space until DLSS-G is off.",
			enableHDR ? "HDR10" : "SDR");
		return false;
	}
	if (FAILED(result)) {
		logger::error(
			"[DX12SwapChain] Could not set the {} color space: 0x{:08X}",
			enableHDR ? "HDR10" : "SDR",
			static_cast<unsigned>(result));
		return false;
	}
	logger::info(
		"[DX12SwapChain] Set color space to {}",
		enableHDR ? "HDR10 (PQ/BT.2020)" : "SDR (sRGB)");
	return true;
}

HRESULT DX12SwapChain::SetColorSpace1(DXGI_COLOR_SPACE_TYPE a_colorSpace)
{
	if (!nativeSwapChain)
		return DXGI_ERROR_INVALID_CALL;
	if (const HRESULT prepareResult = PrepareForSwapChainChange();
		FAILED(prepareResult))
		return prepareResult;
	return SetColorSpaceNow(a_colorSpace);
}

HRESULT DX12SwapChain::SetColorSpaceNow(DXGI_COLOR_SPACE_TYPE a_colorSpace)
{
	if (!nativeSwapChain)
		return DXGI_ERROR_INVALID_CALL;

	UINT support = 0;
	const HRESULT supportResult =
		nativeSwapChain->CheckColorSpaceSupport(a_colorSpace, &support);
	if (FAILED(supportResult)) {
		colorSpaceChangePending = false;
		return supportResult;
	}
	if ((support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) == 0) {
		colorSpaceChangePending = false;
		return DXGI_ERROR_UNSUPPORTED;
	}

	const HRESULT result = nativeSwapChain->SetColorSpace1(a_colorSpace);
	colorSpaceChangePending = false;
	if (SUCCEEDED(result)) {
		hdrColorSpaceActive =
			a_colorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
		RecordExternalSwapChainMutation();
	}
	return result;
}

DX12SwapChain::BlurResources DX12SwapChain::GetBlurResources() const
{
	BlurResources res;
	if (swapChainBufferWrapped) {
		res.backbufferTex = swapChainBufferWrapped->resource11.get();
		res.backbufferRTV = swapChainBufferWrapped->rtv.get();
		res.backbufferSRV = swapChainBufferWrapped->srv.get();
	}
	if (uiBufferWrapped) {
		res.uiBufferSRV = uiBufferWrapped->srv.get();
		res.uiBufferRTV = uiBufferWrapped->rtv.get();
	}
	return res;
}

void DX12SwapChain::CreateSharedResources()
{
	depthBufferShared12.reset();
	motionVectorBufferShared12.reset();

	auto renderer = globals::game::renderer;
	if (!renderer || !d3d11Device || !d3d12Device) {
		logger::error("[DX12SwapChain] Cannot create shared resources because renderer or device state is incomplete.");
		return;
	}

	// Create depth buffer
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
	if (!main.texture || !motionVector.texture) {
		logger::error("[DX12SwapChain] Cannot create shared resources because source textures are missing.");
		return;
	}

	try {
		D3D11_TEXTURE2D_DESC texDesc{};
		main.texture->GetDesc(&texDesc);
		texDesc.Format = DXGI_FORMAT_R32_FLOAT;
		auto newDepthBuffer = std::make_unique<WrappedResource>(
			texDesc,
			d3d11Device.get(),
			d3d12Device.get(),
			"Frame-generation depth interop");

		motionVector.texture->GetDesc(&texDesc);
		auto newMotionVectorBuffer = std::make_unique<WrappedResource>(
			texDesc,
			d3d11Device.get(),
			d3d12Device.get(),
			"Frame-generation motion-vector interop");

		// Both inputs describe the same render generation. Publish them together.
		depthBufferShared12 = std::move(newDepthBuffer);
		motionVectorBufferShared12 = std::move(newMotionVectorBuffer);
	} catch (const std::exception& error) {
		logger::error(
			"[DX12SwapChain] Could not create frame-generation shared resources: {}",
			error.what());
	} catch (...) {
		logger::error(
			"[DX12SwapChain] Could not create frame-generation shared resources: unknown exception");
	}
}
