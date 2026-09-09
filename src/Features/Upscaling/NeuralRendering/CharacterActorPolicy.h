#pragma once

#include "CharacterRegionPolicy.h"

#include <array>
#include <span>

namespace NeuralRendering
{
	enum class CharacterProjectionResult
	{
		Visible,
		Offscreen,
		Uncertain,
	};

	enum class CharacterProjectionReason
	{
		ProjectedBounds,
		ClippedBounds,
		BehindCamera,
		OutsideViewport,
		InvalidExtent,
		InvalidCornerCount,
		NonFiniteInput,
		UnsupportedClipTopology,
		DegenerateBounds,
	};

	[[nodiscard]] inline const char* CharacterProjectionReasonName(CharacterProjectionReason a_reason) noexcept
	{
		switch (a_reason) {
		case CharacterProjectionReason::ProjectedBounds:
			return "projected_bounds";
		case CharacterProjectionReason::ClippedBounds:
			return "clipped_bounds";
		case CharacterProjectionReason::BehindCamera:
			return "behind_camera";
		case CharacterProjectionReason::OutsideViewport:
			return "outside_viewport";
		case CharacterProjectionReason::InvalidExtent:
			return "invalid_extent";
		case CharacterProjectionReason::InvalidCornerCount:
			return "invalid_corner_count";
		case CharacterProjectionReason::NonFiniteInput:
			return "non_finite_input";
		case CharacterProjectionReason::UnsupportedClipTopology:
			return "unsupported_clip_topology";
		case CharacterProjectionReason::DegenerateBounds:
			return "degenerate_bounds";
		}
		return "unknown";
	}

	/** Projected homogeneous corners; IDs are never interpolated or packed here. */
	struct CharacterClipPoint
	{
		float x = 0.0f;
		float y = 0.0f;
		float w = 0.0f;
	};

