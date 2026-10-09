#pragma once

#include "Utils/Finite.h"

#include <array>
#include <cmath>

/** Owns one live weather-colour scale between engine colour updates. */
template <class Color>
class WeatherColorAdjustment
{
public:
	/** Removes our last scale only while the same live object still contains our output. */
	void Restore(const void* a_owner, Color& a_color) noexcept
	{
		if (owner == a_owner && owner && a_color == applied)
			a_color = source;
		owner = nullptr;
	}

	/** Applies a bounded scale without accumulating it or replacing a later external edit. */
	void Apply(const void* a_owner, Color& a_color, float a_scale) noexcept
	{
		Restore(a_owner, a_color);
		const float scale = Util::ClampFinite(a_scale, 0.0f, 2.0f, 1.0f);
		if (!a_owner || scale == 1.0f)
			return;

		const auto adjusted = a_color * scale;
		if (!std::isfinite(adjusted[0]) || !std::isfinite(adjusted[1]) || !std::isfinite(adjusted[2]))
			return;
		source = a_color;
		applied = adjusted;
		owner = a_owner;
		a_color = adjusted;
	}

	/** Grades a live sky colour while retaining the engine's source for the next update. */
	void ApplyGrade(const void* a_owner, Color& a_color, float a_scale, float a_curve, const std::array<float, 3>& a_tint) noexcept
	{
		Restore(a_owner, a_color);
		const float scale = Util::ClampFinite(a_scale, 0.0f, 5.0f, 1.0f);
		const float curve = Util::ClampFinite(a_curve, 0.1f, 4.0f, 1.0f);
		if (!a_owner || (scale == 1.0f && curve == 1.0f && a_tint == std::array{ 1.0f, 1.0f, 1.0f }))
			return;
		auto adjusted = a_color;
		for (std::size_t i = 0; i < 3; ++i) {
			const float tint = Util::ClampFinite(a_tint[i], 0.0f, 2.0f, 1.0f);
			if (scale == 0.0f || tint == 0.0f)
				adjusted[i] = 0.0f;
			else {
				if (curve != 1.0f)
					adjusted[i] = std::pow(std::max(adjusted[i], 0.0f), curve);
				adjusted[i] *= scale * tint;
			}
			if (!std::isfinite(adjusted[i]))
				return;
		}
		source = a_color;
		applied = adjusted;
		owner = a_owner;
		a_color = adjusted;
	}

private:
	const void* owner = nullptr;
	Color source{};
	Color applied{};
};
