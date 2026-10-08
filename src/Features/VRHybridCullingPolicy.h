#pragma once

#include "VRDepthCullingTemporalPolicy.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

namespace VRHybridCullingPolicy
{

#ifdef DEVBENCH_BRIDGE_ENABLED
	inline constexpr std::uint32_t kDisableSourceRefinement = 1;
	inline constexpr std::uint32_t kDisableDirectIntersection = 2;
	inline constexpr std::uint32_t kDisableFarClip = 4;
	inline constexpr std::uint32_t kDevBenchTestControls = kDisableSourceRefinement | kDisableDirectIntersection | kDisableFarClip;
#endif

	inline constexpr std::uint32_t kEyeCount = 2;
	inline constexpr std::uint32_t kMaximumSourceDimension = 16384;
	inline constexpr std::uint32_t kMaximumPyramidDimension = 4096;
	inline constexpr std::uint32_t kDefaultSourceReduction = 2;
	inline constexpr std::uint32_t kLargeSourceReduction = 4;
	inline constexpr float kDefaultDepthBias = 8.0f / 16777216.0f;
	inline constexpr float kDefaultPixelGuardBand = 1.0f;
	using OBBTransform = VRDepthCullingTemporalPolicy::OBBTransform;

	struct EyeRect
	{
		std::uint32_t x = 0;
		std::uint32_t y = 0;
		std::uint32_t width = 0;
		std::uint32_t height = 0;
	};

	struct PyramidLayout
	{
		std::uint32_t width = 0;
		std::uint32_t height = 0;
		std::uint32_t mipCount = 0;
		std::uint32_t sourceReduction = 0;
	};

	struct BuildConstants
	{
		std::array<EyeRect, kEyeCount> eyes{};
		std::uint32_t outputWidth = 0;
		std::uint32_t outputHeight = 0;
		std::uint32_t sourceReduction = 0;
		std::uint32_t reserved = 0;
	};

	struct ReduceConstants
	{
		std::uint32_t outputWidth = 0;
		std::uint32_t outputHeight = 0;
		std::uint32_t reserved[2]{};
	};

	struct TestConstants
	{
		float viewProjection[kEyeCount][4][4]{};
		float cameraAdjust[kEyeCount][4]{};
		std::array<EyeRect, kEyeCount> eyes{};
		PyramidLayout pyramid{};
		std::uint32_t objectCount = 0;
		float depthBias = kDefaultDepthBias;
		float pixelGuardBand = kDefaultPixelGuardBand;
		std::uint32_t reserved = 0;
	};

	static_assert(sizeof(EyeRect) == 16);
	static_assert(sizeof(PyramidLayout) == 16);
	static_assert(sizeof(BuildConstants) == 48);
	static_assert(sizeof(ReduceConstants) == 16);
	static_assert(sizeof(TestConstants) == 224);

	/** Reject dimensions before any texture allocation or dispatch arithmetic. */
	constexpr bool IsValidSourceReduction(std::uint32_t a_reduction)
	{
		return a_reduction >= 1 && a_reduction <= 8 && std::has_single_bit(a_reduction);
	}

	/** Eye rectangles remain within one known source resource without unsigned overflow. */
	constexpr bool IsValidEyeRect(const EyeRect& a_eye, std::uint32_t a_sourceWidth, std::uint32_t a_sourceHeight)
	{
		return a_sourceWidth > 0 && a_sourceWidth <= kMaximumSourceDimension &&
		       a_sourceHeight > 0 && a_sourceHeight <= kMaximumSourceDimension &&
		       a_eye.width > 0 && a_eye.width <= a_sourceWidth &&
		       a_eye.height > 0 && a_eye.height <= a_sourceHeight &&
		       a_eye.x <= a_sourceWidth - a_eye.width &&
		       a_eye.y <= a_sourceHeight - a_eye.height;
	}

	/** Pad each eye independently so ordinary mip halving cannot lose an odd edge. */
	constexpr bool TryBuildLayout(
		const std::array<EyeRect, kEyeCount>& a_eyes,
		std::uint32_t a_sourceWidth,
		std::uint32_t a_sourceHeight,
		std::uint32_t a_reduction,
		PyramidLayout& a_layout)
	{
		a_layout = {};
		if (!IsValidSourceReduction(a_reduction))
			return false;
		std::uint32_t width = 0;
		std::uint32_t height = 0;
		for (const auto& eye : a_eyes) {
			if (!IsValidEyeRect(eye, a_sourceWidth, a_sourceHeight))
				return false;
			width = std::max(width, (eye.width + a_reduction - 1) / a_reduction);
			height = std::max(height, (eye.height + a_reduction - 1) / a_reduction);
		}
		width = std::bit_ceil(width);
		height = std::bit_ceil(height);
		if (width > kMaximumPyramidDimension || height > kMaximumPyramidDimension)
			return false;
		// Every rectangle already fits the <=2x2 terminal mip; a smaller mip is unused.
		const auto mipCount = std::max(1u, static_cast<std::uint32_t>(std::bit_width(std::max(width, height)) - 1));
		a_layout = { width, height, mipCount, a_reduction };
		return true;
	}

