#ifdef NDEBUG
#	undef NDEBUG
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <vector>

using uint = unsigned int;
struct float3
{
	float x, y, z;
};
#define NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(...)
#define STATIC_ASSERT_ALIGNAS_16(Type) static_assert(alignof(Type) == 16)
#include "Features/Bloom.h"
#include "Features/SharedLighting.h"
#include "Features/WaterAppearance.h"
#include "Utils/Finite.h"

struct LinearLighting
{
#include "linear_lighting_members.h"
	bool runtimeEnabled = false;
	bool IsRuntimeEnabled() const { return runtimeEnabled; }
};

namespace RE
{
	struct PlayerCharacter
	{
		bool hasCell = true;
		static PlayerCharacter* GetSingleton()
		{
			static PlayerCharacter player;
			return &player;
		}
	};
	struct Weather
	{
		struct
		{
			uint8_t windSpeed = 0;
		} data;
	};
	struct Sky
	{
		float windSpeed = 0.8f;
		Weather* currentWeather = nullptr;
		static Sky* GetSingleton()
		{
			static Sky sky;
			return &sky;
		}
	};
	float GetSecondsSinceLastFrame() { return 1.0f / 60.0f; }
}
const void* GetCurrentPlayerCell(RE::PlayerCharacter* player) { return player->hasCell ? player : nullptr; }
namespace globals
{
	struct State
	{
		bool menuOpen = false;
		std::atomic<uint32_t> frameCountAtomic{ 1 };
		bool IsMainOrLoadingMenuOpen() const { return menuOpen; }
	} stateValue;
	auto* state = &stateValue;
	namespace game
	{
		RE::Sky* sky = RE::Sky::GetSingleton();
	}
	namespace features
	{
		LinearLighting linearLighting;
	}
}
namespace LocationContext
{
	struct Context
	{
		bool inInterior = false;
	} context;
	const Context& Get() { return context; }
}
namespace Util::Units
{
	float WindRawToNormalized(uint8_t raw) { return static_cast<float>(raw) / 255.0f; }
}

struct AdaptiveBrightness
{
#include "adaptive_balance_members.h"
	bool loaded = true;
	ActiveProfileBlend testProfileBlend;
	std::vector<const LocationOverride*> testLocationLayers;
	mutable unsigned profileResolutions = 0;

	bool IsRuntimeAvailable() const;
	bool IsRuntimeEnabled() const;
	void SetEnabled(bool enabled);
	PerFrameData GetCommonBufferData() const;
	bool NeedsVanillaPointLightData() const;
	EffectiveLinearLightingSettings GetEffectiveLinearLightingSettings(const LinearLighting::Settings&, bool) const;
	SharedLightingSettings GetEffectiveSharedLightingSettings() const;
	Bloom::Settings GetEffectiveBloomSettings() const;
	WaterAppearance::Settings GetEffectiveWaterAppearanceSettings() const;
};
AdaptiveBrightness::ActiveProfileBlend AdaptiveBrightness::GetActiveProfileBlend() const
{
	++profileResolutions;
	return testProfileBlend;
}
const std::vector<const AdaptiveBrightness::LocationOverride*>& AdaptiveBrightness::GetActiveLocationLayers() const
{
	++profileResolutions;
	return testLocationLayers;
}

#include "adaptive_balance_under_test.h"

bool Close(float a, float b) { return std::abs(a - b) < 0.00001f; }

