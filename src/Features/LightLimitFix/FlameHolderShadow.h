#pragma once

#include "Utils/Finite.h"
#include <algorithm>
#include <array>
#include <cstdint>

namespace RE
{
	class BSGeometry;
	class BSShadowLight;
	class NiLight;
}

namespace LightLimitFixDetail::ShadowCulling
{
	/** Light ownership is cached only for one synchronous native shadow render. */
	struct FlameHolderContext
	{
		const RE::NiLight* source = nullptr;
		std::array<float, 3> restOrigin{};
		std::uint32_t ownerHandle = 0;
		bool captured = false;
	};

	inline constexpr double FlameHolderMaxRadius = 20;
	inline constexpr double FlameHolderReachMargin = 4;
	inline constexpr double FlameAboveHolderMax = 24;
	inline constexpr double FlameFlickerMaxOffset = 16;

	/** Large fixtures and invalid bounds retain their shadows. */
	[[nodiscard]] inline bool FlameHolderSizeEligible(double radius, double scale) noexcept
	{
		return Util::IsFinite(radius) && Util::IsFinite(scale) && scale > 0 && radius > 0 && radius <= FlameHolderMaxRadius * scale;
	}

	/** Match a small holder enclosing the flame or directly beneath it. */
	[[nodiscard]] inline bool HoldsFlame(const std::array<double, 3>& delta, double radius, double scale) noexcept
	{
		if (!FlameHolderSizeEligible(radius, scale) || !std::all_of(delta.begin(), delta.end(), [](double value) { return Util::IsFinite(value); }))
			return false;
		const double reach = radius + FlameHolderReachMargin * scale;
		const double horizontalSquared = delta[0] * delta[0] + delta[1] * delta[1];
		return horizontalSquared + delta[2] * delta[2] <= reach * reach ||
		       (delta[2] < 0 && horizontalSquared <= reach * reach && -delta[2] - radius <= FlameAboveHolderMax * scale);
	}

	/** Reject only an identified non-actor light's own holder; uncertain ownership keeps the shadow. */
	[[nodiscard]] bool ShouldSkipFlameHolder(FlameHolderContext& context, RE::BSShadowLight& light, const RE::BSGeometry& geometry) noexcept;
}
