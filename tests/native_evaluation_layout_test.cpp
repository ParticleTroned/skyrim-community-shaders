#include "Features/Upscaling/NeuralRendering/NativeEvaluationLayout.h"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace
{
	using namespace NeuralRendering;
	void Require(bool value)
	{
		if (!value)
			throw std::runtime_error("native evaluation layout contract failed");
	}
}

int main()
{
	constexpr UpscalingDLSS::ViewportCrop crop{
		{ 99, 73 }, { 11, 7, 78, 56 }, { 197, 145 }, { 23, 15, 158, 114 }
	};
	constexpr auto motionScale = UpscalingDLSS::BuildMotionVectorPixelScale(crop);
	constexpr auto layout = BuildNativeEvaluationLayout(
		{ 135, 99 }, { 67, 49 }, { 135, 99 }, {}, { 3, 5, 113, 71 }, motionScale, true);
	static_assert(layout.creation == NativeCreationExtents{ { 67, 49 }, { 135, 99 } });
	static_assert(layout.output.valid == ComputeSubrect{ 3, 5, 113, 71 });
	static_assert(layout.depth.valid == ComputeSubrect{ 1, 2, 57, 36 });
	static_assert(layout.depth == layout.motion);
	static_assert(layout.motionVectorScale == std::array{ 99.0f, 73.0f });
	Require(GetNativeEvaluationLayoutViolation(layout, false).empty());

	// Cropping does not rescale normalized full-input displacements or add its origin twice.
	Require(std::abs((1.0f / 99.0f) * layout.motionVectorScale[0] - 1.0f) < 1e-6f);
	Require(std::abs((-2.0f / 73.0f) * layout.motionVectorScale[1] + 2.0f) < 1e-6f);
	const auto moved = BuildNativeEvaluationLayout({ 135, 99 }, { 67, 49 }, { 135, 99 }, {},
		{ 17, 11, 47, 39 }, motionScale, true);
	Require(GetNativeEvaluationLayoutViolation(moved, false).empty());
	Require(moved.creation == layout.creation && moved.motionVectorScale == layout.motionVectorScale);
	Require(moved.depth.valid == ComputeSubrect{ 8, 5, 24, 20 });
	Require(moved.color.valid != layout.color.valid);

	const auto rejects = [&](const auto& change, bool mask = false) {
		auto invalid = layout;
		change(invalid);
		Require(!GetNativeEvaluationLayoutViolation(invalid, mask).empty());
	};
	rejects([](auto& value) { value.creation.input.width = 0; });
	rejects([](auto& value) { value.creation.output.width = 113; });
	rejects([](auto& value) { value.output.valid.baseX = std::numeric_limits<unsigned>::max(); });
	rejects([](auto& value) { value.output.valid.width = std::numeric_limits<unsigned>::max(); });
	rejects([](auto& value) { value.color.valid.width = 0; });
	rejects([](auto& value) { value.color.valid.baseX++; });
	rejects([](auto& value) { value.depth.valid.baseY++; value.motion = value.depth; });
	rejects([](auto& value) { value.motion.backing.width++; });
	rejects([](auto& value) { value.motion.valid.baseX++; });
	rejects([](auto& value) { value.featureUpscaling = false; });
	rejects([](auto& value) { value.motionVectorScale[0] = 0; });
	rejects([](auto& value) { value.motionVectorScale[1] = std::numeric_limits<float>::quiet_NaN(); });
	rejects([](auto& value) { value.motionVectorScale[0] = std::numeric_limits<float>::infinity(); });
	rejects([](auto& value) { value.controlMask.backing.height = 99; });
	rejects([](auto&) {}, true);
	auto masked = layout;
	masked.controlMask = masked.output;
	Require(GetNativeEvaluationLayoutViolation(masked, true).empty());
	Require(!GetNativeEvaluationLayoutViolation(masked, false).empty());
	masked.controlMask.valid.width--;
	Require(!GetNativeEvaluationLayoutViolation(masked, true).empty());
	Require(!GetNativeEvaluationLayoutViolation({}, false).empty());

	// A/C native geometry and late upscaled guides share the same adapter on either eye.
	for (bool upscaling : { false, true }) {
		for (const auto rect : { ComputeSubrect{ 0, 0, 135, 99 }, ComputeSubrect{ 13, 7, 31, 19 } }) {
			const auto guides = upscaling ? UpscalingDLSS::Extent{ 67, 49 } : UpscalingDLSS::Extent{ 135, 99 };
			const auto current = BuildNativeEvaluationLayout({ 135, 99 }, guides, { 135, 99 }, {}, rect, motionScale, upscaling);
			Require(GetNativeEvaluationLayoutViolation(current, false).empty());
			Require(current.output.valid == rect && current.creation.output == UpscalingDLSS::Extent{ 135, 99 });
		}
	}
	// Resource-local phase remains independent of the full-eye crop's translation.
	auto translated = crop;
	translated.input.left++;
	translated.input.right++;
	translated.output.left += 2;
	translated.output.right += 2;
	Require(UpscalingDLSS::BuildMotionVectorPixelScale(translated).x == motionScale.x);
	const auto history = UpscalingDLSS::MakeSuccessfulCropHistory(10, 4, crop);
	Require(UpscalingDLSS::EvaluateCropContinuity(history, 11, 4, translated).reset);
	Require(!UpscalingDLSS::BuildMotionVectorPixelScale({}).valid);
	return 0;
}