void CheckVisualControls()
{
	WaterAppearance::Profile invalid;
	invalid.CausticsStrength = std::numeric_limits<float>::quiet_NaN();
	invalid.CausticsTiling = -1.0f;
	invalid.CausticsSpeed = 4.0f;
	invalid.CausticsDispersion = std::numeric_limits<float>::infinity();
	invalid.ParallaxStrength = -2.0f;
	invalid.ParallaxQuality = std::numeric_limits<int>::max();
	WaterAppearance::SanitizeProfile(invalid);
	assert(invalid.CausticsStrength == 1.0f && invalid.CausticsTiling == 0.25f);
	assert(invalid.CausticsSpeed == 3.0f && invalid.CausticsDispersion == 1.0f);
	assert(invalid.ParallaxStrength == 0.0f && invalid.ParallaxQuality == 64);
	invalid.ParallaxQuality = -1;
	WaterAppearance::SanitizeProfile(invalid);
	assert(invalid.ParallaxQuality == 4);

	AdaptiveBrightness balance;
	balance.SetEnabled(true);
	assert(!balance.GetEffectiveWaterAppearanceSettings().Enabled);
	assert(balance.GetCommonBufferData().skySaturation == 1.0f);
	auto& global = balance.settings.globalProfile;
	global.advanced = true;
	global.skySaturation = 1.5f;
	global.water.CausticsStrength = 1.2f;
	global.water.CausticsTiling = 2.0f;
	global.water.CausticsSpeed = 2.0f;
	global.water.CausticsDispersion = 0.8f;
	global.water.ParallaxStrength = 0.6f;
	AdaptiveBrightness::ProfileSettings day, night;
	day.advanced = night.advanced = true;
	day.skySaturation = 0.5f;
	night.skySaturation = 1.0f;
	day.water.CausticsStrength = 0.5f;
	night.water.CausticsStrength = 1.5f;
	day.water.ParallaxQuality = 32;
	night.water.ParallaxQuality = 8;
	balance.testProfileBlend = { &day, &night, 0.5f };
	auto water = balance.GetEffectiveWaterAppearanceSettings();
	assert(water.Enabled && Close(water.CausticsStrength, 1.2f));
	assert(water.CausticsTiling == 2.0f && water.CausticsSpeed == 2.0f);
	assert(water.CausticsDispersion == 0.8f && water.ParallaxStrength == 0.6f);
	assert(water.ParallaxQuality == 20);
	balance.testProfileBlend.factor = 0.3f;
	assert(balance.GetEffectiveWaterAppearanceSettings().ParallaxQuality == 25);
	balance.testProfileBlend.factor = 0.5f;
	assert(Close(balance.GetCommonBufferData().skySaturation, 1.125f));
	day.advanced = false;
	assert(Close(balance.GetCommonBufferData().skySaturation, 1.5f));
	day.advanced = true;

	AdaptiveBrightness::LocationOverride location;
	location.profile.advanced = true;
	location.profile.skySaturation = 0.5f;
	location.profile.water.CausticsStrength = 0.5f;
	location.profile.water.ParallaxQuality = 8;
	balance.testLocationLayers = { &location };
	for (bool layered : { false, true }) {
		location.layered = layered;
		water = balance.GetEffectiveWaterAppearanceSettings();
		assert(Close(water.CausticsStrength, 0.6f));
		assert(water.ParallaxQuality == (layered ? 10u : 8u));
		assert(Close(balance.GetCommonBufferData().skySaturation, layered ? 0.5625f : 0.75f));
	}
	balance.testLocationLayers.clear();
	day.water.ParallaxQuality = night.water.ParallaxQuality = 64;
	global.water.ParallaxQuality = 64;
	assert(balance.GetEffectiveWaterAppearanceSettings().ParallaxQuality == 64);
	global.skySaturation = std::numeric_limits<float>::quiet_NaN();
	ClampProfileSettings(global);
	assert(global.skySaturation == 1.0f);
	global.skySaturation = 20.0f;
	ClampProfileSettings(global);
	assert(global.skySaturation == 2.0f);

	// Wind scales the composed base; switching it off preserves that base.
	balance.testProfileBlend = {};
	global.water.WaveAmplitude = 0.8f;
	global.waterWind = { true, true, 0.65f, 1.35f };
	for (const float wind : { 0.0f, 1.0f }) {
		globals::game::sky->windSpeed = wind;
		balance.ResetWaterWindSmoothing();
		assert(Close(balance.GetEffectiveWaterAppearanceSettings().WaveAmplitude, wind == 0.0f ? 0.52f : 1.08f));
	}
	global.water.WaveAmplitude = 2.0f;
	assert(balance.GetEffectiveWaterAppearanceSettings().WaveAmplitude == 2.0f);
	global.waterWind.enabled = false;
	global.water.WaveAmplitude = 0.8f;
	assert(balance.GetEffectiveWaterAppearanceSettings().WaveAmplitude == 0.8f);
	globals::game::sky->windSpeed = 0.8f;
}

