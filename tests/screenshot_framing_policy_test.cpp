#include "Features/ScreenshotFramingPolicy.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>

namespace
{
	using namespace CSX::ScreenshotPolicy;

	struct CropCase
	{
		std::size_t sourceWidth;
		std::size_t sourceHeight;
		std::size_t targetWidth;
		std::size_t targetHeight;
		FramedCrop expected;
		const char* expectation;
	};

	struct SizeCase
	{
		double frameTangentWidth;
		double eyeTangentWidth;
		double eyeTangentHeight;
		std::size_t eyeWidth;
		std::size_t eyeHeight;
		FramedSize expected;
		const char* expectation;
	};

	constexpr double kNotANumber = std::numeric_limits<double>::quiet_NaN();
	constexpr double kInfinity = std::numeric_limits<double>::infinity();
	// Without saturation, this side times the maximum width wraps the pixel budget to zero.
	constexpr std::size_t kOverflowingSide = std::size_t{ 1 } << 50;

	constexpr CropCase kCropCases[] = {
		{ 3072, 3216, FramedAspectWidth, FramedAspectHeight, { 0, 744, 3072, 1728 }, "a tall eye keeps its full submitted width" },
		{ 3072, 3216, FramedFixedWidth, FramedFixedHeight, { 0, 744, 3072, 1728 }, "fixed and source-native framing select the same source region" },
		{ 2700, 2900, FramedAspectWidth, FramedAspectHeight, { 6, 694, 2688, 1512 }, "a partial aspect step is trimmed evenly from both sides" },
		{ 4000, 1000, FramedAspectWidth, FramedAspectHeight, { 1112, 0, 1776, 999 }, "a wide source is limited by its height" },
		{ 16, 9, FramedAspectWidth, FramedAspectHeight, { 0, 0, 16, 9 }, "a single aspect step is a valid crop" },
		{ 15, 9, FramedAspectWidth, FramedAspectHeight, {}, "a source narrower than one aspect step has no crop" },
		{ 16, 8, FramedAspectWidth, FramedAspectHeight, {}, "a source shorter than one aspect step has no crop" },
		{ 0, 9, FramedAspectWidth, FramedAspectHeight, {}, "an empty source has no crop" },
		{ 16, 9, 0, FramedAspectHeight, {}, "a target without a width has no crop" },
		{ 16, 9, FramedAspectWidth, 0, {}, "a target without a height has no crop" },
	};

	// A 2000 x 2000 eye spanning two tangent units holds 1000 pixels per unit,
	// and 166 steps is the largest 16:9 size within its pixel count.
	constexpr SizeCase kSizeCases[] = {
		{ 2.0, 2.0, 2.0, 2000, 2000, { 2000, 1125 }, "a frame as wide as the eye keeps the eye's width" },
		{ 2.4, 2.0, 2.0, 2000, 2000, { 2400, 1350 }, "a frame wider than one eye keeps that eye's pixel density" },
		{ 1.0, 2.0, 4.0, 2000, 1000, { 1008, 567 }, "a horizontally denser eye sets the output density" },
		{ 1.0, 4.0, 2.0, 1000, 2000, { 1008, 567 }, "a vertically denser eye sets the output density" },
		{ 2.007, 2.0, 2.0, 2000, 2000, { 2000, 1125 }, "the output rounds down to the nearest aspect step" },
		{ 2.009, 2.0, 2.0, 2000, 2000, { 2016, 1134 }, "the output rounds up to the nearest aspect step" },
		{ 0.001, 2.0, 2.0, 2000, 2000, { 16, 9 }, "a narrow frame still produces one aspect step" },
		{ 4.0, 2.0, 2.0, 2000, 2000, { 2656, 1494 }, "the output never exceeds the eye's pixel count" },
		{ 100.0, 2.0, 2.0, 16384, 16384, { MaximumFramedWidth, 9216 }, "the output never exceeds the maximum framed width" },
		{ 100.0, 2.0, 2.0, kOverflowingSide, kOverflowingSide, { MaximumFramedWidth, 9216 }, "an implausible eye size saturates at the maximum framed width" },
		{ 2.0, 2.0, 2.0, 16, 9, { 16, 9 }, "an eye of one aspect step bounds the output to that step" },
		{ 2.0, 2.0, 2.0, 8, 8, {}, "an eye smaller than one aspect step has no size" },
		{ 2.0, 2.0, 2.0, 0, 2000, {}, "an eye without a width has no size" },
		{ 2.0, 2.0, 2.0, 2000, 0, {}, "an eye without a height has no size" },
		{ 0.0, 2.0, 2.0, 2000, 2000, {}, "an empty frame has no size" },
		{ -1.0, 2.0, 2.0, 2000, 2000, {}, "a negative frame has no size" },
		{ kNotANumber, 2.0, 2.0, 2000, 2000, {}, "an undefined frame has no size" },
		{ kInfinity, 2.0, 2.0, 2000, 2000, {}, "an unbounded frame has no size" },
		{ 2.0, 0.0, 2.0, 2000, 2000, {}, "an empty horizontal eye extent has no size" },
		{ 2.0, kNotANumber, 2.0, 2000, 2000, {}, "an undefined horizontal eye extent has no size" },
		{ 2.0, kInfinity, 2.0, 2000, 2000, {}, "an unbounded horizontal eye extent has no size" },
		{ 2.0, 2.0, -1.0, 2000, 2000, {}, "a negative vertical eye extent has no size" },
		{ 2.0, 2.0, kNotANumber, 2000, 2000, {}, "an undefined vertical eye extent has no size" },
		{ 2.0, 2.0, kInfinity, 2000, 2000, {}, "an unbounded vertical eye extent has no size" },
	};

	constexpr bool Holds(const CropCase& a_case)
	{
		const auto crop = ResolveFramedCrop(a_case.sourceWidth, a_case.sourceHeight, a_case.targetWidth, a_case.targetHeight);
		return crop.x == a_case.expected.x && crop.y == a_case.expected.y &&
		       crop.width == a_case.expected.width && crop.height == a_case.expected.height;
	}

	constexpr bool Holds(const SizeCase& a_case)
	{
		const auto size = ResolveFramedSourceDensitySize(
			a_case.frameTangentWidth,
			a_case.eyeTangentWidth,
			a_case.eyeTangentHeight,
			a_case.eyeWidth,
			a_case.eyeHeight);
		return size.width == a_case.expected.width && size.height == a_case.expected.height;
	}

	/** Returns the first violated expectation, or nullptr when the policy holds. */
	constexpr const char* FindFramingPolicyFailure()
	{
		for (const auto& cropCase : kCropCases) {
			if (!Holds(cropCase))
				return cropCase.expectation;
		}
		for (const auto& sizeCase : kSizeCases) {
			if (!Holds(sizeCase))
				return sizeCase.expectation;
		}
		return nullptr;
	}

	// Constant evaluation applies the same cases wherever the tests are built.
	static_assert(FramedAspectWidth == 16 && FramedAspectHeight == 9);
	static_assert(FindFramingPolicyFailure() == nullptr);
}

int main()
{
	const char* failure = FindFramingPolicyFailure();
	if (failure != nullptr) {
		std::cerr << "framing policy violated: " << failure << '\n';
		return 1;
	}
	return 0;
}
