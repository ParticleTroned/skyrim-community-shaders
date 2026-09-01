#pragma once

#include <Windows.Foundation.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdio.h>
#include <string_view>
#include <winrt/base.h>
#include <wrl\client.h>
#include <wrl\wrappers\corewrappers.h>

#include <d3d11_4.h>
#include <directx/d3d12.h>
#include <dxgi1_5.h>

#include <directx/d3dx12.h>

class WrappedResource
{
public:
	WrappedResource(
		D3D11_TEXTURE2D_DESC a_texDesc,
		ID3D11Device5* a_d3d11Device,
		ID3D12Device* a_d3d12Device,
		std::string_view a_debugName = {});
	~WrappedResource() = default;

	winrt::com_ptr<ID3D11Texture2D> resource11;
	winrt::com_ptr<ID3D11ShaderResourceView> srv;
	winrt::com_ptr<ID3D11UnorderedAccessView> uav;
	winrt::com_ptr<ID3D11RenderTargetView> rtv;
	winrt::com_ptr<ID3D12Resource> resource;
};

struct DXGISwapChainProxy : IDXGISwapChain4
{
public:
	DXGISwapChainProxy(
		IDXGISwapChain4* a_streamlineSwapChain,
		IDXGISwapChain4* a_nativeSwapChain);

	winrt::com_ptr<IDXGISwapChain4> streamlineSwapChain;
	winrt::com_ptr<IDXGISwapChain4> nativeSwapChain;
	std::atomic<ULONG> referenceCount{ 1 };

