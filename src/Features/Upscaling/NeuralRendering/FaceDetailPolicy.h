#pragma once

#include "PeripheralCompressionPolicy.h"
#include "Utils/Finite.h"

namespace NeuralRendering
{
	/** Map a source rectangle conservatively through the baseline radial compression. */
	inline std::array<float, 4> FaceDetailModelBounds(const PeripheralMapping& mapping, const std::array<float, 4>& bounds)
	{
		PeripheralMapping::Point low{}, high{}, nearest{}, farthest{};
		for (unsigned axis = 0; axis < 2; ++axis) {
			low[axis] = bounds[axis] - mapping.center[axis];
			high[axis] = bounds[axis + 2] - mapping.center[axis];
			nearest[axis] = low[axis] <= 0 && high[axis] >= 0 ? 0 : std::min(std::abs(low[axis]), std::abs(high[axis]));
			farthest[axis] = std::max(std::abs(low[axis]), std::abs(high[axis]));
		}
		const auto scale = [&](float radius) { return radius > mapping.protectedFraction ? mapping.ForwardRadius(radius) / radius : 1.0f; };
		const float nearScale = scale(mapping.Distance(nearest)), farScale = scale(mapping.Distance(farthest));
		std::array<float, 4> result{};
		for (unsigned axis = 0; axis < 2; ++axis) {
			result[axis] = std::clamp((mapping.center[axis] + std::min(low[axis] * nearScale, low[axis] * farScale) - mapping.origin[axis]) / mapping.work[axis], 0.0f, 1.0f);
			result[axis + 2] = std::clamp((mapping.center[axis] + std::max(high[axis] * nearScale, high[axis] * farScale) - mapping.origin[axis]) / mapping.work[axis], 0.0f, 1.0f);
		}
		return result;
	}

	/** Redistribute a fixed grid; footprint admission reduces strength instead of increasing allocation. */
	inline void ApplyFaceDetailProtection(PeripheralCompressionPlan& plan, const FaceDetailRegions& faces, std::uint32_t percent, float minimumDensity)
	{
		if (!plan.Active() || !faces.current || !faces.count || faces.count > faces.bounds.size() || !percent || percent > kMaximumFaceDetailProtectionPercent ||
			!Util::IsFinite(minimumDensity) || minimumDensity >= 1.0f)
			return;
		minimumDensity = std::max(minimumDensity, kFaceDetailMinimumDensity);
		std::array<std::array<float, kFaceDetailBins>, 2> coverage{};
		for (std::uint32_t face = 0; face < faces.count; ++face) {
			const auto& source = faces.bounds[face];
			if (!std::ranges::all_of(source, [](float v) { return Util::IsFinite(v) && v >= 0.0f && v <= 1.0f; }) || source[0] >= source[2] || source[1] >= source[3])
				continue;
			const auto bounds = FaceDetailModelBounds(plan.mapping, source);
			if (bounds[0] >= bounds[2] || bounds[1] >= bounds[3])
				continue;
			for (unsigned axis = 0; axis < 2; ++axis)
				for (unsigned bin = 0; bin < kFaceDetailBins; ++bin) {
					const float overlap = std::max(0.0f, std::min(float(bin + 1), bounds[axis + 2] * kFaceDetailBins) - std::max(float(bin), bounds[axis] * kFaceDetailBins));
					coverage[axis][bin] = std::max(coverage[axis][bin], overlap);
				}
		}
		FaceDetailMapping mapping;
		float lowestDensity = 1.0f;
		bool changed = false;
		for (unsigned axis = 0; axis < 2; ++axis) {
			std::array<float, kFaceDetailBins> smooth{};
			float mean = 0.0f;
			for (unsigned bin = 0; bin < kFaceDetailBins; ++bin) {
				// Continuous coverage and a short spatial kernel avoid quantized head-bound jumps.
				for (int tap = -2; tap <= 2; ++tap)
					smooth[bin] += coverage[axis][std::clamp(int(bin) + tap, 0, int(kFaceDetailBins - 1))] * (3 - std::abs(tap)) / 9.0f;
				mean += smooth[bin] / kFaceDetailBins;
			}
			float gain = percent * 0.01f;
			if (mean > 0 && minimumDensity > 0)
				gain = std::min(gain, std::max(0.0f, (1.0f / minimumDensity - 1.0f) / mean));
			float cumulative = 0;
			for (unsigned bin = 0; bin < kFaceDetailBins; ++bin) {
				mapping.knots[axis * 4 + bin / 4][bin % 4] = cumulative;
				const float density = (1.0f + gain * smooth[bin]) / (1.0f + gain * mean);
				changed |= std::abs(density - 1.0f) > 1e-5f;
				cumulative += density / kFaceDetailBins;
			}
			for (unsigned bin = 0; bin < kFaceDetailBins; ++bin)
				lowestDensity = std::min(lowestDensity, (mapping.Knot(axis, bin + 1) - mapping.Knot(axis, bin)) * kFaceDetailBins);
		}
		if (!changed || !Util::IsFinite(lowestDensity) || lowestDensity <= 0.0f)
			return;
		auto maximumFootprint = plan.maximumFootprint;
		for (auto& footprint : maximumFootprint) {
			footprint /= lowestDensity;
			if (!Util::IsFinite(footprint) || footprint <= 0.0f || footprint > kPeripheralMaximumFootprint)
				return;
		}
		mapping.enabled = 1;
		plan.mapping.faceDetail = mapping;
		plan.maximumFootprint = maximumFootprint;
	}
}
