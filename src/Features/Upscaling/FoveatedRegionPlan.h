#pragma once

#include "../FoveatedCommon.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

struct FoveatedRegionPlan
{
	static constexpr uint32_t kDefaultUnderlayHolePadding = 2u;
	static constexpr uint32_t kDefaultPeripheryHistoryPadding = 2u;
	static constexpr uint32_t kDefaultPeripheryInputPadding = 2u;

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

	struct TextureRegion
	{
		float2 scale{ 1.0f, 1.0f };
		float2 offset{ 0.0f, 0.0f };
	};

	struct Eye
	{
		Rect output;
		Rect input;
		Rect centerInteriorOutput;
		Rect centerUnderlayHoleOutput;
		Rect peripheryTAAOuterOutput;
		Rect peripheryTAAHistoryOutput;
		Rect peripheryTAAOuterInput;
		Rect encodeInput;
		float2 centerOffset{ 0.0f, 0.0f };
		float2 pinholeOffset{ 0.0f, 0.0f };

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
	float peripheryTAAOuterScale = 0.0f;
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
		uint32_t a_inputPadding = 0u,
		float a_peripheryTAAOuterScale = 0.0f,
		uint32_t a_centerUnderlayHolePadding = kDefaultUnderlayHolePadding,
		uint32_t a_peripheryHistoryPadding = kDefaultPeripheryHistoryPadding,
		uint32_t a_peripheryInputPadding = kDefaultPeripheryInputPadding)
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
		plan.peripheryTAAOuterScale = std::isfinite(a_peripheryTAAOuterScale) && a_peripheryTAAOuterScale > 0.0f ?
			std::max(plan.centerScale, FoveatedCommon::ClampCenterArea(a_peripheryTAAOuterScale)) :
			0.0f;

		if (!a_inputWidthPerEye || !a_inputHeight || !a_outputWidthPerEye || !a_outputHeight)
			return plan;

		const uint32_t eyeCount = a_isVR ? 2u : 1u;
		for (uint32_t eyeIndex = 0; eyeIndex < eyeCount; ++eyeIndex) {
			auto& eye = plan.eyes[eyeIndex];
			eye.centerOffset = a_centerOffsets[eyeIndex];

			eye.output = BuildCenteredOutputRect(
				a_outputWidthPerEye,
				a_outputHeight,
				plan.centerScale,
				plan.centerFeather,
				plan.centerHorizontalScale,
				eye.centerOffset);
			if (!eye.output.IsValid())
				continue;

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
			eye.encodeInput = eye.input;
			eye.centerInteriorOutput = BuildCenteredInscribedOutputRect(
				a_outputWidthPerEye,
				a_outputHeight,
				plan.centerScale,
				eye.centerOffset,
				plan.centerHorizontalScale);
			eye.centerUnderlayHoleOutput = ShrinkRect(eye.centerInteriorOutput, a_centerUnderlayHolePadding);
			eye.pinholeOffset = GetPinholeOffset(eye.output, a_outputWidthPerEye, a_outputHeight);

			if (plan.peripheryTAAOuterScale > 0.0f) {
				eye.peripheryTAAOuterOutput = BuildCenteredOutputRect(
					a_outputWidthPerEye,
					a_outputHeight,
					plan.peripheryTAAOuterScale,
					0.0f,
					plan.centerHorizontalScale,
					eye.centerOffset);
				eye.peripheryTAAHistoryOutput = ExpandRect(
					eye.peripheryTAAOuterOutput,
					a_peripheryHistoryPadding,
					a_outputWidthPerEye,
					a_outputHeight);
				if (eye.peripheryTAAOuterOutput.IsValid()) {
					eye.peripheryTAAOuterInput = MapOutputRectToInputRect(
						eye.peripheryTAAOuterOutput.minX,
						eye.peripheryTAAOuterOutput.minY,
						eye.peripheryTAAOuterOutput.maxX,
						eye.peripheryTAAOuterOutput.maxY,
						a_outputWidthPerEye,
						a_outputHeight,
						a_inputWidthPerEye,
						a_inputHeight,
						a_peripheryInputPadding);
					eye.encodeInput = UnionRects(eye.encodeInput, eye.peripheryTAAOuterInput);
				}
			}
		}