void CheckOff(AdaptiveBrightness& balance, const LinearLighting::Settings& independentLighting)
{
	assert(!balance.IsRuntimeEnabled());
	assert(!balance.IsPerformanceCostMeasurementEnabled());
	const auto profileResolutions = balance.profileResolutions;
	const auto lights = balance.GetCommonBufferData();
	assert(lights.skyBrightness == 1.0f && lights.directionalLightMult == 1.0f);
	assert(lights.skySaturation == 1.0f);
	assert(lights.ambientMult == 1.0f);
	assert(lights.pointLightMult == 1.0f && lights.linearPointLightMult == 1.0f);
	assert(lights.spotlightMult == 1.0f && lights.linearSpotlightMult == 1.0f);
	assert(lights.omnidirectionalBulbMult == 1.0f && lights.linearOmnidirectionalBulbMult == 1.0f);
	const auto bloom = balance.GetEffectiveBloomSettings();
	assert(!bloom.Enabled && bloom.BlendWeight == 0.0f);
	const auto water = balance.GetEffectiveWaterAppearanceSettings();
	assert(!water.Enabled && water.WaterBrightness == 1.0f && water.WaveAmplitude == 1.0f);
	assert(water.GlobalReflectionAmount == 1.0f && water.RefractionAmount == 1.0f);
	assert(water.SunSpecularMultiplier == 1.0f && water.Muddiness == 1.0f);
	assert(water.FresnelMin == 0.0f && water.FresnelMax == 1.0f);
	assert(water.CausticsStrength == 1.0f && water.CausticsTiling == 1.0f);
	assert(water.CausticsSpeed == 1.0f && water.CausticsDispersion == 1.0f);
	assert(water.ParallaxStrength == 1.0f && water.ParallaxQuality == 16);
	const auto linear = balance.GetEffectiveLinearLightingSettings(independentLighting, true);
	assert(!linear.hasColorAdjustments);
	assert(linear.settings.skyGamma == independentLighting.skyGamma);
	assert(linear.settings.waterGamma == independentLighting.waterGamma);
	assert(linear.settings.fogGamma == independentLighting.fogGamma);
	assert(linear.settings.ambientMult == independentLighting.ambientMult);
	assert(linear.settings.glowmapMult == independentLighting.glowmapMult);
	assert(linear.settings.effectLightingMult == independentLighting.effectLightingMult);
	assert(linear.settings.enableLinearLighting == independentLighting.enableLinearLighting);
	const auto nonLinear = balance.GetEffectiveLinearLightingSettings(independentLighting, false);
	assert(!nonLinear.hasColorAdjustments && nonLinear.settings.waterGamma == 1.0f);
	assert(balance.profileResolutions == profileResolutions);
}

void CheckAmbientComposition()
{
	AdaptiveBrightness balance;
	auto& global = balance.settings.globalProfile;
	global.advanced = true;
	AdaptiveBrightness::ProfileSettings day, night;
	day.advanced = night.advanced = true;
	day.ambientMult = 0.5f;
	night.ambientMult = 1.5f;
	balance.testProfileBlend = { &day, &night, 0.5f };
	LinearLighting::Settings linear;
	linear.ambientMult = 0.65f;
	linear.ambientGamma = 1.8f;
	for (const float ambient : { 0.0f, 0.5f, 1.0f, 2.0f, 5.0f }) {
		global.ambientMult = ambient;
		assert(Close(balance.GetCommonBufferData().ambientMult, ambient));
		for (bool enabled : { false, true }) {
			const auto effective = balance.GetEffectiveLinearLightingSettings(linear, enabled);
			assert(Close(effective.settings.ambientMult, enabled ? 0.65f : 1.0f));
			assert(Close(effective.settings.ambientGamma, enabled ? 1.8f : 1.0f));
			assert(!effective.hasColorAdjustments);
		}
	}
	AdaptiveBrightness::LocationOverride location;
	location.profile.advanced = true;
	location.profile.ambientMult = 0.5f;
	balance.testLocationLayers = { &location };
	global.ambientMult = 2.0f;
	balance.testProfileBlend.factor = 0.0f;
	for (bool layered : { false, true }) {
		location.layered = layered;
		assert(Close(balance.GetCommonBufferData().ambientMult, layered ? 0.5f : 1.0f));
		global.ambientMult = 0.0f;
		assert(balance.GetCommonBufferData().ambientMult == 0.0f);
		global.ambientMult = 2.0f;
	}
	balance.testLocationLayers.clear();
	global.ambientMult = day.ambientMult = 5.0f;
	balance.testProfileBlend = { &day, &day, 0.0f };
	assert(balance.GetCommonBufferData().ambientMult == 10.0f);
	balance.testProfileBlend = {};
	global.advanced = false;
	assert(balance.GetCommonBufferData().ambientMult == 1.0f);
	global.brightness = 0.5f;
	assert(Close(balance.GetCommonBufferData().ambientMult, 0.525f));
	balance.SetEnabled(false);
	assert(balance.GetCommonBufferData().ambientMult == 1.0f);
	global.advanced = true;
	global.ambientMult = std::numeric_limits<float>::quiet_NaN();
	ClampProfileSettings(global);
	assert(global.ambientMult == 1.0f);
}

