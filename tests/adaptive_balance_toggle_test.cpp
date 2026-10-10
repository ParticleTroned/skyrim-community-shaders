#ifdef NDEBUG
#	undef NDEBUG
#endif

#include "Features/AdaptiveBalanceDepthOfField.h"
#include "Features/AdaptiveBalanceGodraySettings.h"
#include "SettingsMigrations.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using uint = unsigned int;
using json = nlohmann::json;
struct float3
{
	float x, y, z;
};
#undef NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT
#define NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(...)
#define STATIC_ASSERT_ALIGNAS_16(Type) static_assert(alignof(Type) == 16)
#include "Features/Bloom.h"
#include "Features/SharedLighting.h"
#include "Features/WaterAppearance.h"
#include "Features/WeatherColorAdjustment.h"
#include "Utils/Finite.h"

struct LinearLighting
{
#include "linear_lighting_members.h"
	bool runtimeEnabled = false;
	bool IsRuntimeEnabled() const { return runtimeEnabled; }
};

namespace RE
{
	struct NiColor
	{
		std::array<float, 3> values{};
		bool operator==(const NiColor&) const = default;
		float operator[](std::size_t i) const { return values[i]; }
		float& operator[](std::size_t i) { return values[i]; }
		NiColor operator*(float scale) const { return { { values[0] * scale, values[1] * scale, values[2] * scale } }; }
	};
	struct TESWeather
	{
		enum ColorTypes
		{
			kSkyUpper = 0,
			kSkyLower = 1,
			kHorizon = 2,
			kEffectLighting = 9,
			kSkyStatics = 13
		};
	};
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
		NiColor skyColor[17]{};
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
	bool throwProfileResolution = false;

	bool IsRuntimeAvailable() const;
	bool IsRuntimeEnabled() const;
	float GetEffectiveGodrayFinalBrightness() const;
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
	if (throwProfileResolution)
		throw std::runtime_error("injected profile resolution failure");
	++profileResolutions;
	return testProfileBlend;
}
const std::vector<const AdaptiveBrightness::LocationOverride*>& AdaptiveBrightness::GetActiveLocationLayers() const
{
	++profileResolutions;
	return testLocationLayers;
}

#include "adaptive_balance_under_test.h"

namespace globals::features
{
	AdaptiveBrightness adaptiveBrightness;
}
namespace logger
{
	unsigned weatherWarnings = 0;
	template <class... Args>
	void warn(const char*, Args&&...)
	{
		++weatherWarnings;
	}
}
struct WeatherUpdateHook
{
#include "adaptive_balance_weather_hook_under_test.h"
	static inline void (*func)(RE::Sky*, float) = [](RE::Sky*, float) {};
	static inline WeatherColorAdjustment<RE::NiColor> effect;
	static inline WeatherColorAdjustment<RE::NiColor> statics;
	static inline WeatherColorAdjustment<RE::NiColor> upper;
	static inline WeatherColorAdjustment<RE::NiColor> middle;
	static inline WeatherColorAdjustment<RE::NiColor> horizon;
	static inline bool loggedFailure = false;
};

bool Close(float a, float b) { return std::abs(a - b) < 0.00001f; }

void CheckWeatherColors()
{
	const RE::NiColor effectSource{ { 0.2f, 0.4f, 0.6f } };
	const RE::NiColor staticSource{ { 0.3f, 0.5f, 0.7f } };
	RE::Weather weather;
	RE::Sky sky;
	sky.currentWeather = &weather;
	auto& effect = sky.skyColor[RE::TESWeather::kEffectLighting];
	auto& statics = sky.skyColor[RE::TESWeather::kSkyStatics];
	effect = effectSource;
	statics = staticSource;
	sky.skyColor[0] = staticSource;
	auto& balance = globals::features::adaptiveBrightness;
	balance.settings.globalProfile.advanced = true;
	auto& profile = balance.settings.globalProfile;
	profile.effectBrightness = 0.5f;
	profile.skyStaticBrightness = 2.0f;
	for (int i = 0; i < 3; ++i) {
		WeatherUpdateHook::thunk(&sky, 0.0f);
		assert(effect == effectSource * 0.5f && statics == staticSource * 2.0f);
		assert(sky.skyColor[0] == staticSource);
	}
	profile.effectBrightness = 0.0f;
	WeatherUpdateHook::thunk(&sky, 0.0f);
	assert(effect == RE::NiColor{});
	profile.effectBrightness = 1.5f;
	WeatherUpdateHook::thunk(&sky, 0.0f);
	assert(effect == effectSource * 1.5f);
	balance.SetEnabled(false);
	WeatherUpdateHook::thunk(&sky, 0.0f);
	assert(effect == effectSource && statics == staticSource);
	balance.SetEnabled(true);
	profile.advanced = false;
	WeatherUpdateHook::thunk(&sky, 0.0f);
	assert(effect == effectSource && statics == staticSource);
	profile.advanced = true;
	WeatherUpdateHook::thunk(&sky, 0.0f);
	WeatherUpdateHook::func = [](RE::Sky* live, float delta) {
		assert(delta == 0.25f);
		assert((live->skyColor[RE::TESWeather::kEffectLighting] == RE::NiColor{ { 0.2f, 0.4f, 0.6f } }));
		live->skyColor[RE::TESWeather::kEffectLighting] = { { 0.4f, 0.2f, 0.8f } };
	};
	WeatherUpdateHook::thunk(&sky, 0.25f);
	const RE::NiColor newWeather{ { 0.4f, 0.2f, 0.8f } };
	assert(effect == newWeather * 1.5f);
	WeatherUpdateHook::func = [](RE::Sky*, float) {};
	effect = staticSource;
	WeatherUpdateHook::thunk(&sky, 0.0f);
	assert(effect == staticSource * 1.5f);
	balance.throwProfileResolution = true;
	WeatherUpdateHook::thunk(&sky, 0.0f);
	WeatherUpdateHook::thunk(&sky, 0.0f);
	assert(effect == staticSource && statics == staticSource && logger::weatherWarnings == 1);
	balance.throwProfileResolution = false;
	WeatherUpdateHook::thunk(&sky, 0.0f);
	assert(effect == staticSource * 1.5f && !WeatherUpdateHook::loggedFailure);
	for (bool* gate : { &balance.loaded, &balance.performanceCostMeasurementEnabled }) {
		*gate = false;
		WeatherUpdateHook::thunk(&sky, 0.0f);
		assert(effect == staticSource && statics == staticSource);
		*gate = true;
		WeatherUpdateHook::thunk(&sky, 0.0f);
	}
	globals::state->menuOpen = true;
	WeatherUpdateHook::thunk(&sky, 0.0f);
	assert(effect == staticSource && statics == staticSource);
	globals::state->menuOpen = false;
	WeatherUpdateHook::thunk(&sky, 0.0f);
	sky.currentWeather = nullptr;
	WeatherUpdateHook::thunk(&sky, 0.0f);
	assert(effect == staticSource && statics == staticSource);
	WeatherUpdateHook::thunk(nullptr, 0.0f);

	WeatherColorAdjustment<RE::NiColor> adjustment;
	RE::Sky otherSky;
	adjustment.Apply(&sky, effect, 2.0f);
	otherSky.skyColor[0] = effect;
	const auto otherColor = otherSky.skyColor[0];
	adjustment.Restore(&otherSky, otherSky.skyColor[0]);
	assert(otherSky.skyColor[0] == otherColor);
	effect = effectSource;
	adjustment.Apply(&sky, effect, -1.0f);
	assert(effect == RE::NiColor{});
	adjustment.Apply(&sky, effect, std::numeric_limits<float>::quiet_NaN());
	assert(effect == effectSource);
	adjustment.Apply(&sky, effect, 20.0f);
	assert(effect == effectSource * 2.0f);
	adjustment.Apply(&sky, effect, std::numeric_limits<float>::infinity());
	assert(effect == effectSource);
	effect = { { std::numeric_limits<float>::max(), 0.0f, 1.0f } };
	const auto largeColor = effect;
	adjustment.Apply(&sky, effect, 2.0f);
	assert(effect == largeColor);
	balance = {};
}

