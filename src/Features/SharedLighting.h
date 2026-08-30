#pragma once

// Composed renderer-light state shared by Adaptive Balance and CS Utility's
// shader-buffer plumbing. Adaptive Balance builds it from the global and
// active time/location adjustment layers.
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
