#include "Features/ScreenshotFramingPolicy.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>

namespace
{
	using namespace CSX::ScreenshotPolicy;

	struct SizeCase
	{
		double frameTangentWidth;
		double pixelsPerHeadTangent;
		std::size_t eyeWidth;
		std::size_t eyeHeight;
		FramedSize expected;
		const char* expectation;
	};

	constexpr double kNotANumber = std::numeric_limits<double>::quiet_NaN();
	constexpr double kInfinity = std::numeric_limits<double>::infinity();
	constexpr SizeCase kSizeCases[] = {
		{ 2.0, 2000, 4000, 4000, { 4000, 2250 }, "equal frame and eye widths retain native dimensions" },
		{ 2.4, 2000, 4000, 4000, { 4800, 2700 }, "a wider combined frame retains the central source density" },
		{ 2.0005, 2000, 4000, 4000, { 4016, 2259 }, "partial aspect steps round up to retain density" },
		{ 1.0, 2000, 4000, 4000, { 4000, 2250 }, "coverage fitting never shrinks the existing native crop" },
		{ 2.0, 1000, 2000, 2000, { 3840, 2160 }, "small submissions retain the existing 4K minimum" },
		{ 4.0, 2000, 4000, 4000, { 5328, 2997 }, "growth is bounded by the dominant eye's pixel count" },
		{ 100, 8192, 16384, 16384, { 16384, 9216 }, "large frames remain inside the dimension limit" },
		{ 2.0, 1000, 16, 9, { 3840, 2160 }, "the smallest valid source retains the 4K minimum" },
		{ 2.0, 2000, 0, 4000, {}, "empty sources fail closed" },
		{ 2.0, 2000, 4000, 8, {}, "sources shorter than an aspect step fail closed" },
		{ 2.0, 2000, 16385, 4000, {}, "oversized sources fail closed" },
		{ 2.0, 2000, 4000, std::numeric_limits<std::size_t>::max(), {}, "source bounds precede multiplication" },
		{ 0, 2000, 4000, 4000, {}, "empty frames fail closed" },
		{ -1, 2000, 4000, 4000, {}, "negative frames fail closed" },
		{ kNotANumber, 2000, 4000, 4000, {}, "undefined frames fail closed" },
		{ kInfinity, 2000, 4000, 4000, {}, "unbounded frames fail closed" },
		{ 2.0, 0, 4000, 4000, {}, "zero density fails closed" },
		{ 2.0, -1, 4000, 4000, {}, "negative density fails closed" },
		{ 2.0, kNotANumber, 4000, 4000, {}, "undefined density fails closed" },
		{ 2.0, kInfinity, 4000, 4000, {}, "unbounded density fails closed" },
	};

	constexpr const char* FindFramingPolicyFailure()
	{
		for (const auto& test : kSizeCases) {
			const auto size = ResolveFramedSourceDensitySize(test.frameTangentWidth, test.pixelsPerHeadTangent, test.eyeWidth, test.eyeHeight);
			if (test.expected.width == 0 ? size.has_value() :
										   !size || size->width != test.expected.width || size->height != test.expected.height)
				return test.expectation;
		}
		return nullptr;
	}

	static_assert(FindFramingPolicyFailure() == nullptr);
}

int main()
{
	if (const auto* failure = FindFramingPolicyFailure()) {
		std::cerr << "framing policy violated: " << failure << '\n';
		return 1;
	}
	if (ResolveFramedSourceDensitySize(std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), 4000, 4000)) {
		std::cerr << "overflowed projected density accepted\n";
		return 1;
	}
	return 0;
}
