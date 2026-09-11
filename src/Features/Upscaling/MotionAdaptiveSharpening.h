#pragma once

#include "MotionSharpeningPolicy.h"
#include "SharpenerDispatch.h"

#include <atomic>
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

		/** Applies bounded per-eye motion adjustment or invokes the caller's fixed-strength pass. */
		bool Apply(ID3D11ShaderResourceView* inputSRV, ID3D11UnorderedAccessView* outputUAV,
			float sharpness, float baseStrength, const MotionSharpening::Settings& settings,
			ID3D11ShaderResourceView* motionVectors, std::span<const MotionSharpening::Region> regions,
			const std::function<bool()>& fallback);

		/** Reports the last dispatch attempt for this sharpener, independently of current applicability. */
		const char* GetStatus() const noexcept;

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
		std::atomic<MotionStatus> motionStatus{ MotionStatus::NotDispatched };
		winrt::com_ptr<ID3D11ComputeShader> motionAdaptiveComputeShader;
		std::unique_ptr<ConstantBuffer> motionAdaptiveConfigCB;
		bool motionAdaptiveShaderFailed = false;
	};
}
