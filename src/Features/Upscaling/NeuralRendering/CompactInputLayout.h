#pragma once

#include "NativeEvaluationLayout.h"
#include "RoiDescriptor.h"
#include <algorithm>
#include <array>
#include <optional>

namespace NeuralRendering
{
	struct CompactInputLayout
	{
		ComputeSubrect source{};
		RoiDescriptor roi{};
		NativeEvaluationLayout native{};
	};
	/** Stateless, equal-grid crop: full buckets are initialized and evaluated at unchanged density. */
	[[nodiscard]] inline std::optional<CompactInputLayout> BuildCompactInputLayout(
		const RoiDescriptor& roi, const NativeEvaluationLayout& baseline, std::uint32_t retainedSide = 0) noexcept
	{
		if (baseline.featureUpscaling || baseline.color.backing != baseline.output.backing ||
			baseline.depth.backing != baseline.output.backing || baseline.motion.backing != baseline.output.backing ||
			baseline.controlMask.backing.IsValid() || !roi.samplingSupport ||
			!GetNativeEvaluationLayoutViolation(baseline, false).empty() ||
			roi.allocationCapacity != baseline.output.backing || roi.inferenceContext != baseline.output.valid ||
			!ContainsComputeSubrect(roi.inferenceContext, roi.ownedOutput) || !ContainsComputeSubrect(roi.ownedOutput, *roi.samplingSupport))
			return std::nullopt;
		const auto full = baseline.output.backing;
		constexpr std::array<std::uint32_t, 3> buckets{ 256, 512, 768 };
		if (retainedSide && std::ranges::find(buckets, retainedSide) == buckets.end())
			return std::nullopt;
		for (const auto side : buckets) {
			if (side < retainedSide || roi.inferenceContext.width > side || roi.inferenceContext.height > side || side > full.width || side > full.height ||
				std::uint64_t(side) * side >= std::uint64_t(full.width) * full.height)
				continue;
			const auto centered = [&](std::uint32_t origin, std::uint32_t extent, std::uint32_t limit) {
				const auto margin = (side - extent) / 2u;
				return std::min(origin > margin ? origin - margin : 0u, limit - side);
			};
			CompactInputLayout result;
			result.source = { centered(roi.inferenceContext.baseX, roi.inferenceContext.width, full.width),
				centered(roi.inferenceContext.baseY, roi.inferenceContext.height, full.height), side, side };
			if (!ContainsComputeSubrect(result.source, roi.inferenceContext))
				return std::nullopt;
			const auto local = [&](ComputeSubrect rect) {
				rect.baseX -= result.source.baseX;
				rect.baseY -= result.source.baseY;
				return rect;
			};
			result.roi = roi;
			result.roi.samplingSupport = local(*roi.samplingSupport);
			result.roi.ownedOutput = local(roi.ownedOutput);
			result.roi.inferenceContext = { 0, 0, side, side };
			result.roi.temporalEnvelope.reset();
			result.roi.allocationCapacity = { side, side };
#ifdef DEVBENCH_BRIDGE_ENABLED
			result.roi.currentContextApplied = false;
			result.roi.compactSource = result.source;
#endif
			result.native = BuildNativeEvaluationLayout({ side, side }, { side, side }, { side, side }, {},
				result.roi.inferenceContext, { .valid = true, .x = baseline.motionVectorScale[0], .y = baseline.motionVectorScale[1] }, false);
			return result;
		}
		return std::nullopt;
	}
}
