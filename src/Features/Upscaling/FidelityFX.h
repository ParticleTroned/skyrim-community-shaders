#pragma once

#include <winrt/base.h>

#include <FidelityFX/host/backends/dx11/ffx_dx11.h>
#include <FidelityFX/host/ffx_fsr3.h>
#include <FidelityFX/host/ffx_interface.h>

#include "../../Buffer.h"
#include "../../State.h"

/**
 * @brief AMD FidelityFX Super Resolution 3 upscaler, running on the D3D11 device.
 *
 * Under the DXVK integration this dispatches entirely through the FFX DX11 backend
 * (`ffx_dx11`), i.e. on `globals::d3d::device`, which DXVK translates onto its single
 * Vulkan device. There is no separate DX12 device and no DXGI shared-handle interop —
 * the upscaler runs as pure D3D11 work on the proxy device. Frame generation (which
 * previously required a DX12 swap chain) is handled separately on the DXVK Vulkan device.
 */
class FidelityFX
{
public:
	static constexpr const wchar_t* PluginDir = L"Data\\Shaders\\Upscaling\\FidelityFX";

	FfxFsr3Context fsrContext[2];

	/** @brief Creates FSR3 upscaling resources including the scratch buffer and context. */
	void CreateFSRResources();

	/** @brief Destroys FSR3 upscaling resources and frees the scratch buffer. */
	void DestroyFSRResources();

	/**
	 * @brief Dispatches FSR3 upscaling for the current frame.
	 * @param a_upscalingTexture The input color texture to upscale (also receives the output).
	 * @param a_reactiveMask Reactive mask identifying areas needing temporal stability.
	 * @param a_transparencyCompositionMask Mask for transparency and composition handling.
	 * @param a_motionVectors Per-pixel motion vectors for temporal reprojection.
	 * @param a_sharpness RCAS sharpening strength applied after upscaling.
	 */
	void Upscale(ID3D11Resource* a_upscalingTexture, ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_motionVectors, float a_sharpness);

private:
	// FSR scratch buffer — allocated in CreateFSRResources, freed in DestroyFSRResources.
	void* fsrScratchBuffer = nullptr;

	// Prevents spamming the log if an FSR3 dispatch faults (e.g. under RenderDoc capture).
	bool fsrDispatchCrashLogged = false;
};