void CheckWeatherBrightnessComposition()
{
	AdaptiveBrightness balance;
	auto& global = balance.settings.globalProfile;
	assert(global.effectBrightness == 1.0f);
	assert(SharedLightingSettings{}.effectBrightness == 1.0f);
	assert(AdaptiveBrightness::ProfileSettings::AdjustmentDefaults().effectBrightness == 1.0f);
	global.advanced = true;
	global.effectBrightness = 1.5f;
	AdaptiveBrightness::ProfileSettings day, night;
	day.advanced = night.advanced = true;
	day.effectBrightness = 0.5f;
	night.effectBrightness = 1.5f;
	balance.testProfileBlend = { &day, &night, 0.25f };
	assert(Close(balance.GetEffectiveSharedLightingSettings().effectBrightness, 1.0625f));
	AdaptiveBrightness::LocationOverride location;
	location.profile.advanced = true;
	location.profile.effectBrightness = 0.5f;
	balance.testLocationLayers = { &location };
	assert(Close(balance.GetEffectiveSharedLightingSettings().effectBrightness, 0.75f));
	location.layered = true;
	assert(Close(balance.GetEffectiveSharedLightingSettings().effectBrightness, 0.53125f));
	balance.testLocationLayers.clear();
	balance.testProfileBlend = {};
	assert(balance.GetCommonBufferData().useAmbientEffectLighting == 0u);
	assert(balance.GetCommonBufferData().effectBrightness == global.effectBrightness);
	for (float invalid : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() }) {
		global.effectBrightness = invalid;
		ClampProfileSettings(global);
		assert(global.effectBrightness == 1.0f);
	}
	global.effectBrightness = 20.0f;
	ClampProfileSettings(global);
	assert(global.effectBrightness == 2.0f);
	global.effectBrightness = -1.0f;
	ClampProfileSettings(global);
	assert(global.effectBrightness == 0.0f);
	balance.SetEnabled(false);
	assert(balance.GetEffectiveSharedLightingSettings().effectBrightness == 1.0f);
}

void CheckColorControls()
{
	AdaptiveBrightness balance;
	auto& global = balance.settings.globalProfile;
	assert(global.contrast == 1.0f && global.saturation == 1.0f);
	global.advanced = false;
	global.contrast = 1.2f;
	global.saturation = 0.8f;
	AdaptiveBrightness::ProfileSettings day, night;
	day.contrast = 0.5f;
	night.contrast = 1.5f;
	day.saturation = 0.5f;
	night.saturation = 1.5f;
	balance.testProfileBlend = { &day, &night, 0.25f };
	auto color = balance.GetCommonBufferData();
	assert(Close(color.contrast, 0.9f) && Close(color.saturation, 0.6f));
	AdaptiveBrightness::LocationOverride location;
	location.profile.contrast = 1.25f;
	location.profile.saturation = 0.5f;
	balance.testLocationLayers = { &location };
	color = balance.GetCommonBufferData();
	assert(Close(color.contrast, 1.5f) && Close(color.saturation, 0.4f));
	location.layered = true;
	color = balance.GetCommonBufferData();
	assert(Close(color.contrast, 1.0625f) && Close(color.saturation, 0.3f));
	balance.SetEnabled(false);
	color = balance.GetCommonBufferData();
	assert(color.contrast == 1.0f && color.saturation == 1.0f);
	balance.SetEnabled(true);
	assert(Close(balance.GetCommonBufferData().contrast, 1.0625f));
	balance.testProfileBlend = {};
	balance.testLocationLayers.clear();
	global.contrast = std::numeric_limits<float>::infinity();
	global.saturation = std::numeric_limits<float>::quiet_NaN();
	color = balance.GetCommonBufferData();
	assert(color.contrast == 1.0f && color.saturation == 1.0f);
	ClampProfileSettings(global);
	assert(global.contrast == 1.0f && global.saturation == 1.0f);
	global.contrast = -5.0f;
	global.saturation = 10.0f;
	ClampProfileSettings(global);
	assert(global.contrast == 0.5f && global.saturation == 2.0f);
	global.saturation = 0.0f;
	assert(balance.GetCommonBufferData().saturation == 0.0f);
}