		return plan;
	}

	static TextureRegion ClampNormalizedTextureRegion(float scaleX, float scaleY, float offsetX, float offsetY)
	{
		TextureRegion region{};
		region.offset = {
			ClampUnitOrDefault(offsetX, 0.0f),
			ClampUnitOrDefault(offsetY, 0.0f)
		};
		region.scale = {
			ClampUnitOrDefault(scaleX, 1.0f),
			ClampUnitOrDefault(scaleY, 1.0f)
		};
		region.scale.x = std::min(region.scale.x, 1.0f - region.offset.x);
		region.scale.y = std::min(region.scale.y, 1.0f - region.offset.y);
		return region;
	}

	static TextureRegion BuildTopLeftValidTextureRegion(uint32_t validWidth, uint32_t validHeight, uint32_t textureWidth, uint32_t textureHeight)
	{
		return ClampNormalizedTextureRegion(
			ComputeTopLeftValidScale(validWidth, textureWidth),
			ComputeTopLeftValidScale(validHeight, textureHeight),
			0.0f,
			0.0f);
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

	static Rect UnionRects(const Rect& a_lhs, const Rect& a_rhs)
	{
		if (!a_lhs.IsValid())
			return a_rhs;
		if (!a_rhs.IsValid())
			return a_lhs;

		return {
			std::min(a_lhs.minX, a_rhs.minX),
			std::min(a_lhs.minY, a_rhs.minY),
			std::max(a_lhs.maxX, a_rhs.maxX),
			std::max(a_lhs.maxY, a_rhs.maxY)
		};
	}

	static Rect ExpandRect(const Rect& a_rect, uint32_t a_padding, uint32_t a_maxWidth, uint32_t a_maxHeight)
	{
		if (!a_rect.IsValid())
			return {};

		return {
			a_rect.minX > a_padding ? a_rect.minX - a_padding : 0u,
			a_rect.minY > a_padding ? a_rect.minY - a_padding : 0u,
			static_cast<uint32_t>(std::min<uint64_t>(a_maxWidth, static_cast<uint64_t>(a_rect.maxX) + a_padding)),
			static_cast<uint32_t>(std::min<uint64_t>(a_maxHeight, static_cast<uint64_t>(a_rect.maxY) + a_padding))
		};
	}

	static Rect ShrinkRect(const Rect& a_rect, uint32_t a_padding)
	{
		if (!a_rect.IsValid())
			return {};

		Rect shrunk{
			std::min(a_rect.minX + a_padding, a_rect.maxX),
			std::min(a_rect.minY + a_padding, a_rect.maxY),
			a_rect.maxX > a_padding ? a_rect.maxX - a_padding : a_rect.minX,
			a_rect.maxY > a_padding ? a_rect.maxY - a_padding : a_rect.minY
		};
		return shrunk.IsValid() ? shrunk : Rect{};
	}

	static Rect BuildCenteredOutputRect(
		uint32_t a_outputWidth,
		uint32_t a_outputHeight,
		float a_centerScale,
		float a_centerFeather,
		float a_centerHorizontalScale,
		const float2& a_centerOffset)
	{
		const auto bounds = FoveatedCommon::BuildCenteredDispatchBounds(
			0,
			a_outputWidth,
			a_outputHeight,
			a_centerScale,
			a_centerOffset.x,
			a_centerOffset.y,
			a_centerFeather,
			a_centerHorizontalScale);
		return BoundsToRect(bounds);
	}

	static Rect BuildCenteredInscribedOutputRect(
		uint32_t a_outputWidth,
		uint32_t a_outputHeight,
		float a_centerScale,
		const float2& a_centerOffset,
		float a_centerHorizontalScale)
	{
		const auto bounds = FoveatedCommon::BuildCenteredInscribedMaskRect(
			a_outputWidth,
			a_outputHeight,
			a_centerScale,
			a_centerOffset.x,
			a_centerOffset.y,
			a_centerHorizontalScale);
		return BoundsToRect(bounds);
	}

	static float2 GetPinholeOffset(const Rect& a_outputRect, uint32_t a_outputWidth, uint32_t a_outputHeight)
	{
		if (!a_outputRect.IsValid() || !a_outputWidth || !a_outputHeight)
			return { 0.0f, 0.0f };

		const float outputWidthF = static_cast<float>(a_outputWidth);
		const float outputHeightF = static_cast<float>(a_outputHeight);
		const float rectCenterX = (static_cast<float>(a_outputRect.minX) + static_cast<float>(a_outputRect.Width()) * 0.5f) / outputWidthF;
		const float rectCenterY = (static_cast<float>(a_outputRect.minY) + static_cast<float>(a_outputRect.Height()) * 0.5f) / outputHeightF;
		return {
			std::clamp((rectCenterX - 0.5f) * 2.0f, -1.0f, 1.0f),
			std::clamp((0.5f - rectCenterY) * 2.0f, -1.0f, 1.0f)
		};
	}

private:
	static Rect BoundsToRect(const FoveatedCommon::DispatchBounds& a_bounds)
	{
		if (a_bounds.maxX <= a_bounds.minX || a_bounds.maxY <= a_bounds.minY)
			return {};

		return {
			static_cast<uint32_t>(a_bounds.minX),
			static_cast<uint32_t>(a_bounds.minY),
			static_cast<uint32_t>(a_bounds.maxX),
			static_cast<uint32_t>(a_bounds.maxY)
		};
	}

	static float ClampUnitOrDefault(float value, float fallback)
	{
		return std::clamp(std::isfinite(value) ? value : fallback, 0.0f, 1.0f);
	}

	static float ComputeTopLeftValidScale(uint32_t validDimension, uint32_t textureDimension)
	{
		if (!validDimension || !textureDimension)
			return 1.0f;
		return static_cast<float>(validDimension) / static_cast<float>(textureDimension);
	}
};
