#include "Features/Upscaling/NeuralRendering/CharacterActorPolicy.h"

#include <array>
#include <limits>
#include <random>

namespace
{
	using namespace NeuralRendering;

	std::array<CharacterClipPoint, 8> Box(
		CharacterClipPoint a_center, CharacterClipPoint a_x,
		CharacterClipPoint a_y, CharacterClipPoint a_z)
	{
		std::array<CharacterClipPoint, 8> result{};
		for (std::size_t i = 0; i < result.size(); ++i) {
			const float x = i & 1 ? 1.0f : -1.0f;
			const float y = i & 2 ? 1.0f : -1.0f;
			const float z = i & 4 ? 1.0f : -1.0f;
			result[i] = {
				a_center.x + x * a_x.x + y * a_y.x + z * a_z.x,
				a_center.y + x * a_x.y + y * a_y.y + z * a_z.y,
				a_center.w + x * a_x.w + y * a_y.w + z * a_z.w,
			};
		}
		return result;
	}

	bool ProjectionPairTests()
	{
		const std::array<CharacterClipPoint, 4> outside{
			CharacterClipPoint{ 1.001f, -0.5f, 1 },
			CharacterClipPoint{ 1.5f, -0.5f, 1 },
			CharacterClipPoint{ 1.001f, 0.5f, 1 },
			CharacterClipPoint{ 1.5f, 0.5f, 1 },
		};
		auto shifted = outside;
		for (auto& p : shifted)
			p.x -= 0.002f;
		CharacterRect rect{};
		CharacterProjectionReason reason{};
		const auto project = [&](const auto& stable, const auto& jittered) {
			return ResolveCharacterProjectionPair(stable, jittered, 100, 200, rect, &reason);
		};
		// Either coordinate system can contain the only visible edge sliver.
		if (project(outside, shifted) != CharacterProjectionResult::Visible ||
			rect != CharacterRect{ 99, 50, 100, 150 } || reason != CharacterProjectionReason::ProjectedBounds)
			return false;
		if (project(shifted, outside) != CharacterProjectionResult::Visible ||
			rect != CharacterRect{ 99, 50, 100, 150 })
			return false;
		const auto leftEyeRect = rect;
		if (project(outside, outside) != CharacterProjectionResult::Offscreen || rect.IsValid() ||
			reason != CharacterProjectionReason::OutsideViewport || !leftEyeRect.IsValid())
			return false;
		auto invalid = shifted;
		invalid[0].x = std::numeric_limits<float>::quiet_NaN();
		if (project(invalid, shifted) != CharacterProjectionResult::Uncertain ||
			reason != CharacterProjectionReason::NonFiniteInput || rect != CharacterRect{ 0, 0, 100, 200 })
			return false;
		if (project(shifted, invalid) != CharacterProjectionResult::Uncertain ||
			reason != CharacterProjectionReason::NonFiniteInput || rect != CharacterRect{ 0, 0, 100, 200 })
			return false;
		if (project(invalid, outside) != CharacterProjectionResult::Uncertain ||
			project(outside, invalid) != CharacterProjectionResult::Uncertain)
			return false;
		const auto rightBox = Box({ 1, 0, 0 }, { 0.5f, 0, 0 }, { 0, 0.25f, 0 }, { 0, 0, 1 });
		auto leftBox = rightBox;
		for (auto& p : leftBox)
			p.x = -p.x;
		// Both valid rectangles contribute; a clipped projection retains its
		// reason even when the other projection uses the ordinary fast path.
		if (project(rightBox, leftBox) != CharacterProjectionResult::Visible ||
			rect != CharacterRect{ 0, 50, 100, 150 } || reason != CharacterProjectionReason::ClippedBounds)
			return false;
		if (project(shifted, rightBox) != CharacterProjectionResult::Visible ||
			rect != CharacterRect{ 75, 50, 100, 150 } || reason != CharacterProjectionReason::ClippedBounds)
			return false;
		return true;
	}