void CheckPointLightSaturation()
{
	AdaptiveBrightness balance;
	auto& global = balance.settings.globalProfile;
	assert(global.pointLightSaturation == 1.0f);
	assert(SharedLightingSettings{}.pointLightSaturation == 1.0f);
	assert(AdaptiveBrightness::ProfileSettings::AdjustmentDefaults().pointLightSaturation == 1.0f);
	assert(balance.GetCommonBufferData().pointLightSaturation == 1.0f);
	global.pointLightSaturation = 1.5f;
	AdaptiveBrightness::ProfileSettings day, night;
	day.advanced = night.advanced = true;
	day.pointLightSaturation = 0.5f;
	night.pointLightSaturation = 1.0f;
	balance.testProfileBlend = { &day, &night, 0.25f };
	assert(Close(balance.GetCommonBufferData().pointLightSaturation, 0.9375f));
	day.advanced = false;
	assert(Close(balance.GetCommonBufferData().pointLightSaturation, 1.5f));
	day.advanced = true;
	global.advanced = false;
	assert(Close(balance.GetCommonBufferData().pointLightSaturation, 0.625f));
	global.advanced = true;

	AdaptiveBrightness::LocationOverride location;
	location.profile.advanced = true;
	location.profile.pointLightSaturation = 0.5f;
	balance.testLocationLayers = { &location };
	assert(Close(balance.GetCommonBufferData().pointLightSaturation, 0.75f));
	location.layered = true;
	assert(Close(balance.GetCommonBufferData().pointLightSaturation, 0.46875f));
	balance.SetEnabled(false);
	assert(balance.GetCommonBufferData().pointLightSaturation == 1.0f);
	balance.SetEnabled(true);
	assert(Close(balance.GetCommonBufferData().pointLightSaturation, 0.46875f));
	balance.testLocationLayers.clear();

	global.pointLightSaturation = day.pointLightSaturation = night.pointLightSaturation = 2.0f;
	assert(balance.GetCommonBufferData().pointLightSaturation == 2.0f);
	global.pointLightSaturation = 0.0f;
	assert(balance.GetCommonBufferData().pointLightSaturation == 0.0f);
	balance.testProfileBlend = {};
	global.pointLightSaturation = 0.4f;
	global.brightness = 2.0f;
	global.pointLightMult = 0.3f;
	global.linearPointLightMult = 3.0f;
	assert(Close(balance.GetCommonBufferData().pointLightSaturation, 0.4f));

	for (float invalid : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() }) {
		global.pointLightSaturation = invalid;
		assert(balance.GetCommonBufferData().pointLightSaturation == 1.0f);
		ClampProfileSettings(global);
		assert(global.pointLightSaturation == 1.0f);
	}
	for (const auto [input, expected] : std::array<std::array<float, 2>, 2>{ { { -1.0f, 0.0f }, { 20.0f, 2.0f } } }) {
		global.pointLightSaturation = input;
		assert(balance.GetCommonBufferData().pointLightSaturation == expected);
		ClampProfileSettings(global);
		assert(global.pointLightSaturation == expected);
	}
	global.advanced = false;
	assert(balance.GetCommonBufferData().pointLightSaturation == 1.0f);

	for (const float value : { 0.0f, 1.0f, 2.0f })
		assert(ValidateAdaptiveBalanceVisuals({ { "pointLightSaturation", value } }).empty());
	for (const auto& value : std::vector<json>{ -0.001, 2.001, std::numeric_limits<double>::quiet_NaN(),
			 std::numeric_limits<double>::infinity(), true, "1", nullptr })
		assert(!ValidateAdaptiveBalanceVisuals({ { "pointLightSaturation", value } }).empty());
	assert(!ValidateAdaptiveBalanceVisuals({ { "pointLightSaturation", 0.5 }, { "unknown", 1 } }).empty());
}

void CheckPointLightCurve()
{
	AdaptiveBrightness balance;
	auto& global = balance.settings.globalProfile;
	assert(global.pointLightCurve == 1.0f);
	assert(SharedLightingSettings{}.pointLightCurve == 1.0f);
	assert(AdaptiveBrightness::ProfileSettings::AdjustmentDefaults().pointLightCurve == 1.0f);
	assert(balance.GetCommonBufferData().pointLightCurve == 1.0f);
	global.pointLightCurve = 2.0f;
	AdaptiveBrightness::ProfileSettings day, night;
	day.advanced = night.advanced = true;
	day.pointLightCurve = 0.5f;
	night.pointLightCurve = 1.5f;
	balance.testProfileBlend = { &day, &night, 0.25f };
	assert(Close(balance.GetCommonBufferData().pointLightCurve, 1.5f));
	day.advanced = false;
	assert(Close(balance.GetCommonBufferData().pointLightCurve, 2.25f));
	day.advanced = true;
	global.advanced = false;
	assert(Close(balance.GetCommonBufferData().pointLightCurve, 0.75f));
	global.advanced = true;

	AdaptiveBrightness::LocationOverride location;
	location.profile.advanced = true;
	location.profile.pointLightCurve = 0.5f;
	balance.testLocationLayers = { &location };
	assert(Close(balance.GetCommonBufferData().pointLightCurve, 1.0f));
	location.layered = true;
	assert(Close(balance.GetCommonBufferData().pointLightCurve, 0.75f));
	balance.SetEnabled(false);
	assert(balance.GetCommonBufferData().pointLightCurve == 1.0f);
	balance.SetEnabled(true);
	assert(Close(balance.GetCommonBufferData().pointLightCurve, 0.75f));
	balance.SetPerformanceCostMeasurementEnabled(false);
	assert(balance.GetCommonBufferData().pointLightCurve == 1.0f);
	balance.SetPerformanceCostMeasurementEnabled(true);
	assert(Close(balance.GetCommonBufferData().pointLightCurve, 0.75f));
	balance.testLocationLayers.clear();

	for (const float edge : { 0.1f, 4.0f }) {
		global.pointLightCurve = day.pointLightCurve = night.pointLightCurve = edge;
		assert(balance.GetCommonBufferData().pointLightCurve == edge);
	}
	balance.testProfileBlend = {};
	global.pointLightCurve = 2.0f;
	global.brightness = 0.5f;
	global.pointLightMult = global.pointLightSaturation = 0.0f;
	assert(balance.GetCommonBufferData().pointLightCurve == 2.0f);
	for (float invalid : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() }) {
		global.pointLightCurve = invalid;
		assert(balance.GetCommonBufferData().pointLightCurve == 1.0f);
		ClampProfileSettings(global);
		assert(global.pointLightCurve == 1.0f);
		SharedLightingSettings shared;
		shared.pointLightCurve = invalid;
		SanitizeSharedLightingSettings(shared);
		assert(shared.pointLightCurve == 1.0f);
	}
	for (const auto& [input, expected] : std::array<std::array<float, 2>, 2>{ { { -1.0f, 0.1f }, { 20.0f, 4.0f } } }) {
		global.pointLightCurve = input;
		assert(balance.GetCommonBufferData().pointLightCurve == expected);
		ClampProfileSettings(global);
		assert(global.pointLightCurve == expected);
	}
	global.advanced = false;
	assert(balance.GetCommonBufferData().pointLightCurve == 1.0f);

	for (const float value : { 0.1f, 1.0f, 4.0f })
		assert(ValidateAdaptiveBalanceVisuals({ { "pointLightCurve", value } }).empty());
	for (const auto& value : std::vector<json>{ 0.099, 4.001, std::numeric_limits<double>::quiet_NaN(),
			 std::numeric_limits<double>::infinity(), true, "1", nullptr })
		assert(!ValidateAdaptiveBalanceVisuals({ { "pointLightCurve", value } }).empty());
	assert(!ValidateAdaptiveBalanceVisuals({ { "pointLightCurve", 0.5 }, { "unknown", 1 } }).empty());
}

