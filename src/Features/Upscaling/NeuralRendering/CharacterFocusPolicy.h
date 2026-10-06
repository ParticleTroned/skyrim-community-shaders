#pragma once

#include "CharacterRegionPolicy.h"
#include "Features/FoveatedCommon.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace NeuralRendering
{
	/** The existing FOV shape and resolved centres, independent of TAA/rendering mode. */
	struct CharacterFocusMask
	{
		float visibleScale = 1.0f;
		float horizontalScale = 1.0f;
		bool configuredFov = false;
		std::array<std::array<float, 2>, 2> offsets{};
		bool operator==(const CharacterFocusMask&) const = default;
	};

	/** Conservatively retain a face touching the focus area in either eye. */
	inline float CharacterFocusDistance(const CharacterRect& rect, std::uint32_t width,
		std::uint32_t height, float scale, const CharacterFocusMask& mask, std::uint32_t eye)
	{
		if (!width || !height || eye >= mask.offsets.size())
			return 0.0f;
		if (!rect.IsValid())
			return std::numeric_limits<float>::infinity();
		const auto& offset = mask.offsets[eye];
		const float x = std::clamp(std::clamp(0.5f + offset[0], 0.0f, 1.0f),
			float(rect.minX) / width, float(rect.maxX) / width);
		const float y = std::clamp(std::clamp(0.5f + offset[1], 0.0f, 1.0f),
			float(rect.minY) / height, float(rect.maxY) / height);
		return FoveatedCommon::MaskDistanceUV(x, y, mask.visibleScale, mask.horizontalScale, offset[0], offset[1], scale);
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
