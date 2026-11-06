#include "../package/Shaders/Common/SnowExclusions.hlsli"
#include "Features/Wetterness/SnowMelt.h"

#include <atomic>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>

// Substitute the engine boundary while compiling the production history controller.
struct Float4
{
	float x = 0, y = 0, z = 0, w = 0;
};
struct SnowCover
{
	struct UserSettings
	{
		float value = 0;
	};
	struct WorldSettings
	{
		float value = 0;
	};
	struct FireMeltData
	{
		uint32_t Count = 0;
		float Strength = 1, RadiusScale = 1;
		Float4 Spheres[8]{};
	};
	struct
	{
		UserSettings settings;
		WorldSettings wsettings;
		SnowExclusions::MaskData snowExclusions{};
		FireMeltData fireMelt;
		float TimeSnowing = 0, SnowingDensity = 500, SeasonalAltitude = 0;
	} perFrame;
	struct
	{
		bool Enabled = false;
		float RadiusScale = 1, Strength = 1;
	} fireMeltSettings;
	int MaxSummerMonth = 6, MaxWinterMonth = 0;
	float SummerHeightOffset = 1, WinterHeightOffset = -1, snowingSpeed = 1, meltingSpeed = 1;
	std::string lastWorldspace = "tamriel";
	float timeSnowing = 0;
	double lastGameHours = -1;
	bool active = true;
	bool IsActive() const { return active; }
};
namespace globals
{
	namespace game
	{
		struct Calendar
		{
			double days = 0;
			double GetCurrentGameTime() const { return days; }
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
			Float4 GetDryingHours(float) const { return { 12, 12, 12, 12 }; }
		} wetterness;
	}
}
namespace Util
{
	bool IsInterior() { return false; }
}
#include "snow_melt_runtime.h"

int main()
{
	using namespace WetternessSnowMelt;
	auto require = [](bool condition, const char* message) { if (!condition) throw std::runtime_error(message); };
	auto& snow = globals::features::snowCover;
	auto observe = [&](float amount, double hours) {
		globals::game::clock.days = hours / 24;
		snow.perFrame.TimeSnowing = amount;
		Update(snow, -1);
	};
	observe(1, 0);
	observe(0, 1);
	require(HasHistory(), "Melting must publish history");
	Update(snow, -1);
	require(HasHistory(), "Unchanged area settings must retain paused history");
	snow.perFrame.snowExclusions.count = 1;
	auto& area = snow.perFrame.snowExclusions.areas[0];
	area.count = 3;
	area.maxX = area.maxY = 10;
	area.vertices[1].x = area.vertices[2].y = 10;
	Update(snow, -1);
	require(!HasHistory(), "Adding an area must invalidate old history even at the same game time");
	observe(1, 2);
	observe(0, 3);
	require(HasHistory(), "Melt can accumulate after configuration reset");
	area.vertices[1].x = 9;
	Update(snow, -1);
	require(!HasHistory(), "Moving a corner with unchanged bounds must invalidate old history");
	observe(1, 4);
	observe(0, 5);
	SuspendWorld();
	snow.perFrame.snowExclusions = {};
	ResumeWorld(snow);
	Update(snow, -1);
	require(!HasHistory(), "Returning to a world must not restore history for removed areas");
	observe(1, 6);
	observe(0, 7);
	globals::features::wetterness.active = false;
	Update(snow, -1);
	globals::features::wetterness.active = true;
	require(!HasHistory(), "Disabling Wetterness must clear cached melt history");
}
