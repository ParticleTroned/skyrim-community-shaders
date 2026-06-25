#pragma once

#include <cstdint>
#include <d3d11.h>

// NVIDIA Streamline (DLSS / Reflex / DLSS-G) on DXVK's single Vulkan device.
//
// Streamline is driven through its VULKAN path (renderAPI eVulkan + manual
// hooking): DXVK's own VkInstance / VkPhysicalDevice / VkDevice / VkQueue are
// handed to SL via slSetVulkanInfo, so there is no second device and — unlike the
// classic Streamline integration — no d3d11.dll/dxgi.dll interposer shim in the
// game root (DXVK already owns those base names). sl.interposer.dll and the
// sl.*.dll plugins are loaded explicitly, by absolute path, from the Community
// Shaders subfolder (<plugin>/CommunityShaders/streamline) — never the game root.
//
// DLSS, Reflex and DLSS-G are NVIDIA-only. On non-NVIDIA hardware (e.g. this AMD
// machine) the feature is detected as incompatible and never initialized: the
// interposer is not even loaded, slInit is not called, and the upscaler stays on
// the FSR3-on-DXVK path. The NVIDIA dispatch paths are a best-effort scaffold and
// cannot be verified on non-NVIDIA hardware.
//
// SL types are intentionally kept out of this header (sl.h pulls in a large
// surface); all Streamline state lives in Streamline.cpp behind an opaque impl.

class Streamline
{
public:
	static Streamline* GetSingleton();

	/**
		 * @brief Loads sl.interposer.dll from the CS folder and runs slInit on the
		 *        Vulkan backend (manual hooking). Idempotent.
		 * @return true if Streamline initialized. Returns false WITHOUT loading any
		 *         DLL on non-NVIDIA hardware, or if the SL plugin DLLs are absent.
		 */
	bool Initialize();

	/**
		 * @brief Hands DXVK's Vulkan device to Streamline (slSetVulkanInfo) and probes
		 *        which features are supported on the active adapter (slIsFeatureSupported).
		 *        Must be called after the D3D11/DXVK device exists and DxvkInterop is up.
		 */
	void SetVulkanDevice();

	/** @brief slShutdown + frees the interposer. Safe to call when not initialized. */
	void Shutdown();

	/** @brief Whether Streamline initialized (implies NVIDIA + plugins present). */
	[[nodiscard]] bool IsInitialized() const { return initialized; }

	/** @brief Whether DLSS super-resolution is supported on this adapter. */
	[[nodiscard]] bool IsDLSSSupported() const { return featureDLSS; }

	/** @brief Whether Reflex low-latency is supported on this adapter. */
	[[nodiscard]] bool IsReflexSupported() const { return featureReflex; }

	/** @brief Whether DLSS-G frame generation is supported on this adapter. */
	[[nodiscard]] bool IsDLSSGSupported() const { return featureDLSSG; }

	/**
		 * @brief DLSS super-resolution dispatch over DXVK's VkDevice (best-effort,
		 *        NVIDIA-only). Tags the color/depth/motion VkImages (via DxvkInterop)
		 *        and runs slEvaluateFeature(kFeatureDLSS). No-op unless IsDLSSSupported().
		 */
	void EvaluateDLSS(ID3D11Resource* a_colorIn, ID3D11Resource* a_colorOut,
		ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
		uint32_t a_renderWidth, uint32_t a_renderHeight,
		uint32_t a_outputWidth, uint32_t a_outputHeight,
		uint32_t a_qualityMode, float a_sharpness);

	/**
		 * @brief Per-frame Reflex low-latency sleep (best-effort, NVIDIA-only).
		 *        No-op unless IsReflexSupported().
		 */
	void UpdateReflex(bool a_enable, bool a_boost);

private:
	Streamline() = default;

	bool triedInit = false;    ///< Initialize() ran (success or not) — gates re-entry.
	bool initialized = false;  ///< slInit succeeded and the interposer is loaded.
	bool vulkanDeviceSet = false;
	bool featureDLSS = false;
	bool featureReflex = false;
	bool featureDLSSG = false;
};
