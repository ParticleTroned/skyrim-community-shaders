#pragma once

#include <winrt/base.h>

// The FSR3 frame-generation entry points (ffxFsr3DispatchFrameGeneration,
// ffxFsr3ContextDispatchFrameGenerationPrepare) are gated in ffx_fsr3.h behind
// FFX_OF && FFX_IF (optical flow + frame interpolation). The SDK static libs are
// built with both enabled by default, so the symbols exist; expose the
// declarations to CS by defining these before including the header.
#ifndef FFX_OF
#	define FFX_OF
#endif
#ifndef FFX_IF
#	define FFX_IF
#endif

#include <FidelityFX/host/backends/vk/ffx_vk.h>
#include <FidelityFX/host/ffx_fsr3.h>
#include <FidelityFX/host/ffx_interface.h>

#include "../../Buffer.h"
#include "../../State.h"

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
class FidelityFX
{
public:
	static constexpr const wchar_t* PluginDir = L"Data\\Shaders\\Upscaling\\FidelityFX";

	FfxFsr3Context fsrContext[2];

	// The FFX device handle wrapping DXVK's VkDevice.
	FfxDevice fsrFfxDevice = nullptr;

	// Whether the live FSR3 context was created with frame interpolation enabled.
	bool frameGenContextActive = false;

	// Monotonic frame id shared between the upscale dispatch (prepares FG inputs)
	// and the frame-generation dispatch (consumes them).
	uint64_t frameID = 0;

	// CS-owned upscale output (display resolution) and interpolated frame.
	Texture2D* upscaledTexture = nullptr;
	Texture2D* interpolatedTexture = nullptr;

	// True for the current frame once DispatchFrameGeneration has produced an
	// interpolated frame into interpolatedTexture; consumed by the present path.
	// Reset at the start of each UpscaleAndGenerate so a skipped frame can't
	// re-present a stale interpolated image.
	bool interpolatedFrameReady = false;

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
	 * @brief Dispatches frame generation, producing one interpolated frame into interpolatedTexture.
	 *        Must be called after Upscale() in the same frame. No-op unless frame-gen is enabled.
	 * @param a_presentColor The final upscaled color for this frame.
	 * @param a_isHDR Whether HDR output is active.
	 * @param a_peakNits HDR peak luminance.
	 * @return true if an interpolated frame was produced.
	 */
	bool DispatchFrameGeneration(ID3D11Resource* a_presentColor, bool a_isHDR, float a_peakNits);

	/**
	 * @brief SEH-guarded upscale (+ optional frame generation) for the per-frame call site.
	 *        Catches any fault in the VK dispatch/submit path, logs the fault address, and
	 *        disables further dispatch so a single fault doesn't CTD or crash-loop.
	 */
	void UpscaleAndGenerate(ID3D11Resource* a_color, ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_motionVectors, float a_sharpness, bool a_frameGeneration, bool a_isHDR, float a_peakNits);

	// Set after a dispatch fault is caught; stops further FSR VK dispatch this session.
	bool dispatchFaulted = false;

private:
	// FSR scratch buffers. Upscaling-only uses [1]; frame generation uses all three
	// (shared resources, upscaling, frame interpolation), per the FFX backend model.
	void* fsrScratchBuffers[3] = { nullptr, nullptr, nullptr };

	// Whether ffxFsr3ContextCreate succeeded (so DestroyFSRResources only tears down a
	// real context — destroying an uncreated one faults).
	bool contextCreated = false;

	// The command buffer in flight for the current frame (begun in Upscale, submitted in
	// Upscale or DispatchFrameGeneration depending on whether frame-gen is enabled).
	VkCommandBuffer currentCommandBuffer = VK_NULL_HANDLE;

	// Prevents spamming the log if a dispatch faults (e.g. under RenderDoc capture).
	bool fsrDispatchCrashLogged = false;
	bool fgDispatchCrashLogged = false;
};
