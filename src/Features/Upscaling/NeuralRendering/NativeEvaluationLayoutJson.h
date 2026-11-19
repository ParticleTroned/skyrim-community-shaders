#pragma once

#include "NativeEvaluationLayout.h"
#include <nlohmann/json.hpp>

namespace NeuralRendering::Evidence
{
	using Json = nlohmann::json;
	inline Json ExtentJson(const UpscalingDLSS::Extent& value)
	{
		return { { "width", value.width }, { "height", value.height } };
	}
	inline Json RectJson(const UpscalingDLSS::Rect& value)
	{
		return { { "left", value.left }, { "top", value.top }, { "right", value.right }, { "bottom", value.bottom } };
	}
	inline Json SubrectJson(const ComputeSubrect& value)
	{
		return { { "x", value.baseX }, { "y", value.baseY }, { "width", value.width }, { "height", value.height } };
	}
	inline Json ViewportJson(const UpscalingDLSS::ViewportCrop& value)
	{
		return { { "fullInput", ExtentJson(value.fullInput) }, { "input", RectJson(value.input) },
			{ "fullOutput", ExtentJson(value.fullOutput) }, { "output", RectJson(value.output) } };
	}
	/** Frozen layout facts do not claim knowledge of the provider's hidden read footprint. */
	inline Json NativeLayoutJson(const NativeEvaluationLayout& value)
	{
		const auto image = [](const NativeImageRegion& region) {
			return Json{ { "backingExtent", ExtentJson(region.backing) }, { "validRect", SubrectJson(region.valid) } };
		};
		return { { "coordinateDomain", "resource_local_texels" },
			{ "creationInputExtent", ExtentJson(value.creation.input) }, { "creationOutputExtent", ExtentJson(value.creation.output) },
			{ "color", image(value.color) }, { "depth", image(value.depth) }, { "motion", image(value.motion) }, { "output", image(value.output) },
			{ "controlMask", value.controlMask.backing.IsValid() ? image(value.controlMask) : Json(nullptr) },
			{ "inputInitializationContract", "valid_rectangles_before_evaluation" }, { "nativeReadableFootprint", nullptr },
			{ "motionSourceUnits", "full_input_normalized" }, { "motionConsumerUnits", "native_guide_pixels" },
			{ "motionConversion", "native_parameter_scale_once" }, { "motionVectorScale", value.motionVectorScale },
			{ "jitterPixels", value.jitterPixels }, { "scalingRatio", value.ScalingRatio() },
			{ "nativeControlMask", value.nativeControlMask },
			{ "featureUpscaling", value.featureUpscaling } };
	}
}
