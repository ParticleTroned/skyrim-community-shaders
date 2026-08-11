#pragma once

namespace ReflexPolicy
{
	inline constexpr bool kHasAuthoritativeFullFrameMarkerCoverage = false;

	struct MarkerOptimizationDecision
	{
		bool available = false;
		bool enabled = false;
	};

	/// Resolve whether marker-driven Reflex scheduling may be exposed and used.
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

	/// Resolve CSX's current marker policy from the canonical coverage state.
	[[nodiscard]] constexpr MarkerOptimizationDecision ResolveCSXMarkerOptimization(
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
