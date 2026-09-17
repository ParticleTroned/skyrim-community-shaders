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

	void AssertVisibleMapsIntoWork(const FoveatedRegionPlan::Eye& a_eye)
	{
		Require(Contains(a_eye.output, a_eye.visibleOutput));
		const auto sourceOffsetX =
			a_eye.visibleOutput.minX - a_eye.output.minX;
		const auto sourceOffsetY =
			a_eye.visibleOutput.minY - a_eye.output.minY;
		Require(sourceOffsetX + a_eye.visibleOutput.Width() <=
				a_eye.output.Width());
		Require(sourceOffsetY + a_eye.visibleOutput.Height() <=
				a_eye.output.Height());
	}
}

int main()
{
	const std::array<float2, 2> offsets{
		float2{ -0.04f, 0.02f },
		float2{ 0.04f, 0.02f }
	};
	const auto baseline = FoveatedRegionPlan::Build(
		960u, 1080u, 1920u, 2160u, true,
		0.60f, 0.05f, 1.20f, offsets);
	const auto guarded = FoveatedRegionPlan::Build(
		960u, 1080u, 1920u, 2160u, true,
		0.60f, 0.05f, 1.20f, offsets,
		0u, 0.0f, 16u);

	Require(baseline.IsValid());
	Require(guarded.IsValid());
	Require(baseline.reconstructionGuardBandPixels == 0u);
	Require(guarded.reconstructionGuardBandPixels == 16u);
	for (std::size_t eyeIndex = 0; eyeIndex < guarded.eyes.size(); ++eyeIndex) {
		const auto& baselineEye = baseline.eyes[eyeIndex];
		const auto& guardedEye = guarded.eyes[eyeIndex];
		Require(Equal(baselineEye.visibleOutput, guardedEye.visibleOutput));
		Require(Equal(baselineEye.output, baselineEye.visibleOutput));
		AssertVisibleMapsIntoWork(guardedEye);
		Require(guardedEye.output.minX + 16u == guardedEye.visibleOutput.minX);
		Require(guardedEye.output.minY + 16u == guardedEye.visibleOutput.minY);
		Require(guardedEye.output.maxX == guardedEye.visibleOutput.maxX + 16u);
		Require(guardedEye.output.maxY == guardedEye.visibleOutput.maxY + 16u);
		Require(Contains(guardedEye.input, baselineEye.input));
		Require(Equal(
			baselineEye.centerInteriorOutput,
			guardedEye.centerInteriorOutput));
		Require(Equal(
			baselineEye.centerUnderlayHoleOutput,
			guardedEye.centerUnderlayHoleOutput));
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
		Require(Contains(wideEye.visibleOutput, narrowEye.visibleOutput));
		Require(!Equal(wideEye.visibleOutput, narrowEye.visibleOutput));
	}

	const auto edgeClamped = FoveatedRegionPlan::Build(
		960u, 1080u, 1920u, 2160u, false,
		0.60f, 0.05f, 1.0f,
		{ float2{ -0.30f, -0.30f }, float2{} },
		0u, 0.0f, 128u);
	Require(edgeClamped.IsValid());
	const auto& edgeEye = edgeClamped.eyes[0];
	Require(edgeEye.output.minX == 0u);
	Require(edgeEye.output.minY == 0u);
	Require(edgeEye.visibleOutput.minX - edgeEye.output.minX < 128u);
	Require(edgeEye.visibleOutput.minY - edgeEye.output.minY < 128u);
	AssertVisibleMapsIntoWork(edgeEye);
	return 0;
}
