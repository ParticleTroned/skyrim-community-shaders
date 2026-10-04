#include "Features/VRHybridCullingPolicy.h"

#include <array>
#include <cstdio>
#include <limits>
#include <utility>

namespace
{
	using namespace VRHybridCullingPolicy;

	bool CoversPaddedEyeEdges()
	{
		const std::array<EyeRect, 2> eyes{ EyeRect{ 0, 0, 2017, 2031 }, EyeRect{ 2017, 0, 2017, 2031 } };
		PyramidLayout layout{};
		BuildConstants constants{};
		if (!TryMakeBuildConstants(eyes, 4034, 2031, 4, constants, layout) ||
			layout.width != 512 || layout.height != 512 || layout.mipCount != 9 ||
			constants.eyes[1].x != 2017 || constants.outputWidth != layout.width ||
			constants.outputHeight != layout.height || constants.sourceReduction != 4)
			return false;

		// Every source pixel has an ancestor at every mip, including odd edges.
		for (std::uint32_t mip = 0; mip < layout.mipCount; ++mip) {
			const auto mipWidth = std::max(layout.width >> mip, 1u);
			const auto mipHeight = std::max(layout.height >> mip, 1u);
			for (const auto& eye : eyes) {
				if (((eye.width - 1) / layout.sourceReduction >> mip) >= mipWidth ||
					((eye.height - 1) / layout.sourceReduction >> mip) >= mipHeight)
					return false;
			}
		}
		return true;
	}

	bool CoversAsymmetricRectsAndSinglePixel()
	{
		PyramidLayout layout{};
		const std::array<EyeRect, 2> asymmetric{ EyeRect{ 2, 1, 49, 70 }, EyeRect{ 53, 4, 47, 64 } };
		if (!TryBuildLayout(asymmetric, 100, 100, 8, layout) ||
			layout.width != 8 || layout.height != 16 || layout.mipCount != 4)
			return false;
		const std::array<EyeRect, 2> pixels{ EyeRect{ 0, 0, 1, 1 }, EyeRect{ 1, 0, 1, 1 } };
		if (!TryBuildLayout(pixels, 2, 1, 1, layout) ||
			layout.width != 1 || layout.height != 1 || layout.mipCount != 1)
			return false;
		const std::array<EyeRect, 2> maximum{ EyeRect{ 0, 0, 8192, 16384 }, EyeRect{ 8192, 0, 8192, 16384 } };
		return TryBuildLayout(maximum, 16384, 16384, 4, layout) &&
		       layout.width == 2048 && layout.height == 4096 && layout.mipCount == 12;
	}

	bool SelectsFinerDepthWithinResourceLimits()
	{
		PyramidLayout layout{};
		BuildConstants constants{};
		const std::array<EyeRect, 2> typical{ EyeRect{ 0, 0, 1344, 1492 }, EyeRect{ 1344, 0, 1344, 1492 } };
		if (!TryMakePreferredBuildConstants(typical, 2688, 1492, constants, layout) ||
			layout.width != 1024 || layout.height != 1024 || layout.mipCount != 10 ||
			layout.sourceReduction != 2 || constants.sourceReduction != layout.sourceReduction)
			return false;
		for (const auto height : { 8192u, 8193u, 16384u }) {
			const std::array<EyeRect, 2> eyes{ EyeRect{ 0, 0, 8192, height }, EyeRect{ 8192, 0, 8192, height } };
			const auto expectedReduction = height == 8192 ? 2u : 4u;
			if (!TryMakePreferredBuildConstants(eyes, 16384, height, constants, layout) ||
				layout.sourceReduction != expectedReduction || constants.sourceReduction != expectedReduction ||
				layout.width != 8192 / expectedReduction || layout.height != 4096 || layout.mipCount != 12)
				return false;
		}
		auto invalid = typical;
		invalid[1].x = std::numeric_limits<std::uint32_t>::max();
		return !TryMakePreferredBuildConstants(invalid, 2688, 1492, constants, layout) &&
		       constants.outputWidth == 0 && layout.width == 0;
	}

	bool CoversEveryIntervalWithRetainedMips()
	{
		for (std::uint32_t extent = 1; extent <= 256; extent *= 2) {
			const auto height = std::max(extent / 2, 1u);
			const std::array<EyeRect, 2> eyes{ EyeRect{ 0, 0, extent, height }, EyeRect{ extent, 0, extent, height } };
			PyramidLayout layout{};
			if (!TryBuildLayout(eyes, extent * 2, height, 1, layout))
				return false;
			const auto terminalWidth = std::max(layout.width >> (layout.mipCount - 1), 1u);
			const auto terminalHeight = std::max(layout.height >> (layout.mipCount - 1), 1u);
			if (std::max(terminalWidth, terminalHeight) != std::min(extent, 2u))
				return false;

			for (const auto dimension : { layout.width, layout.height }) {
				for (std::uint32_t first = 0; first < dimension; ++first) {
					for (std::uint32_t last = first; last < dimension; ++last) {
						bool covered = false;
						for (std::uint32_t mip = 0; mip < layout.mipCount; ++mip) {
							const auto firstCell = first >> mip;
							const auto lastCell = last >> mip;
							if (lastCell - firstCell > 1)
								continue;
							const auto coveredFirst = firstCell << mip;
							const auto coveredLast = ((lastCell + 1) << mip) - 1;
							if (coveredFirst > first || coveredLast < last || lastCell >= std::max(dimension >> mip, 1u))
								return false;
							covered = true;
							break;
						}
						if (!covered)
							return false;
					}
				}
			}
		}
		return true;
	}

