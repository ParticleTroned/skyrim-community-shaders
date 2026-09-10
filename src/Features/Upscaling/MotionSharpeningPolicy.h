#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace MotionSharpening
{
	inline constexpr float kMaximumRCASGain = 1.15457f;
	struct Settings
	{
		bool enabled = false;
		float adjustment = -0.5f;
		float thresholdPixels = 2.0f;
		float strengthCap = 1.0f;
	};

	inline bool IsValid(const Settings& value) noexcept
	{
		return std::isfinite(value.adjustment) && value.adjustment >= -1.0f && value.adjustment <= 1.0f &&
		       std::isfinite(value.thresholdPixels) && value.thresholdPixels >= 0.0f && value.thresholdPixels <= 64.0f &&
		       std::isfinite(value.strengthCap) && value.strengthCap >= 0.0f && value.strengthCap <= 1.0f;
	}

	/** Settings use the same normalized strength scale as the DLSS sharpness slider. */
	inline Settings Sanitize(Settings value) noexcept
	{
		const Settings defaults{};
		value.adjustment = std::isfinite(value.adjustment) ? std::clamp(value.adjustment, -1.0f, 1.0f) : defaults.adjustment;
		value.thresholdPixels = std::isfinite(value.thresholdPixels) ? std::clamp(value.thresholdPixels, 0.0f, 64.0f) : defaults.thresholdPixels;
		value.strengthCap = std::isfinite(value.strengthCap) ? std::clamp(value.strengthCap, 0.0f, 1.0f) : defaults.strengthCap;
		return value;
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
