#include "../package/Shaders/Common/WetternessDrying.hlsli"
#include "Features/WetternessLightingPolicy.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

using uint = unsigned int;
struct float2
{
	float x, y;
};
struct float4
{
	float x, y, z, w;
};
namespace WetternessSnowMelt
{
	bool active = false;
	bool HasHistory() { return active; }
}
namespace REX::W32
{
	struct XMFLOAT4X4
	{
		float data[16]{};
	};
}
#define STATIC_ASSERT_ALIGNAS_16(Type) static_assert(sizeof(Type) % 16 == 0)
unsigned weatherUpdates = 0;
#define CS_PROFILE_CPU_SCOPE(label) ++weatherUpdates

struct ID3D11ShaderResourceView
{};
namespace RE
{
	template <class T>
	struct Value
	{
		T value{};
		T get() const { return value; }
		bool any(T flag) const { return (static_cast<uint>(value) & static_cast<uint>(flag)) != 0; }
	};
	struct BGSShaderParticleGeometryData
	{
		enum class DataID
		{
			kParticleDensity
		};
		struct Setting
		{
			float f;
		};
		float density = 3.0f;
		Setting GetSettingValue(DataID) const { return { density }; }
	};
	struct TESWeather
	{
		enum class WeatherDataFlag
		{
			kRainy = 1,
			kSnow = 2,
			kCloudy = 4,
			kPleasant = 8
		};
		struct
		{
			Value<WeatherDataFlag> flags;
		} data;
		BGSShaderParticleGeometryData* precipitationData = nullptr;
	};
	struct BSParticleShaderRainEmitter
	{
		REX::W32::XMFLOAT4X4 occlusionProjection{};
	};
	struct Geometry
	{
		BSParticleShaderRainEmitter* emitter = nullptr;
		BSParticleShaderRainEmitter* get() const { return emitter; }
		explicit operator bool() const { return emitter != nullptr; }
	};
	struct Precipitation
	{
		Geometry currentPrecip, lastPrecip;
	};
	struct Climate
	{
		struct
		{
			struct
			{
				float begin, end;
			} sunrise{ 36, 48 }, sunset{ 108, 120 };
		} timing;
	};
	struct Sky
	{
		enum class Mode
		{
			kFull,
			kInterior
		};
		Value<Mode> mode{ Mode::kFull };
		TESWeather* currentWeather = nullptr;
		TESWeather* lastWeather = nullptr;
		float currentWeatherPct = 1.0f, currentGameHour = 12.0f;
		Climate* currentClimate = nullptr;
		Precipitation* precip = nullptr;
		bool raining = false;
		bool IsRaining() const { return raining; }
	};
	struct Calendar
	{
		static inline Calendar* singleton = nullptr;
		double seconds = 0.0;
		static Calendar* GetSingleton() { return singleton; }
		double GetCurrentGameTime() const { return seconds / 86400.0; }
		uint32_t GetMonth() const { return 6; }
	};
	float frameDelta = 1.0f / 60.0f;
	float GetSecondsSinceLastFrame() { return frameDelta; }
	struct UI
	{
		bool paused = false;
		bool GameIsPaused() const { return paused; }
	};
	enum RENDER_TARGETS_DEPTHSTENCIL
	{
		kPRECIPITATION_OCCLUSION_MAP
	};
	struct Renderer
	{
		struct DepthData
		{
			struct
			{
				ID3D11ShaderResourceView* depthSRV = nullptr;
			} depthStencils[1];
		} depths;
		DepthData& GetDepthStencilData() { return depths; }
	};
}
struct Context
{
	unsigned binds = 0, clears = 0;
	void PSSetShaderResources(uint, uint count, ID3D11ShaderResourceView* const* views)
	{
		++binds;
		if (count == 2 && !views[0] && !views[1])
			++clears;
	}
};
namespace globals
{
	struct State
	{
		uint32_t frameCount = 0;
	};
	State* state = nullptr;
	namespace game
	{
		RE::Sky* sky = nullptr;
		RE::UI* ui = nullptr;
		RE::Renderer* renderer = nullptr;
	}
	namespace d3d
	{
		Context* context = nullptr;
	}
}
namespace Util
{
	namespace Units
	{
		constexpr float GAME_UNIT_TO_M = 0.01428f;
	}
	bool interior = false;
	bool IsInterior() { return interior; }
	RE::BSParticleShaderRainEmitter* GetRainEmitterFromPrecipGeometry(RE::BSParticleShaderRainEmitter* emitter) { return emitter; }
}

