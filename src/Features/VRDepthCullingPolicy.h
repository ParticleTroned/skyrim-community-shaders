#pragma once

#include <cstdint>

namespace VRDepthCullingPolicy
{
	constexpr double kMinimumCoherentRotationCosine = 0.9999996192282494;  // cos(0.05 degrees)
	constexpr float kMaximumCoherentTranslationSquared = 0.01f;           // 0.1 world units squared

	constexpr bool IsViewCoherent(
		bool a_poseValid,
		double a_rotationCosine,
		float a_translationSquared)
	{
		return a_poseValid &&
		       a_rotationCosine >= kMinimumCoherentRotationCosine &&
		       a_translationSquared <= kMaximumCoherentTranslationSquared;
	}

	constexpr std::uint32_t ResolveVisibility(
		std::uint32_t a_previousResult,
		bool a_viewCoherent)
	{
		return !a_viewCoherent && a_previousResult == 0 ? 1u : a_previousResult;
	}
}
