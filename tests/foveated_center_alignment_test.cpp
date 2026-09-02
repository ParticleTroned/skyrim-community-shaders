#include "Features/Upscaling/FoveatedCenterAlignment.h"

#include <cassert>
#include <cmath>
#include <limits>
#include <string_view>

namespace
{
	bool NearlyEqual(float a_left, float a_right)
	{
		return std::abs(a_left - a_right) <= 1.0e-6f;
	}

	FoveatedCenterAlignment::ProjectionMatrixInput MakeProjection(
		float a_principalNdcX,
		float a_principalNdcY)
	{
		FoveatedCenterAlignment::ProjectionMatrixInput projection{};
		projection.available = true;
		projection.values[0] = 1.0f;
		projection.values[5] = 1.0f;
		projection.values[8] = a_principalNdcX;
		projection.values[9] = a_principalNdcY;
		projection.values[11] = 1.0f;
		return projection;
	}
}

int main()
{
	using namespace FoveatedCenterAlignment;
	static_assert(kCompatibilityCenterOrigin == CenterOrigin::ImageCenter);
	static_assert(
		kCompatibilityHorizontalAnchor == HorizontalAnchor::Outward);
	static_assert(std::string_view(GetCenterOriginName(
		CenterOrigin::OpticalCenter)) == "optical_center");
	static_assert(std::string_view(GetHorizontalAnchorName(
		HorizontalAnchor::Symmetric)) == "symmetric");

	std::array<EyeOpticalInputs, 2> opticalInputs{};
	const auto compatibility = ResolveStereo(Settings{}, opticalInputs);
	assert(compatibility.origin == CenterOrigin::ImageCenter);
	assert(compatibility.anchor == HorizontalAnchor::Outward);
	for (const auto& eye : compatibility.eyes) {
		assert(eye.source == OpticalCenterSource::ImageCenter);
		assert(
			eye.fallbackReason ==
			OpticalFallbackReason::OpticalOriginNotRequested);
		assert(NearlyEqual(eye.finalOffset.x, 0.0f));
		assert(NearlyEqual(eye.finalOffset.y, 0.0f));
	}

	Settings outwardSettings{};
	outwardSettings.centerHorizontalScale = 1.5f;
	const auto outward = ResolveStereo(outwardSettings, opticalInputs);
	assert(NearlyEqual(outward.eyes[0].anchorOffset.x, -0.15f));
	assert(NearlyEqual(outward.eyes[1].anchorOffset.x, 0.15f));
	assert(NearlyEqual(outward.eyes[0].finalOffset.x, -0.15f));
	assert(NearlyEqual(outward.eyes[1].finalOffset.x, 0.15f));

	Settings symmetricSettings = outwardSettings;
	symmetricSettings.anchor = HorizontalAnchor::Symmetric;
	const auto symmetric = ResolveStereo(symmetricSettings, opticalInputs);
	assert(NearlyEqual(symmetric.eyes[0].finalOffset.x, 0.0f));
	assert(NearlyEqual(symmetric.eyes[1].finalOffset.x, 0.0f));

	Settings opticalSettings{};
	opticalSettings.origin = CenterOrigin::OpticalCenter;
	opticalSettings.anchor = HorizontalAnchor::Symmetric;
	opticalInputs[0].projection = MakeProjection(0.20f, -0.10f);
	opticalInputs[1].projection = MakeProjection(-0.20f, 0.10f);
	const auto projectionResolved = ResolveStereo(opticalSettings, opticalInputs);
	assert(
		projectionResolved.eyes[0].source ==
		OpticalCenterSource::Projection);
	assert(
		projectionResolved.eyes[1].source ==
		OpticalCenterSource::Projection);
	assert(NearlyEqual(projectionResolved.eyes[0].baseCenterUV.x, 0.60f));
	assert(NearlyEqual(projectionResolved.eyes[0].baseCenterUV.y, 0.55f));
	assert(NearlyEqual(projectionResolved.eyes[1].baseCenterUV.x, 0.40f));
	assert(NearlyEqual(projectionResolved.eyes[1].baseCenterUV.y, 0.45f));

	opticalInputs[0].projection.values[11] = 0.0f;
	opticalInputs[0].tangents = {
		.available = true,
		.left = -1.0f,
		.right = 3.0f,
		.bottom = -2.0f,
		.top = 2.0f,
	};
	const auto tangentFallback = ResolveStereo(opticalSettings, opticalInputs);
	assert(tangentFallback.eyes[0].projectionValidity == InputValidity::Invalid);
	assert(tangentFallback.eyes[0].tangentValidity == InputValidity::Valid);
	assert(
		tangentFallback.eyes[0].source == OpticalCenterSource::Tangents);
	assert(
		tangentFallback.eyes[0].fallbackReason ==
		OpticalFallbackReason::ProjectionInvalid);
	assert(NearlyEqual(tangentFallback.eyes[0].baseCenterUV.x, 0.25f));
	assert(NearlyEqual(tangentFallback.eyes[0].baseCenterUV.y, 0.50f));

	opticalInputs[0].tangents.top = -2.0f;
	const auto imageFallback = ResolveStereo(opticalSettings, opticalInputs);
	assert(
		imageFallback.eyes[0].source ==
		OpticalCenterSource::ImageCenterFallback);
	assert(
		imageFallback.eyes[0].fallbackReason ==
		OpticalFallbackReason::NoValidOpticalInput);
	assert(NearlyEqual(imageFallback.eyes[0].baseCenterUV.x, 0.50f));
	assert(NearlyEqual(imageFallback.eyes[0].baseCenterUV.y, 0.50f));

	Settings clampedSettings{};
	clampedSettings.anchor = HorizontalAnchor::Symmetric;
	clampedSettings.manualOffsets[0] = { 4.0f, -4.0f };
	clampedSettings.manualOffsets[1] = {
		std::numeric_limits<float>::quiet_NaN(),
		0.10f,
	};
	const auto clamped = ResolveStereo(clampedSettings, {});
	assert(clamped.eyes[0].manualOffsetClampedX);
	assert(clamped.eyes[0].manualOffsetClampedY);
	assert(NearlyEqual(clamped.eyes[0].finalOffset.x, 0.30f));
	assert(NearlyEqual(clamped.eyes[0].finalOffset.y, -0.30f));
	assert(clamped.eyes[1].manualOffsetClampedX);
	assert(NearlyEqual(clamped.eyes[1].manualOffset.x, 0.0f));
	assert(NearlyEqual(clamped.eyes[1].finalOffset.y, 0.10f));

	Settings combinedSettings{};
	combinedSettings.origin = CenterOrigin::OpticalCenter;
	combinedSettings.anchor = HorizontalAnchor::Outward;
	combinedSettings.centerHorizontalScale = 1.5f;
	combinedSettings.manualOffsets[0] = { -0.10f, 0.0f };
	std::array<EyeOpticalInputs, 2> combinedInputs{};
	combinedInputs[0].projection = MakeProjection(-0.40f, 0.0f);
	const auto combined = ResolveStereo(combinedSettings, combinedInputs);
	assert(NearlyEqual(combined.eyes[0].baseOffset.x, -0.20f));
	assert(NearlyEqual(combined.eyes[0].anchorOffset.x, -0.15f));
	assert(NearlyEqual(combined.eyes[0].manualOffset.x, -0.10f));
	assert(NearlyEqual(combined.eyes[0].unclampedOffset.x, -0.45f));
	assert(NearlyEqual(combined.eyes[0].finalOffset.x, -0.30f));
	assert(combined.eyes[0].finalOffsetClampedX);

	Settings invalidSettings{};
	invalidSettings.origin = static_cast<CenterOrigin>(255u);
	invalidSettings.anchor = static_cast<HorizontalAnchor>(255u);
	invalidSettings.centerScale = std::numeric_limits<float>::infinity();
	invalidSettings.centerHorizontalScale =
		std::numeric_limits<float>::quiet_NaN();
	const auto sanitized = ResolveStereo(invalidSettings, {});
	assert(sanitized.origin == kCompatibilityCenterOrigin);
	assert(sanitized.anchor == kCompatibilityHorizontalAnchor);
	assert(NearlyEqual(sanitized.centerScale, 0.60f));
	assert(NearlyEqual(sanitized.centerHorizontalScale, 1.0f));
	return 0;
}