int main()
{
	CheckAmbientComposition();
	CheckVisualControls();
	// Keep zero-identity guards exercised at runtime under Release optimization.
	for (const auto& [identity, expected] : std::array<std::array<float, 2>, 6>{ { { 0.0f, 2.5f }, { -0.0f, 2.5f }, { 0.0001f, 2.5f },
			 { -0.0001f, 2.5f }, { 1.0f, 1.0f }, { -1.0f, -1.0f } } }) {
		volatile float runtimeIdentity = identity;
		assert(Close(ApplyRelativeValue(2.0f, 0.5f, runtimeIdentity), expected));
	}

	AdaptiveBrightness balance;
	auto& global = balance.settings.globalProfile;
	global.brightness = 1.2f;
	global.skyBrightnessMult = 1.3f;
	global.advanced = true;
	global.skySaturation = 0.8f;
	global.ambientMult = 0.5f;
	global.water.CausticsStrength = 1.5f;
	global.water.CausticsTiling = 2.0f;
	global.water.CausticsSpeed = 0.0f;
	global.water.CausticsDispersion = 0.5f;
	global.water.ParallaxStrength = 0.0f;
	global.water.ParallaxQuality = 32;
	global.water.WaterBrightness = 0.8f;
	global.water.WaveAmplitude = 0.6f;
	global.bloom.EnhancementIntensity = 0.3f;
	global.waterWind.enabled = true;
	LinearLighting::Settings independent;
	independent.enableLinearLighting = true;
	independent.waterGamma = 2.2f;
	independent.skyGamma = 1.7f;
	independent.ambientMult = 0.65f;

	// Both a base-profile branch and a location layer can explicitly enable wind.
	for (bool layered : { false, true }) {
		AdaptiveBrightness::ProfileSettings day;
		AdaptiveBrightness::ProfileSettings night;
		day.brightness = 0.9f;
		night.brightness = 1.1f;
		day.waterWind = { true, true, 0.8f, 1.2f };
		night.waterWind = { true, true, 0.6f, 1.1f };
		AdaptiveBrightness::LocationOverride location;
		location.layered = layered;
		global.waterWind.enabled = layered;
		location.profile.brightness = 1.1f;
		location.profile.water.WaterBrightness = 1.3f;
		location.profile.waterWind = { true, true, 0.7f, 1.25f };
		balance.testProfileBlend = { &day, &night, 0.4f };
		balance.testLocationLayers = { &location };
		balance.SetEnabled(true);
		const auto before = balance.GetEffectiveWaterAppearanceSettings();
		const auto beforeLight = balance.GetEffectiveSharedLightingSettings();
		assert(before.Enabled && balance.GetEffectiveBloomSettings().Enabled);
		assert(balance.GetEffectiveLinearLightingSettings(independent, true).hasColorAdjustments);
		assert(balance.waterWindSmoothingInitialized);
		const auto engineWind = globals::game::sky->windSpeed;
		balance.SetEnabled(false);
		CheckOff(balance, independent);
		assert(!balance.waterWindSmoothingInitialized);
		assert(globals::game::sky->windSpeed == engineWind);
		assert(global.water.WaveAmplitude == 0.6f && location.profile.waterWind.enabled);
		balance.SetEnabled(true);
		const auto after = balance.GetEffectiveWaterAppearanceSettings();
		assert(Close(after.WaveAmplitude, before.WaveAmplitude));
		assert(Close(after.WaterBrightness, before.WaterBrightness));
		assert(after.CausticsStrength == before.CausticsStrength && after.CausticsTiling == before.CausticsTiling);
		assert(after.CausticsSpeed == before.CausticsSpeed && after.CausticsDispersion == before.CausticsDispersion);
		assert(after.ParallaxStrength == before.ParallaxStrength && after.ParallaxQuality == before.ParallaxQuality);
		assert(balance.GetCommonBufferData().skySaturation == beforeLight.skySaturation);
		assert(balance.GetCommonBufferData().ambientMult == beforeLight.ambientMult);
		assert(Close(balance.GetEffectiveSharedLightingSettings().directionalLightMult, beforeLight.directionalLightMult));
	}
	balance.testProfileBlend = {};
	balance.testLocationLayers.clear();
	balance.SetEnabled(false);
	CheckOff(balance, independent);
	globals::features::linearLighting.runtimeEnabled = true;
	assert(balance.NeedsVanillaPointLightData());
	globals::features::linearLighting.runtimeEnabled = false;
	assert(!balance.NeedsVanillaPointLightData());

	// Off skips wind reads; re-enabling starts smoothing from the current engine wind.
	globals::game::sky->windSpeed = 0.1f;
	balance.SetEnabled(true);
	balance.GetEffectiveWaterAppearanceSettings();
	assert(Close(balance.smoothedWaterWindSpeed, 0.1f));
	balance.SetEnabled(false);
	globals::game::sky->windSpeed = 0.9f;
	CheckOff(balance, independent);
	balance.SetEnabled(true);
	balance.GetEffectiveWaterAppearanceSettings();
	assert(Close(balance.smoothedWaterWindSpeed, 0.9f));
	assert(globals::game::sky->windSpeed == 0.9f);

	// No water update occurs between these transitions, including measurement restore.
	balance.SetPerformanceCostMeasurementEnabled(false);
	assert(balance.IsPerformanceCostMeasurementReady());
	globals::game::sky->windSpeed = 0.2f;
	balance.SetPerformanceCostMeasurementEnabled(true);
	assert(balance.IsPerformanceCostMeasurementEnabled());
	balance.GetEffectiveWaterAppearanceSettings();
	assert(Close(balance.smoothedWaterWindSpeed, 0.2f));
	balance.SetEnabled(false);
	globals::game::sky->windSpeed = 0.5f;
	balance.SetEnabled(true);
	balance.GetEffectiveWaterAppearanceSettings();
	assert(Close(balance.smoothedWaterWindSpeed, 0.5f));
	// Repeated requests for the current state must not restart smoothing.
	globals::game::sky->windSpeed = 0.7f;
	balance.SetEnabled(true);
	balance.SetPerformanceCostMeasurementEnabled(true);
	balance.GetEffectiveWaterAppearanceSettings();
	assert(Close(balance.smoothedWaterWindSpeed, 0.5f));
	balance.SetPerformanceCostMeasurementEnabled(false);
	CheckOff(balance, independent);
	assert(balance.settings.enabled && balance.IsPerformanceCostMeasurementReady());
	balance.SetEnabled(false);
	balance.SetPerformanceCostMeasurementEnabled(true);
	CheckOff(balance, independent);
	assert(!balance.settings.enabled && balance.IsPerformanceCostMeasurementReady());
	balance.SetEnabled(true);
	globals::state->menuOpen = true;
	CheckOff(balance, independent);
	globals::state->menuOpen = false;
	RE::PlayerCharacter::GetSingleton()->hasCell = false;
	CheckOff(balance, independent);
	RE::PlayerCharacter::GetSingleton()->hasCell = true;
	balance.loaded = false;
	CheckOff(balance, independent);
	std::cout << "Adaptive Balance master toggle: global, layered/replacement locations, wind restoration, independent lighting, and inactive-runtime checks passed\n";
}
