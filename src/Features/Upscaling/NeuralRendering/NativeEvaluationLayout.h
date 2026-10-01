#pragma once

#include "../DLSSViewportCrop.h"
#include "ComputeSubrect.h"
#include "PipelinePolicy.h"

#include <array>
#include <cmath>
#include <initializer_list>
#include <string_view>

namespace NeuralRendering
{
	/** A valid resource-local rectangle inside physical storage, not an occupancy mask. */
	struct NativeImageRegion
	{
		UpscalingDLSS::Extent backing{};
		ComputeSubrect valid{};

		bool operator==(const NativeImageRegion&) const = default;
	};

	/** Immutable dimensions supplied when creating the native feature. */
	struct NativeCreationExtents
	{
		/** Guide dimensions supplied as DLSSNR.InputWidth/InputHeight. */
		UpscalingDLSS::Extent input{};
		/** Dimensions supplied as Width/Height and the native output creation keys. */
		UpscalingDLSS::Extent output{};

		bool operator==(const NativeCreationExtents&) const = default;
	};

	/** Geometry only; the enclosing execution retains source, color and history identity. */
	struct NativeEvaluationLayout
	{
		NativeCreationExtents creation{};
		NativeImageRegion color{}, depth{}, motion{}, output{}, controlMask{};
		/** Full-input normalized vectors to native guide-pixel displacement; applied once by NGX. */
		std::array<float, 2> motionVectorScale{};
		bool featureUpscaling = false;

		bool operator==(const NativeEvaluationLayout&) const = default;
	};

	/** Preserves the full-coordinate adapter's outward mapping and creation capacity. */
	[[nodiscard]] inline constexpr NativeEvaluationLayout BuildNativeEvaluationLayout(
		UpscalingDLSS::Extent a_color, UpscalingDLSS::Extent a_guides,
		UpscalingDLSS::Extent a_output, UpscalingDLSS::Extent a_controlMask,
		const ComputeSubrect& a_outputRect, UpscalingDLSS::MotionVectorScale a_motionScale,
		bool a_featureUpscaling) noexcept
	{
		const auto map = [&](UpscalingDLSS::Extent a_extent) {
			return NativeImageRegion{ a_extent, MapComputeSubrect(a_outputRect,
													a_output.width, a_output.height, a_extent.width, a_extent.height) };
		};
		const auto guides = map(a_guides);
		return {
			.creation = { a_guides, a_output },
			.color = map(a_color),
			.depth = guides,
			.motion = guides,
			.output = { a_output, a_outputRect },
			.controlMask = map(a_controlMask),
			.motionVectorScale = a_motionScale.valid ? std::array{ a_motionScale.x, a_motionScale.y } : std::array{ 0.0f, 0.0f },
			.featureUpscaling = a_featureUpscaling,
		};
	}

	/** Rejects unqualified layout/phase changes without changing the admitted baseline. */
	[[nodiscard]] inline std::string_view GetNativeEvaluationLayoutViolation(
		const NativeEvaluationLayout& a_layout, bool a_hasControlMask) noexcept
	{
		for (const auto* image : { &a_layout.color, &a_layout.depth, &a_layout.motion, &a_layout.output }) {
			if (!image->valid.Fits(image->backing.width, image->backing.height))
				return "native valid rectangle exceeds its backing extent";
		}
		if (a_layout.creation.input != a_layout.depth.backing ||
			a_layout.creation.output != a_layout.output.backing)
			return "native creation extents must retain the full-coordinate capacity";
		if (a_layout.motion != a_layout.depth)
			return "native depth and motion grids differ";
		const auto expectedUpscaling = ResolveFeatureUpscaling(
			a_layout.creation.input.width, a_layout.creation.input.height,
			a_layout.creation.output.width, a_layout.creation.output.height);
		if (!expectedUpscaling || *expectedUpscaling != a_layout.featureUpscaling)
			return "native creation scale does not match feature upscaling";
		for (const auto* image : { &a_layout.color, &a_layout.depth }) {
			if (image->valid != MapComputeSubrect(a_layout.output.valid,
									a_layout.output.backing.width, a_layout.output.backing.height,
									image->backing.width, image->backing.height))
				return "native valid rectangle does not preserve the outward guide/color phase";
		}
		if (a_hasControlMask ? a_layout.controlMask != a_layout.output : a_layout.controlMask != NativeImageRegion{})
			return "native control-mask presence or rectangle differs from output";
		for (const float scale : a_layout.motionVectorScale) {
			if (!std::isfinite(scale) || scale <= 0.0f)
				return "native motion conversion must be finite and positive";
		}
		return {};
	}
}
