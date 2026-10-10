#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>

namespace AdaptiveBalanceDepthOfField
{
	/** Shared bounds for saved controls and the DevBench validation schema. */
	inline constexpr float kDofStrengthMin = 0.0f;
	inline constexpr float kDofStrengthMax = 1.0f;
	inline constexpr float kDofDistanceMin = 0.0f;
	inline constexpr float kDofDistanceMax = 50000.0f;
	inline constexpr float kDofRangeMin = 0.0f;
	inline constexpr float kDofRangeMax = 50000.0f;
	inline constexpr float kDofAutoFocusDepthMax = 10000.0f;
	inline constexpr float kDofAutoFocusBlurMin = 0.0f;
	inline constexpr float kDofAutoFocusBlurMax = 1.0f;
	inline constexpr float kDofAutoFocusBlurMultiplierMin = 0.0f;
	inline constexpr float kDofAutoFocusBlurMultiplierMax = 1.0f;
	inline constexpr uint32_t kDofModeMask = 0x3;
	inline constexpr uint32_t kDofBlurRadiusMax = 7;

	struct DepthOfFieldAutoFocusSettings
	{
		float nearDistance = 0.0f;
		float farDistance = 0.0f;
		float nearRange = 0.0f;
		float farRange = 0.0f;
		float nearBlur = 0.0f;
		float farBlur = 0.0f;
		float blurMultiplier = 1.0f;
	};

	struct DepthOfFieldSettings
	{
		float strength = 0.0f;
		float distance = 0.0f;
		float range = 0.0f;
		uint32_t mode = 2;
		bool excludeSky = false;
		bool autoFocus = false;
		DepthOfFieldAutoFocusSettings autoFocusSettings;
		uint32_t blurRadius = 2;
	};

	struct DepthOfFieldOverride
	{
		/** True while manual values override the live image space. */
		bool locked = false;
		DepthOfFieldSettings values;
		DepthOfFieldSettings baseline;
	};

	/** Global controls; correction does not depend on the manual override toggle. */
	struct Settings
	{
		bool enabled = false;
		bool fixUnderwaterFogDofBlur = false;
		DepthOfFieldOverride sceneDof;
		DepthOfFieldOverride underwaterDof;
	};

	/** Clamps all saved scene and underwater override parameters. */
	void SanitizeSettings(Settings& a_settings);
	/** Validates finite depth, blur, autofocus and packed-mode parameters. */
	void SanitizeDepthOfFieldSettings(DepthOfFieldSettings& a_settings);
	/** Validates both manual values and the baseline used by lock confirmation. */
	void SanitizeDepthOfFieldOverride(DepthOfFieldOverride& a_override);
	/** Draws the global controls in Adaptive Balance's Depth of field group. */
	void DrawSettings();
	/** Installs the shared SE, AE and VR depth-of-field hooks. */
	void InstallHooks();
	/** Applies Adaptive Balance's runtime gate and the manual override toggle. */
	bool IsRuntimeEnabled();
	/** Applies Adaptive Balance's runtime gate and the independent correction toggle. */
	bool IsCorrectionEnabled();
	/** Returns current scene values on the game thread, or no value without live data. */
	std::optional<DepthOfFieldSettings> GetLiveSceneSettings();
	/** Returns underwater values on the game thread, or no value without an active record. */
	std::optional<DepthOfFieldSettings> GetLiveUnderwaterSettings();

	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
		DepthOfFieldAutoFocusSettings,
		nearDistance,
		farDistance,
		nearRange,
		farRange,
		nearBlur,
		farBlur,
		blurMultiplier)

	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
		DepthOfFieldSettings,
		strength,
		distance,
		range,
		mode,
		excludeSky,
		autoFocus,
		autoFocusSettings,
		blurRadius)

	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
		DepthOfFieldOverride,
		locked,
		values,
		baseline)

	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
		Settings,
		enabled,
		fixUnderwaterFogDofBlur,
		sceneDof,
		underwaterDof)
}
