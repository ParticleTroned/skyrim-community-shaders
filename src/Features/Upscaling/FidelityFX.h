#pragma once

#include <d3d11_4.h>
#include <directx/d3d12.h>
#include <winrt/base.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <FidelityFX/host/backends/dx11/ffx_dx11.h>
#include <FidelityFX/host/ffx_fsr3.h>
#include <FidelityFX/host/ffx_interface.h>

#include <FidelityFX/api/include/dx12/ffx_api_dx12.hpp>

#include <FidelityFX/api/include/ffx_api.hpp>
#include <FidelityFX/api/include/ffx_api_loader.h>
#include <FidelityFX/framegeneration/include/dx12/ffx_api_framegeneration_dx12.hpp>
#include <FidelityFX/framegeneration/include/ffx_framegeneration.hpp>
#include <FidelityFX/upscalers/include/ffx_upscale.hpp>

#include "../../Buffer.h"
#include "../../State.h"

class WrappedResource;

class FidelityFX
{
public:
	static constexpr const wchar_t* PluginDir = L"Data\\Shaders\\Upscaling\\FidelityFX";
	static constexpr uint32_t Fsr3Version = FFX_UPSCALER_MAKE_VERSION(FFX_FSR3_VERSION_MAJOR, FFX_FSR3_VERSION_MINOR, FFX_FSR3_VERSION_PATCH);
	static constexpr std::wstring_view RuntimeUpscalerDllName = L"amd_fidelityfx_upscaler_dx12.dll";
	static constexpr std::string_view RuntimeUpscalerDllNameUtf8 = "amd_fidelityfx_upscaler_dx12.dll";
	~FidelityFX();

	HMODULE module = nullptr;

	ffx::Context swapChainContext{};
	ffx::Context frameGenContext;
	FfxFsr3Context fsrContext[2];

	bool featureFSR3FG = false;
	bool featureRuntimeUpscaler = false;

	// Track if FidelityFX is currently being used for frame generation
	bool isFrameGenActive = false;

	// Cached DLL version info for FidelityFX plugin directory
	static std::vector<std::pair<std::string, std::string>> dllVersions;

	void LoadFFX();
	void SetupFrameGeneration();
	void Present(bool a_useFrameGeneration);

	void CreateFSRResources();

	void DestroyFSRResources(bool a_waitForIdle = true);
	bool HasFSRResources() const;
	bool AreFSRResourcesCompatible(uint32_t a_renderWidth, uint32_t a_renderHeight, uint32_t a_displayWidth, uint32_t a_displayHeight, uint32_t a_contextCount) const;
	bool HasRuntimeUpscalerResources() const;
	bool PollRuntimeUpscalerTeardownReady(const char* a_reason = nullptr);
	void ReleaseRuntimeUpscalerResourcesForRelatch(bool a_waitForIdle = true);
	bool HasFSRResourcesPendingTeardown() const;
	bool PollFSRResourceTeardownReady(const char* a_reason = nullptr);
	void ResetFSRIdleFence();
	void ResetRuntimeUpscalerResources(bool a_invalidateProviderCache = false);

	bool IsAmdAdapterDetected() const;
	bool IsNvidiaAdapterDetected() const;
	bool IsRuntimeUpscalerPresent() const;
	bool IsRuntimeFsr4AutoEligible() const;
	bool IsRuntimeFsr4Available() const;
	bool ShouldRequestRuntimeFsr4() const;
	bool ShouldUseRuntimeUpscalerForFSR() const;
	bool HasRuntimeUpscalerSupportCheckResult() const;
	bool IsRuntimeUpscalerSupportConfirmed() const;
	bool IsRuntimeUpscalerProviderMatchingRequestedVersion() const;
	bool IsRuntimeUpscalerFailureLatched() const;
	bool IsRuntimeFsr4FailureLatched() const;
	const std::string& GetRuntimeUpscalerLastFramePathLabel() const;
	const std::string& GetConfiguredFsrPathLabel() const;
	const std::string& GetDisplayedFsrPathLabel() const;
	static const std::string& GetHostFsrSdkLabel();
	static const std::string& GetRuntimeUpscalerLabel(uint32_t a_version);
	std::string GetRuntimeUpscalerProviderName() const;
	std::string GetRuntimeUpscalerRequestedVersionString() const;

	void Upscale(ID3D11Resource* a_upscalingTexture, ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_motionVectors, float a_sharpness);
	bool UpscaleRegion(uint32_t a_contextIndex, ID3D11Resource* a_color, ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
		ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_output,
		uint32_t a_renderWidth, uint32_t a_renderHeight, uint32_t a_displayWidth, uint32_t a_displayHeight,
		float a_motionVectorScaleX, float a_motionVectorScaleY, float a_sharpness);

private:
	// FSR scratch buffer - needs to be freed in DestroyFSRResources
	void* fsrScratchBuffer = nullptr;
	uint32_t fsrContextCount = 0;
	uint32_t fsrContextMaxRenderWidth = 0;
	uint32_t fsrContextMaxRenderHeight = 0;
	uint32_t fsrContextDisplayWidth = 0;
	uint32_t fsrContextDisplayHeight = 0;

