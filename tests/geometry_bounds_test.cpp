#include "Features/Upscaling/NeuralRendering/CharacterComputeSubrect.h"
#include "Utils/GeometryBounds.h"

#include <random>
#include <stdexcept>

namespace
{
	void Require(bool a_condition)
	{
		if (!a_condition)
			throw std::runtime_error("Animated bounds lost conservative coverage");
	}

	bool Contains(const Util::GeometryBounds& a_box, const std::array<double, 3>& a_point)
	{
		for (std::size_t axis = 0; axis < 3; ++axis)
			if (a_point[axis] < a_box.minimum[axis] || a_point[axis] > a_box.maximum[axis])
				return false;
		return true;
	}
}

int main()
{
	const std::array<std::array<float, 3>, 3> identity{ { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } } };
	Require(!Util::GeometryBounds{}.IsValid());
	for (const float invalid : { 0.0f, -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() })
		Require(!Util::TransformGeometrySphere({}, invalid, identity, {}, 1.0f).IsValid());
	Require(!Util::TransformGeometrySphere({}, 1.0f, identity, {}, 0.0f).IsValid());
	Require(!Util::TransformGeometrySphere({}, 2.0f, identity, {}, std::numeric_limits<float>::max()).IsValid());

	// Random affine poses include rotations, shears, reflections and nonuniform matrices.
	std::mt19937 engine(6183);
	std::uniform_real_distribution<float> random(-2.0f, 2.0f);
	for (unsigned pose = 0; pose < 200; ++pose) {
		std::array<std::array<float, 3>, 3> rotation{};
		for (auto& row : rotation)
			for (auto& value : row)
				value = random(engine);
		const std::array<float, 3> center{ random(engine), random(engine), random(engine) };
		const std::array<float, 3> translation{ random(engine) * 10000, random(engine) * 10000, random(engine) * 10000 };
		const float scale = pose & 1 ? -1.7f : 0.3f;
		const auto box = Util::TransformGeometrySphere(center, 2.0f, rotation, translation, scale);
		Require(box.IsValid());
		for (unsigned sample = 0; sample < 100; ++sample) {
			std::array<double, 3> direction{ random(engine), random(engine), random(engine) };
			const auto length = std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
			std::array<double, 3> point{};
			for (std::size_t row = 0; row < 3; ++row) {
				point[row] = translation[row];
				for (std::size_t column = 0; column < 3; ++column)
					point[row] += rotation[row][column] * (center[column] + 2.0 * direction[column] / length) * scale;
			}
			Require(Contains(box, point));
		}
	}

	// A convex enclosure includes skin blended between separated bones, not just their centers.
	auto skin = Util::TransformGeometrySphere({}, 2.0f, identity, { -20, 0, 0 }, 1.0f);
	skin.Include(Util::TransformGeometrySphere({}, 2.0f, identity, { 20, 0, 0 }, 1.0f));
	for (unsigned weight = 0; weight <= 100; ++weight)
		Require(Contains(skin, { -22.0 + 44.0 * weight / 100.0, 1.0, 0.0 }));
	Require(skin.maximum[1] - skin.minimum[1] < 4.01f);
	Require(skin.maximum[0] - skin.minimum[0] > 44.0f);
	Require(skin.NearestDistance({ 0, 0, 0 }) == 0.0f);
	Require(std::abs(skin.NearestDistance({ 0, 5, 6 }) - 5.0f) < 0.001f);
	Require(skin.NearestDistance({ std::numeric_limits<float>::quiet_NaN(), 0, 0 }) == 0.0f);
	Require(Util::GeometryBounds{}.NearestDistance({ 1, 2, 3 }) == 0.0f);

	Util::GeometryBoundsBudget budget;
	Require(!budget.Reserve(0));
	Require(!budget.Reserve(257));
	for (unsigned mesh = 0; mesh < 16; ++mesh)
		Require(budget.Reserve(256));
	Require(!budget.Reserve(1));
	Require(budget.reserved == 4096);
	budget = {};
	Require(budget.Reserve(1));
	const auto fallback = Util::GeometryBounds::FromCenterExtent({ 0, 0, 0 }, { 50, 50, 50 });
	unsigned calls = 0;
	bool refined = true;
	const auto incomplete = Util::RefineGeometryBounds(fallback, 3, budget, [&](unsigned index) noexcept {
		++calls;
		return index == 1 ? Util::GeometryBounds{} : skin; }, refined);
	Require(!refined && incomplete.minimum == fallback.minimum && incomplete.maximum == fallback.maximum);
	Require(calls == 2 && budget.reserved == 4);
	const auto complete = Util::RefineGeometryBounds(fallback, 3, budget, [&](unsigned) noexcept { return skin; }, refined);
	Require(refined && complete.minimum == skin.minimum && complete.maximum == skin.maximum);
	budget.reserved = Util::GeometryBoundsBudget::kMaximumBonesPerFrame;
	calls = 0;
	const auto exhausted = Util::RefineGeometryBounds(fallback, 1, budget, [&](unsigned) noexcept { ++calls; return skin; }, refined);
	Require(!refined && calls == 0 && exhausted.minimum == fallback.minimum && exhausted.maximum == fallback.maximum);

	// Pose changes grow immediately. Tight poses cannot force an immediate history shrink.
	using namespace NeuralRendering;
	Require(ResolveCharacterMaskFootprintGuard(1024, 2048, 4, 0.5f) == 13.0f);
	Require(ResolveCharacterMaskFootprintGuard(1024, 1024, 4, 0.0f) == 6.0f);
	Require(ResolveCharacterMaskFootprintGuard(1024, 1024, 0, 0.0f) == 2.0f);
	Require(ResolveCharacterMaskFootprintGuard(0, 1024, 4, 0.0f) == 1024.0f);
	Require(ResolveCharacterMaskFootprintGuard(1024, 1024, 4, std::numeric_limits<float>::quiet_NaN()) == 1024.0f);
	StableCharacterComputeSubrect stable;
	const ComputeSubrect small{ 400, 400, 100, 100 };
	const auto first = ResolveStableCharacterComputeSubrect(small, 2048, 2048, stable);
	const ComputeSubrect reach{ 100, 200, 850, 800 };
	const auto grown = ResolveStableCharacterComputeSubrect(reach, 2048, 2048, stable);
	Require(grown.baseX <= reach.baseX && grown.baseY <= reach.baseY);
	Require(grown.baseX + grown.width >= reach.baseX + reach.width);
	Require(grown.baseY + grown.height >= reach.baseY + reach.height);
	const auto retained = ResolveStableCharacterComputeSubrect(small, 2048, 2048, stable);
	Require(retained == grown);
	ComputeSubrect settled;
	for (unsigned frame = 0; frame < 100; ++frame)
		settled = ResolveStableCharacterComputeSubrect(small, 2048, 2048, stable);
	Require(settled == first);
	return 0;
}
