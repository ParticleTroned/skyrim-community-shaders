#include "Features/LightLimitFix/ShadowCullingPolicy.h"
#include "Features/TextureStreaming/RestorationPriority.h"
#include <iostream>
#include <limits>
#include <stdexcept>

void Require(bool value, const char* message)
{
	if (!value)
		throw std::runtime_error(message);
}
namespace ShadowStartupFixture
{
	bool spsLoaded = false, conflict = false, available = false;
	unsigned writes = 0;
	std::string failure;
	namespace RE
	{
		constexpr std::array<std::uintptr_t, 1> VTABLE_BSCullingProcess{ 1 };
	}
	using AppendFunction = void (*)();
	AppendFunction appendOriginal = nullptr;
	void Append() {}
	void Original() {}
	void* Module(const wchar_t*) { return spsLoaded ? reinterpret_cast<void*>(1) : nullptr; }
	namespace REL
	{
		template <class T>
		struct Relocation
		{
			explicit Relocation(T) {}
			T write_vfunc(unsigned, AppendFunction)
			{
				++writes;
				return reinterpret_cast<T>(&Original);
			}
		};
	}
	namespace logger
	{
		template <class... T>
		void warn(const char*, T&&...)
		{}
	}
#define GetModuleHandleW Module
#include "shadow_startup_under_test.h"
#undef GetModuleHandleW
	void CheckStartup()
	{
		spsLoaded = true;
		Install();
		Require(conflict && !available && writes == 0, "SPS detection still replaced native shadow hooks");
		conflict = spsLoaded = false;
		Install();
		Require(available && writes == 1, "Sun collection hook was not installed without SPS");
		Install();
		Require(writes == 1, "Repeated initialization chained the hook to itself");
	}
}
namespace ShadowSnapshotFixture
{
	using namespace LightLimitFixDetail::ShadowCulling;
	struct Camera
	{
		Pass pass = Pass::Unknown;
		unsigned cascade = 0;
	};
	struct Snapshot
	{
		Settings settings;
		std::array<Camera, 72> cameras;
		unsigned cameraCount = 0, eyeCount = 2;
		double sunSine = 1;
		bool interior = false;
	};
#include "shadow_eligibility_under_test.h"
	void CheckEligibility()
	{
		Snapshot captured;
		captured.cameras[0].pass = Pass::Sun;
		Require(!HasEligibleCamera(captured), "Uncaptured cameras activated culling");
		captured.cameraCount = 1;
		Require(HasEligibleCamera(captured), "Valid sun demand did not reactivate culling");
		captured.cameras[0].pass = Pass::Point;
		captured.interior = true;
		captured.settings.interiorTorches = false;
		Require(!HasEligibleCamera(captured), "Disabled interior point demand activated culling");
		captured.settings.interiorTorches = true;
		Require(HasEligibleCamera(captured), "Interior torch opt-in did not reactivate culling");
		captured.settings.sun = captured.settings.torch = false;
		Require(HasEligibleCamera(captured), "Actors-only point demand was discarded");
		captured.cameras[0].pass = Pass::Sun;
		Require(HasEligibleCamera(captured), "Actors-only nearest sun cascade was discarded");
		for (const double sine : { 0.0, MinimumSunSine - 0.001, 1.01, std::numeric_limits<double>::quiet_NaN() }) {
			captured.sunSine = sine;
			Require(!HasEligibleCamera(captured), "Invalid or low sun activated culling");
		}
		captured.sunSine = MinimumSunSine;
		Require(HasEligibleCamera(captured), "Valid boundary sun demand was discarded");
		for (const unsigned eyes : { 0u, 3u }) {
			captured.eyeCount = eyes;
			Require(!HasEligibleCamera(captured), "Invalid eye count activated culling");
		}
		for (const unsigned eyes : { 1u, 2u }) {
			captured.eyeCount = eyes;
			Require(HasEligibleCamera(captured), "Mono or stereo demand was discarded");
		}
		captured.settings.actors = false;
		Require(!HasEligibleCamera(captured), "Disabled policy activated culling");
		captured = {};
		captured.cameraCount = 71;
		captured.cameras.back().pass = Pass::Sun;
		Require(!HasEligibleCamera(captured), "Camera beyond the captured range activated culling");
		++captured.cameraCount;
		Require(HasEligibleCamera(captured), "Last admitted camera was ignored");
		captured.cameras.back().pass = Pass::Unknown;
		Require(!HasEligibleCamera(captured), "Unknown camera activated culling");
		captured = {};
		captured.cameraCount = 1;
		captured.cameras[0].pass = Pass::Sun;
		captured.settings.actors = false;
		Require(!HasEligibleCamera(captured), "Protected nearest cascade activated object-only culling");
		captured.cameras[0].cascade = 1;
		Require(HasEligibleCamera(captured), "An eligible outer cascade did not reactivate culling");
		for (unsigned flags = 0; flags < 16; ++flags)
			for (const auto pass : { Pass::Unknown, Pass::Sun, Pass::Point })
				for (const unsigned cascade : { 0u, 1u })
					for (const bool interior : { false, true }) {
						captured.settings.sun = (flags & 1) != 0;
						captured.settings.torch = (flags & 2) != 0;
						captured.settings.actors = (flags & 4) != 0;
						captured.settings.interiorTorches = (flags & 8) != 0;
						captured.cameras[0] = { pass, cascade };
						captured.interior = interior;
						Demand caster;
						caster.pass = pass;
						caster.cascade = cascade;
						caster.eyeCount = captured.eyeCount;
						caster.sunElevationSine = captured.sunSine;
						caster.interior = interior;
						caster.dynamic = false;
						caster.radius = 1;
						caster.eyeDistance = { 1e6, 1e6 };
						caster.lightDistance = caster.lightRadius = 1000;
						const bool objectEligible = ShouldCull(captured.settings, caster);
						caster.actor = true;
						Require(HasEligibleCamera(captured) == (objectEligible || ShouldCull(captured.settings, caster)),
							"Snapshot eligibility disagreed with authoritative caster policy");
					}
	}
}
int main()
{
	try {
		ShadowStartupFixture::CheckStartup();
		ShadowSnapshotFixture::CheckEligibility();
		namespace C = LightLimitFixDetail::ShadowCulling;
		const std::array<double, 3> actorRoot{ 100, 0, 0 }, view{ 0, 0, 0 }, staleCenter{ 10000, 0, 0 };
		Require(C::SurfaceDistance(staleCenter, 16, view, &actorRoot) == 84, "Stale skin bounds overrode the nearby actor root");
		Require(!std::isfinite(C::SurfaceDistance({ std::numeric_limits<double>::infinity(), 0, 0 }, 16, view, &actorRoot)), "An actor root hid invalid geometry bounds");
		C::Settings settings;
		Require(settings.sun && settings.torch && settings.actors && settings.interiorTorches, "Culling defaults lost indoor torch coverage");
		Require(C::Parse(nlohmann::json::object()) == settings, "Missing saved settings did not use the new defaults");
		C::Demand demand{ C::Pass::Sun, { 10000, 10000 }, 2, 16, 1, 1000, 2000, false, false, false };
		Require(C::ShouldCull(settings, demand), "Default sun culling was inactive");
		demand.pass = C::Pass::Point;
		Require(C::ShouldCull(settings, demand), "Default torch culling was inactive");
		demand.interior = true;
		Require(C::ShouldCull(settings, demand), "Default indoor torch culling was inactive");
		settings.interiorTorches = true;
		demand.actor = true;
		Require(C::ShouldCull(settings, demand), "Default distant NPC culling was inactive");
		demand.interior = false;
		settings.sun = settings.torch = settings.actors = settings.interiorTorches = false;
		Require(nlohmann::json(settings).get<C::Settings>() == settings, "Saved disabled preferences were overwritten by defaults");
		demand.pass = C::Pass::Sun;
		demand.actor = false;
		Require(!C::ShouldCull(settings, demand), "Disabled culling still discarded shadows");
		settings.sun = true;
		Require(C::ShouldCull(settings, demand), "Tiny distant sun caster was not admitted");
		demand.eyeDistance[1] = 1000;
		Require(!C::ShouldCull(settings, demand), "A close second VR eye lost its shadow");
		demand.eyeDistance[1] = 10000;
		demand.sunElevationSine = 0.1;
		Require(!C::ShouldCull(settings, demand), "Low sun lost a long shadow");
		demand.sunElevationSine = 0.2;
		demand.radius = 40;
		Require(!C::ShouldCull(settings, demand), "Magnified sun shadow was treated as a tiny caster");
		demand.radius = 16;
		demand.sunElevationSine = 1;
		demand.dynamic = true;
		Require(!C::ShouldCull(settings, demand), "Animated geometry was culled as static");
		demand.dynamic = false;
		demand.pass = C::Pass::Unknown;
		Require(!C::ShouldCull(settings, demand), "A world or preview pass was culled");
		demand.pass = C::Pass::Point;
		settings.torch = true;
		Require(C::ShouldCull(settings, demand), "Small distant point-light shadow was not admitted");
		demand.lightDistance = 100;
		Require(!C::ShouldCull(settings, demand), "A near-light caster lost its magnified shadow");
		demand.lightDistance = 500;
		demand.lightRadius = 10000;
		Require(!C::ShouldCull(settings, demand), "A large projected wall shadow was discarded");
		demand.lightDistance = 1000;
		demand.lightRadius = 2000;
		demand.interior = true;
		Require(!C::ShouldCull(settings, demand), "Indoor shadow culling started without permission");
		settings.interiorTorches = true;
		Require(C::ShouldCull(settings, demand), "Indoor torch opt-in was not respected");
		demand.actor = true;
		Require(!C::ShouldCull(settings, demand), "NPC shadows were culled without opting in");
		settings.actors = true;
		Require(C::ShouldCull(settings, demand), "Distant NPC shadow option was ignored");
		demand.protectedGeometry = true;
		Require(!C::ShouldCull(settings, demand), "NPC effects bypassed the special-geometry guard");
		demand.protectedGeometry = false;
		demand.lightRadius = 20000;
		Require(!C::ShouldCull(settings, demand), "NPC lost a large projected wall shadow");
		demand.lightRadius = 2000;
		demand.eyeDistance[1] = 2000;
		Require(!C::ShouldCull(settings, demand), "Nearby NPC lost its shadow in the second eye");
		demand.eyeDistance = { 10000, 10000 };
		demand.radius = std::numeric_limits<double>::quiet_NaN();
		Require(!C::ShouldCull(settings, demand), "Invalid bounds authorized culling");
		for (const auto& bad : { nlohmann::json{ { "sun", 1 } }, nlohmann::json{ { "minimumDistanceMeters", 4 } },
				 nlohmann::json{ { "actorDistanceMeters", 4 } }, nlohmann::json{ { "torch", true }, { "unknown", false } } }) {
			bool rejected = false;
			try {
				(void)C::Parse(bad, settings);
			} catch (const std::invalid_argument&) {
				rejected = true;
			}
			Require(rejected, "Malformed shadow settings escaped validation");
		}
		Require(C::Parse({ { "torch", false } }, settings).sun, "Partial settings discarded the existing sun preference");
		Require(nlohmann::json(settings).get<C::Settings>() == settings, "Saved culling preferences changed on reload");
		using P = StreamingTextures::RestorationPriority;
		const P near{ 2, 8, 4000, 0, 9 }, far{ 2, 2, 4000, 0, 1 }, aged{ 2, 1, 1, 0, 10 }, refill{ 1, 64, 1, 0, 5 }, preview{ 3, 1, 4000, 0, 6 };
		Require(near.BetterThan(far, 4500), "Urgent visible restoration queued behind a less-needed texture");
		Require(aged.BetterThan(near, 6000), "Urgency starved an older restoration");
		Require(near.BetterThan(refill, 6000), "Optional refill displaced a required restore");
		Require(preview.BetterThan(aged, 6000), "World restoration displaced an active preview");
		Require(P{ 0, 0, 0, 100, 2 }.BetterThan(P{ 0, 0, 0, 50, 1 }, 6000), "Shrink selection lost largest-saving priority");
		std::cout << "SPS follow-up culling and restoration policies passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