void CheckFireControls()
{
	AdaptiveBrightness balance;
	auto expect = [&](float intensity, float saturation, float curve) {
		const auto data = balance.GetCommonBufferData();
		assert(Close(data.fireIntensity, intensity));
		assert(Close(data.fireSaturation, saturation));
		assert(Close(data.fireCurve, curve));
	};
	expect(1.0f, 1.0f, 1.0f);
	auto& global = balance.settings.globalProfile;
	global.fireIntensity = 2.0f;
	global.fireSaturation = 1.5f;
	global.fireCurve = 0.5f;
	AdaptiveBrightness::ProfileSettings day, night;
	day.advanced = night.advanced = true;
	day.fireIntensity = day.fireSaturation = day.fireCurve = 0.5f;
	night.fireIntensity = 1.5f;
	night.fireSaturation = 1.0f;
	night.fireCurve = 2.0f;
	balance.testProfileBlend = { &day, &night, 0.25f };
	expect(1.5f, 0.9375f, 0.4375f);
	day.advanced = false;
	expect(2.25f, 1.5f, 0.625f);
	day.advanced = true;
	global.advanced = false;
	expect(0.75f, 0.625f, 0.875f);
	global.advanced = true;

	AdaptiveBrightness::LocationOverride location;
	location.profile.advanced = true;
	location.profile.fireIntensity = location.profile.fireSaturation = 0.5f;
	location.profile.fireCurve = 2.0f;
	balance.testLocationLayers = { &location };
	expect(1.0f, 0.75f, 1.0f);
	location.layered = true;
	expect(0.75f, 0.46875f, 0.875f);
	balance.SetEnabled(false);
	expect(1.0f, 1.0f, 1.0f);
	balance.SetEnabled(true);
	expect(0.75f, 0.46875f, 0.875f);
	balance.SetPerformanceCostMeasurementEnabled(false);
	expect(1.0f, 1.0f, 1.0f);
	balance.SetPerformanceCostMeasurementEnabled(true);
	expect(0.75f, 0.46875f, 0.875f);

	balance.testLocationLayers.clear();
	balance.testProfileBlend = {};
	global.brightness = 2.0f;
	global.pointLightMult = global.pointLightSaturation = 0.0f;
	expect(2.0f, 1.5f, 0.5f);
	global.advanced = false;
	expect(1.0f, 1.0f, 1.0f);

	using Profile = AdaptiveBrightness::ProfileSettings;
	using Buffer = AdaptiveBrightness::PerFrameData;
	struct FireControl
	{
		const char* name;
		float Profile::* profile;
		float SharedLightingSettings::* shared;
		float Buffer::* buffer;
		float minimum;
		float maximum;
	};
	const std::array controls{
		FireControl{ "fireIntensity", &Profile::fireIntensity, &SharedLightingSettings::fireIntensity, &Buffer::fireIntensity, 0.0f, 5.0f },
		FireControl{ "fireSaturation", &Profile::fireSaturation, &SharedLightingSettings::fireSaturation, &Buffer::fireSaturation, 0.0f, 2.0f },
		FireControl{ "fireCurve", &Profile::fireCurve, &SharedLightingSettings::fireCurve, &Buffer::fireCurve, 0.25f, 4.0f }
	};
	for (const auto& control : controls) {
		assert(Profile::AdjustmentDefaults().*control.profile == 1.0f);
		assert(Profile::GlobalDefaults().*control.profile == 1.0f);
		assert(SharedLightingSettings{}.*control.shared == 1.0f);
		AdaptiveBrightness bounds;
		auto& profile = bounds.settings.globalProfile;
		for (float invalid : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() }) {
			profile.*control.profile = invalid;
			assert(bounds.GetCommonBufferData().*control.buffer == 1.0f);
			ClampProfileSettings(profile);
			assert(profile.*control.profile == 1.0f);
			SharedLightingSettings shared;
			shared.*control.shared = invalid;
			SanitizeSharedLightingSettings(shared);
			assert(shared.*control.shared == 1.0f);
		}
		for (const float edge : { control.minimum, control.maximum }) {
			profile.*control.profile = edge == control.minimum ? edge - 1.0f : edge + 1.0f;
			assert(bounds.GetCommonBufferData().*control.buffer == edge);
			ClampProfileSettings(profile);
			assert(profile.*control.profile == edge);
			Profile layer;
			layer.advanced = true;
			layer.*control.profile = edge;
			bounds.testProfileBlend = { &layer, &layer, 0.0f };
			assert(bounds.GetCommonBufferData().*control.buffer == edge);
			bounds.testProfileBlend = {};
		}
		for (const float value : { control.minimum, 1.0f, control.maximum })
			assert(ValidateAdaptiveBalanceVisuals({ { control.name, value } }).empty());
		for (const auto& value : std::vector<json>{ control.minimum - 0.001f, control.maximum + 0.001f,
				 std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), true, "1", nullptr })
			assert(!ValidateAdaptiveBalanceVisuals({ { control.name, value } }).empty());
		assert(!ValidateAdaptiveBalanceVisuals({ { control.name, 1.0f }, { "unknown", 1 } }).empty());
	}
}

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
	assert(lights.contrast == 1.0f && lights.saturation == 1.0f);
	assert(lights.ambientMult == 1.0f);
	assert(lights.pointLightMult == 1.0f && lights.linearPointLightMult == 1.0f);
	assert(lights.pointLightSaturation == 1.0f);
	assert(lights.pointLightCurve == 1.0f);
	assert(lights.fireIntensity == 1.0f && lights.fireSaturation == 1.0f && lights.fireCurve == 1.0f);
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

