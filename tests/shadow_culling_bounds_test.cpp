#include "Features/LightLimitFix/ShadowCullingPolicy.h"
#include <iostream>
#include <random>

namespace C = LightLimitFixDetail::ShadowCulling;
void Require(bool value, const char* reason)
{
	if (!value)
		throw std::runtime_error(reason);
}

// Frozen production policy provides an independent oracle for the reordered checks.
bool BaselineShouldCull(const C::Settings& settings, const C::Demand& demand)
{
	if (!settings.Active() || demand.pass == C::Pass::Unknown || !demand.eyeCount || demand.eyeCount > demand.eyeDistance.size() ||
		!std::isfinite(demand.radius) || demand.radius <= 0 || demand.protectedGeometry)
		return false;
	if (demand.pass == C::Pass::Sun && (!std::isfinite(demand.sunElevationSine) || demand.sunElevationSine < 0.17364817766693033))
		return false;
	if (demand.pass == C::Pass::Point && demand.interior && !settings.interiorTorches)
		return false;
	if (demand.actor) {
		if (!settings.actors)
			return false;
	} else if (demand.dynamic || (demand.pass == C::Pass::Sun ? !settings.sun : !settings.torch)) {
		return false;
	}
	double shadowRadius = demand.radius;
	if (demand.pass == C::Pass::Point) {
		if (!std::isfinite(demand.lightDistance) || !std::isfinite(demand.lightRadius) || demand.lightDistance <= 1 ||
			demand.lightRadius <= 0 || demand.radius / demand.lightDistance >= 0.1)
			return false;
		shadowRadius *= std::max(1.0, demand.lightRadius / demand.lightDistance);
	} else {
		shadowRadius /= demand.sunElevationSine;
	}
	if (!std::isfinite(shadowRadius) || shadowRadius >= (demand.actor ? 256 : 64))
		return false;
	const auto minimum = (demand.actor ? settings.actorDistanceMeters : settings.minimumDistanceMeters) / 0.01428;
	for (unsigned eye = 0; eye < demand.eyeCount; ++eye) {
		const auto distance = demand.eyeDistance[eye];
		if (!std::isfinite(distance) || distance <= minimum || shadowRadius / distance >= (demand.actor ? 0.02 : 0.005))
			return false;
	}
	return true;
}

int main()
{
	try {
		C::Settings settings;
		Require(settings.sun && settings.torch && settings.actors && settings.interiorTorches &&
					settings.minimumDistanceMeters == 50 && settings.actorDistanceMeters == 75,
			"Production culling defaults changed");
		const auto chosen = C::Parse({ { "minimumDistanceMeters", 42 }, { "actorDistanceMeters", 95 } });
		Require(nlohmann::json(chosen).get<C::Settings>() == chosen, "Existing distance choices changed");
		Require(C::ObjectShadowRadius == 64 && C::ActorShadowRadius == 256 && C::ObjectAngularSize == 0.005 && C::ActorAngularSize == 0.02,
			"Production shadow thresholds changed");
		for (const auto pass : { C::Pass::Sun, C::Pass::Point }) {
			C::Demand boundary{ pass, {}, 2, 16, 1, 1000, 1000, false, false, false };
			const double reach = boundary.radius + C::EyeDistanceMargin + C::MinimumDistance(settings, false);
			const std::array<std::array<double, 3>, 2> eyes{};
			for (const double position : { std::nextafter(reach, 0.0), reach, std::nextafter(reach, INFINITY), reach + 1e-9 }) {
				const std::array<double, 3> center{ position, 0, 0 };
				boundary.eyeDistance.fill(C::SurfaceDistance(center, boundary.radius, eyes[0]) - C::EyeDistanceMargin);
				Require(C::ShouldCull(settings, boundary) == BaselineShouldCull(settings, boundary), "Threshold rounding changed the production policy");
				Require(!C::ShouldCull(settings, boundary) || C::BoundsMayCull(settings, boundary, center, eyes),
					"Rounding at the near-eye threshold changed a culling decision");
			}
		}

		std::mt19937_64 random(0x534841444f57);
		std::uniform_real_distribution<double> coordinate(-30000, 30000), radius(0.01, 400), sun(0.01, 1.2);
		unsigned accepted = 0, fastKeeps = 0;
		for (unsigned sample = 0; sample < 200000; ++sample) {
			C::Demand demand;
			demand.pass = sample % 29 == 0 ? C::Pass::Unknown : sample % 2 ? C::Pass::Sun :
			                                                                 C::Pass::Point;
			demand.eyeCount = sample % 31 == 0 ? 0 : sample % 37 == 0 ? 3 :
			                                     sample % 3           ? 2 :
			                                                            1;
			demand.radius = radius(random);
			demand.sunElevationSine = sun(random);
			demand.interior = sample % 7 == 0;
			demand.actor = sample % 3 == 0;
			demand.dynamic = sample % 11 == 0;
			demand.protectedGeometry = sample % 13 == 0;
			settings.sun = sample % 5 != 0;
			settings.torch = sample % 9 != 0;
			settings.actors = sample % 4 != 0;
			settings.interiorTorches = sample % 2 == 0;
			settings.minimumDistanceMeters = float(30 + sample % 271);
			settings.actorDistanceMeters = float(50 + sample % 251);
			const std::array<double, 3> actorRoot{ coordinate(random), coordinate(random), coordinate(random) };
			std::array<double, 3> center{ coordinate(random), coordinate(random), coordinate(random) };
			std::array<std::array<double, 3>, 2> eyes{ { { 0, 0, 0 }, { 4.4, 0, 0 } } };
			if (sample % 17 == 0)
				eyes[1] = center;
			if (sample % 19 == 0)
				center[0] = std::numeric_limits<float>::max();
			if (sample % 23 == 0)
				center[1] = std::numeric_limits<double>::quiet_NaN();
			const auto* root = demand.actor ? &actorRoot : nullptr;
			for (unsigned eye = 0; eye < demand.eyeDistance.size(); ++eye)
				demand.eyeDistance[eye] = C::SurfaceDistance(center, demand.radius, eyes[eye], root) - C::EyeDistanceMargin;
			demand.lightDistance = C::SurfaceDistance(center, demand.radius, { 100, 200, 0 }, root);
			demand.lightRadius = 5000;
			const bool authoritative = C::ShouldCull(settings, demand);
			Require(authoritative == BaselineShouldCull(settings, demand), "Reordered checks changed the production culling policy");
			const bool possible = C::BoundsMayCull(settings, demand, center, eyes);
			Require(!authoritative || possible, "Early keep changed an authoritative culling decision");
			accepted += authoritative;
			fastKeeps += !possible;
		}
		Require(accepted > 100 && fastKeeps > 10000, "Property coverage did not exercise both outcomes");
		std::cout << "PASS: existing settings, policy equivalence and 200000 conservative broad-phase cases\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
