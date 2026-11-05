#include "SnowMelt.h"

#include "Features/SnowCover.h"
#include "Features/Wetterness.h"
#include "Utils/Game.h"

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <type_traits>
#include <unordered_map>

namespace WetternessSnowMelt
{
	static_assert(MaxFires == std::extent_v<decltype(SnowCover::FireMeltData::Spheres)>);

	namespace
	{
		History history;
		std::mutex historyMutex;
		std::atomic<bool> historyAvailable{ false };
		SnowCover::UserSettings previousSettings{};
		SnowCover::WorldSettings previousWorld{};
		bool hasConfiguration = false;
		struct WorldHistory
		{
			History history;
			SnowCover::UserSettings settings;
			SnowCover::WorldSettings world;
		};
		std::unordered_map<std::string, WorldHistory> worlds;
		std::string currentWorld;
		double lastObservedHours = -1.0;
		constexpr size_t MaximumWorldHistories = 32;
	}

	void Reset()
	{
		const std::scoped_lock lock(historyMutex);
		history.Clear();
		worlds.clear();
		currentWorld.clear();
		lastObservedHours = -1.0;
		hasConfiguration = false;
		historyAvailable.store(false, std::memory_order_relaxed);
	}
	void SuspendWorld()
	{
		const std::scoped_lock lock(historyMutex);
		if (!currentWorld.empty() && history.HasSamples() && hasConfiguration) {
			if (worlds.size() == MaximumWorldHistories && !worlds.contains(currentWorld)) {
				const auto oldest = std::min_element(worlds.begin(), worlds.end(), [](const auto& a, const auto& b) {
					return a.second.history.LastHours() < b.second.history.LastHours();
				});
				worlds.erase(oldest);
			}
			worlds.insert_or_assign(currentWorld, WorldHistory{ history, previousSettings, previousWorld });
		}
		currentWorld.clear();
		history.Clear();
		hasConfiguration = false;
		historyAvailable.store(false, std::memory_order_relaxed);
	}

	void ResumeWorld(SnowCover& snow)
	{
		const std::scoped_lock lock(historyMutex);
		currentWorld = snow.lastWorldspace;
		snow.timeSnowing = 0.0f;
		snow.lastGameHours = -1.0;
		const auto found = worlds.find(currentWorld);
		if (found == worlds.end())
			return;
		history = found->second.history;
		previousSettings = found->second.settings;
		previousWorld = found->second.world;
		hasConfiguration = true;
		worlds.erase(found);
		if (history.HasSamples()) {
			snow.timeSnowing = history.Latest().state[0];
			snow.lastGameHours = history.LastHours();
		}
	}
	bool HasHistory()
	{
		return globals::features::wetterness.IsRuntimeActive() && globals::features::snowCover.IsActive() &&
		       historyAvailable.load(std::memory_order_relaxed);
	}

	void Update(const SnowCover& snow, float snowRate)
	{
		const auto* calendar = globals::game::calendar;
		if (!globals::features::wetterness.IsRuntimeActive() || !calendar) {
			Reset();
			return;
		}
		const std::scoped_lock lock(historyMutex);
		const double hours = static_cast<double>(calendar->GetCurrentGameTime()) * 24.0;
		if (!std::isfinite(hours) || hours < lastObservedHours) {
			history.Clear();
			worlds.clear();
			historyAvailable.store(false, std::memory_order_relaxed);
		}
		lastObservedHours = std::isfinite(hours) ? hours : -1.0;
		if (!snow.IsActive() || Util::IsInterior())
			return;
		currentWorld = snow.lastWorldspace;
		const auto& frame = snow.perFrame;
		if (hasConfiguration && (std::memcmp(&previousSettings, &frame.settings, sizeof(previousSettings)) != 0 ||
									std::memcmp(&previousWorld, &frame.wsettings, sizeof(previousWorld)) != 0))
			history.Clear();
		previousSettings = frame.settings;
		previousWorld = frame.wsettings;
		hasConfiguration = true;
		if (history.HasSamples() && hours == history.LastHours())
			return;
		Sample sample{};
		sample.state = { frame.TimeSnowing, frame.SnowingDensity, frame.SeasonalAltitude, 0.0f };
		sample.fireCount = frame.fireMelt.Count;
		sample.fireStrength = frame.fireMelt.Strength;
		sample.fireRadiusScale = frame.fireMelt.RadiusScale;
		for (uint32_t i = 0; i < std::min(sample.fireCount, MaxFires); ++i) {
			const auto& sphere = frame.fireMelt.Spheres[i];
			sample.fires[i] = { sphere.x, sphere.y, sphere.z, sphere.w };
		}
		const auto light = globals::features::wetterness.GetDryingHours(0.0f);
		const auto heavy = globals::features::wetterness.GetDryingHours(1.0f);
		history.Observe(sample, hours, snowRate,
			{ light.x, light.y, light.z, light.w }, { heavy.x, heavy.y, heavy.z, heavy.w });
		historyAvailable.store(history.HasMelt(), std::memory_order_relaxed);
	}

	PerFrame GetCommonBufferData()
	{
		if (!HasHistory() || !globals::features::snowCover.IsActive() || Util::IsInterior())
			return {};
		const std::scoped_lock lock(historyMutex);
		auto data = history.GetData();
		const auto light = globals::features::wetterness.GetDryingHours(0.0f);
		const auto heavy = globals::features::wetterness.GetDryingHours(1.0f);
		data.lightDryingHours = { light.x, light.y, light.z, light.w };
		data.heavyDryingHours = { heavy.x, heavy.y, heavy.z, heavy.w };
		return data;
	}
}
