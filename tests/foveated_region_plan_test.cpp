struct float2
{
	float x = 0.0f;
	float y = 0.0f;
};

#include "Features/Upscaling/FoveatedRegionPlan.h"

#include <array>
#include <cstddef>
#include <cstdlib>

namespace
{
	void Require(bool a_condition)
	{
		if (!a_condition)
			std::abort();
	}

	bool Equal(
		const FoveatedRegionPlan::Rect& a_left,
		const FoveatedRegionPlan::Rect& a_right)
	{
		return a_left.minX == a_right.minX &&
		       a_left.minY == a_right.minY &&
		       a_left.maxX == a_right.maxX &&
		       a_left.maxY == a_right.maxY;
	}

	bool Contains(
		const FoveatedRegionPlan::Rect& a_outer,
		const FoveatedRegionPlan::Rect& a_inner)
	{
		return a_outer.minX <= a_inner.minX &&
		       a_outer.minY <= a_inner.minY &&
		       a_outer.maxX >= a_inner.maxX &&
		       a_outer.maxY >= a_inner.maxY;
	}
}

int main()
{
	Require(FoveatedRegionPlan::Rect{ 0u, 0u, 9u, 5u }.CoversExtent(9u, 5u));
	Require(!FoveatedRegionPlan::Rect{}.CoversExtent(0u, 0u));
	Require(!FoveatedRegionPlan::Rect{ 1u, 0u, 9u, 5u }.CoversExtent(9u, 5u));
	Require(!FoveatedRegionPlan::Rect{ 0u, 1u, 9u, 5u }.CoversExtent(9u, 5u));
	Require(!FoveatedRegionPlan::Rect{ 0u, 0u, 8u, 5u }.CoversExtent(9u, 5u));
	Require(!FoveatedRegionPlan::Rect{ 0u, 0u, 9u, 4u }.CoversExtent(9u, 5u));
	Require(!FoveatedRegionPlan::Rect{ 0u, 0u, 10u, 5u }.CoversExtent(9u, 5u));
	const auto fullImage = FoveatedRegionPlan::Build(9u, 5u, 17u, 11u, true,
		1.0f, 0.0f, 1.0f, {});
	Require(fullImage.IsValid());
	for (const auto& eye : fullImage.eyes) {
		Require(eye.output.CoversExtent(17u, 11u));
	}
	const std::array<float2, 2> offsets{
		float2{ -0.04f, 0.02f },
		float2{ 0.04f, 0.02f }
	};
	const auto baseline = FoveatedRegionPlan::Build(
		960u, 1080u, 1920u, 2160u, true,
		0.60f, 0.05f, 1.20f, offsets);
	Require(baseline.IsValid());
	for (const auto& eye : baseline.eyes) {
		Require(!eye.output.CoversExtent(1920u, 2160u));
		Require(Contains(eye.output, eye.centerInteriorOutput));
		Require(Contains(eye.centerInteriorOutput, eye.centerUnderlayHoleOutput));
		Require(Equal(eye.input, eye.encodeInput));
		const auto expected = FoveatedCommon::BuildCenteredDispatchBounds(
			0u, 1920u, 2160u, 0.60f, eye.centerOffset.x, eye.centerOffset.y, 0.05f, 1.20f);
		Require(Equal(eye.output, { static_cast<uint32_t>(expected.minX),
									  static_cast<uint32_t>(expected.minY), static_cast<uint32_t>(expected.maxX),
									  static_cast<uint32_t>(expected.maxY) }));
	}

	const auto narrowFeather = FoveatedRegionPlan::Build(
		960u, 1080u, 1920u, 2160u, true,
		0.60f, 0.02f, 1.20f, offsets);
	const auto wideFeather = FoveatedRegionPlan::Build(
		960u, 1080u, 1920u, 2160u, true,
		0.60f, 0.08f, 1.20f, offsets);
	Require(narrowFeather.IsValid());
	Require(wideFeather.IsValid());
	for (std::size_t eyeIndex = 0; eyeIndex < wideFeather.eyes.size(); ++eyeIndex) {
		const auto& narrowEye = narrowFeather.eyes[eyeIndex];
		const auto& wideEye = wideFeather.eyes[eyeIndex];
		Require(Contains(wideEye.output, narrowEye.output));
		Require(!Equal(wideEye.output, narrowEye.output));
	}

	const auto edgeClamped = FoveatedRegionPlan::Build(
		960u, 1080u, 1920u, 2160u, false,
		0.60f, 0.05f, 1.0f,
		{ float2{ -0.30f, -0.30f }, float2{} },
		0u, 0.0f);
	Require(edgeClamped.IsValid());
	const auto& edgeEye = edgeClamped.eyes[0];
	Require(edgeEye.output.minX == 0u);
	Require(edgeEye.output.minY == 0u);
	Require(edgeEye.input.minX == 0u && edgeEye.input.minY == 0u);
	const auto taa = FoveatedRegionPlan::Build(
		960u, 1080u, 1920u, 2160u, true, 0.35f, 0.05f, 1.20f, offsets, 0u, 0.80f);
	Require(taa.IsValid());
	for (const auto& eye : taa.eyes) {
		Require(Contains(eye.peripheryTAAOuterOutput, eye.output));
		Require(Contains(eye.peripheryTAAHistoryOutput, eye.peripheryTAAOuterOutput));
		Require(Contains(eye.encodeInput, eye.input));
		Require(Contains(eye.encodeInput, eye.peripheryTAAOuterInput));
	}
	return 0;
}
