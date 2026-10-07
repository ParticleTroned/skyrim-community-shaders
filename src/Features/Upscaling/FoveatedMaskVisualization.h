#pragma once

#include "Features/FoveatedCommon.h"

namespace FoveatedMaskVisualization
{
	struct Coverage
	{
		float totalPercent = 0.0f;
		float savedPercent = 0.0f;
	};

	/** Estimates mask area in one rendered eye, including feather support and clipping. */
	[[nodiscard]] inline Coverage MeasureCoverage(float centerScale, float feather, float horizontalScale,
		float offsetX, float offsetY, bool peripheryTaa, float outerScale)
	{
		centerScale = FoveatedCommon::ClampCenterScale(centerScale);
		if (!FoveatedCommon::IsActiveCoverage(centerScale))
			return { 100.0f, 0.0f };
		horizontalScale = FoveatedCommon::ClampCenterHorizontalScale(horizontalScale);
		feather = std::isfinite(feather) ? std::clamp(feather, 1e-4f, 0.1f) : FoveatedCommon::kCenterFeather;
		const float centerX = std::isfinite(offsetX) ? std::clamp(0.5f + offsetX, 0.0f, 1.0f) : 0.5f;
		const float centerY = std::isfinite(offsetY) ? std::clamp(0.5f + offsetY, 0.0f, 1.0f) : 0.5f;
		const float supportScale = centerScale + 2.0f * feather;
		outerScale = std::isfinite(outerScale) ? outerScale : 1.0f;
		// Both zones share a centre and shape, so their union uses the larger support.
		const float visibleScale = peripheryTaa ? std::max(supportScale, std::min(std::max(outerScale, supportScale), 1.0f)) : supportScale;
		const auto areaPercent = [&](float scale) {
			const float radiusX = scale * horizontalScale * 0.5f;
			const float radiusY = scale * 0.5f;
			const float left = std::max(0.0f, centerX - radiusX);
			const float right = std::min(1.0f, centerX + radiusX);
			constexpr uint32_t kAreaSamples = 512;
			const float step = (right - left) / kAreaSamples;
			float area = 0.0f;
			for (uint32_t i = 0; i < kAreaSamples; ++i) {
				const float x = std::abs((left + (static_cast<float>(i) + 0.5f) * step - centerX) / radiusX);
				const float halfHeight = radiusY * std::pow(std::max(0.0f, 1.0f - std::pow(x, FoveatedCommon::kMaskShapePower)), 1.0f / FoveatedCommon::kMaskShapePower);
				area += std::max(0.0f, std::min(1.0f, centerY + halfHeight) - std::max(0.0f, centerY - halfHeight));
			}
			return std::clamp(area * step * 100.0f, 0.0f, 100.0f);
		};
		const float totalPercent = areaPercent(visibleScale);
		return { totalPercent, 100.0f - totalPercent };
	}
}