void CheckAmbientEffectLighting()
{
	AdaptiveBrightness balance;
	assert(!balance.settings.useAmbientEffectLighting);
	assert(balance.GetCommonBufferData().useAmbientEffectLighting == 0u);
	balance.settings.useAmbientEffectLighting = true;
	balance.settings.globalProfile.effectBrightness = 0.5f;
	balance.settings.globalProfile.skyStaticBrightness = 1.5f;
	auto data = balance.GetCommonBufferData();
	assert(data.useAmbientEffectLighting == 1u);
	assert(data.effectBrightness == 0.5f && data.skyStaticBrightness == 1.5f);
	balance.settings.globalProfile.advanced = false;
	data = balance.GetCommonBufferData();
	assert(data.useAmbientEffectLighting == 1u);
	assert(data.effectBrightness == 1.0f && data.skyStaticBrightness == 1.0f);
	balance.SetEnabled(false);
	assert(balance.GetCommonBufferData().useAmbientEffectLighting == 0u);
	assert(balance.settings.useAmbientEffectLighting);
	balance.SetEnabled(true);
	balance.SetPerformanceCostMeasurementEnabled(false);
	assert(balance.GetCommonBufferData().useAmbientEffectLighting == 0u);
	balance.SetPerformanceCostMeasurementEnabled(true);
	balance.loaded = false;
	assert(balance.GetCommonBufferData().useAmbientEffectLighting == 0u);
	balance.loaded = true;
	globals::stateValue.menuOpen = true;
	assert(balance.GetCommonBufferData().useAmbientEffectLighting == 0u);
	globals::stateValue.menuOpen = false;
	RE::PlayerCharacter::GetSingleton()->hasCell = false;
	assert(balance.GetCommonBufferData().useAmbientEffectLighting == 0u);
	RE::PlayerCharacter::GetSingleton()->hasCell = true;
	assert(balance.GetCommonBufferData().useAmbientEffectLighting == 1u);
	balance.settings.useAmbientEffectLighting = false;
	assert(balance.GetCommonBufferData().useAmbientEffectLighting == 0u);
}

void CheckAtmosphereControls()
{
	AdaptiveBrightness balance;
	auto& global = balance.settings.globalProfile;
	LinearLighting::Settings linear;
	linear.skyGamma = 1.65f;
	global.cloudBrightnessMult = 1.4f;
	global.cloudSaturation = 0.8f;
	global.cloudGammaOffset = -0.2f;
	global.fogIntensity = 0.5f;
	global.sunGlareIntensity = 2.0f;
	global.skyStaticBrightness = 1.5f;
	global.skyStaticTransparency = 0.25f;
	global.contrast = 1.2f;
	global.saturation = 0.9f;

	AdaptiveBrightness::ProfileSettings day, night;
	day.advanced = night.advanced = true;
	day.cloudBrightnessMult = 0.5f;
	night.cloudBrightnessMult = 1.0f;
	day.cloudGammaOffset = 0.4f;
	night.cloudGammaOffset = -0.4f;
	day.skyStaticTransparency = 0.5f;
	day.fogIntensity = night.fogIntensity = 0.5f;
	balance.testProfileBlend = { &day, &night, 0.5f };
	auto data = balance.GetCommonBufferData();
	assert(Close(data.cloudBrightness, 1.05f) && data.skyBrightness == 1.0f);
	assert(Close(data.cloudSaturation, 0.8f) && data.skySaturation == 1.0f);
	assert(Close(data.fogIntensity, 0.25f) && data.sunGlareIntensity == 2.0f);
	assert(Close(data.skyStaticTransparency, 0.4375f) && balance.GetEffectiveSharedLightingSettings().skyStaticBrightness == 1.5f);
	assert(Close(data.contrast, 1.2f) && Close(data.saturation, 0.9f));
	for (bool enabled : { false, true }) {
		const auto gamma = balance.GetEffectiveLinearLightingSettings(linear, enabled);
		assert(Close(gamma.settings.cloudGamma, (enabled ? 1.65f : 1.0f) - 0.2f));
		assert(Close(gamma.settings.skyGamma, enabled ? 1.65f : 1.0f));
		assert(gamma.hasColorAdjustments);
	}
	AdaptiveBrightness::LocationOverride location;
	location.profile.advanced = true;
	location.profile.cloudBrightnessMult = 0.5f;
	location.profile.skyStaticTransparency = 0.5f;
	location.profile.fogIntensity = 0.5f;
	balance.testLocationLayers = { &location };
	balance.testProfileBlend.factor = 0.0f;
	for (bool layered : { false, true }) {
		location.layered = layered;
		data = balance.GetCommonBufferData();
		assert(Close(data.cloudBrightness, layered ? 0.35f : 0.7f));
		assert(Close(data.skyStaticTransparency, layered ? 0.8125f : 0.625f));
		assert(Close(data.fogIntensity, layered ? 0.125f : 0.25f));
	}
	balance.testLocationLayers.clear();
	balance.testProfileBlend = {};
	global.advanced = false;
	data = balance.GetCommonBufferData();
	assert(data.cloudBrightness == 1 && data.cloudSaturation == 1 && data.fogIntensity == 1);
	assert(data.sunGlareIntensity == 1 && balance.GetEffectiveSharedLightingSettings().skyStaticBrightness == 1 && data.skyStaticTransparency == 0);
	assert(Close(data.contrast, 1.2f) && Close(data.saturation, 0.9f));
	assert(!balance.GetEffectiveLinearLightingSettings(linear, true).hasColorAdjustments);
	global.advanced = true;
	balance.SetEnabled(false);
	data = balance.GetCommonBufferData();
	assert(data.cloudBrightness == 1 && data.fogIntensity == 1 && data.sunGlareIntensity == 1);
	assert(data.skyStaticTransparency == 0 && data.contrast == 1 && data.saturation == 1);
	assert(Close(balance.GetEffectiveLinearLightingSettings(linear, true).settings.cloudGamma, linear.skyGamma));
	balance.SetEnabled(true);
	assert(Close(balance.GetCommonBufferData().cloudBrightness, 1.4f));
	global.fogIntensity = global.sunGlareIntensity = std::numeric_limits<float>::infinity();
	global.cloudBrightnessMult = global.cloudSaturation = std::numeric_limits<float>::quiet_NaN();
	global.skyStaticBrightness = 10;
	global.skyStaticTransparency = -1;
	global.cloudGammaOffset = 10;
	ClampProfileSettings(global);
	assert(global.fogIntensity == 1 && global.sunGlareIntensity == 1);
	assert(global.cloudBrightnessMult == 1 && global.cloudSaturation == 1);
	assert(global.skyStaticBrightness == 2 && global.skyStaticTransparency == 0 && global.cloudGammaOffset == 1);
	global.fogIntensity = global.sunGlareIntensity = day.fogIntensity = day.sunGlareIntensity = 5;
	global.skyStaticBrightness = day.skyStaticBrightness = 2;
	balance.testProfileBlend = { &day, &day, 0 };
	data = balance.GetCommonBufferData();
	assert(data.fogIntensity == 5 && data.sunGlareIntensity == 5 && balance.GetEffectiveSharedLightingSettings().skyStaticBrightness == 2);
}

