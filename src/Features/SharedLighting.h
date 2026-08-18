#pragma once

// Global renderer-light calibration owned by Adaptive Balance. These are
// baseline values; the active time/location profile is applied on top.
struct SharedLightingSettings
{
	float skyBrightness = 1.0f;
	float directionalLightMult = 1.0f;
	float pointLightMult = 1.0f;
	float linearPointLightMult = 1.0f;
	float spotlightMult = 1.0f;
	float linearSpotlightMult = 1.0f;
	float omnidirectionalBulbMult = 1.0f;
	float linearOmnidirectionalBulbMult = 1.0f;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	SharedLightingSettings,
	skyBrightness,
	directionalLightMult,
	pointLightMult,
	linearPointLightMult,
	spotlightMult,
	linearSpotlightMult,
	omnidirectionalBulbMult,
	linearOmnidirectionalBulbMult)