	/** Validate every source rectangle and derive the base dispatch in one operation. */
	constexpr bool TryMakeBuildConstants(
		const std::array<EyeRect, kEyeCount>& a_eyes,
		std::uint32_t a_sourceWidth,
		std::uint32_t a_sourceHeight,
		std::uint32_t a_reduction,
		BuildConstants& a_constants,
		PyramidLayout& a_layout)
	{
		a_constants = {};
		if (!TryBuildLayout(a_eyes, a_sourceWidth, a_sourceHeight, a_reduction, a_layout))
			return false;
		a_constants = { a_eyes, a_layout.width, a_layout.height, a_reduction, 0 };
		return true;
	}

	/** Retain finer depth where it fits, preserving the admitted source and pyramid limits. */
	constexpr bool TryMakePreferredBuildConstants(
		const std::array<EyeRect, kEyeCount>& a_eyes,
		std::uint32_t a_sourceWidth,
		std::uint32_t a_sourceHeight,
		BuildConstants& a_constants,
		PyramidLayout& a_layout)
	{
		return TryMakeBuildConstants(a_eyes, a_sourceWidth, a_sourceHeight, kDefaultSourceReduction, a_constants, a_layout) ||
		       TryMakeBuildConstants(a_eyes, a_sourceWidth, a_sourceHeight, kLargeSourceReduction, a_constants, a_layout);
	}

	/** Non-finite or singular cameras cannot establish an occlusion proof. */
	inline bool IsValidProjection(const float (&a_matrix)[4][4])
	{
		double matrix[4][4]{};
		for (std::uint32_t row = 0; row < 4; ++row)
			for (std::uint32_t column = 0; column < 4; ++column) {
				if (!std::isfinite(a_matrix[row][column]))
					return false;
				matrix[row][column] = a_matrix[row][column];
			}
		for (std::uint32_t column = 0; column < 4; ++column) {
			std::uint32_t pivot = column;
			for (std::uint32_t row = column + 1; row < 4; ++row)
				if (std::abs(matrix[row][column]) > std::abs(matrix[pivot][column]))
					pivot = row;
			if (matrix[pivot][column] == 0.0)
				return false;
			for (std::uint32_t component = column; component < 4; ++component)
				std::swap(matrix[column][component], matrix[pivot][component]);
			for (std::uint32_t row = column + 1; row < 4; ++row) {
				const double factor = matrix[row][column] / matrix[column][column];
				for (std::uint32_t component = column + 1; component < 4; ++component)
					matrix[row][component] -= factor * matrix[column][component];
			}
		}
		return true;
	}

	/** Require a matching pyramid and finite stereo constants before submitting OBB work. */
	inline bool IsValidTestConstants(const TestConstants& a_constants, std::uint32_t a_sourceWidth, std::uint32_t a_sourceHeight)
	{
		PyramidLayout expected{};
		if (!TryBuildLayout(a_constants.eyes, a_sourceWidth, a_sourceHeight, a_constants.pyramid.sourceReduction, expected) ||
			expected.width != a_constants.pyramid.width || expected.height != a_constants.pyramid.height ||
			expected.mipCount != a_constants.pyramid.mipCount ||
			a_constants.objectCount == 0 || a_constants.objectCount > VRDepthCullingTemporalPolicy::kMaximumObjects ||
			!std::isfinite(a_constants.depthBias) || a_constants.depthBias < kDefaultDepthBias || a_constants.depthBias > 1.0f ||
			!std::isfinite(a_constants.pixelGuardBand) || a_constants.pixelGuardBand < kDefaultPixelGuardBand ||
			a_constants.pixelGuardBand > kMaximumSourceDimension) {
			return false;
		}
#ifdef DEVBENCH_BRIDGE_ENABLED
		if ((a_constants.reserved & ~kDevBenchTestControls) != 0)
#else
		if (a_constants.reserved != 0)
#endif
			return false;
		for (std::uint32_t eye = 0; eye < kEyeCount; ++eye) {
			if (!IsValidProjection(a_constants.viewProjection[eye]))
				return false;
			for (float adjustment : a_constants.cameraAdjust[eye])
				if (!std::isfinite(adjustment))
					return false;
		}
		return true;
	}
}
