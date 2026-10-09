#pragma once

#include "AdaptiveBalanceAppearance.h"

// Composed renderer-light state shared by Adaptive Balance and CS Utility's
// shader-buffer plumbing. Adaptive Balance builds it from the global and
// active time/location adjustment layers.
struct SharedLightingSettings
{
	float skyBrightness = 1.0f;
	float directionalLightMult = 1.0f;
	float pointLightMult = 1.0f;
	float pointLightSaturation = 1.0f;
	float pointLightCurve = 1.0f;
	float fireIntensity = 1.0f;
	float fireSaturation = 1.0f;
	float fireCurve = 1.0f;
	float linearPointLightMult = 1.0f;
	float spotlightMult = 1.0f;
	float linearSpotlightMult = 1.0f;
	float omnidirectionalBulbMult = 1.0f;
	float linearOmnidirectionalBulbMult = 1.0f;
	float skySaturation = 1.0f;
	float ambientMult = 1.0f;
	float contrast = 1.0f;
	float saturation = 1.0f;
	float cloudBrightness = 1.0f;
	float cloudSaturation = 1.0f;
	float fogIntensity = 1.0f;
	float sunGlareIntensity = 1.0f;
	float effectBrightness = 1.0f;
	float skyStaticBrightness = 1.0f;
	float skyStaticTransparency = 0.0f;
	AdaptiveBalanceAppearanceSettings appearance;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	SharedLightingSettings,
	skyBrightness,
	directionalLightMult,
	pointLightMult,
	pointLightSaturation,
	pointLightCurve,
	fireIntensity,
	fireSaturation,
	fireCurve,
	linearPointLightMult,
	spotlightMult,
	linearSpotlightMult,
	omnidirectionalBulbMult,
	linearOmnidirectionalBulbMult,
	skySaturation,
	ambientMult,
	contrast,
	saturation,
	cloudBrightness,
	cloudSaturation,
	fogIntensity,
	sunGlareIntensity,
	effectBrightness,
	skyStaticBrightness,
	skyStaticTransparency,
	appearance)