	/****IUnknown****/
	virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObj) override;
	virtual ULONG STDMETHODCALLTYPE AddRef() override;
	virtual ULONG STDMETHODCALLTYPE Release() override;
	/** @brief Releases the inner Streamline proxy after terminal backend retirement. */
	void RetireStreamlineSwapChain();

	/****IDXGIObject****/
	virtual HRESULT STDMETHODCALLTYPE SetPrivateData(_In_ REFGUID Name, UINT DataSize, _In_reads_bytes_(DataSize) const void* pData) override;
	virtual HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(_In_ REFGUID Name, _In_opt_ const IUnknown* pUnknown) override;
	virtual HRESULT STDMETHODCALLTYPE GetPrivateData(_In_ REFGUID Name, _Inout_ UINT* pDataSize, _Out_writes_bytes_(*pDataSize) void* pData) override;
	virtual HRESULT STDMETHODCALLTYPE GetParent(_In_ REFIID riid, _COM_Outptr_ void** ppParent) override;

	/****IDXGIDeviceSubObject****/
	virtual HRESULT STDMETHODCALLTYPE GetDevice(_In_ REFIID riid, _COM_Outptr_ void** ppDevice) override;

	/****IDXGISwapChain****/
	virtual HRESULT STDMETHODCALLTYPE Present(UINT SyncInterval, UINT Flags);
	virtual HRESULT STDMETHODCALLTYPE GetBuffer(UINT Buffer, _In_ REFIID riid, _COM_Outptr_ void** ppSurface);
	virtual HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL Fullscreen, _In_opt_ IDXGIOutput* pTarget);
	virtual HRESULT STDMETHODCALLTYPE GetFullscreenState(_Out_opt_ BOOL* pFullscreen, _COM_Outptr_opt_result_maybenull_ IDXGIOutput** ppTarget);
	virtual HRESULT STDMETHODCALLTYPE GetDesc(_Out_ DXGI_SWAP_CHAIN_DESC* pDesc);
	virtual HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
	virtual HRESULT STDMETHODCALLTYPE ResizeTarget(_In_ const DXGI_MODE_DESC* pNewTargetParameters);
	virtual HRESULT STDMETHODCALLTYPE GetContainingOutput(_COM_Outptr_ IDXGIOutput** ppOutput);
	virtual HRESULT STDMETHODCALLTYPE GetFrameStatistics(_Out_ DXGI_FRAME_STATISTICS* pStats);
	virtual HRESULT STDMETHODCALLTYPE GetLastPresentCount(_Out_ UINT* pLastPresentCount);

	/****IDXGISwapChain1****/
	virtual HRESULT STDMETHODCALLTYPE GetDesc1(_Out_ DXGI_SWAP_CHAIN_DESC1* pDesc) override;
	virtual HRESULT STDMETHODCALLTYPE GetFullscreenDesc(_Out_ DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pDesc) override;
	virtual HRESULT STDMETHODCALLTYPE GetHwnd(_Out_ HWND* pHwnd) override;
	virtual HRESULT STDMETHODCALLTYPE GetCoreWindow(_In_ REFIID refiid, _COM_Outptr_ void** ppUnk) override;
	virtual HRESULT STDMETHODCALLTYPE Present1(UINT SyncInterval, UINT PresentFlags, _In_ const DXGI_PRESENT_PARAMETERS* pPresentParameters) override;
	virtual BOOL STDMETHODCALLTYPE IsTemporaryMonoSupported() override;
	virtual HRESULT STDMETHODCALLTYPE GetRestrictToOutput(_COM_Outptr_ IDXGIOutput** ppRestrictToOutput) override;
	virtual HRESULT STDMETHODCALLTYPE SetBackgroundColor(_In_ const DXGI_RGBA* pColor) override;
	virtual HRESULT STDMETHODCALLTYPE GetBackgroundColor(_Out_ DXGI_RGBA* pColor) override;
	virtual HRESULT STDMETHODCALLTYPE SetRotation(_In_ DXGI_MODE_ROTATION Rotation) override;
	virtual HRESULT STDMETHODCALLTYPE GetRotation(_Out_ DXGI_MODE_ROTATION* pRotation) override;

	/****IDXGISwapChain2****/
	virtual HRESULT STDMETHODCALLTYPE SetSourceSize(UINT Width, UINT Height) override;
	virtual HRESULT STDMETHODCALLTYPE GetSourceSize(_Out_ UINT* pWidth, _Out_ UINT* pHeight) override;
	virtual HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT MaxLatency) override;
	virtual HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(_Out_ UINT* pMaxLatency) override;
	virtual HANDLE STDMETHODCALLTYPE GetFrameLatencyWaitableObject() override;
	virtual HRESULT STDMETHODCALLTYPE SetMatrixTransform(const DXGI_MATRIX_3X2_F* pMatrix) override;
	virtual HRESULT STDMETHODCALLTYPE GetMatrixTransform(_Out_ DXGI_MATRIX_3X2_F* pMatrix) override;

	/****IDXGISwapChain3****/
	virtual UINT STDMETHODCALLTYPE GetCurrentBackBufferIndex() override;
	virtual HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE ColorSpace, _Out_ UINT* pColorSpaceSupport) override;
	virtual HRESULT STDMETHODCALLTYPE SetColorSpace1(DXGI_COLOR_SPACE_TYPE ColorSpace) override;
	virtual HRESULT STDMETHODCALLTYPE ResizeBuffers1(
		UINT BufferCount,
		UINT Width,
		UINT Height,
		DXGI_FORMAT Format,
		UINT SwapChainFlags,
		_In_reads_(BufferCount) const UINT* pCreationNodeMask,
		_In_reads_(BufferCount) IUnknown* const* ppPresentQueue) override;

	/****IDXGISwapChain4****/
	virtual HRESULT STDMETHODCALLTYPE SetHDRMetaData(DXGI_HDR_METADATA_TYPE Type, UINT Size, _In_reads_bytes_opt_(Size) void* pMetaData) override;
};

class DX12SwapChain
{
public:
	static constexpr UINT kFidelityFXBackBufferCount = 2;
	static constexpr UINT kDLSSGBackBufferCount = 3;
	static constexpr UINT kMaximumBackBufferCount = kDLSSGBackBufferCount;

	enum class FrameGenerationBackend
	{
		kNone,
		kFidelityFX,
		kDLSSG
	};

	/** @brief One validated delta from the real swap chain's presentation statistics. */
	struct OutputPresentationTiming
	{
		double averageFrameTimeMs = 0.0;
		double fps = 0.0;
		double sampledDurationMs = 0.0;
		uint32_t presentedFrameCount = 0;
		uint64_t sampleId = 0;
		uint64_t discontinuityEpoch = 0;
		bool valid = false;
	};

	/** @brief Monotonic counters for bounded frame-generation measurements. */
	struct PresentationTelemetry
	{
		uint64_t acceptedGamePresentCount = 0;
		uint64_t outputPresentedFrameCount = 0;
		double outputSampledDurationMs = 0.0;
		uint64_t sampleId = 0;
		uint64_t discontinuityEpoch = 0;
		bool outputValid = false;
	};

