#pragma once
#include "ColorPolicy.h"
#include <limits>

namespace NeuralRendering::Color
{
	// Float codes are also written by ColorExposureCS. UnitFallback reproduces
	// ISHDR's zero-component branch; it is not a measured unit exposure.
	enum class ExposureValidity : std::uint32_t { Invalid, Ratio, UnitFallback };
	struct ExposureValue
	{
		float average = 0, target = 0, ratio = 1;
		ExposureValidity validity = ExposureValidity::Invalid;
	};
	[[nodiscard]] inline ExposureValue EvaluateHDRExposure(float average, float target) noexcept
	{
		ExposureValue value{ average, target };
		if (!Finite(average) || !Finite(target)) return value;
		if (average == 0 || target == 0) {
			value.validity = ExposureValidity::UnitFallback;
			return value;
		}
		value.ratio = target / average;
		if (Finite(value.ratio) && value.ratio >= 1.0f / 256.0f && value.ratio <= 256.0f)
			value.validity = ExposureValidity::Ratio;
		return value;
	}
	[[nodiscard]] inline bool ResolveExposureProfile(const Profile& requested, const ExposureValue& captured, Profile& effective) noexcept
	{
		if (!Valid(requested)) return false;
		effective = requested;
		if (requested.exposureSource == ExposureSource::Manual) return true;
		// Reject a zero/uninitialized adaptation texture rather than describing
		// its unit fallback as measured exposure. The shader makes the same test.
		if (captured.validity != ExposureValidity::Ratio) return false;
		effective.exposureSource = ExposureSource::Manual;
		effective.exposureMultiplier *= captured.ratio;
		return Valid(effective);
	}
	struct ExposureStamp
	{
		std::uint32_t frame = std::numeric_limits<std::uint32_t>::max();
		std::uint64_t epoch = 0, sequence = 0;
		bool ambiguous = false;
	};
	[[nodiscard]] constexpr bool MatchesExposure(const ExposureStamp& stamp, std::uint32_t sourceWorldFrame, std::uint64_t epoch) noexcept
	{
		return stamp.sequence != 0 && !stamp.ambiguous && stamp.epoch == epoch &&
		       stamp.frame == sourceWorldFrame && sourceWorldFrame != std::numeric_limits<std::uint32_t>::max();
	}
}