	bool ProjectionClippingTests()
	{
		CharacterRect rect{};
		CharacterProjectionReason reason{};
		const auto project = [&](const auto& points) {
			return ResolveCharacterProjection(points, 100, 200, rect, &reason);
		};
		// A behind-camera corner is not a reason to admit the entire eye.
		auto box = Box({ 1, 0, 0 }, { 0.5f, 0, 0 }, { 0, 0.25f, 0 }, { 0, 0, 1 });
		if (project(box) != CharacterProjectionResult::Visible ||
			reason != CharacterProjectionReason::ClippedBounds ||
			rect != CharacterRect{ 75, 50, 100, 150 })
			return false;
		const auto rightEye = rect;
		for (auto& p : box)
			p.x = -p.x;
		if (project(box) != CharacterProjectionResult::Visible ||
			rect != CharacterRect{ 0, 50, 25, 150 } || rect.Area() != rightEye.Area())
			return false;
		box = Box({ 2.5f, 0, 0 }, { 0.5f, 0, 0 }, { 0, 0.25f, 0 }, { 0, 0, 1 });
		if (project(box) != CharacterProjectionResult::Offscreen || rect.IsValid() ||
			reason != CharacterProjectionReason::OutsideViewport)
			return false;
		// No original box vertex or edge is in the viewport. Clipping whole
		// faces must still find the frustum/face intersections and cover it all.
		box = Box({ 0, 0, 0 }, { 4, 0, 0 }, { 0, 4, 0 }, { 0, 0, 1 });
		if (project(box) != CharacterProjectionResult::Visible ||
			rect != CharacterRect{ 0, 0, 100, 200 })
			return false;
		// The camera lies on a box edge: the w=0 apex is singular, but the
		// surviving positive-w faces prove this quadrant rather than full-eye.
		box = Box({ 0.5f, 0.5f, 0 }, { 0.5f, 0, 0 }, { 0, 0.5f, 0 }, { 0, 0, 1 });
		if (project(box) != CharacterProjectionResult::Visible ||
			rect != CharacterRect{ 50, 0, 100, 100 })
			return false;
		// Exact clip-plane endpoints must not accumulate duplicate polygon
		// vertices or trigger the fixed-capacity numerical safety fallback.
		for (int x = -1; x <= 1; ++x)
			for (int y = -1; y <= 1; ++y) {
				box = Box({ static_cast<float>(x), static_cast<float>(y), 0 },
					{ 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 });
				if (project(box) != CharacterProjectionResult::Visible ||
					reason != CharacterProjectionReason::ClippedBounds)
					return false;
			}
		// Projection is invariant under positive homogeneous scale. An epsilon
		// near plane would incorrectly discard these real, small-w visible rays.
		box = Box({ 1, 0, 0 }, { 0.5f, 0, 0 }, { 0, 0.25f, 0 }, { 0, 0, 1 });
		for (auto& p : box) {
			p.x *= 1.0e-8f;
			p.y *= 1.0e-8f;
			p.w *= 1.0e-8f;
		}
		if (project(box) != CharacterProjectionResult::Visible ||
			rect != CharacterRect{ 75, 50, 100, 150 })
			return false;
		// A subpixel sliver must survive until outward integer rounding.
		box = Box({ 1.05f, 0, 0 }, { 0.05001f, 0, 0 }, { 0, 0.25f, 0 }, { 0, 0, 1 });
		if (project(box) != CharacterProjectionResult::Visible || rect.minX != 99 || rect.maxX != 100)
			return false;
		box[7].x += 0.5f;
		if (project(box) != CharacterProjectionResult::Uncertain ||
			reason != CharacterProjectionReason::UnsupportedClipTopology)
			return false;
		box[7].w = std::numeric_limits<float>::infinity();
		if (project(box) != CharacterProjectionResult::Uncertain ||
			reason != CharacterProjectionReason::NonFiniteInput)
			return false;
		box.fill({ 0, 0, 1 });
		if (project(box) != CharacterProjectionResult::Uncertain ||
			reason != CharacterProjectionReason::DegenerateBounds)
			return false;
		if (ResolveCharacterProjection(box, 0, 200, rect, &reason) != CharacterProjectionResult::Uncertain ||
			reason != CharacterProjectionReason::InvalidExtent)
			return false;
		if (ResolveCharacterProjection({}, 100, 200, rect, &reason) != CharacterProjectionResult::Uncertain ||
			reason != CharacterProjectionReason::InvalidCornerCount)
			return false;

		// Deterministic adversarial affine boxes: oblique faces, both eyes,
		// behind-camera crossings, offscreen and fully front-facing placements.
		// Every sampled visible volume point must be enclosed by the result.
		std::mt19937 random(0xC1A0);
		std::uniform_real_distribution<float> distribution(-2.0f, 2.0f);
		std::size_t visibleSamples = 0;
		for (int trial = 0; trial < 160; ++trial) {
			const CharacterClipPoint center{ distribution(random), distribution(random), distribution(random) };
			const CharacterClipPoint x{ distribution(random), distribution(random), distribution(random) };
			const CharacterClipPoint y{ distribution(random), distribution(random), distribution(random) };
			const CharacterClipPoint z{ distribution(random), distribution(random), distribution(random) };
			for (int eye = -1; eye <= 1; eye += 2) {
				box = Box(center, x, y, z);
				for (auto& p : box)
					p.x += eye * 0.03125f * p.w;
				const auto result = project(box);
				if (result == CharacterProjectionResult::Uncertain)
					return false;
				for (int ix = 0; ix <= 8; ++ix)
					for (int iy = 0; iy <= 8; ++iy)
						for (int iz = 0; iz <= 8; ++iz) {
							// Trilinear interpolation is a convex combination of the
							// exact float corners passed to the projection function.
							double px = 0, py = 0, pw = 0;
							for (std::size_t i = 0; i < box.size(); ++i) {
								const double weight = (i & 1 ? ix : 8 - ix) *
								                      (i & 2 ? iy : 8 - iy) *
								                      (i & 4 ? iz : 8 - iz) / 512.0;
								px += weight * box[i].x;
								py += weight * box[i].y;
								pw += weight * box[i].w;
							}
							if (pw <= 0 || std::abs(px) >= pw || std::abs(py) >= pw)
								continue;
							++visibleSamples;
							px = (px / pw * 0.5 + 0.5) * 100;
							py = (0.5 - py / pw * 0.5) * 200;
							if (result != CharacterProjectionResult::Visible ||
								px < rect.minX - 1.0e-7 || px > rect.maxX + 1.0e-7 ||
								py < rect.minY - 1.0e-7 || py > rect.maxY + 1.0e-7)
								return false;
						}
			}
		}
		return visibleSamples > 1000;
	}
}