void CheckAtmosphereMigrationAndValidation()
{
	json profile = { { "skyBrightnessMult", 1.4f }, { "skySaturation", 0.6f }, { "skyGammaOffset", -0.3f }, { "linearPointLightMult", 1.0f } };
	MigrateLegacyProfileLighting(profile);
	assert(profile["cloudBrightnessMult"] == profile["skyBrightnessMult"]);
	assert(profile["cloudSaturation"] == profile["skySaturation"]);
	assert(profile["cloudGammaOffset"] == profile["skyGammaOffset"]);
	const auto migrated = profile;
	profile["skyBrightnessMult"] = 2.0f;
	profile["skyGammaOffset"] = 0.8f;
	profile["cloudSaturation"] = 0.0f;
	MigrateLegacyProfileLighting(profile);
	assert(profile["cloudBrightnessMult"] == migrated["cloudBrightnessMult"]);
	assert(profile["cloudGammaOffset"] == migrated["cloudGammaOffset"] && profile["cloudSaturation"] == 0.0f);
	json empty = json::object();
	SettingsMigrations::MigrateCloudProfileSettings(empty);
	assert(empty.empty());
	json invalid = json::array();
	SettingsMigrations::MigrateCloudProfileSettings(invalid);
	assert(invalid.is_array());
	json legacyLayer = {
		{ "globalProfile", { { "skyBrightnessMult", 0.4 }, { "skySaturation", 0.0 }, { "contrast", 1.2 } } },
		{ "profiles", json::array({ { { "skyGammaOffset", -0.2 } } }) },
		{ "locationOverrides", json::array({ { { "profile", { { "skyBrightnessMult", 0.5 }, { "cloudBrightnessMult", 0.0 } } } }, nullptr }) }
	};
	assert(SettingsMigrations::MigrateCloudSettingsLayer(legacyLayer));
	assert(!SettingsMigrations::MigrateCloudSettingsLayer(legacyLayer));
	json defaults = { { "globalProfile", { { "cloudBrightnessMult", 1.0 }, { "cloudSaturation", 1.0 }, { "saturation", 0.8 } } } };
	defaults.merge_patch(legacyLayer);
	assert(defaults["globalProfile"]["cloudBrightnessMult"] == 0.4);
	assert(defaults["globalProfile"]["cloudSaturation"] == 0.0);
	assert(defaults["globalProfile"]["contrast"] == 1.2 && defaults["globalProfile"]["saturation"] == 0.8);
	assert(defaults["profiles"][0]["cloudGammaOffset"] == -0.2);
	assert(defaults["locationOverrides"][0]["profile"]["cloudBrightnessMult"] == 0.0);
	json legacyGlobal = { { "lighting", { { "skyBrightness", 1.7 } } } };
	assert(SettingsMigrations::MigrateCloudSettingsLayer(legacyGlobal));
	assert(legacyGlobal["globalProfile"]["cloudBrightnessMult"] == 1.7);
	legacyGlobal["globalProfile"] = { { "skyBrightnessMult", 0.6 } };
	assert(SettingsMigrations::MigrateCloudSettingsLayer(legacyGlobal));
	assert(legacyGlobal["globalProfile"]["cloudBrightnessMult"] == 0.6);

	const json valid = { { "cloudBrightness", 2 }, { "cloudSaturation", 0 }, { "cloudGammaOffset", -1 },
		{ "fogIntensity", 5 }, { "sunGlareIntensity", 0 }, { "effectBrightness", 2 }, { "skyStaticBrightness", 2 }, { "skyStaticTransparency", 1 },
		{ "lightingAdvanced", true }, { "useAmbientEffectLighting", true }, { "contrast", 1.2 }, { "saturation", 0.8 } };
	assert(ValidateAdaptiveBalanceVisuals(valid).empty());
	for (const auto& [name, value] : valid.items()) {
		if (name == "lightingAdvanced" || name == "useAmbientEffectLighting") {
			assert(ValidateAdaptiveBalanceVisuals({ { name, false } }).empty());
			assert(!ValidateAdaptiveBalanceVisuals({ { name, 1 } }).empty());
			assert(!ValidateAdaptiveBalanceVisuals({ { name, "true" } }).empty());
			continue;
		}
		assert(!ValidateAdaptiveBalanceVisuals({ { name, 1000 } }).empty());
		assert(!ValidateAdaptiveBalanceVisuals({ { name, -1000 } }).empty());
		assert(!ValidateAdaptiveBalanceVisuals({ { name, true } }).empty());
		assert(!ValidateAdaptiveBalanceVisuals({ { name, "1" } }).empty());
		assert(!ValidateAdaptiveBalanceVisuals({ { name, std::numeric_limits<double>::infinity() } }).empty());
	}
	assert(!ValidateAdaptiveBalanceVisuals({ { "cloudBrightness", 1 }, { "unknown", 1 } }).empty());
}

