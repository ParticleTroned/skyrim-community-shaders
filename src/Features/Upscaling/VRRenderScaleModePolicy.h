#pragma once

namespace VRRenderScaleModePolicy
{
	/** Separates durable user intent from the quality-gated physical mode. */
	struct State
	{
		bool preference = false;
		bool enabled = false;

		bool operator==(const State&) const = default;
	};

	/**
	 * Applies the optional link only to method/quality selections (or a
	 * VR FPS Stabilizer profile that omits an explicit Render Scale field).
	 * Explicit full-profile requests must pass their preference directly to
	 * Resolve so that a requested false remains authoritative.
	 */
	[[nodiscard]] constexpr bool ResolveSelectionPreference(
		bool a_methodEligible,
		bool a_linkedToUpscaling,
		bool a_rememberedPreference) noexcept
	{
		return a_methodEligible && (a_linkedToUpscaling || a_rememberedPreference);
	}

	/** Resolves explicit intent; native AA suspends only physical scaling. */
	[[nodiscard]] constexpr State Resolve(
		bool a_methodEligible,
		bool a_qualityEligible,
		bool a_requestedPreference) noexcept
	{
		const bool preference =
			a_methodEligible && a_requestedPreference;
		return {
			.preference = preference,
			.enabled = preference && a_qualityEligible,
		};
	}
}