	bool RejectsUnsafeDispatchDimensions()
	{
		std::array<EyeRect, 2> eyes{ EyeRect{ 0, 0, 8, 8 }, EyeRect{ 8, 0, 8, 8 } };
		PyramidLayout layout{};
		for (const auto reduction : { 0u, 3u, 16u, std::numeric_limits<std::uint32_t>::max() })
			if (TryBuildLayout(eyes, 16, 8, reduction, layout))
				return false;
		if (TryBuildLayout(eyes, 0, 8, 4, layout) || TryBuildLayout(eyes, 16, 16385, 4, layout))
			return false;
		eyes[1].x = std::numeric_limits<std::uint32_t>::max();
		if (TryBuildLayout(eyes, 16, 8, 4, layout))
			return false;
		eyes[1] = { 0, 0, 16384, 16384 };
		if (TryBuildLayout(eyes, 16384, 16384, 1, layout))
			return false;
		eyes[1] = { 8, 0, 9, 8 };
		BuildConstants constants{};
		constants.outputWidth = 100;
		layout.width = 100;
		return !TryMakeBuildConstants(eyes, 16, 8, 4, constants, layout) &&
		       constants.outputWidth == 0 && layout.width == 0;
	}

	TestConstants MakeTestConstants()
	{
		TestConstants constants{};
		constants.eyes = { EyeRect{ 0, 0, 32, 32 }, EyeRect{ 32, 0, 32, 32 } };
		TryBuildLayout(constants.eyes, 64, 32, 4, constants.pyramid);
		constants.objectCount = 4096;
		for (auto& matrix : constants.viewProjection)
			for (std::uint32_t row = 0; row < 4; ++row)
				matrix[row][row] = 1.0f;
		return constants;
	}

	bool RejectsInvalidProjectionAndBias()
	{
		auto constants = MakeTestConstants();
		if (!IsValidTestConstants(constants, 64, 32))
			return false;
		constants.viewProjection[1][3][3] = 0.0f;
		if (IsValidTestConstants(constants, 64, 32))
			return false;
		constants = MakeTestConstants();
		constants.viewProjection[0][0][0] = std::numeric_limits<float>::infinity();
		if (IsValidTestConstants(constants, 64, 32))
			return false;
		constants = MakeTestConstants();
		constants.cameraAdjust[1][2] = std::numeric_limits<float>::quiet_NaN();
		if (IsValidTestConstants(constants, 64, 32))
			return false;
		for (const auto bias : { -1.0f, 0.0f, kDefaultDepthBias * 0.5f, 1.01f, std::numeric_limits<float>::quiet_NaN() }) {
			constants = MakeTestConstants();
			constants.depthBias = bias;
			if (IsValidTestConstants(constants, 64, 32))
				return false;
		}
		constants = MakeTestConstants();
		constants.depthBias = 1.0f;
		constants.pixelGuardBand = 10.0f;
		return IsValidTestConstants(constants, 64, 32);
	}

	bool RejectsMismatchedPyramidOrObjects()
	{
		auto constants = MakeTestConstants();
		constants.pyramid.width *= 2;
		if (IsValidTestConstants(constants, 64, 32))
			return false;
		constants = MakeTestConstants();
		++constants.pyramid.mipCount;
		if (IsValidTestConstants(constants, 64, 32))
			return false;
		for (const auto count : { 0u, 4097u, std::numeric_limits<std::uint32_t>::max() }) {
			constants = MakeTestConstants();
			constants.objectCount = count;
			if (IsValidTestConstants(constants, 64, 32))
				return false;
		}
		constants = MakeTestConstants();
		constants.pixelGuardBand = 0.0f;
		if (IsValidTestConstants(constants, 64, 32))
			return false;
		for (const auto controls : { 1u, 2u, 4u, 7u, 8u }) {
			constants = MakeTestConstants();
			constants.reserved = controls;
			if (IsValidTestConstants(constants, 64, 32))
				return false;
		}
		return true;
	}
}

int main()
{
	const std::array tests{
		std::pair{ "padded eye edges", CoversPaddedEyeEdges },
		std::pair{ "asymmetric rectangles", CoversAsymmetricRectsAndSinglePixel },
		std::pair{ "finer depth within resource limits", SelectsFinerDepthWithinResourceLimits },
		std::pair{ "all intervals fit retained mips", CoversEveryIntervalWithRetainedMips },
		std::pair{ "unsafe dispatch dimensions", RejectsUnsafeDispatchDimensions },
		std::pair{ "projection and depth bias", RejectsInvalidProjectionAndBias },
		std::pair{ "pyramid and object correspondence", RejectsMismatchedPyramidOrObjects }
	};
	for (const auto& [name, test] : tests) {
		if (!test()) {
			std::fprintf(stderr, "Failed: %s\n", name);
			return 1;
		}
	}
	return 0;
}