	/**
	 * Eight corners describe an affine box, ordered with x/y/z in bits 0/1/2.
	 * Other corner sets can be projected when wholly in front, but have no known
	 * clipping topology. There is no clip z here: w=0 clips the camera hemisphere,
	 * not the renderer's near-depth plane, so the result remains conservative.
	 */
	[[nodiscard]] inline CharacterProjectionResult ResolveCharacterProjection(
		std::span<const CharacterClipPoint> a_corners,
		std::uint32_t a_width,
		std::uint32_t a_height,
		CharacterRect& a_rect,
		CharacterProjectionReason* a_reason = nullptr) noexcept
	{
		a_rect = {};
		const auto finish = [&](CharacterProjectionResult a_result, CharacterProjectionReason a_why) {
			if (a_reason)
				*a_reason = a_why;
			if (a_result == CharacterProjectionResult::Uncertain)
				a_rect = { 0u, 0u, a_width, a_height };
			return a_result;
		};
		if (!a_width || !a_height)
			return finish(CharacterProjectionResult::Uncertain, CharacterProjectionReason::InvalidExtent);
		if (a_corners.size() < 3)
			return finish(CharacterProjectionResult::Uncertain, CharacterProjectionReason::InvalidCornerCount);
		double minX = std::numeric_limits<double>::max();
		double minY = std::numeric_limits<double>::max();
		double maxX = std::numeric_limits<double>::lowest();
		double maxY = std::numeric_limits<double>::lowest();
		const auto include = [&](double a_x, double a_y, double a_w) {
			const double pixelX = (a_x / a_w * 0.5 + 0.5) * a_width;
			const double pixelY = (0.5 - a_y / a_w * 0.5) * a_height;
			minX = std::min(minX, pixelX);
			minY = std::min(minY, pixelY);
			maxX = std::max(maxX, pixelX);
			maxY = std::max(maxY, pixelY);
		};
		bool anyInFront = false;
		bool anyNearOrBehind = false;
		for (const auto& clip : a_corners) {
			if (!std::isfinite(clip.x) || !std::isfinite(clip.y) || !std::isfinite(clip.w))
				return finish(CharacterProjectionResult::Uncertain, CharacterProjectionReason::NonFiniteInput);
			anyInFront = anyInFront || clip.w > 0.0f;
			anyNearOrBehind = anyNearOrBehind || clip.w <= 0.0f;
		}
		if (!anyInFront)
			return finish(CharacterProjectionResult::Offscreen, CharacterProjectionReason::BehindCamera);
		if (!anyNearOrBehind) {
			// Linear-fractional extrema of a front-facing convex bound occur at
			// its vertices. Double arithmetic also handles small positive w without
			// inventing a near plane or overflowing intermediate pixel coordinates.
			for (const auto& clip : a_corners)
				include(clip.x, clip.y, clip.w);
		} else {
			if (a_corners.size() != 8)
				return finish(CharacterProjectionResult::Uncertain, CharacterProjectionReason::UnsupportedClipTopology);
			struct Point
			{
				double x, y, w;
				bool operator==(const Point&) const = default;
			};
			std::array<Point, 8> box{};
			for (std::size_t i = 0; i < box.size(); ++i)
				box[i] = { a_corners[i].x, a_corners[i].y, a_corners[i].w };
			// The fixed face topology is only valid for an affine box. Allow the
			// rounding of the caller's float matrix multiplication, not arbitrary
			// unordered points that could otherwise produce a false empty result.
			for (std::size_t i = 0; i < box.size(); ++i) {
				const auto affine = [&](auto member) {
					const double origin = box[0].*member;
					double expected = origin;
					double scale = std::abs(origin);
					for (std::size_t bit = 1; bit <= 4; bit <<= 1) {
						if (i & bit)
							expected += box[bit].*member - origin;
						scale = std::max(scale, std::abs(box[bit].*member));
					}
					scale = std::max(scale, std::abs(box[i].*member));
					return std::abs(expected - box[i].*member) <=
					       scale * (64.0 * std::numeric_limits<float>::epsilon());
				};
				if (!affine(&Point::x) || !affine(&Point::y) || !affine(&Point::w))
					return finish(CharacterProjectionResult::Uncertain, CharacterProjectionReason::UnsupportedClipTopology);
			}
			constexpr std::array<std::array<std::size_t, 4>, 6> faces{ {
				{ 0, 1, 3, 2 },
				{ 4, 5, 7, 6 },
				{ 0, 1, 5, 4 },
				{ 2, 3, 7, 6 },
				{ 0, 2, 6, 4 },
				{ 1, 3, 7, 5 },
			} };
			bool anyClippedPoint = false;
			bool anyProjectedPoint = false;
			for (const auto& face : faces) {
				// A convex quad gains at most one vertex per clipping plane.
				std::array<Point, 12> polygon{}, output{};
				std::size_t count = face.size();
				for (std::size_t i = 0; i < count; ++i)
					polygon[i] = box[face[i]];
				for (unsigned int plane = 0; plane < 5 && count; ++plane) {
					const auto distance = [plane](const Point& p) {
						switch (plane) {
						case 0:
							return p.w;
						case 1:
							return p.w + p.x;
						case 2:
							return p.w - p.x;
						case 3:
							return p.w + p.y;
						default:
							return p.w - p.y;
						}
					};
					std::size_t written = 0;
					const auto append = [&](const Point& p) {
						if (written && output[written - 1] == p)
							return true;
						if (written == output.size())
							return false;
						output[written++] = p;
						return true;
					};
					Point previous = polygon[count - 1];
					double previousDistance = distance(previous);
					for (std::size_t i = 0; i < count; ++i) {
						const Point current = polygon[i];
						const double currentDistance = distance(current);
						if ((previousDistance >= 0.0) != (currentDistance >= 0.0)) {
							const double t = previousDistance / (previousDistance - currentDistance);
							Point intersection = previousDistance == 0.0 ? previous : currentDistance == 0.0 ? current :
							                                                                                   Point{
																												   previous.x + t * (current.x - previous.x),
																												   previous.y + t * (current.y - previous.y),
																												   previous.w + t * (current.w - previous.w),
																											   };
							// Put the intersection exactly on its clipping plane.
							switch (plane) {
							case 0:
								intersection.w = 0.0;
								break;
							case 1:
								intersection.x = -intersection.w;
								break;
							case 2:
								intersection.x = intersection.w;
								break;
							case 3:
								intersection.y = -intersection.w;
								break;
							default:
								intersection.y = intersection.w;
								break;
							}
							if (!append(intersection))
								return finish(CharacterProjectionResult::Uncertain, CharacterProjectionReason::DegenerateBounds);
						}
						if (currentDistance >= 0.0 && !append(current))
							return finish(CharacterProjectionResult::Uncertain, CharacterProjectionReason::DegenerateBounds);
						previous = current;
						previousDistance = currentDistance;
					}
					if (written > 1 && output[0] == output[written - 1])
						--written;
					polygon = output;
					count = written;
				}
				anyClippedPoint = anyClippedPoint || count != 0;
				for (std::size_t i = 0; i < count; ++i) {
					if (polygon[i].w > 0.0) {
						include(polygon[i].x, polygon[i].y, polygon[i].w);
						anyProjectedPoint = true;
					}
				}
			}
			// Clipping the box faces also finds frustum edges through a face,
			// unlike clipping the twelve box edges alone. A viewport-containing
			// box therefore keeps the full eye, even with no visible original vertex.
			if (!anyProjectedPoint)
				return finish(anyClippedPoint ? CharacterProjectionResult::Uncertain : CharacterProjectionResult::Offscreen,
					anyClippedPoint ? CharacterProjectionReason::DegenerateBounds : CharacterProjectionReason::OutsideViewport);
		}
		if (maxX <= minX || maxY <= minY)
			return finish(CharacterProjectionResult::Uncertain, CharacterProjectionReason::DegenerateBounds);
		if (maxX <= 0.0f || maxY <= 0.0f || minX >= a_width || minY >= a_height)
			return finish(CharacterProjectionResult::Offscreen, CharacterProjectionReason::OutsideViewport);
		a_rect = {
			static_cast<std::uint32_t>(std::clamp(std::floor(minX), 0.0, static_cast<double>(a_width))),
			static_cast<std::uint32_t>(std::clamp(std::floor(minY), 0.0, static_cast<double>(a_height))),
			static_cast<std::uint32_t>(std::clamp(std::ceil(maxX), 0.0, static_cast<double>(a_width))),
			static_cast<std::uint32_t>(std::clamp(std::ceil(maxY), 0.0, static_cast<double>(a_height))),
		};
		return a_rect.IsValid() ?
		           finish(CharacterProjectionResult::Visible, anyNearOrBehind ? CharacterProjectionReason::ClippedBounds : CharacterProjectionReason::ProjectedBounds) :
		           finish(CharacterProjectionResult::Uncertain, CharacterProjectionReason::DegenerateBounds);
	}

