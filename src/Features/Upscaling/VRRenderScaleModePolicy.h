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
