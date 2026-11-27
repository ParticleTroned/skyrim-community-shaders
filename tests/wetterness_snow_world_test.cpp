#include "Features/Wetterness/SnowMelt.h"

#include <atomic>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>

struct Vector4
{
	float x = 0, y = 0, z = 0, w = 0;
};
struct SnowCover
{
	struct UserSettings
	{
		float offset = 0;
	};
	struct WorldSettings
	{
		float scale = 1;
	};
	struct FireMeltData
	{
		uint32_t Count = 0;
		float Strength = 1, RadiusScale = 1;
		Vector4 Spheres[8]{};
	};
	struct Frame
	{
		UserSettings settings;
		WorldSettings wsettings;
		float TimeSnowing = 0, SnowingDensity = 500, SeasonalAltitude = 0;
		FireMeltData fireMelt;
	} perFrame;
	struct FireSettings
	{
		bool Enabled = true;
		float RadiusScale = 1, Strength = 1;
	} fireMeltSettings;
	bool meltWetnessEnabled = true, active = true;
	std::string lastWorldspace = "Tamriel";
	float timeSnowing = 0;
	double lastGameHours = -1;
	unsigned MaxSummerMonth = 6, MaxWinterMonth = 0;
	float SummerHeightOffset = 20000, WinterHeightOffset = -20000, snowingSpeed = 1, meltingSpeed = 1;
	bool IsActive() const { return active; }
};
namespace globals
{
	namespace game
	{
		struct Calendar
		{
			double hours = 0;
			double GetCurrentGameTime() const { return hours / 24; }
		} clock;
		Calendar* calendar = &clock;
	}
	namespace features
	{
		SnowCover snowCover;
		struct Wetterness
		{
			bool active = true;
			bool IsRuntimeActive() const { return active; }
			Vector4 GetDryingHours(float) const { return { 24, 24, 24, 24 }; }
		} wetterness;
	}
}
namespace Util
{
	bool interior = false;
	bool IsInterior() { return interior; }
}

#include "snow_melt_runtime.h"

int main()
{
	using namespace WetternessSnowMelt;
	auto require = [](bool condition, const char* message) {
		if (!condition)
			throw std::runtime_error(message);
	};
	auto& snow = globals::features::snowCover;
	auto observe = [&](double hours, float amount) {
		globals::game::clock.hours = hours;
		snow.perFrame.TimeSnowing = amount;
		Update(snow, { -1, -1 });
	};
	ResumeWorld(snow);
	observe(100, 1);
	observe(101, 0);
	require(HasHistory() && GetCommonBufferData().enabled, "Melt history was not published");
	const auto before = GetCommonBufferData();
	observe(101, 0);
	require(GetCommonBufferData().count == before.count, "Paused clock changed history");
	SuspendWorld();
	Util::interior = true;
	snow.lastWorldspace = "Interior";
	observe(102, 0);
	require(!HasHistory() && !GetCommonBufferData().enabled, "Interior retained active snowmelt");
	SuspendWorld();
	Util::interior = false;
	snow.lastWorldspace = "Tamriel";
	ResumeWorld(snow);
	require(snow.lastGameHours == 101 && snow.timeSnowing == 0, "World return lost its weather clock");
	observe(103, 0);
	require(HasHistory() && GetCommonBufferData().states[0][3] == 3, "Travel lost or backdated meltwater");
	snow.perFrame.settings.offset = 1;
	observe(104, 0);
	require(!HasHistory(), "Changed coverage reused incompatible history");
	observe(105, 1);
	observe(106, 0);
	require(HasHistory(), "Fresh coverage history did not restart");
	globals::features::wetterness.active = false;
	observe(107, 0);
	require(!HasHistory() && !hasState.load(), "Disabled Wetterness retained world history");
	const auto disabledRevision = revision;
	observe(108, 0);
	require(revision == disabledRevision, "Disabled frames repeated history resets");
	globals::features::wetterness.active = true;
	observe(109, 0);
	require(!HasHistory(), "Re-enabling Wetterness resurrected old meltwater");
	observe(110, 1);
	observe(111, 0);
	SuspendWorld();
	snow.lastWorldspace = "OtherWorld";
	ResumeWorld(snow);
	observe(112, 1);
	observe(113, 0);
	require(HasHistory(), "Second world did not record its own history");
	observe(90, 0);
	require(!HasHistory(), "Calendar rewind retained active meltwater");
	SuspendWorld();
	snow.lastWorldspace = "Tamriel";
	snow.lastGameHours = -1;
	ResumeWorld(snow);
	require(snow.lastGameHours == -1, "Calendar rewind retained another world's cached timeline");
	Reset();
	for (int i = 0; i < 40; ++i) {
		snow.lastWorldspace = std::to_string(i);
		ResumeWorld(snow);
		observe(200 + i, 1);
		SuspendWorld();
	}
	require(worlds.size() == MaximumWorldHistories && !worlds.contains("0") && worlds.contains("39"),
		"World cache did not evict the oldest history at its limit");
	Reset();
	require(worlds.empty() && !GetCommonBufferData().enabled, "Save-load reset retained world history");
}