	/** Cover both stable lookup coordinates and the actual jittered raster. */
	[[nodiscard]] inline CharacterProjectionResult ResolveCharacterProjectionPair(
		std::span<const CharacterClipPoint> a_unjitteredCorners,
		std::span<const CharacterClipPoint> a_jitteredCorners,
		std::uint32_t a_width,
		std::uint32_t a_height,
		CharacterRect& a_rect,
		CharacterProjectionReason* a_reason = nullptr) noexcept
	{
		CharacterRect unjitteredRect{}, jitteredRect{};
		CharacterProjectionReason unjitteredReason{}, jitteredReason{};
		const auto unjittered = ResolveCharacterProjection(a_unjitteredCorners, a_width, a_height, unjitteredRect, &unjitteredReason);
		const auto jittered = ResolveCharacterProjection(a_jitteredCorners, a_width, a_height, jitteredRect, &jitteredReason);
		const auto finish = [&](CharacterProjectionResult a_result, CharacterProjectionReason a_why) {
			if (a_reason)
				*a_reason = a_why;
			return a_result;
		};
		// A valid projection cannot prove that the other, invalid transform has
		// no visible pixels. Preserve its conservative fallback and explanation.
		if (unjittered == CharacterProjectionResult::Uncertain) {
			a_rect = unjitteredRect;
			return finish(unjittered, unjitteredReason);
		}
		if (jittered == CharacterProjectionResult::Uncertain) {
			a_rect = jitteredRect;
			return finish(jittered, jitteredReason);
		}
		a_rect = CharacterRegionPolicy::Union(unjitteredRect, jitteredRect);
		if (unjittered == CharacterProjectionResult::Offscreen && jittered == CharacterProjectionResult::Offscreen)
			return finish(CharacterProjectionResult::Offscreen,
				unjitteredReason == CharacterProjectionReason::BehindCamera && jitteredReason == CharacterProjectionReason::BehindCamera ?
					CharacterProjectionReason::BehindCamera :
					CharacterProjectionReason::OutsideViewport);
		return finish(CharacterProjectionResult::Visible,
			unjitteredReason == CharacterProjectionReason::ClippedBounds || jitteredReason == CharacterProjectionReason::ClippedBounds ?
				CharacterProjectionReason::ClippedBounds :
				CharacterProjectionReason::ProjectedBounds);
	}

	struct CharacterActorAdmissionState
	{
		std::uint32_t lastSizeEligibleFrame = 0;
		bool sizeEligible = false;
		bool adaptiveSelected = false;
	};

	/** One actor-wide decision, made before any of its categories are authored. */
	[[nodiscard]] inline bool ResolveCharacterActorAdmission(
		std::uint32_t a_frame,
		std::uint32_t a_facePixelSize,
		float a_distanceMeters,
		bool a_projectionUncertain,
		std::uint32_t a_minimumFaceSize,
		std::uint32_t a_holdFrames,
		bool a_adaptive,
		CharacterActorAdmissionState& a_state) noexcept
	{
		if (a_projectionUncertain) {
			// Missing bounds may cost extra work, but cannot erase valid materials.
			a_state.sizeEligible = true;
			a_state.lastSizeEligibleFrame = a_frame;
			a_state.adaptiveSelected = true;
			return true;
		}
		const auto threshold = a_state.sizeEligible ?
		                           CharacterRegionPolicy::ResolveFaceSizeExitThreshold(a_minimumFaceSize) :
		                           a_minimumFaceSize;
		if (a_facePixelSize >= threshold) {
			a_state.sizeEligible = true;
			a_state.lastSizeEligibleFrame = a_frame;
		} else if (!a_state.sizeEligible ||
				   static_cast<std::uint32_t>(a_frame - a_state.lastSizeEligibleFrame) > a_holdFrames) {
			a_state.sizeEligible = false;
		}
		a_state.adaptiveSelected = a_state.sizeEligible &&
		                           (!a_adaptive || CharacterRegionPolicy::IsDetailRelevantWithHysteresis(
													   a_distanceMeters, a_facePixelSize, a_state.adaptiveSelected));
		return a_state.sizeEligible && a_state.adaptiveSelected;
	}
}