	uint32_t runtimeUpscalerContextCount = 0;
	uint32_t runtimeUpscalerMaxRenderWidth = 0;
	uint32_t runtimeUpscalerMaxRenderHeight = 0;
	uint32_t runtimeUpscalerMaxDisplayWidth = 0;
	uint32_t runtimeUpscalerMaxDisplayHeight = 0;
	uint32_t runtimeUpscalerRequestedVersion = 0;
	D3D11_TEXTURE2D_DESC runtimeColorSharedDesc{};
	D3D11_TEXTURE2D_DESC runtimeDepthSharedDesc{};
	D3D11_TEXTURE2D_DESC runtimeMotionSharedDesc{};
	D3D11_TEXTURE2D_DESC runtimeReactiveSharedDesc{};
	D3D11_TEXTURE2D_DESC runtimeTransparencySharedDesc{};
	D3D11_TEXTURE2D_DESC runtimeOutputSharedDesc{};
	ffx::Context runtimeUpscalerContexts[2]{};

	winrt::com_ptr<ID3D11Fence> runtimeD3D11Fence;
	winrt::com_ptr<ID3D12Fence> runtimeD3D12Fence;
	ID3D11Query* pendingFSRResourceFreeIdleFence = nullptr;
	uint64_t pendingRuntimeTeardownD3D11FenceValue = 0;
	uint64_t pendingRuntimeTeardownD3D12FenceValue = 0;
	uint64_t runtimeFenceValue = 1;

	static constexpr uint32_t kRuntimeCommandContextCount = 8;
	struct RuntimeCommandContext
	{
		winrt::com_ptr<ID3D12CommandAllocator> commandAllocator;
		winrt::com_ptr<ID3D12GraphicsCommandList4> commandList;
		uint64_t fenceValue = 0;
	};
	std::array<RuntimeCommandContext, kRuntimeCommandContextCount> runtimeCommandContexts;
	uint32_t runtimeCommandContextCursor = 0;

	WrappedResource* runtimeColorShared[2]{};
	WrappedResource* runtimeDepthShared[2]{};
	WrappedResource* runtimeMotionShared[2]{};
	WrappedResource* runtimeReactiveShared[2]{};
	WrappedResource* runtimeTransparencyShared[2]{};
	WrappedResource* runtimeOutputShared[2]{};

	HMODULE frameGenerationModule = nullptr;
	HMODULE runtimeUpscalerModule = nullptr;

	// Flag to prevent spamming the log with FSR3 dispatch crash messages
	bool fsrDispatchCrashLogged = false;

	enum class RuntimeUpscalerFramePath : uint8_t
	{
		kInactive = 0,
		kHostFsr31 = 1,
		kRuntimeFsr31 = 2,
		kRuntimeFsr4 = 3,
		kHostFsr31Fallback = 4
	};

	bool runtimeUpscalerFailureLatched = false;
	bool runtimeFsr4FailureLatched = false;
	uint32_t runtimeFallbackResetDispatchesRemaining = 0;
	bool runtimeUpscalerLastFramePathValid = false;
	uint32_t runtimeUpscalerLastFrameIndex = 0;
	RuntimeUpscalerFramePath runtimeUpscalerLastFramePath = RuntimeUpscalerFramePath::kInactive;

	bool runtimeUpscalerSupportCheckKnown = false;
	bool runtimeUpscalerSupportConfirmed = false;
	uint64_t runtimeUpscalerProviderMatchedVersionId = 0;
	std::string runtimeUpscalerProviderMatchedVersionName;

	bool CanUseRuntimeUpscalerPath();
	uint32_t GetPreferredRuntimeUpscalerVersion() const;
	void ResetRuntimeUpscalerTracking(bool a_invalidateProviderCache);
	void LatchRuntimeUpscalerFailure();
	void LatchRuntimeFsr4Failure();
	RuntimeUpscalerFramePath GetRuntimeUpscalerProviderFramePath(uint32_t a_requestedVersion) const;
	void RecordRuntimeUpscalerFramePath(RuntimeUpscalerFramePath a_path);
	bool EnsureRuntimeUpscalerInterop();
	bool EnsureRuntimeCommandContexts();
	RuntimeCommandContext* AcquireRuntimeCommandContext();
	void ResetRuntimeCommandContexts();
	bool WaitForRuntimeD3D12Fence(uint64_t a_value);
	bool EnsureRuntimeUpscalerContexts(uint32_t a_fullRenderWidth, uint32_t a_fullRenderHeight, uint32_t a_fullDisplayWidth, uint32_t a_fullDisplayHeight, uint32_t a_contextCount, uint32_t a_requestedVersion);
	void WaitForRuntimeUpscalerIdle();
	bool PollRuntimeUpscalerTeardownIdle(const char* a_reason);
	bool EnsureRuntimeUpscalerSharedResources(uint32_t a_contextCount, uint32_t a_fullRenderWidth, uint32_t a_fullRenderHeight, uint32_t a_fullDisplayWidth, uint32_t a_fullDisplayHeight,
		const D3D11_TEXTURE2D_DESC& a_colorDesc,
		const D3D11_TEXTURE2D_DESC& a_depthDesc,
		const D3D11_TEXTURE2D_DESC& a_motionDesc,
		const D3D11_TEXTURE2D_DESC& a_reactiveDesc,
		const D3D11_TEXTURE2D_DESC& a_transparencyDesc,
		const D3D11_TEXTURE2D_DESC& a_outputDesc);
	bool DispatchRuntimeUpscalerSingle(uint32_t a_contextIndex, ID3D11Resource* a_color, ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
		ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_output,
		uint32_t a_renderWidth, uint32_t a_renderHeight, uint32_t a_displayWidth, uint32_t a_displayHeight,
		float a_motionVectorScaleX, float a_motionVectorScaleY, float a_sharpness);
	void DestroyRuntimeUpscalerContexts(bool a_waitForIdle = true);
	void DestroyRuntimeUpscalerResources(bool a_waitForIdle = true);
};