void CheckAppearanceProfiles()
{
	using namespace AdaptiveBalanceAppearance;
	const Settings defaults;
	const json defaultJson = defaults;
	assert(defaultJson == json(defaultJson.get<Settings>()));
	assert(!defaultJson.contains("moonIntensity") && !defaultJson.contains("auroraIntensity"));
	for (const auto& field : kScalars) {
		assert(defaults.*field.member == field.neutral);
		Settings invalid;
		invalid.*field.member = std::numeric_limits<float>::quiet_NaN();
		Sanitize(invalid);
		assert(invalid.*field.member == field.neutral);
		invalid.*field.member = field.maximum + 10.0f;
		Sanitize(invalid);
		assert(invalid.*field.member == field.maximum);
		invalid.*field.member = field.minimum - 10.0f;
		Sanitize(invalid);
		assert(invalid.*field.member == field.minimum);
		const auto low = AdaptiveBalanceAppearanceBound(field.minimum);
		const auto high = AdaptiveBalanceAppearanceBound(field.maximum);
		for (const double value : { low, high, double(field.neutral) })
			assert(ValidateAdaptiveBalanceVisuals({ { "appearance", { { field.name, value } } } }).empty());
		for (const auto& value : std::vector<json>{ low - 0.001, high + 0.001, true, "1", nullptr, std::numeric_limits<double>::infinity() })
			assert(!ValidateAdaptiveBalanceVisuals({ { "appearance", { { field.name, value } } } }).empty());
		if (field.neutral == 0.0f)
			continue;
		AdaptiveBrightness balance;
		balance.settings.globalProfile.appearance.*field.member = 1.25f;
		AdaptiveBrightness::ProfileSettings day, night;
		day.advanced = night.advanced = true;
		day.appearance.*field.member = 0.5f;
		night.appearance.*field.member = 1.5f;
		balance.testProfileBlend = { &day, &night, 0.25f };
		assert(Close(balance.GetCommonBufferData().appearance.*field.member, 0.9375f));
		AdaptiveBrightness::LocationOverride location;
		location.profile.advanced = true;
		location.profile.appearance.*field.member = 0.4f;
		balance.testLocationLayers = { &location };
		assert(Close(balance.GetCommonBufferData().appearance.*field.member, 0.5f));
		location.layered = true;
		assert(Close(balance.GetCommonBufferData().appearance.*field.member, 0.375f));
		balance.SetEnabled(false);
		assert(balance.GetCommonBufferData().appearance.*field.member == 1.0f);
		balance.SetEnabled(true);
		balance.testLocationLayers.clear();
		balance.testProfileBlend = {};
		balance.settings.globalProfile.advanced = false;
		assert(balance.GetCommonBufferData().appearance.*field.member == 1.0f);
	}
	for (const auto& field : kTints) {
		assert(ValidateAdaptiveBalanceVisuals({ { "appearance", { { field.name, { 0.0, 0.5, 1.0 } } } } }).empty());
		for (const auto& value : std::vector<json>{ 1.0, json::array({ 1, 1 }), json::array({ 1, 1, 1, 1 }), json::array({ 1, -0.1, 1 }), json::array({ 1, true, 1 }), json::array({ 1, field.maximum + 0.01, 1 }) })
			assert(!ValidateAdaptiveBalanceVisuals({ { "appearance", { { field.name, value } } } }).empty());
		Settings layer;
		layer.*field.member = { 0.25f, 0.5f, 0.75f };
		const json encoded = layer;
		const auto decoded = encoded.get<Settings>();
		assert((decoded.*field.member).y == 0.5f);
	}
	for (const auto& value : std::vector<json>{ nullptr, true, json::array(), 1.0, json{ { "missingControl", 1.0 } }, json{ { "moonIntensity", 1.0 } }, json{ { "auroraIntensity", 1.0 } } })
		assert(!ValidateAdaptiveBalanceVisuals({ { "appearance", value } }).empty());

	for (const auto& malformed : std::vector<json>{ nullptr, true, json::array(),
			 json{ { "directionalCurve", true } }, json{ { "cloudTint", { 1, 1 } } },
			 json{ { "fogTint", { 1, 1, 1, 1 } } }, json{ { "skyTopTint", { 1, "1", 1 } } }, json{ { "directionalCurve", 3 }, { "cloudTint", { 1, 1 } } } }) {
		Settings retained;
		retained.directionalCurve = 2;
		const json before = retained;
		bool rejected = false;
		try {
			from_json(malformed, retained);
		} catch (const json::exception&) {
			rejected = true;
		}
		assert(rejected && json(retained) == before);
	}
	assert(json::object().get<Settings>().directionalCurve == 1);
	Settings red, blue;
	red.godrayTint = { 1, 0, 0 };
	red.godrayTintAmount = 0.5f;
	blue.godrayTint = { 0, 0, 1 };
	blue.godrayTintAmount = 0.5f;
	const auto unchanged = Compose(red, {});
	assert(json(unchanged) == json(red));
	const auto overlay = Compose(red, blue);
	assert(Close(overlay.godrayTintAmount, 0.75f));
	assert(Close(overlay.godrayTint.x, 1.0f / 3.0f) && Close(overlay.godrayTint.z, 2.0f / 3.0f));
	const auto midpoint = Lerp({}, red, 0.5f);
	assert(Close(midpoint.godrayTintAmount, 0.25f));
	assert(midpoint.godrayTint.x == 1 && midpoint.godrayTint.y == 0);
	Settings tinted;
	tinted.directionalTint = { 0.5f, 1.5f, 0.75f };
	const auto twice = Compose(tinted, tinted);
	assert(Close(twice.directionalTint.x, 0.25f) && twice.directionalTint.y == 2.0f);

	const auto schema = BuildAdaptiveBalanceAppearanceSchema();
	for (const auto& field : kScalars) {
		const auto& property = schema.at("properties").at(field.name);
		assert(property.at("minimum") == AdaptiveBalanceAppearanceBound(field.minimum));
		assert(property.at("maximum") == AdaptiveBalanceAppearanceBound(field.maximum));
	}
	std::cout << "Appearance profiles: neutral defaults, all field bounds, JSON/schema, day/night and location composition passed\n";
}

