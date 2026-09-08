#pragma once

#include "CharacterRegionPolicy.h"

#include <array>
#include <span>

namespace NeuralRendering
{
	enum class CharacterProjectionResult
	{
		Visible,
		Offscreen,
		Uncertain,
	};

	/** Projected homogeneous corners; IDs are never interpolated or packed here. */
	struct CharacterClipPoint
	{
		float x = 0.0f;
		float y = 0.0f;
		float w = 0.0f;
	};

	/** Distinguishes proven offscreen bounds from invalid or near-plane bounds. */
	[[nodiscard]] inline CharacterProjectionResult ResolveCharacterProjection(
		std::span<const CharacterClipPoint> a_corners,
		std::uint32_t a_width,
		std::uint32_t a_height,
		CharacterRect& a_rect) noexcept
	{
		a_rect = {};
		const auto uncertain = [&]() {
			a_rect = { 0u, 0u, a_width, a_height };
			return CharacterProjectionResult::Uncertain;
		};
		if (a_corners.empty() || !a_width || !a_height)
			return uncertain();
		float minX = std::numeric_limits<float>::max();
		float minY = std::numeric_limits<float>::max();
		float maxX = std::numeric_limits<float>::lowest();
		float maxY = std::numeric_limits<float>::lowest();
		bool anyInFront = false;
		bool anyNearOrBehind = false;
		for (const auto& clip : a_corners) {
			if (!std::isfinite(clip.x) || !std::isfinite(clip.y) || !std::isfinite(clip.w))
				return uncertain();
			anyInFront = anyInFront || clip.w > 0.0f;
			if (clip.w <= 1.0e-4f) {
				anyNearOrBehind = true;
				continue;
			}
			const float pixelX = (clip.x / clip.w * 0.5f + 0.5f) * a_width;
			const float pixelY = (0.5f - clip.y / clip.w * 0.5f) * a_height;
			if (!std::isfinite(pixelX) || !std::isfinite(pixelY))
				return uncertain();
			minX = std::min(minX, pixelX);
			minY = std::min(minY, pixelY);
			maxX = std::max(maxX, pixelX);
			maxY = std::max(maxY, pixelY);
		}
		if (!anyInFront)
			return CharacterProjectionResult::Offscreen;
		if (anyNearOrBehind)
			return uncertain();
		if (maxX <= 0.0f || maxY <= 0.0f || minX >= a_width || minY >= a_height)
			return CharacterProjectionResult::Offscreen;
		a_rect = {
			static_cast<std::uint32_t>(std::clamp(std::floor(minX), 0.0f, static_cast<float>(a_width))),
			static_cast<std::uint32_t>(std::clamp(std::floor(minY), 0.0f, static_cast<float>(a_height))),
			static_cast<std::uint32_t>(std::clamp(std::ceil(maxX), 0.0f, static_cast<float>(a_width))),
			static_cast<std::uint32_t>(std::clamp(std::ceil(maxY), 0.0f, static_cast<float>(a_height))),
		};
		return a_rect.IsValid() ? CharacterProjectionResult::Visible : uncertain();
	}

	struct CharacterActorAdmissionState
	{
		std::uint32_t lastSizeEligibleFrame = 0;
		bool sizeEligible = false;
		bool adaptiveSelected = false;
	};

	/** One actor-wide decision, made before any of its categories are authored. */
	[[nodiscard]] inline bool ResolveCharacterActorAdmission(
		std::uint32_t a_frame,
		std::uint32_t a_facePixelSize,
		float a_distanceMeters,
		bool a_projectionUncertain,
		std::uint32_t a_minimumFaceSize,
		std::uint32_t a_holdFrames,
		bool a_adaptive,
		CharacterActorAdmissionState& a_state) noexcept
	{
		if (a_projectionUncertain) {
			// Missing bounds may cost extra work, but cannot erase valid materials.
			a_state.sizeEligible = true;
			a_state.lastSizeEligibleFrame = a_frame;
			a_state.adaptiveSelected = true;
			return true;
		}
		const auto threshold = a_state.sizeEligible ?
		                           CharacterRegionPolicy::ResolveFaceSizeExitThreshold(a_minimumFaceSize) :
		                           a_minimumFaceSize;
		if (a_facePixelSize >= threshold) {
			a_state.sizeEligible = true;
			a_state.lastSizeEligibleFrame = a_frame;
		} else if (!a_state.sizeEligible ||
				   static_cast<std::uint32_t>(a_frame - a_state.lastSizeEligibleFrame) > a_holdFrames) {
			a_state.sizeEligible = false;
		}
		a_state.adaptiveSelected = a_state.sizeEligible &&
		                           (!a_adaptive || CharacterRegionPolicy::IsDetailRelevantWithHysteresis(
													   a_distanceMeters, a_facePixelSize, a_state.adaptiveSelected));
		return a_state.sizeEligible && a_state.adaptiveSelected;
	}
}
