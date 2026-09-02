#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace FoveatedCenterAlignment
{
	inline constexpr float kCenterScaleMin = 0.25f;
	inline constexpr float kCenterScaleMax = 1.0f;
	inline constexpr float kCenterHorizontalScaleMin = 1.0f;
	inline constexpr float kCenterHorizontalScaleMax = 2.0f;
	inline constexpr float kManualOffsetMin = -0.30f;
	inline constexpr float kManualOffsetMax = 0.30f;
	inline constexpr float kResolvedOffsetMin = -0.30f;
	inline constexpr float kResolvedOffsetMax = 0.30f;
	inline constexpr float kProjectionEpsilon = 1.0e-6f;

	enum class CenterOrigin : std::uint8_t
	{
		ImageCenter,
		OpticalCenter,
		Count
	};

	enum class HorizontalAnchor : std::uint8_t
	{
		Symmetric,
		Outward,
		Count
	};

	enum class OpticalCenterSource : std::uint8_t
	{
		ImageCenter,
		Projection,
		Tangents,
		ImageCenterFallback
	};

	enum class OpticalFallbackReason : std::uint8_t
	{
		None,
		OpticalOriginNotRequested,
		ProjectionUnavailable,
		ProjectionInvalid,
		NoValidOpticalInput
	};

	enum class InputValidity : std::uint8_t
	{
		Unavailable,
		Invalid,
		Valid
	};

	inline constexpr CenterOrigin kCompatibilityCenterOrigin =
		CenterOrigin::ImageCenter;
	inline constexpr HorizontalAnchor kCompatibilityHorizontalAnchor =
		HorizontalAnchor::Outward;

	struct Point
	{
		float x = 0.0f;
		float y = 0.0f;
	};

	/** @brief Row-major projection sample after conversion to Streamline matrix convention. */
	struct ProjectionMatrixInput
	{
		bool available = false;
		std::array<float, 16> values{};

		[[nodiscard]] float At(std::size_t a_row, std::size_t a_column) const noexcept
		{
			return values[a_row * 4u + a_column];
		}
	};

	/** @brief Actual left, right, bottom, and top eye-plane tangents. */
	struct ProjectionTangentsInput
	{
		bool available = false;
		float left = 0.0f;
		float right = 0.0f;
		float bottom = 0.0f;
		float top = 0.0f;
	};

	struct EyeOpticalInputs
	{
		ProjectionMatrixInput projection{};
		ProjectionTangentsInput tangents{};
	};

	/** @brief Stereo centre-alignment settings with compatibility-preserving defaults. */
	struct Settings
	{
		CenterOrigin origin = kCompatibilityCenterOrigin;
		HorizontalAnchor anchor = kCompatibilityHorizontalAnchor;
		float centerScale = 0.60f;
		float centerHorizontalScale = 1.0f;
		std::array<Point, 2> manualOffsets{};
	};

	struct EyeDiagnostics
	{
		std::uint32_t eyeIndex = 0;
		OpticalCenterSource source = OpticalCenterSource::ImageCenter;
		OpticalFallbackReason fallbackReason =
			OpticalFallbackReason::OpticalOriginNotRequested;
		InputValidity projectionValidity = InputValidity::Unavailable;
		InputValidity tangentValidity = InputValidity::Unavailable;
		Point projectionCenterUV{ 0.5f, 0.5f };
		Point tangentCenterUV{ 0.5f, 0.5f };
		Point baseCenterUV{ 0.5f, 0.5f };
		Point baseOffset{};
		Point anchorOffset{};
		Point requestedManualOffset{};
		Point manualOffset{};
		Point unclampedOffset{};
		Point finalOffset{};
		bool manualOffsetClampedX = false;
		bool manualOffsetClampedY = false;
		bool finalOffsetClampedX = false;
		bool finalOffsetClampedY = false;
	};

	struct StereoDiagnostics
	{
		CenterOrigin origin = kCompatibilityCenterOrigin;
		HorizontalAnchor anchor = kCompatibilityHorizontalAnchor;
		float centerScale = 0.60f;
		float centerHorizontalScale = 1.0f;
		std::array<EyeDiagnostics, 2> eyes{};
	};

	[[nodiscard]] inline constexpr bool IsValid(CenterOrigin a_origin) noexcept
	{
		return a_origin == CenterOrigin::ImageCenter ||
		       a_origin == CenterOrigin::OpticalCenter;
	}

	[[nodiscard]] inline constexpr bool IsValid(HorizontalAnchor a_anchor) noexcept
	{
		return a_anchor == HorizontalAnchor::Symmetric ||
		       a_anchor == HorizontalAnchor::Outward;
	}

	[[nodiscard]] inline constexpr const char* GetCenterOriginName(
		CenterOrigin a_origin) noexcept
	{
		switch (a_origin) {
		case CenterOrigin::ImageCenter:
			return "image_center";
		case CenterOrigin::OpticalCenter:
			return "optical_center";
		default:
			return "unknown";
		}
	}

	[[nodiscard]] inline constexpr const char* GetHorizontalAnchorName(
		HorizontalAnchor a_anchor) noexcept
	{
		switch (a_anchor) {
		case HorizontalAnchor::Symmetric:
			return "symmetric";
		case HorizontalAnchor::Outward:
			return "outward";
		default:
			return "unknown";
		}
	}

	[[nodiscard]] inline constexpr const char* GetOpticalCenterSourceName(
		OpticalCenterSource a_source) noexcept
	{
		switch (a_source) {
		case OpticalCenterSource::ImageCenter:
			return "image_center";
		case OpticalCenterSource::Projection:
			return "projection";
		case OpticalCenterSource::Tangents:
			return "tangents";
		case OpticalCenterSource::ImageCenterFallback:
			return "image_center_fallback";
		default:
			return "unknown";
		}
	}

	[[nodiscard]] inline constexpr const char* GetOpticalFallbackReasonName(
		OpticalFallbackReason a_reason) noexcept
	{
		switch (a_reason) {
		case OpticalFallbackReason::None:
			return "none";
		case OpticalFallbackReason::OpticalOriginNotRequested:
			return "optical_origin_not_requested";
		case OpticalFallbackReason::ProjectionUnavailable:
			return "projection_unavailable";
		case OpticalFallbackReason::ProjectionInvalid:
			return "projection_invalid";
		case OpticalFallbackReason::NoValidOpticalInput:
			return "no_valid_optical_input";
		default:
			return "unknown";
		}
	}

	namespace detail
	{
		inline float ClampFinite(
			float a_value,
			float a_fallback,
			float a_minimum,
			float a_maximum) noexcept
		{
			return std::clamp(
				std::isfinite(a_value) ? a_value : a_fallback,
				a_minimum,
				a_maximum);
		}

		inline bool IsUnitPoint(const Point& a_point) noexcept
		{
			return std::isfinite(a_point.x) && std::isfinite(a_point.y) &&
			       a_point.x >= 0.0f && a_point.x <= 1.0f &&
			       a_point.y >= 0.0f && a_point.y <= 1.0f;
		}

		inline InputValidity ResolveProjectionCenter(
			const ProjectionMatrixInput& a_projection,
			Point& a_centerUV) noexcept
		{
			if (!a_projection.available)
				return InputValidity::Unavailable;

			const float scaleX = a_projection.At(0u, 0u);
			const float scaleY = a_projection.At(1u, 1u);
			const float offsetX = a_projection.At(2u, 0u);
			const float offsetY = a_projection.At(2u, 1u);
			const float homogeneousScale = a_projection.At(2u, 3u);
			if (!std::isfinite(scaleX) || !std::isfinite(scaleY) ||
				!std::isfinite(offsetX) || !std::isfinite(offsetY) ||
				!std::isfinite(homogeneousScale) ||
				std::abs(scaleX) <= kProjectionEpsilon ||
				std::abs(scaleY) <= kProjectionEpsilon ||
				std::abs(homogeneousScale) <= kProjectionEpsilon) {
				return InputValidity::Invalid;
			}

			const float principalNdcX = offsetX / homogeneousScale;
			const float principalNdcY = offsetY / homogeneousScale;
			const Point resolved{
				(principalNdcX + 1.0f) * 0.5f,
				(1.0f - principalNdcY) * 0.5f
			};
			if (!IsUnitPoint(resolved))
				return InputValidity::Invalid;

			a_centerUV = resolved;
			return InputValidity::Valid;
		}

		inline InputValidity ResolveTangentCenter(
			const ProjectionTangentsInput& a_tangents,
			Point& a_centerUV) noexcept
		{
			if (!a_tangents.available)
				return InputValidity::Unavailable;
			if (!std::isfinite(a_tangents.left) ||
				!std::isfinite(a_tangents.right) ||
				!std::isfinite(a_tangents.bottom) ||
				!std::isfinite(a_tangents.top) ||
				a_tangents.left >= 0.0f || a_tangents.right <= 0.0f ||
				a_tangents.bottom >= 0.0f || a_tangents.top <= 0.0f) {
				return InputValidity::Invalid;
			}

			const float horizontalSpan = a_tangents.right - a_tangents.left;
			const float verticalSpan = a_tangents.top - a_tangents.bottom;
			if (horizontalSpan <= kProjectionEpsilon ||
				verticalSpan <= kProjectionEpsilon) {
				return InputValidity::Invalid;
			}

			const Point resolved{
				-a_tangents.left / horizontalSpan,
				a_tangents.top / verticalSpan
			};
			if (!IsUnitPoint(resolved))
				return InputValidity::Invalid;

			a_centerUV = resolved;
			return InputValidity::Valid;
		}
	}

	/** @brief Resolves an immutable two-eye alignment snapshot for one stereo transaction. */
	[[nodiscard]] inline StereoDiagnostics ResolveStereo(
		const Settings& a_settings,
		const std::array<EyeOpticalInputs, 2>& a_opticalInputs) noexcept
	{
		StereoDiagnostics result{};
		result.origin = IsValid(a_settings.origin) ?
		                    a_settings.origin :
		                    kCompatibilityCenterOrigin;
		result.anchor = IsValid(a_settings.anchor) ?
		                    a_settings.anchor :
		                    kCompatibilityHorizontalAnchor;
		result.centerScale = detail::ClampFinite(
			a_settings.centerScale, 0.60f, kCenterScaleMin, kCenterScaleMax);
		result.centerHorizontalScale = detail::ClampFinite(
			a_settings.centerHorizontalScale,
			1.0f,
			kCenterHorizontalScaleMin,
			kCenterHorizontalScaleMax);

		const float outwardExpansion =
			result.anchor == HorizontalAnchor::Outward ?
				result.centerScale * 0.5f *
					std::max(0.0f, result.centerHorizontalScale - 1.0f) :
				0.0f;

		for (std::uint32_t eyeIndex = 0; eyeIndex < result.eyes.size(); ++eyeIndex) {
			auto& eye = result.eyes[eyeIndex];
			eye.eyeIndex = eyeIndex;
			eye.projectionValidity = detail::ResolveProjectionCenter(
				a_opticalInputs[eyeIndex].projection, eye.projectionCenterUV);
			eye.tangentValidity = detail::ResolveTangentCenter(
				a_opticalInputs[eyeIndex].tangents, eye.tangentCenterUV);

			if (result.origin == CenterOrigin::ImageCenter) {
				eye.source = OpticalCenterSource::ImageCenter;
				eye.fallbackReason =
					OpticalFallbackReason::OpticalOriginNotRequested;
			} else if (eye.projectionValidity == InputValidity::Valid) {
				eye.source = OpticalCenterSource::Projection;
				eye.fallbackReason = OpticalFallbackReason::None;
				eye.baseCenterUV = eye.projectionCenterUV;
			} else if (eye.tangentValidity == InputValidity::Valid) {
				eye.source = OpticalCenterSource::Tangents;
				eye.fallbackReason =
					eye.projectionValidity == InputValidity::Unavailable ?
						OpticalFallbackReason::ProjectionUnavailable :
						OpticalFallbackReason::ProjectionInvalid;
				eye.baseCenterUV = eye.tangentCenterUV;
			} else {
				eye.source = OpticalCenterSource::ImageCenterFallback;
				eye.fallbackReason =
					OpticalFallbackReason::NoValidOpticalInput;
			}

			eye.baseOffset = {
				eye.baseCenterUV.x - 0.5f,
				eye.baseCenterUV.y - 0.5f
			};
			eye.anchorOffset = {
				eyeIndex == 0u ? -outwardExpansion : outwardExpansion,
				0.0f
			};
			eye.requestedManualOffset = a_settings.manualOffsets[eyeIndex];
			eye.manualOffset = {
				detail::ClampFinite(
					eye.requestedManualOffset.x,
					0.0f,
					kManualOffsetMin,
					kManualOffsetMax),
				detail::ClampFinite(
					eye.requestedManualOffset.y,
					0.0f,
					kManualOffsetMin,
					kManualOffsetMax)
			};
			eye.manualOffsetClampedX =
				!std::isfinite(eye.requestedManualOffset.x) ||
				eye.manualOffset.x != eye.requestedManualOffset.x;
			eye.manualOffsetClampedY =
				!std::isfinite(eye.requestedManualOffset.y) ||
				eye.manualOffset.y != eye.requestedManualOffset.y;

			eye.unclampedOffset = {
				eye.baseOffset.x + eye.anchorOffset.x + eye.manualOffset.x,
				eye.baseOffset.y + eye.anchorOffset.y + eye.manualOffset.y
			};
			eye.finalOffset = {
				std::clamp(
					eye.unclampedOffset.x,
					kResolvedOffsetMin,
					kResolvedOffsetMax),
				std::clamp(
					eye.unclampedOffset.y,
					kResolvedOffsetMin,
					kResolvedOffsetMax)
			};
			eye.finalOffsetClampedX =
				eye.finalOffset.x != eye.unclampedOffset.x;
			eye.finalOffsetClampedY =
				eye.finalOffset.y != eye.unclampedOffset.y;
		}

		return result;
	}
}
