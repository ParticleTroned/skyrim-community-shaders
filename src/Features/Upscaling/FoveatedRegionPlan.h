#pragma once

#include "../FoveatedCommon.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

struct FoveatedRegionPlan
{
	struct Rect
	{
		uint32_t minX = 0;
		uint32_t minY = 0;
		uint32_t maxX = 0;
		uint32_t maxY = 0;

		[[nodiscard]] bool IsValid() const
		{
			return maxX > minX && maxY > minY;
		}

		[[nodiscard]] uint32_t Width() const
		{
			return IsValid() ? maxX - minX : 0u;
		}

		[[nodiscard]] uint32_t Height() const
		{
			return IsValid() ? maxY - minY : 0u;
		}
	};

	struct Eye
	{
		Rect output;
		Rect input;
		float2 centerOffset{ 0.0f, 0.0f };

		[[nodiscard]] bool IsValid() const
		{
			return output.IsValid() && input.IsValid();
		}
	};

	uint32_t inputWidthPerEye = 0;
	uint32_t inputHeight = 0;
	uint32_t outputWidthPerEye = 0;
	uint32_t outputHeight = 0;
	bool isVR = false;
	float centerScale = FoveatedCommon::kCenterAreaMax;
	float centerFeather = FoveatedCommon::kCenterFeather;
	float centerHorizontalScale = 1.0f;
	std::array<Eye, 2> eyes{};

	[[nodiscard]] bool IsValid() const
	{
		return eyes[0].IsValid() && (!isVR || eyes[1].IsValid());
	}

	static FoveatedRegionPlan Build(
		uint32_t a_inputWidthPerEye,
		uint32_t a_inputHeight,
		uint32_t a_outputWidthPerEye,
		uint32_t a_outputHeight,
		bool a_isVR,
		float a_centerScale,
		float a_centerFeather,
		float a_centerHorizontalScale,
		const std::array<float2, 2>& a_centerOffsets,
		uint32_t a_inputPadding = 0u)
	{
		FoveatedRegionPlan plan{};
		plan.inputWidthPerEye = a_inputWidthPerEye;
		plan.inputHeight = a_inputHeight;
		plan.outputWidthPerEye = a_outputWidthPerEye;
		plan.outputHeight = a_outputHeight;
		plan.isVR = a_isVR;
		plan.centerScale = FoveatedCommon::ClampCenterArea(a_centerScale);
		plan.centerFeather = std::isfinite(a_centerFeather) ? std::max(0.0f, a_centerFeather) : FoveatedCommon::kCenterFeather;
		plan.centerHorizontalScale = FoveatedCommon::ClampCenterHorizontalScale(a_centerHorizontalScale);

		if (!a_inputWidthPerEye || !a_inputHeight || !a_outputWidthPerEye || !a_outputHeight)
			return plan;

		const uint32_t eyeCount = a_isVR ? 2u : 1u;
		for (uint32_t eyeIndex = 0; eyeIndex < eyeCount; ++eyeIndex) {
			auto& eye = plan.eyes[eyeIndex];
			eye.centerOffset = a_centerOffsets[eyeIndex];

			const auto bounds = FoveatedCommon::BuildCenteredDispatchBounds(
				0,
				a_outputWidthPerEye,
				a_outputHeight,
				plan.centerScale,
				eye.centerOffset.x,
				eye.centerOffset.y,
				plan.centerFeather,
				plan.centerHorizontalScale);
			if (bounds.maxX <= bounds.minX || bounds.maxY <= bounds.minY)
				continue;

			eye.output = {
				static_cast<uint32_t>(bounds.minX),
				static_cast<uint32_t>(bounds.minY),
				static_cast<uint32_t>(bounds.maxX),
				static_cast<uint32_t>(bounds.maxY)
			};
			eye.input = MapOutputRectToInputRect(
				eye.output.minX,
				eye.output.minY,
				eye.output.maxX,
				eye.output.maxY,
				a_outputWidthPerEye,
				a_outputHeight,
				a_inputWidthPerEye,
				a_inputHeight,
				a_inputPadding);
		}

		return plan;
	}

	static Rect MapOutputRectToInputRect(
		uint32_t a_outputMinX,
		uint32_t a_outputMinY,
		uint32_t a_outputMaxX,
		uint32_t a_outputMaxY,
		uint32_t a_outputWidth,
		uint32_t a_outputHeight,
		uint32_t a_inputWidth,
		uint32_t a_inputHeight,
		uint32_t a_padding = 0u)
	{
		Rect mapped{};
		if (a_outputWidth == 0 || a_outputHeight == 0 || a_inputWidth == 0 || a_inputHeight == 0)
			return mapped;

		a_outputMinX = std::min(a_outputMinX, a_outputWidth);
		a_outputMinY = std::min(a_outputMinY, a_outputHeight);
		a_outputMaxX = std::min(a_outputMaxX, a_outputWidth);
		a_outputMaxY = std::min(a_outputMaxY, a_outputHeight);
		if (a_outputMaxX < a_outputMinX)
			std::swap(a_outputMaxX, a_outputMinX);
		if (a_outputMaxY < a_outputMinY)
			std::swap(a_outputMaxY, a_outputMinY);
		if (a_outputMaxX <= a_outputMinX || a_outputMaxY <= a_outputMinY)
			return mapped;

		const auto mapMin = [](uint32_t value, uint32_t outputExtent, uint32_t inputExtent) {
			const double scale = static_cast<double>(inputExtent) / static_cast<double>(outputExtent);
			return static_cast<uint32_t>(std::floor(static_cast<double>(value) * scale));
		};
		const auto mapMax = [](uint32_t value, uint32_t outputExtent, uint32_t inputExtent) {
			const double scale = static_cast<double>(inputExtent) / static_cast<double>(outputExtent);
			return static_cast<uint32_t>(std::ceil(static_cast<double>(value) * scale));
		};

		uint32_t inputMinX = mapMin(a_outputMinX, a_outputWidth, a_inputWidth);
		uint32_t inputMaxX = mapMax(a_outputMaxX, a_outputWidth, a_inputWidth);
		uint32_t inputMinY = mapMin(a_outputMinY, a_outputHeight, a_inputHeight);
		uint32_t inputMaxY = mapMax(a_outputMaxY, a_outputHeight, a_inputHeight);

		if (a_padding > 0) {
			inputMinX = inputMinX > a_padding ? inputMinX - a_padding : 0u;
			inputMinY = inputMinY > a_padding ? inputMinY - a_padding : 0u;
			inputMaxX = static_cast<uint32_t>(std::min<uint64_t>(a_inputWidth, static_cast<uint64_t>(inputMaxX) + a_padding));
			inputMaxY = static_cast<uint32_t>(std::min<uint64_t>(a_inputHeight, static_cast<uint64_t>(inputMaxY) + a_padding));
		}

		inputMinX = std::min(inputMinX, a_inputWidth);
		inputMinY = std::min(inputMinY, a_inputHeight);
		inputMaxX = std::min(inputMaxX, a_inputWidth);
		inputMaxY = std::min(inputMaxY, a_inputHeight);
		if (inputMaxX <= inputMinX || inputMaxY <= inputMinY)
			return mapped;

		mapped.minX = inputMinX;
		mapped.minY = inputMinY;
		mapped.maxX = inputMaxX;
		mapped.maxY = inputMaxY;
		return mapped;
	}
};
