#pragma once

namespace ReflexPolicy
{
	// The current RenderSubmit markers bracket only DLSS evaluation, not a complete
	// game frame. Keep marker-driven pacing unavailable until authoritative
	// full-frame coverage exists.
	inline constexpr bool kHasAuthoritativeFullFrameMarkerCoverage = false;

	struct MarkerOptimizationDecision
	{
		bool available = false;
		bool enabled = false;
	};

	[[nodiscard]] constexpr MarkerOptimizationDecision ResolveMarkerOptimization(
		bool a_reflexAvailable,
		bool a_pclAvailable,
		bool a_requested,
		bool a_hasAuthoritativeFullFrameCoverage) noexcept
	{
		const bool available =
			a_reflexAvailable &&
			a_pclAvailable &&
			a_hasAuthoritativeFullFrameCoverage;
		return {
			.available = available,
			.enabled = available && a_requested,
		};
	}

	[[nodiscard]] constexpr MarkerOptimizationDecision ResolveCSMarkerOptimization(
		bool a_reflexAvailable,
		bool a_pclAvailable,
		bool a_requested) noexcept
	{
		return ResolveMarkerOptimization(
			a_reflexAvailable,
			a_pclAvailable,
			a_requested,
			kHasAuthoritativeFullFrameMarkerCoverage);
	}
}
