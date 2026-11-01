#pragma once

#include "CharacterRegionPolicy.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace NeuralRendering
{
	/** Screen-space focus remains independent of camera FOV and inference scale. */
	struct CharacterFocusMask
	{
		float visibleScale = 1.0f;
		float horizontalScale = 1.0f;
		std::array<float, 2> offset{};
		bool operator==(const CharacterFocusMask&) const = default;
	};
	/** The nearest point on a conservative mono bound owns the whole actor's fade. */
	inline float CharacterFocusDistance(const CharacterRect& rect, std::uint32_t width,
		std::uint32_t height, float scale, const CharacterFocusMask& mask)
	{
		if (!width || !height || !std::isfinite(scale) || scale <= 0.0f)
			return 0.0f;
		if (!rect.IsValid())
			return std::numeric_limits<float>::infinity();
		const float cx = std::clamp(0.5f + mask.offset[0], 0.0f, 1.0f);
		const float cy = std::clamp(0.5f + mask.offset[1], 0.0f, 1.0f);
		const float x = std::clamp(cx, float(rect.minX) / width, float(rect.maxX) / width);
		const float y = std::clamp(cy, float(rect.minY) / height, float(rect.maxY) / height);
		const float dx = std::abs(x - cx) * 2.0f / (scale * std::max(0.1f, mask.horizontalScale));
		const float dy = std::abs(y - cy) * 2.0f / scale;
		return std::pow(dx * dx * dx * dx + dy * dy * dy * dy, 0.25f);
	}

	struct CharacterFocusState
	{
		std::uint32_t frame = 0;
		std::uint32_t lastInsideFrame = 0;
		std::uint32_t fade = 0;
		bool selected = false;
		bool initialized = false;
	};

	/** Fade whole actors, retaining their complete crop until their weight reaches zero. */
	inline std::uint32_t ResolveCharacterFocusFade(std::uint32_t frame, float distance,
		bool enabled, bool uncertain, std::uint32_t holdFrames, CharacterFocusState& state)
	{
		constexpr float exitDistance = 1.08f;
		constexpr std::uint32_t fadeStep = 32u;
		if (!enabled || uncertain || std::isnan(distance)) {
			state = { frame, frame, 0u, true, true };
			return 0u;
		}
		if (state.initialized && state.frame == frame)
			return state.fade;
		if (!state.initialized) {
			const bool inside = distance <= 1.0f;
			state = { frame, frame, inside ? 0u : 255u, inside, true };
			return state.fade;
		}
		const auto elapsed = std::min(frame - state.frame, 8u);
		state.frame = frame;
		if (distance <= (state.selected ? exitDistance : 1.0f)) {
			state.selected = true;
			state.lastInsideFrame = frame;
		} else if (frame - state.lastInsideFrame > std::min(holdFrames, 30u)) {
			state.selected = false;
		}
		const auto step = fadeStep * elapsed;
		state.fade = state.selected ? state.fade - std::min(state.fade, step) :
		                              std::min(255u, state.fade + step);
		return state.fade;
	}
}
