#pragma once

#include "MotionSharpeningPolicy.h"
#include "SharpenerDispatch.h"

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <atomic>
#endif
#include <functional>
#include <memory>
#include <span>

namespace UpscalingSharpener
{
	/** Owns motion mapping, optional shader resources and fallback for either DLSS sharpener. */
	class MotionAdaptiveSharpening
	{
	public:
		explicit MotionAdaptiveSharpening(Pass a_pass) : pass(a_pass) {}

		/** Preloads the optional shader; failure retains fixed sharpening until the cache is cleared. */
		void Initialize();
		void ClearShaderCache();

		/** Checks optional resources or latched failure; the caller must also check its fixed fallback. */
		bool CanApplyWithoutResourceCreation() const noexcept
		{
			return motionAdaptiveShaderFailed ||
			       (motionAdaptiveComputeShader && motionAdaptiveConfigCB && motionAdaptiveConfigCB->CB());
		}

		/** Applies bounded per-eye motion adjustment or invokes the caller's fixed-strength pass. */
		bool Apply(ID3D11ShaderResourceView* inputSRV, ID3D11UnorderedAccessView* outputUAV,
			float sharpness, float baseStrength, const MotionSharpening::Settings& settings,
			ID3D11ShaderResourceView* motionVectors, std::span<const MotionSharpening::Region> regions,
			const std::function<bool()>& fallback);

#ifdef DEVBENCH_BRIDGE_ENABLED
		/** Reports the last dispatch attempt for this sharpener, independently of current applicability. */
		const char* GetStatus() const noexcept;
#endif

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
		bool EnsureResources();
		const Pass pass;
#ifdef DEVBENCH_BRIDGE_ENABLED
		std::atomic<MotionStatus> motionStatus{ MotionStatus::NotDispatched };
#endif
		winrt::com_ptr<ID3D11ComputeShader> motionAdaptiveComputeShader;
		std::unique_ptr<ConstantBuffer> motionAdaptiveConfigCB;
		bool motionAdaptiveShaderFailed = false;
	};
}