struct Wetterness
{
#include "wetterness_members.h"
#include "wetterness_state.h"
	bool loaded = true, rainResourcesBound = false;
	struct
	{
		ID3D11ShaderResourceView view;
		ID3D11ShaderResourceView* get() const { return const_cast<ID3D11ShaderResourceView*>(&view); }
		explicit operator bool() const { return true; }
	} puddleMaskSrv;
	static constexpr float MAX_RAIN_PARTICLE_DENSITY = 3.0f;
	bool IsRuntimeActive() const { return loaded && settings.EnableWetterness != 0; }
	void ResetRuntimeState() const;
	void InvalidateSanitizedSettingsCache();
	const Settings& GetSanitizedSettings() const;
	bool HasPendingDrying() const;
	void DecayRainHistory(float) const;
	float UpdateRuntimeClock(bool) const;
	PerFrame MakeDryPerFrameData() const;
	bool IsRuntimeProcessingActive() const;
	float4 GetDryingHours(float) const;
	PerFrame GetCommonBufferData() const;
	void Prepass();
};
namespace
{
	// clang-format off
#include "wetterness_constants.h"
#include "wetterness_weather.h"
#include "wetterness_shader_settings.h"
#include "wetterness_packing.h"
#include "wetterness_sanitization.h"
	// clang-format on
}
#include "wetterness_activity.h"
#include "wetterness_reset.h"
#include "wetterness_update.h"

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	struct Fixture
	{
		Wetterness wetness;
		globals::State state;
		RE::Calendar calendar;
		RE::UI ui;
		RE::Sky sky;
		RE::Climate climate;
		RE::BGSShaderParticleGeometryData particles;
		RE::TESWeather clear, rainy;
		RE::BSParticleShaderRainEmitter emitter;
		RE::Precipitation precip;
		RE::Renderer renderer;
		Context context;

		Fixture()
		{
			globals::state = &state;
			globals::game::sky = &sky;
			globals::game::ui = &ui;
			globals::game::renderer = &renderer;
			globals::d3d::context = &context;
			RE::Calendar::singleton = &calendar;
			RE::frameDelta = 1.0f / 60.0f;
			Util::interior = false;
			weatherUpdates = 0;
			WetternessSnowMelt::active = false;
			wetness.ResetRuntimeState();
			clear.data.flags.value = RE::TESWeather::WeatherDataFlag::kPleasant;
			rainy.data.flags.value = RE::TESWeather::WeatherDataFlag::kRainy;
			rainy.precipitationData = &particles;
			sky.currentWeather = &clear;
			sky.lastWeather = &clear;
			sky.currentClimate = &climate;
			sky.precip = &precip;
		}
		Wetterness::PerFrame Step(double seconds = 1)
		{
			calendar.seconds += seconds;
			++state.frameCount;
			auto data = wetness.GetCommonBufferData();
			wetness.Prepass();
			return data;
		}
		void Rain()
		{
			sky.currentWeather = &rainy;
			sky.raining = true;
			precip.currentPrecip.emitter = &emitter;
		}
		void Dry()
		{
			sky.currentWeather = sky.lastWeather = &clear;
			sky.raining = false;
			precip.currentPrecip.emitter = nullptr;
		}
	};

	void CheckIdleAndRestart()
	{
		Fixture f;
		f.wetness.settings.MaxShoreWetness = 0.63f;
		f.wetness.settings.ShoreRange = 53;
		f.wetness.shorePersistentDarkeningStrength = 1.4f;
		const auto fresh = f.Step();
		Require(!fresh.settings.EnableWetterness && weatherUpdates == 0 && f.context.binds == 0, "Fresh dry weather did rain work");
		f.Rain();
		f.Step();
		const auto raining = f.Step(8 * 3600);
		Require(raining.Raining > 0 && raining.Wetness > 0 && raining.PuddleWetness > 0, "Rain did not accumulate");
		f.Dry();
		f.Step();
		const auto dried = f.Step(16 * 3600);
		Require(!dried.settings.EnableWetterness && dried.Wetness == 0 && dried.PuddleWetness == 0 && dried.GrassWetnessPhase == 0, "Dry storm kept render work active");
		Require(!f.wetness.IsRuntimeProcessingActive() && f.wetness.runtimeState.postRainEventWeight == 0, "Finished drying retained its activity latch");
		Require(f.wetness.runtimeState.rainEventExposure > 0, "Idle transition erased future storm history");
		Require(dried.settings.MaxShoreWetness == fresh.settings.MaxShoreWetness && dried.settings.ShoreRange == fresh.settings.ShoreRange &&
					dried.settings.ShorePersistentDarkeningStrength == fresh.settings.ShorePersistentDarkeningStrength,
			"Drying changed shore controls");
		const auto updates = weatherUpdates, binds = f.context.binds;
		const float exposure = f.wetness.runtimeState.rainEventExposure;
		f.Step(3600);
		Require(weatherUpdates == updates && f.context.binds == binds && f.context.clears == 1, "Idle frames repeated weather or resource work");
		Require(f.wetness.runtimeState.rainEventExposure < exposure, "Idle weather history stopped decaying");
		f.Step(48 * 3600);
		Require(f.wetness.runtimeState.rainEventExposure == 0 && weatherUpdates == updates && f.context.binds == binds, "Exhausted history reactivated dry work");
		f.Rain();
		const auto restarted = f.Step(1);
		Require(restarted.Raining > 0 && restarted.Wetness > 0 && restarted.Wetness < 0.01f, "Rain restart accumulated dry elapsed time or stayed disabled");
		const auto cached = f.wetness.GetCommonBufferData();
		Require(cached.Wetness == restarted.Wetness && weatherUpdates == updates + 1, "Same frame advanced the simulation twice");
	}

	void CheckGrassAndClock()
	{
		Fixture f;
		f.wetness.enableWeatherDrivenDryingModel = false;
		f.wetness.settings.StoneDryingMultiplier = f.wetness.settings.DirtDryingMultiplier = 1;
		f.wetness.puddleDryingHours = 1;
		f.wetness.settings.GrassDryingMultiplier = 24;
		f.Step();
		f.Rain();
		f.Step(3600);
		f.Dry();
		f.Step();
		auto data = f.Step(12 * 3600);
		Require(data.Wetness == 0 && data.PuddleWetness == 0 && data.GrassWetnessPhase > 0.49f, "Grass drying was cut off with surface wetness");
		f.ui.paused = true;
		const float phase = data.GrassWetnessPhase;
		data = f.Step(0);
		Require(data.GrassWetnessPhase == phase, "Paused frame advanced drying");
		Util::interior = true;
		f.sky.mode.value = RE::Sky::Mode::kInterior;
		data = f.Step(13 * 3600);
		Require(!data.settings.EnableWetterness && !f.wetness.HasPendingDrying(), "Wait/interior transition prevented dry retirement");
		Util::interior = false;
		f.sky.mode.value = RE::Sky::Mode::kFull;
		f.ui.paused = false;
		f.Rain();
		f.Step(3600);
		f.Dry();
		data = f.Step(-48 * 3600);
		Require(!data.settings.EnableWetterness && f.wetness.runtimeState.rainEventExposure == 0, "Calendar rewind retained wet state");
		RE::Calendar::singleton = nullptr;
		f.Rain();
		data = f.Step();
		Require(data.Wetness > 0, "Missing calendar did not use frame delta");
		const float fallbackDepth = f.wetness.runtimeState.wetnessDepth;
		RE::Calendar::singleton = &f.calendar;
		f.Step(3600);
		Require(f.wetness.runtimeState.wetnessDepth == fallbackDepth, "Returning calendar counted fallback elapsed time twice");
		f.Step(1);
		Require(f.wetness.runtimeState.wetnessDepth > fallbackDepth, "Returning calendar did not resume progression");
		f.calendar.seconds = std::numeric_limits<double>::quiet_NaN();
		f.Step();
		Require(!f.wetness.runtimeState.hasLastGameTime && std::isfinite(f.wetness.runtimeState.wetnessDepth), "Invalid calendar poisoned runtime state");
		RE::frameDelta = std::numeric_limits<float>::infinity();
		const float validDepth = f.wetness.runtimeState.wetnessDepth;
		f.Step();
		Require(f.wetness.runtimeState.wetnessDepth == validDepth, "Invalid frame time advanced wetness");
		f.calendar.seconds = 1000;
		f.Step();
		Require(f.wetness.runtimeState.hasLastGameTime && f.wetness.runtimeState.wetnessDepth == validDepth, "Valid calendar did not recover cleanly");
	}

	void CheckTransitionsAndOverrides()
	{
		Fixture f;
		f.Step();
		f.sky.lastWeather = &f.rainy;
		f.sky.currentWeatherPct = 0.5f;
		f.precip.lastPrecip.emitter = &f.emitter;
		Require(f.wetness.IsRuntimeProcessingActive(), "Activity query missed outgoing rain");
		const auto transition = f.Step(60);
		Require(transition.Raining > 0 && transition.Wetness > 0, "Outgoing rain was skipped when engine flag cleared");
		f.Dry();
		f.sky.currentWeatherPct = 1;
		f.wetness.ResetRuntimeState();
		f.wetness.debugSettings.EnableWetnessOverride = true;
		f.wetness.debugSettings.WetnessOverride.y = 0.4f;
		Require(f.Step().Wetness == 0.4f, "Dry fast path ignored debug override");
		f.wetness.settings.EnableWetterness = 0;
		const auto disabled = f.Step();
		Require(!disabled.settings.EnableWetterness && disabled.Wetness == 0 && disabled.settings.MaxShoreWetness == 0, "Master disable retained effects");
		const auto binds = f.context.binds;
		f.Step();
		Require(f.context.binds == binds, "Master disable repeated resource clearing");
	}

	void CheckRainMetadata()
	{
		Fixture f;
		f.Step();
		f.Rain();
		Util::interior = true;
		f.sky.mode.value = RE::Sky::Mode::kInterior;
		Require(f.wetness.IsRuntimeProcessingActive(), "Activity query missed indoor weather history");
		auto data = f.Step(3600);
		Require(data.Raining == 0 && data.Wetness == 0 && f.wetness.runtimeState.wetnessDepth > 0, "Interior rain failed to preserve the hidden timeline");
		Util::interior = false;
		f.sky.mode.value = RE::Sky::Mode::kFull;
		data = f.Step(0);
		Require(data.Wetness > 0, "Leaving an interior lost accumulated rain wetness");
		f.wetness.ResetRuntimeState();
		f.particles.density = 0;
		const auto updates = weatherUpdates;
		data = f.Step();
		Require(!f.wetness.IsRuntimeProcessingActive() && !data.settings.EnableWetterness && weatherUpdates == updates, "Zero-density rain kept dry processing active");
		f.particles.density = std::numeric_limits<float>::quiet_NaN();
		data = f.Step();
		Require(!data.settings.EnableWetterness && weatherUpdates == updates, "Invalid precipitation activated rain processing");
		f.wetness.loaded = false;
		data = f.Step();
		Require(data.settings.MaxShoreWetness == 0 && !f.wetness.IsRuntimeProcessingActive(), "Unloaded feature retained effects");
	}

	void CheckSnowMeltLifecycle()
	{
		Fixture f;
		f.Step();
		WetternessSnowMelt::active = true;
		auto data = f.Step(3600);
		Require(data.settings.EnableWetterness && f.wetness.IsRuntimeProcessingActive(), "Local snowmelt did not keep Wetterness shading available");
		Require(data.Raining == 0 && data.Wetness == 0 && data.PuddleWetness == 0 && data.GrassWetnessPhase == 0 &&
					!data.settings.EnableRaindropFx,
			"Local snowmelt created global rain or grass wetness");
		WetternessSnowMelt::active = false;
		data = f.Step(24 * 3600);
		Require(!data.settings.EnableWetterness && !f.wetness.IsRuntimeProcessingActive(), "Expired snowmelt did not restore dry inactivity");
	}
}

int main()
{
	try {
		CheckIdleAndRestart();
		CheckGrassAndClock();
		CheckTransitionsAndOverrides();
		CheckRainMetadata();
		CheckSnowMeltLifecycle();
		std::cout << "Wetterness runtime lifecycle passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
