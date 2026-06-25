#pragma once

#include <winrt/base.h>

#include <vulkan/vulkan.h>  // before vk/ffx_api_vk.h; provides VkDevice/VkImage/etc.

#include <ffx_api/ffx_api.h>              // ffxContext, ffxFunctions typedefs, FfxApiReturnCodes
#include <ffx_api/ffx_api_loader.h>       // ffxFunctions + ffxLoadFunctions(module)
#include <ffx_api/ffx_api_types.h>        // FfxApiResource, state/usage/format/transfer enums
#include <ffx_api/ffx_upscale.h>          // ffxCreateContextDescUpscale / ffxDispatchDescUpscale
#include <ffx_api/ffx_framegeneration.h>  // FG create/configure/prepare/dispatch descs
#include <ffx_api/vk/ffx_api_vk.h>        // ffxCreateBackendVKDesc + ffxApiGet*VK helpers

#include "../../Buffer.h"
#include "../../State.h"
#include "FrameGenProvider.h"

/**
 * @brief AMD FidelityFX Super Resolution 3 (upscaling + frame generation) on DXVK's Vulkan device.
 *
 * Drives the FFX **Vulkan** backend (ffx_vk) on DXVK's OWN VkDevice — obtained via
 * the DxvkInterop bridge (IDXGIVkInteropDevice). CS's D3D11 textures are handed to FFX
 * as the VkImages that back them (IDXGIVkInteropSurface), recorded into a command buffer
 * on DXVK's queue family and submitted on DXVK's queue under its submission lock. There is
 * no DX12 device, no DX11 FFX backend, and no DXGI shared-handle interop — everything runs
 * on the single DXVK Vulkan device.
 *
 * Frame generation uses the dispatch-only path (ffxFsr3DispatchFrameGeneration → a CS-owned
 * VkImage); presentation of the generated frame is handled by the present hook (the FFX
 * frame-gen swapchain is DX12/VK-WSI-owning and incompatible with DXVK owning the swapchain).
 */
class FidelityFX : public SIE::IFrameGenProvider
{
public:
	static constexpr const wchar_t* PluginDir = L"Data\\Shaders\\Upscaling\\FidelityFX";

	// --- IFrameGenProvider: the proper FFX VK frame-generation SWAPCHAIN path -----------------
	// When frame generation is enabled, DXVK's swapchain is wrapped by the FFX FG swapchain (via
	// DxvkWsiHook), which interpolates + paces presentation internally. The legacy dispatch-only
	// double-present is used only when the swapchain is NOT wrapped.
	const char* Name() const override { return "FFX FSR3"; }
	bool WantsToWrap() const override;
	SIE::FrameGenDeviceRequirements GetDeviceRequirements(VkPhysicalDevice physicalDevice) override;
	void OnDeviceCreated(VkPhysicalDevice physicalDevice, VkDevice device, const SIE::FrameGenQueues& queues) override;
	VkResult CreateSwapchain(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo,
		const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain) override;
	VkResult GetSwapchainImages(VkDevice device, VkSwapchainKHR swapchain, uint32_t* pCount, VkImage* pImages) override;
	VkResult AcquireNextImage(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout,
		VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex) override;
	VkResult QueuePresent(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) override;
	void DestroySwapchain(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator) override;

	// Per-frame frame-generation state for the wrapped (FG swapchain) present path: enable
	// interpolation only on frames an upscale actually prepared (gameplay), and disable it at
	// menus / loading screens (no prepare) so the FG swapchain passes the frame through 1:1
	// instead of blocking on absent interpolation inputs. Call once per present while wrapped.
	// a_hudlessColor is the scene-without-UI image (back-buffer format); when interpolating it is
	// handed to FFX as HUDLessColor so the UI region isn't ghosted on generated frames. Pass null
	// to skip UI suppression.
	void SetFrameGenForPresent(bool a_enabled, ID3D11Resource* a_hudlessColor);