	winrt::com_ptr<ID3D12Device> d3d12Device;
	winrt::com_ptr<ID3D12Device> d3d12ProxyDevice;
	winrt::com_ptr<ID3D12CommandQueue> commandQueue;
	winrt::com_ptr<ID3D12CommandQueue> commandQueueProxy;
	winrt::com_ptr<ID3D12CommandAllocator> commandAllocators[kMaximumBackBufferCount];
	winrt::com_ptr<ID3D12GraphicsCommandList4> commandLists[kMaximumBackBufferCount];
	winrt::com_ptr<IDXGIFactory4> dxgiFactory;
	winrt::com_ptr<IDXGISwapChain4> nativeSwapChain;
	winrt::com_ptr<IDXGISwapChain4> streamlineSwapChainOwner;

	// Streamline proxy used only for the manual-hook methods required by DLSS-G.
	IDXGISwapChain4* swapChain = nullptr;

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};

	std::unique_ptr<WrappedResource> swapChainBufferWrapped;
	std::unique_ptr<WrappedResource> uiBufferWrapped;
	DXGI_SWAP_CHAIN_DESC1 wrappedResourceDesc{};
	bool wrappedResourceDescValid = false;

	// D3D12 interop resources for frame generation
	std::unique_ptr<WrappedResource> depthBufferShared12;
	std::unique_ptr<WrappedResource> motionVectorBufferShared12;

	winrt::com_ptr<ID3D11Device5> d3d11Device;
	winrt::com_ptr<ID3D11DeviceContext4> d3d11Context;

	winrt::com_ptr<ID3D11Fence> d3d11Fence;
	winrt::com_ptr<ID3D12Fence> d3d12Fence;

	winrt::com_ptr<ID3D12Resource> swapChainBuffers[kMaximumBackBufferCount];
	FrameGenerationBackend frameGenerationBackend = FrameGenerationBackend::kNone;
	UINT activeBackBufferCount = 0;
	bool streamlineProxyGraphActive = false;

	UINT frameIndex = 0;
	// Shared fences are created at zero, so the first submitted value must be
	// strictly greater than their initial completed value.
	UINT64 fenceValue = 1;
	UINT64 commandAllocatorFenceValues[kMaximumBackBufferCount]{};
	HRESULT presentInteropFailure = S_OK;
	bool hdrColorSpaceActive = false;
	bool colorSpaceChangePending = false;
	DXGI_COLOR_SPACE_TYPE pendingColorSpace =
		DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
	std::atomic_uint64_t interopGeneration = 0;

	LARGE_INTEGER qpf{};

	double refreshRate = 0;

	winrt::com_ptr<DXGISwapChainProxy> swapChainProxy;

	/**
	 * @brief Returns an average measured at the final DXGI presentation boundary.
	 *
	 * The sample is invalid when DXGI statistics are stale, discontinuous, or
	 * unavailable. Callers must not infer a frame-generation multiplier.
	 */
	[[nodiscard]] OutputPresentationTiming GetOutputPresentationTiming() const;
	/** @brief Returns thread-safe cumulative presentation counters. */
	[[nodiscard]] PresentationTelemetry GetPresentationTelemetry() const;

	void CreateD3D12Device(IDXGIAdapter* a_adapter);
	void CreateSwapChain(IDXGIAdapter* adapter, DXGI_SWAP_CHAIN_DESC swapChainDesc);
	/** @brief Creates and commits a fully Streamline-proxied three-buffer swap chain. */
	HRESULT CreateDLSSGSwapChain(IDXGIAdapter* a_adapter, DXGI_SWAP_CHAIN_DESC a_swapChainDesc);
	/** @brief Upgrades the D3D12 device and recreates the mandatory proxied queue. */
	[[nodiscard]] bool UpgradeD3D12DeviceForDLSSG();
	[[nodiscard]] bool DisableDLSSGCandidate();
	/** @brief Releases terminal Streamline proxy ownership while retaining native DXGI. */
	void RetireStreamlineProxyGraph();

	void CreateInterop();
	[[nodiscard]] HRESULT RecreateWrappedResources(const DXGI_SWAP_CHAIN_DESC1& a_desc);

	IDXGISwapChain4* GetSwapChainProxy();
	/** @brief Selects the Streamline proxy until shutdown, then its native pair. */
	[[nodiscard]] IDXGISwapChain4* GetManualHookSwapChain() const;
	void SetD3D11Device(ID3D11Device* a_d3d11Device);
	void SetD3D11DeviceContext(ID3D11DeviceContext* a_d3d11Context);

	HRESULT GetBuffer(UINT Buffer, REFIID riid, void** ppSurface);
	HRESULT ResizeBuffers(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
	HRESULT Present(UINT SyncInterval, UINT Flags);
	HRESULT ResizeBuffers1(
		UINT a_bufferCount,
		UINT a_width,
		UINT a_height,
		DXGI_FORMAT a_format,
		UINT a_flags,
		const UINT* a_creationNodeMask,
		IUnknown* const* a_presentQueues);
	HRESULT Present1(UINT a_syncInterval, UINT a_flags, const DXGI_PRESENT_PARAMETERS* a_parameters);
	HRESULT GetDevice(_In_ REFIID riid, _COM_Outptr_ void** ppDevice);
	HANDLE GetFrameLatencyWaitableObject();
	[[nodiscard]] bool DisableDLSSGBeforeSwapChainChange();
	/** @brief Returns a retryable failure until generation is off, then drains interop. */
	[[nodiscard]] HRESULT PrepareForSwapChainChange();
	/** @brief Confirms mode-off and drains interop before backend shutdown. */
	[[nodiscard]] bool PrepareForShutdown();
	/** @brief Invalidates snapshots and timing after an externally forwarded mutation. */
	void RecordExternalSwapChainMutation(HRESULT a_failure = S_OK);

	bool SetColorSpace(bool enableHDR);
	HRESULT SetColorSpace1(DXGI_COLOR_SPACE_TYPE a_colorSpace);
	[[nodiscard]] bool IsInteropUsable() const { return SUCCEEDED(presentInteropFailure); }
	[[nodiscard]] bool IsHDRColorSpaceActive() const { return hdrColorSpaceActive; }

	// Resources needed by BackgroundBlur when D3D12 swap chain is active
	struct BlurResources
	{
		ID3D11Texture2D* backbufferTex = nullptr;
		ID3D11RenderTargetView* backbufferRTV = nullptr;
		ID3D11ShaderResourceView* backbufferSRV = nullptr;
		ID3D11ShaderResourceView* uiBufferSRV = nullptr;
		ID3D11RenderTargetView* uiBufferRTV = nullptr;
	};

	// Get all resources needed for background blur in one call
	BlurResources GetBlurResources() const;

	// D3D12 interop resource management
	void CreateSharedResources();

private:
	void CreateD3D12CommandResources();
	HRESULT ResizeBuffersInternal(
		UINT a_bufferCount,
		UINT a_width,
		UINT a_height,
		DXGI_FORMAT a_format,
		UINT a_flags,
		const UINT* a_creationNodeMask,
		IUnknown* const* a_presentQueues,
		bool a_useResizeBuffers1);
	HRESULT PresentInternal(
		UINT a_syncInterval,
		UINT a_flags,
		const DXGI_PRESENT_PARAMETERS* a_parameters);
	[[nodiscard]] HRESULT AcquireSwapChainBuffers(
		IDXGISwapChain4* a_swapChain,
		UINT a_count,
		winrt::com_ptr<ID3D12Resource> (&a_buffers)[kMaximumBackBufferCount]);
	void ReleaseSwapChainBuffers();
	void UpdateOutputPresentationTiming();
	void RecordAcceptedGamePresent();
	void ResetOutputPresentationTiming(bool force = false);
	void InvalidateOutputPresentationTimingLocked();
	void RecordResizeGeneration(const DXGI_SWAP_CHAIN_DESC1* a_desc = nullptr);
	[[nodiscard]] HRESULT WaitForFenceValue(
		ID3D12Fence* a_fence,
		UINT64 a_value,
		std::string_view a_context);
	[[nodiscard]] HRESULT WaitForFenceValue(UINT64 a_value, std::string_view a_context);
	[[nodiscard]] HRESULT WaitForCommandAllocator(UINT a_index);
	[[nodiscard]] HRESULT WaitForInteropIdle();
	[[nodiscard]] HRESULT SynchronizeAfterPresent(
		UINT a_submittedAllocator,
		ID3D12Fence* a_providerCompletionFence,
		UINT64 a_providerCompletionValue);
	[[nodiscard]] HRESULT SetColorSpaceNow(DXGI_COLOR_SPACE_TYPE a_colorSpace);

	mutable std::mutex outputPresentationTimingMutex;
	DXGI_FRAME_STATISTICS previousOutputFrameStatistics{};
	OutputPresentationTiming outputPresentationTiming{};
	uint64_t acceptedGamePresentCount = 0;
	uint64_t outputPresentedFrameCount = 0;
	double outputSampledDurationMs = 0.0;
	bool hasOutputFrameStatisticsBaseline = false;
};
