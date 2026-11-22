#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace LightLimitFixDetail
{
	/** Resolved colour inputs only; readiness and unrelated FeatureData are excluded. */
	struct PointLightColourInputs
	{
		bool linearLighting = false;
		bool utilityEnabled = false;
		bool transformEnabled = false;
		std::array<float, 21> values{};
		static constexpr std::size_t UtilityValueOffset = 9;

		/** An enabled utility with neutral controls needs no additional GPU work. */
		bool HasUtilityTransform() const
		{
			if (utilityEnabled) {
				for (std::size_t i = UtilityValueOffset; i < values.size(); i += 2) {
					if (values[i] != 1.0f || values[i + 1] != 0.0f)
						return true;
				}
			}
			return false;
		}

		/** Scalar gains stay inline; only active nonlinear colour operations need preparation. */
		static constexpr bool HasNonlinearTransform(bool linear, float gamma, float curve, float saturation, bool utility)
		{
			return (linear && gamma != 1.0f) || curve != 1.0f || saturation != 1.0f || utility;
		}

		bool operator==(const PointLightColourInputs& other) const
		{
			if (linearLighting != other.linearLighting || utilityEnabled != other.utilityEnabled || transformEnabled != other.transformEnabled)
				return false;
			// Signed zero multipliers must retain the same FP32 result after publication.
			for (std::size_t i = 0; i < values.size(); ++i) {
				if (std::bit_cast<std::uint32_t>(values[i]) != std::bit_cast<std::uint32_t>(other.values[i]))
					return false;
			}
			return true;
		}

		template <class Balance, class LinearLighting>
		static PointLightColourInputs FromSettings(const Balance& balance, const LinearLighting& lighting)
		{
			const auto& appearance = balance.appearance;
			PointLightColourInputs inputs{
				lighting.enableLinearLighting != 0, appearance.pointLightColorEnabled != 0, false,
				{ lighting.lightGamma, balance.pointLightCurve, balance.pointLightSaturation,
					balance.pointLightMult, balance.linearPointLightMult,
					balance.spotlightMult, balance.linearSpotlightMult,
					balance.omnidirectionalBulbMult, balance.linearOmnidirectionalBulbMult,
					appearance.nonlinearPointLightSaturation, appearance.pointLightGammaOffset,
					appearance.linearPointLightSaturation, appearance.linearPointLightGammaOffset,
					appearance.spotlightSaturation, appearance.spotlightGammaOffset,
					appearance.linearSpotlightSaturation, appearance.linearSpotlightGammaOffset,
					appearance.omnidirectionalBulbSaturation, appearance.omnidirectionalBulbGammaOffset,
					appearance.linearOmnidirectionalBulbSaturation, appearance.linearOmnidirectionalBulbGammaOffset }
			};
			inputs.transformEnabled = HasNonlinearTransform(inputs.linearLighting, lighting.lightGamma,
				balance.pointLightCurve, balance.pointLightSaturation, inputs.HasUtilityTransform());
			return inputs;
		}
	};

	/** Render-thread validity for the current upload, resolved settings and resources. */
	class PreparedPointLightColours
	{
	public:
		static constexpr std::uint32_t ClusteredLights = 1u;
		static constexpr std::uint32_t PreparedColours = 2u;

		void PublishInputs(const PointLightColourInputs& inputs)
		{
			if (!inputsKnown || !(currentInputs == inputs))
				Invalidate();
			currentInputs = inputs;
			inputsKnown = true;
		}

		void Invalidate() { ready = false; }
		// Scalar-only profiles retain the inline path without an additional SRV read.
		bool CanPrepare() const { return inputsKnown && currentInputs.transformEnabled; }
		void Complete(bool produced) { ready = CanPrepare() && produced; }
		bool IsReady() const { return ready; }
		std::uint32_t Flags(bool clusteredLights) const
		{
			return clusteredLights ? ClusteredLights | (ready ? PreparedColours : 0u) : 0u;
		}

	private:
		PointLightColourInputs currentInputs{};
		bool inputsKnown = false;
		bool ready = false;
	};
}
