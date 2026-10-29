#pragma once

#include "Utils/Finite.h"

#include <cstdint>

namespace NeuralRendering
{
	/** Inclusive image-tuning maxima for the detected NR provider. */
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

	/** Match reported provider ranges for 310.8/310.9 without restricting unknown versions. */
	[[nodiscard]] constexpr TuningLimits GetTuningLimitsForVersion(std::uint32_t major, std::uint32_t minor) noexcept
	{
		if (major == 310 && minor == 9)
			return { 1.0f, 2.0f, 1.0f, 1.0f };
		if (major == 310 && minor == 8)
			return { 1.0f, 2.0f, 2.0f, 2.0f };
		return {};
	}
}