int main()
{
	using namespace NeuralRendering;
	std::array<CharacterClipPoint, 4> corners{
		CharacterClipPoint{ -0.5f, -0.5f, 1.0f },
		CharacterClipPoint{ 0.5f, -0.5f, 1.0f },
		CharacterClipPoint{ -0.5f, 0.5f, 1.0f },
		CharacterClipPoint{ 0.5f, 0.5f, 1.0f },
	};
	CharacterRect rect{};
	if (ResolveCharacterProjection(corners, 100, 200, rect) != CharacterProjectionResult::Visible ||
		rect != CharacterRect{ 25, 50, 75, 150 })
		return 1;
	const auto visibleCorners = corners;
	for (auto& corner : corners)
		corner.x += 3.0f;
	if (ResolveCharacterProjection(corners, 100, 200, rect) != CharacterProjectionResult::Offscreen || rect.IsValid())
		return 2;
	// A stereo eye with a known-offscreen bound does not require the full eye.
	CharacterRect otherEye{};
	if (ResolveCharacterProjection(visibleCorners, 100, 200, otherEye) != CharacterProjectionResult::Visible ||
		rect.IsValid())
		return 3;
	corners = visibleCorners;
	for (auto& corner : corners)
		corner.w = -1.0f;
	if (ResolveCharacterProjection(corners, 100, 200, rect) != CharacterProjectionResult::Offscreen)
		return 4;
	corners[0].w = 1.0f;
	if (ResolveCharacterProjection(corners, 100, 200, rect) != CharacterProjectionResult::Uncertain ||
		rect != CharacterRect{ 0, 0, 100, 200 })
		return 5;
	corners = visibleCorners;
	corners[0].x = std::numeric_limits<float>::quiet_NaN();
	if (ResolveCharacterProjection(corners, 100, 200, rect) != CharacterProjectionResult::Uncertain || !rect.IsValid())
		return 6;
	corners = visibleCorners;
	for (auto& corner : corners)
		corner.w = 1.0e-6f;
	if (ResolveCharacterProjection(corners, 100, 200, rect) != CharacterProjectionResult::Visible ||
		rect != CharacterRect{ 0, 0, 100, 200 })
		return 7;
	if (!ProjectionClippingTests())
		return 15;
	if (!ProjectionPairTests())
		return 16;

	CharacterActorAdmissionState actorA{};
	CharacterActorAdmissionState actorB{};
	if (ResolveCharacterActorAdmission(100, 63, 2.0f, false, 64, 3, false, actorA) ||
		!ResolveCharacterActorAdmission(101, 64, 2.0f, false, 64, 3, false, actorA) ||
		!ResolveCharacterActorAdmission(102, 48, 2.0f, false, 64, 3, false, actorA))
		return 8;
	// Separate actors cannot borrow the admitted actor's size hysteresis.
	if (ResolveCharacterActorAdmission(102, 48, 2.0f, false, 64, 3, false, actorB))
		return 9;
	if (!ResolveCharacterActorAdmission(105, 47, 2.0f, false, 64, 3, false, actorA) ||
		ResolveCharacterActorAdmission(106, 47, 2.0f, false, 64, 3, false, actorA))
		return 10;
	actorA = {};
	if (ResolveCharacterActorAdmission(1, 95, 10.0f, false, 1, 0, true, actorA) ||
		!ResolveCharacterActorAdmission(2, 96, 10.0f, false, 1, 0, true, actorA) ||
		!ResolveCharacterActorAdmission(3, 72, 10.0f, false, 1, 0, true, actorA) ||
		ResolveCharacterActorAdmission(4, 71, 10.0f, false, 1, 0, true, actorA))
		return 11;
	actorA = {};
	if (!ResolveCharacterActorAdmission(1, 20, 8.0f, false, 1, 0, true, actorA) ||
		!ResolveCharacterActorAdmission(2, 20, 9.0f, false, 1, 0, true, actorA) ||
		ResolveCharacterActorAdmission(3, 20, 9.01f, false, 1, 0, true, actorA))
		return 12;
	actorA = {};
	if (!ResolveCharacterActorAdmission(0xFFFFFFFEu, 64, 2.0f, false, 64, 2, false, actorA) ||
		!ResolveCharacterActorAdmission(0, 1, 2.0f, false, 64, 2, false, actorA) ||
		ResolveCharacterActorAdmission(1, 1, 2.0f, false, 64, 2, false, actorA))
		return 13;
	actorA = {};
	if (!ResolveCharacterActorAdmission(1, 0, 0.0f, true, 4096, 0, true, actorA))
		return 14;
	return 0;
}
