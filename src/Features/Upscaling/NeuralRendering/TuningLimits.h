#pragma once

#include "NetworkSelection.h"
#include "Utils/Finite.h"

#include <cstdint>

namespace NeuralRendering
{
	/** Inclusive image-tuning maxima for the effective NR network. */
	struct TuningLimits
	{
		float intensity = 2.0f;
		float localToneStrength = 2.0f;
		float localStructureStrength = 2.0f;
		float skinStructureStrength = 2.0f;

		/** Normalize persisted settings and presets before they reach the provider. */
		template <class Settings>
		void Sanitize(Settings& settings) const noexcept
		{
			settings.neuralRenderingIntensity = Util::ClampFinite(settings.neuralRenderingIntensity, 0.0f, intensity, 0.8f);
			settings.neuralRenderingLocalTone = Util::ClampFinite(settings.neuralRenderingLocalTone, 0.0f, localToneStrength, 0.75f);
			settings.neuralRenderingLocalStructure = Util::ClampFinite(settings.neuralRenderingLocalStructure, 0.0f, localStructureStrength, 0.9f);
			settings.neuralRenderingSkinStructure = Util::ClampFinite(settings.neuralRenderingSkinStructure, 0.0f, skinStructureStrength, 0.9f);
		}
	};

	/** Default 310.9 uses newer-network ranges; 310.8 retains legacy ranges, with unknown versions unrestricted. */
	[[nodiscard]] constexpr TuningLimits GetTuningLimitsForVersion(std::uint32_t major, std::uint32_t minor) noexcept
	{
		if (major == 310 && minor == 9)
			return { 1.0f, 2.0f, 1.0f, 1.0f };
		if (major == 310 && minor == 8)
			return { 1.0f, 2.0f, 2.0f, 2.0f };
		return {};
	}

	/** Legacy retains the 310.8 range; default and newer retain the provider's existing range. */
	[[nodiscard]] constexpr TuningLimits GetTuningLimitsForNetwork(TuningLimits runtimeDefaults, std::uint32_t effectivePreset) noexcept
	{
		return effectivePreset == kNetworkLegacy ? GetTuningLimitsForVersion(310, 8) : runtimeDefaults;
	}
}