	// The FFX frame-generation SWAPCHAIN context (distinct from fgContext, the interpolation
	// context). Owns the real VkSwapchainKHR + the present/interpolation pacing threads.
	ffxContext fgSwapchainContext = nullptr;
	// The WRAPPED VkSwapchainKHR handle FFX returns from the swapchain create. This — NOT the
	// ffx-api context handle — is what ffxConfigureDescFrameGeneration.swapChain must point to:
	// FFX reinterpret_casts it straight to FrameInterpolationSwapChainVK* and calls into it.
	VkSwapchainKHR fgWrappedSwapchain = VK_NULL_HANDLE;
	// Replacement WSI functions FFX hands back for the wrapped swapchain.
	ffxQueryDescSwapchainReplacementFunctionsVK fgSwapchainFns{};
	// Queues claimed for the FG swapchain (game = DXVK graphics; present/acquire = injected).
	SIE::FrameGenQueues fgQueues;

	// FFX-API contexts created against DXVK's VkDevice. The DLL owns its own backend
	// scratch memory, so there are no scratch buffers and no host-side backend interface.
	ffxContext upscaleContext = nullptr;  // ffxCreateContextDescUpscale + chained backend-VK
	ffxContext fgContext = nullptr;       // ffxCreateContextDescFrameGeneration + chained backend-VK

	// The 5 FFX-API entry points resolved from amd_fidelityfx_vk.dll (CS dxvk subfolder).
	ffxFunctions ffxApi{};
	HMODULE ffxModule = nullptr;

	// Whether the live FSR3 context was created with frame interpolation enabled.
	bool frameGenContextActive = false;

	// Monotonic frame id shared between the upscale dispatch (prepares FG inputs)
	// and the frame-generation dispatch (consumes them).
	uint64_t frameID = 0;

	// CS-owned upscale output and the FFX frame-interpolation output (interpolatedTexture).
	// interpolatedTexture is in the SWAPCHAIN format so it copies straight to the back buffer.
	Texture2D* upscaledTexture = nullptr;
	Texture2D* interpolatedTexture = nullptr;

	// A snapshot of the real, fully-composited back buffer captured at present time. It is the
	// presentColor fed to frame interpolation and the source used to restore the real frame to
	// the back buffer after the interpolated frame is presented. Swapchain format.
	Texture2D* fgPresentColor = nullptr;

	// The scene WITHOUT the UI (swapchain format), handed to FFX as HUDLessColor so the UI region
	// isn't interpolated/ghosted. Frame generation owns this resource (it is FG state, lifecycle-
	// locked to the FG context); HDRDisplay no longer owns it. It is produced each frame while FG is
	// active: HDR off -> CaptureHudlessFromBackBuffer snapshots the pre-UI back buffer; HDR on ->
	// HDRDisplay::ApplyHDR re-runs its composite with no UI into GetHudlessUAV().
	Texture2D* hudlessColor = nullptr;

	/** @brief HUDLessColor render target for the HDR-on producer; null unless the FG context is
	 *         live (so a producer can never dispatch into a freed/absent UAV). */
	ID3D11UnorderedAccessView* GetHudlessUAV() const { return (frameGenContextActive && hudlessColor) ? hudlessColor->uav.get() : nullptr; }

	/** @brief HUD-less scene resource for the present path (HUDLessColor source); null when absent. */
	ID3D11Resource* GetHudlessResource() const { return hudlessColor ? hudlessColor->resource.get() : nullptr; }

	/** @brief HDR-off producer: snapshots the pre-UI back buffer into hudlessColor. Null-safe; call
	 *         from the pre-UI hook only while frame generation is active. */
	void CaptureHudlessFromBackBuffer();

	/**
	 * @brief Creates FSR3 resources (and, when requested, frame-generation) on DXVK's VkDevice.
	 * @param a_frameGeneration When true, also runs optical flow + frame interpolation.
	 */
	void CreateFSRResources(bool a_frameGeneration);

