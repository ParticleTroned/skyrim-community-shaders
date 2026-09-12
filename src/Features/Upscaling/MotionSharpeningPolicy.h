#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace MotionSharpening
{
	inline constexpr float kMaximumRCASGain = 1.15457f;
	inline constexpr float kMaximumLumaGain = 2.5f;
	inline constexpr float kLumaDetailLimit = 0.75f;
	struct Settings
	{
		bool enabled = false;
		float adjustment = -0.5f;
		float thresholdPixels = 2.0f;
		float strengthCap = 1.0f;
	};

	inline bool AreValidValues(double adjustment, double thresholdPixels, double strengthCap) noexcept
	{
		return std::isfinite(adjustment) && adjustment >= -1.0 && adjustment <= 1.0 &&
		       std::isfinite(thresholdPixels) && thresholdPixels >= 0.0 && thresholdPixels <= 64.0 &&
		       std::isfinite(strengthCap) && strengthCap >= 0.0 && strengthCap <= 1.0;
	}

	inline bool IsValid(const Settings& value) noexcept
	{
		return AreValidValues(value.adjustment, value.thresholdPixels, value.strengthCap);
	}

	/** Rejects the entire live request before narrowing JSON numbers to shader floats. */
	inline bool TryCreateSettings(bool enabled, double adjustment, double thresholdPixels, double strengthCap, Settings& output) noexcept
	{
		if (!AreValidValues(adjustment, thresholdPixels, strengthCap))
			return false;
		output = { enabled, static_cast<float>(adjustment), static_cast<float>(thresholdPixels), static_cast<float>(strengthCap) };
		return true;
	}

	/** Loaded numbers are bounded before narrowing; strengths use the DLSS slider scale. */
	inline Settings Sanitize(bool enabled, double adjustment, double thresholdPixels, double strengthCap) noexcept
	{
		const Settings defaults{};
		return { enabled,
			std::isfinite(adjustment) ? static_cast<float>(std::clamp(adjustment, -1.0, 1.0)) : defaults.adjustment,
			std::isfinite(thresholdPixels) ? static_cast<float>(std::clamp(thresholdPixels, 0.0, 64.0)) : defaults.thresholdPixels,
			std::isfinite(strengthCap) ? static_cast<float>(std::clamp(strengthCap, 0.0, 1.0)) : defaults.strengthCap };
	}

	inline Settings Sanitize(Settings value) noexcept
	{
		return Sanitize(value.enabled, value.adjustment, value.thresholdPixels, value.strengthCap);
	}

	struct Rect
	{
		uint32_t x = 0;
		uint32_t y = 0;
		uint32_t width = 0;
		uint32_t height = 0;
	};

	/** Maps one final image region to raw, unjittered motion in a single engine eye. */
	struct Region
	{
		Rect output;
		Rect source;
		Rect sourceEye;
	};

	constexpr bool Contains(const Rect& outer, const Rect& inner) noexcept
	{
		return inner.width != 0 && inner.height != 0 &&
		       inner.x >= outer.x && inner.y >= outer.y &&
		       inner.x - outer.x <= outer.width && inner.y - outer.y <= outer.height &&
		       inner.width <= outer.width - (inner.x - outer.x) &&
		       inner.height <= outer.height - (inner.y - outer.y);
	}

	constexpr bool IsValid(const Region& region, uint32_t colorWidth, uint32_t colorHeight,
		uint32_t motionWidth, uint32_t motionHeight) noexcept
	{
		return Contains({ 0, 0, colorWidth, colorHeight }, region.output) &&
		       Contains({ 0, 0, motionWidth, motionHeight }, region.sourceEye) &&
		       Contains(region.sourceEye, region.source);
	}

	/** UV motion is normalized to the full engine eye, even for a cropped output. */
	inline float MotionToOutputPixels(uint32_t fullEyeExtent, uint32_t sourceExtent, uint32_t outputExtent) noexcept
	{
		return sourceExtent != 0 ? static_cast<float>(outputExtent) * fullEyeExtent / sourceExtent : 0.0f;
	}
}