void CheckSkyGradientOwnership()
{
	auto& balance = globals::features::adaptiveBrightness;
	balance = {};
	RE::Weather weather;
	RE::Sky sky;
	sky.currentWeather = &weather;
	const RE::NiColor source{ { 0.25f, 0.5f, 0.75f } };
	sky.skyColor[RE::TESWeather::kSkyUpper] = source;
	auto& appearance = balance.settings.globalProfile.appearance;
	appearance.skyTopIntensity = 2.0f;
	appearance.skyTopCurve = 2.0f;
	appearance.skyTopTint = { 1.0f, 0.5f, 0.0f };
	for (int frame = 0; frame < 3; ++frame) {
		WeatherUpdateHook::thunk(&sky, 0);
		const auto& color = sky.skyColor[RE::TESWeather::kSkyUpper];
		assert(Close(color[0], 0.125f) && Close(color[1], 0.25f) && color[2] == 0.0f);
	}
	balance.SetEnabled(false);
	WeatherUpdateHook::thunk(&sky, 0);
	assert(sky.skyColor[RE::TESWeather::kSkyUpper] == source);
	balance.SetEnabled(true);
	WeatherUpdateHook::thunk(&sky, 0);
	const RE::NiColor external{ { 0.3f, 0.3f, 0.3f } };
	sky.skyColor[RE::TESWeather::kSkyUpper] = external;
	balance.SetEnabled(false);
	WeatherUpdateHook::thunk(&sky, 0);
	assert(sky.skyColor[RE::TESWeather::kSkyUpper] == external);
	WeatherUpdateHook::thunk(nullptr, 0);
	balance = {};
}

void CheckFinalGodrayBrightness()
{
	AdaptiveBrightness balance;
	assert(balance.settings.godrayFinalBrightness == 1.0f);
	balance.settings.globalProfile.advanced = false;
	balance.throwProfileResolution = true;
	balance.settings.godrayFinalBrightness = 4.5f;
	for (unsigned flags = 0; flags < 32; ++flags) {
		balance.loaded = (flags & 1) != 0;
		balance.SetEnabled((flags & 2) != 0);
		balance.SetPerformanceCostMeasurementEnabled((flags & 4) != 0);
		globals::state->menuOpen = (flags & 8) != 0;
		RE::PlayerCharacter::GetSingleton()->hasCell = (flags & 16) != 0;
		const bool active = (flags & 7) == 7 && !(flags & 8) && (flags & 16);
		assert(balance.GetEffectiveGodrayFinalBrightness() == (active ? 4.5f : 1.0f));
		assert(balance.settings.godrayFinalBrightness == 4.5f);
	}
	globals::state->menuOpen = false;
	RE::PlayerCharacter::GetSingleton()->hasCell = true;
	balance.loaded = true;
	balance.SetEnabled(true);
	balance.SetPerformanceCostMeasurementEnabled(true);
	struct BrightnessCase
	{
		float input;
		float expected;
		bool valid;
	};
	for (const auto& [input, expected, valid] : std::array<BrightnessCase, 8>{ { { 0.0f, 0.0f, true }, { 1.0f, 1.0f, true }, { 5.0f, 5.0f, true },
			 { -0.01f, 0.0f, false }, { 5.01f, 5.0f, false },
			 { std::numeric_limits<float>::infinity(), 1.0f, false },
			 { -std::numeric_limits<float>::infinity(), 1.0f, false },
			 { std::numeric_limits<float>::quiet_NaN(), 1.0f, false } } }) {
		// Runtime inputs keep nonfinite cases meaningful under /fp:fast.
		volatile float runtimeInput = input;
		balance.settings.godrayFinalBrightness = runtimeInput;
		assert(balance.GetEffectiveGodrayFinalBrightness() == expected);
		assert(AdaptiveBalanceGodray::SanitizeFinalBrightness(runtimeInput) == expected);
		assert(AdaptiveBalanceGodray::IsValidFinalBrightness(runtimeInput) == valid);
	}
	assert(balance.profileResolutions == 0);
	for (const auto value : { 0.0, 1.0, 5.0 })
		assert(ValidateAdaptiveBalanceVisuals({ { "godrayFinalBrightness", value } }).empty());
	for (const auto& value : std::vector<json>{ -0.01, 5.01, true, nullptr, "1",
			 std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN() })
		assert(!ValidateAdaptiveBalanceVisuals({ { "godrayFinalBrightness", value } }).empty());
}

int main()
{
	CheckFinalGodrayBrightness();
	CheckAppearanceProfiles();
	CheckSkyGradientOwnership();
	CheckWeatherColors();
	CheckWeatherBrightnessComposition();
	CheckAtmosphereControls();
	CheckAmbientEffectLighting();
	CheckAtmosphereMigrationAndValidation();
	CheckColorControls();
	CheckPointLightSaturation();
	CheckPointLightCurve();
	CheckFireControls();
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
	global.pointLightSaturation = 0.3f;
	global.pointLightCurve = 1.5f;
	global.fireIntensity = 2.0f;
	global.fireSaturation = 0.5f;
	global.fireCurve = 1.5f;
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
		assert(balance.GetCommonBufferData().pointLightSaturation == beforeLight.pointLightSaturation);
		assert(balance.GetCommonBufferData().pointLightCurve == beforeLight.pointLightCurve);
		assert(balance.GetCommonBufferData().fireIntensity == beforeLight.fireIntensity);
		assert(balance.GetCommonBufferData().fireSaturation == beforeLight.fireSaturation);
		assert(balance.GetCommonBufferData().fireCurve == beforeLight.fireCurve);
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