	/** @brief Destroys FSR3 resources, scratch buffers and CS-owned textures. */
	void DestroyFSRResources();

	/**
	 * @brief Dispatches FSR3 upscaling for the current frame on DXVK's Vulkan device.
	 * @param a_upscalingTexture The input color texture (the upscaled result is copied back into it).
	 * @param a_reactiveMask Reactive mask.
	 * @param a_transparencyCompositionMask Transparency/composition mask.
	 * @param a_motionVectors Per-pixel motion vectors.
	 * @param a_sharpness RCAS sharpening strength.
	 */
	void Upscale(ID3D11Resource* a_upscalingTexture, ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_motionVectors, float a_sharpness);

	/**
	 * @brief SEH-guarded FSR3 upscale (+ frame-gen prepare) for the per-frame upscale call site.
	 *        Frame interpolation itself is dispatched later, at present time, on the final back
	 *        buffer (see GenerateInterpolatedFrame) — this is independent of HDR Display.
	 */
	void UpscaleAndGenerate(ID3D11Resource* a_color, ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_motionVectors, float a_sharpness);

	/**
	 * @brief Captures the composited back buffer and dispatches frame interpolation from it.
	 *        Called at present time; the interpolated frame lands in interpolatedTexture.
	 * @param a_backBuffer The fully-composited real back buffer (the actual color to be presented).
	 * @param a_hudlessColor Optional scene-without-UI image (swapchain format) so FFX suppresses
	 *        UI interpolation; null when unavailable (e.g. HDR off). The UI is then preserved from
	 *        the back buffer rather than ghosted.
	 * @param a_isHDR Whether the back buffer is PQ-encoded HDR (vs sRGB). The only HDR input.
	 * @param a_peakNits HDR peak luminance for the interpolation transfer function.
	 * @param a_debugFlags FFX_FSR3_FRAME_GENERATION_FLAG_* debug-draw bits (0 for none).
	 * @return true if an interpolated frame was produced.
	 */
	bool GenerateInterpolatedFrame(ID3D11Resource* a_backBuffer, ID3D11Resource* a_hudlessColor, bool a_isHDR, float a_peakNits, uint32_t a_debugFlags);

	/** @brief Copy the interpolated frame into the swap chain's current back buffer. */
	void BlitInterpolatedToBackBuffer(IDXGISwapChain* a_swapChain);

	/** @brief Restore the captured real frame into the swap chain's current back buffer. */
	void BlitRealToBackBuffer(IDXGISwapChain* a_swapChain);

	// Set after a dispatch fault is caught; stops further FSR VK dispatch this session.
	bool dispatchFaulted = false;

	// True once the upscale has recorded this frame's frame-gen prepare descriptions; the
	// present-time interpolation dispatch consumes and clears it, so a present without a
	// matching upscale (menus, loading screens) won't interpolate from stale prepare data.
	bool fgPreparedThisFrame = false;

	/** @brief Debug (CS_FG_ONLY_INTERP): present only the interpolated frame, hiding the real one. */
	bool DebugOnlyInterpolated() const;

private:
	/** @brief Records the frame-interpolation dispatch (presentColor -> interpolatedTexture) on DXVK's VkDevice. */
	bool DispatchFrameGeneration(ID3D11Resource* a_presentColor, ID3D11Resource* a_hudlessColor, bool a_isHDR, float a_peakNits, uint32_t a_debugFlags);

	// Whether both ffxCreateContext calls succeeded (so DestroyFSRResources only tears
	// down a real context — destroying an uncreated one faults).
	bool contextCreated = false;

	// The command buffer in flight for the current frame (begun in Upscale, submitted in
	// Upscale or DispatchFrameGeneration depending on whether frame-gen is enabled).
	VkCommandBuffer currentCommandBuffer = VK_NULL_HANDLE;

	// Prevents spamming the log if a dispatch faults (e.g. under RenderDoc capture).
	bool fsrDispatchCrashLogged = false;
	bool fgDispatchCrashLogged = false;
};
