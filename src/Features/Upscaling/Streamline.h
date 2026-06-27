#pragma once

#include <cstdint>
#include <d3d11.h>

// NVIDIA Streamline (DLSS / Reflex / DLSS-G) + community plugins (FSR / XeSS) on
// DXVK's Vulkan device via full interposition (sl.interposer.dll IS DXVK's vulkan-1.dll).
//
// SL types are intentionally kept out of this header (sl.h pulls in a large surface);
// all Streamline state lives in Streamline.cpp behind an opaque impl.

class Streamline
{
public:
	static Streamline* GetSingleton();

	/** @brief Map sl.interposer.dll before DXVK creates its VkInstance, so DXVK's loader
	 *  aliases it and routes its entire Vulkan surface through Streamline. Call from
	 *  Upscaling::Load (plugin load), before any DXGI call. Cheap + idempotent. */
	void PreloadInterposer();

	/** @brief Runs slInit on the Vulkan backend. Idempotent.
	 *  @return true if Streamline initialized. */
	bool Initialize();

	/** @brief Probes per-adapter feature support and resolves feature-specific entry points.
	 *  Must be called after the D3D11/DXVK device exists and DxvkInterop is up. */
	void SetVulkanDevice();

	/** @brief slShutdown + frees the interposer. Safe to call when not initialized. */
	void Shutdown();

	[[nodiscard]] bool IsInitialized() const { return initialized; }
	[[nodiscard]] bool IsDLSSSupported() const { return featureDLSS; }
	[[nodiscard]] bool IsReflexSupported() const { return featureReflex; }
	[[nodiscard]] bool IsDLSSGSupported() const { return featureDLSSG; }
	[[nodiscard]] bool IsXeSSSupported() const { return featureXeSS; }
	[[nodiscard]] bool IsFSRSupported() const { return featureFSR; }

	void EvaluateDLSS(ID3D11Resource* a_colorIn, ID3D11Resource* a_colorOut,
		ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
		uint32_t a_renderWidth, uint32_t a_renderHeight,
		uint32_t a_outputWidth, uint32_t a_outputHeight,
		uint32_t a_qualityMode, float a_sharpness,
		float a_jitterX, float a_jitterY);

	void EvaluateXeSS(ID3D11Resource* a_colorIn, ID3D11Resource* a_colorOut,
		ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
		uint32_t a_renderWidth, uint32_t a_renderHeight,
		uint32_t a_outputWidth, uint32_t a_outputHeight,
		uint32_t a_qualityMode, float a_sharpness,
		float a_jitterX, float a_jitterY);

	void EvaluateFSR(ID3D11Resource* a_colorIn, ID3D11Resource* a_colorOut,
		ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
		uint32_t a_renderWidth, uint32_t a_renderHeight,
		uint32_t a_outputWidth, uint32_t a_outputHeight,
		uint32_t a_qualityMode, float a_sharpness,
		float a_jitterX, float a_jitterY);

	[[nodiscard]] bool SetFSRFrameGen(bool a_enable, uint32_t a_renderWidth, uint32_t a_renderHeight,
		uint32_t a_displayWidth, uint32_t a_displayHeight, bool a_hdr);

	[[nodiscard]] bool IsFSRFrameGenActive() const;

	void UpdateReflex(bool a_enable, bool a_boost);

	enum class PclMarker : uint32_t
	{
		SimulationStart = 0,
		SimulationEnd = 1,
		RenderSubmitStart = 2,
		RenderSubmitEnd = 3,
		PresentStart = 4,
		PresentEnd = 5,
	};

	void SetPCLMarker(PclMarker a_marker);

	void SetDLSSGMode(bool a_enable, uint32_t a_renderWidth, uint32_t a_renderHeight,
		uint32_t a_displayWidth, uint32_t a_displayHeight, uint32_t a_numFramesToGenerate = 1);

	[[nodiscard]] bool GetDLSSGState(uint64_t& a_vramUsage, uint32_t& a_maxFrames) const;

	void LogDLSSGFrameStats();

	void TagDLSSGResources(ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
		ID3D11Resource* a_hudlessColor, uint32_t a_renderWidth, uint32_t a_renderHeight);

	void TagFSRFGHudless(ID3D11Resource* a_hudlessColor, uint32_t a_renderWidth, uint32_t a_renderHeight);

	void ClearDLSSGTags();
	void EnsureDLSSGPresentTag();

	void SetConstants(const float4x4& a_clipToPrevClip, const float4x4& a_prevClipToClip,
		float2 a_jitter, float2 a_mvecScale, float a_cameraNear, float a_cameraFar,
		float a_cameraFOV, float a_cameraAspect, bool a_depthInverted, bool a_reset);

	// Register the DXVK frame-gen ownership predicate so DXVK treats all swapchains as
	// externally paced under interposition (skips present-fence, present-wait worker).
	static void RegisterDxvkOwnershipPredicate();

private:
	Streamline() = default;

	bool triedInit = false;
	bool initialized = false;
	bool vulkanDeviceSet = false;

	bool featureDLSS = false;
	bool featureReflex = false;
	bool featureDLSSG = false;
	bool featureXeSS = false;
	bool featureFSR = false;

	bool isNvidiaGPU = false;
	bool isRTXBelow40Series = false;
};
