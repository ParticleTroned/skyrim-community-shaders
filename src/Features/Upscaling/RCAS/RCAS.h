#pragma once

#include "../../../Buffer.h"
#include "../MotionSharpeningPolicy.h"

#include <atomic>
#include <d3d11_4.h>
#include <memory>
#include <span>
#include <winrt/base.h>

/**
 * @brief Robust Contrast Adaptive Sharpening (RCAS) implementation.
 *
 * Standalone sharpening pass based on AMD FidelityFX FSR1 RCAS algorithm.
 * Used to apply sharpening to DLSS output in HDR space before tonemapping.
 */
class RCAS
{
public:
	RCAS() = default;
	~RCAS();

	/**
	 * @brief Initializes RCAS resources including compute shader and constant buffer.
	 *
	 * Safe to call multiple times - will early-out if already initialized.
	 */
	void Initialize(bool motionAdaptive = false);
	void ClearShaderCache();

	/**
	 * @brief Applies RCAS sharpening to the input texture.
	 *
	 * @param inputTexture SRV of the texture to sharpen (typically kMAIN render target).
	 * @param outputUAV UAV to write sharpened result to.
	 * @param sharpness Sharpening strength (0.0 = no sharpening, higher = more sharp).
	 * @return true when the RCAS dispatch is submitted; false when required resources are unavailable.
	 */
	bool ApplySharpen(ID3D11ShaderResourceView* inputTexture, ID3D11UnorderedAccessView* outputUAV, float sharpness);

	/** Applies bounded motion adjustment, retaining fixed RCAS when motion is unavailable or invalid. */
	bool ApplyMotionAdaptiveSharpen(ID3D11ShaderResourceView* inputTexture, ID3D11UnorderedAccessView* outputUAV,
		float sharpness, float baseStrength, const MotionSharpening::Settings& settings,
		ID3D11ShaderResourceView* motionVectors, std::span<const MotionSharpening::Region> regions);

	/** Reports the last attempted RCAS dispatch; applicability is determined by the active upscaler. */
	const char* GetMotionAdaptiveStatus() const noexcept;

private:
	enum class MotionStatus : uint8_t
	{
		NotDispatched,
		Disabled,
		MotionUnavailable,
		InvalidGeometry,
		ShaderUnavailable,
		Applied,
		DispatchFailed
	};
	std::atomic<MotionStatus> motionStatus{ MotionStatus::NotDispatched };
	void CreateComputeShader();
	bool EnsureMotionAdaptiveResources();

	winrt::com_ptr<ID3D11ComputeShader> rcasComputeShader;
	winrt::com_ptr<ID3D11ComputeShader> motionAdaptiveComputeShader;
	std::unique_ptr<ConstantBuffer> motionAdaptiveConfigCB;
	bool motionAdaptiveShaderFailed = false;
	ConstantBuffer* rcasConfigCB = nullptr;
};
