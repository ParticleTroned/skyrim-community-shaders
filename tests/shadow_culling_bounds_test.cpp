#include "Features/LightLimitFix/ShadowCullingPolicy.h"
#include <iostream>
#include <random>

namespace C = LightLimitFixDetail::ShadowCulling;
void Require(bool value, const char* reason)
{
	if (!value)
		throw std::runtime_error(reason);
}

// Independent scalar policy checks the conservative production fast path.
bool BaselineShouldCull(const C::Settings& settings, const C::Demand& demand)
{
	if (!settings.Active() || demand.pass == C::Pass::Unknown || !demand.eyeCount || demand.eyeCount > demand.eyeDistance.size() ||
		!std::isfinite(demand.radius) || demand.radius <= 0 || demand.protectedGeometry)
		return false;
	if (demand.pass == C::Pass::Sun && (!std::isfinite(demand.sunElevationSine) || demand.sunElevationSine < 0.17364817766693033 || demand.sunElevationSine > 1))
		return false;
	if (demand.pass == C::Pass::Point && demand.interior && !settings.interiorTorches)
		return false;
	if (demand.actorPart) {
		if (!settings.smallActorParts || !demand.actor || settings.actorPartDistanceMeters == 0 || settings.actorPartReturnDistanceMeters == 0 || settings.actorPartRadiusCentimeters == 0)
			return false;
	} else if (demand.actor) {
		if (!settings.actors)
			return false;
	} else if (demand.dynamic || (demand.pass == C::Pass::Sun ? !settings.sun || demand.cascade < 1 : !settings.torch)) {
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
	if (!std::isfinite(shadowRadius) || shadowRadius >= (demand.actorPart ? settings.actorPartRadiusCentimeters / 1.428 : demand.actor ? 256 :
																																		 150))
		return false;
	const auto minimum = (demand.actorPart ? settings.actorPartReturnDistanceMeters : demand.actor            ? settings.actorDistanceMeters :
																				  demand.pass == C::Pass::Sun ? settings.sunDistanceMeters :
																												settings.torchDistanceMeters) /
	                     0.01428;
	for (unsigned eye = 0; eye < demand.eyeCount; ++eye) {
		const auto distance = demand.eyeDistance[eye];
		if (!std::isfinite(distance) || distance <= minimum || shadowRadius / distance >= (demand.actor ? 0.02 : 0.035))
			return false;
	}
	return true;
}

int main()
{
	try {
		C::Settings settings;
		Require(settings.sunDistanceMeters == 20 && settings.torchDistanceMeters == 15 && settings.actorDistanceMeters == 50, "Rounded shadow distance defaults changed");
		const auto migrated = nlohmann::json{ { "minimumDistanceMeters", 50 }, { "actorDistanceMeters", 75 } }.get<C::Settings>();
		Require(migrated == settings, "Legacy automatic distances were not migrated");
		const auto chosen = C::Parse({ { "sunDistanceMeters", 31 }, { "torchDistanceMeters", 19 }, { "actorDistanceMeters", 95 } }, settings);
		Require(nlohmann::json(chosen).get<C::Settings>() == chosen, "New slider choices were not preserved");
		for (const double distance : { 5.0, 300.0 }) {
			const auto boundary = C::Parse({ { "sunDistanceMeters", distance }, { "torchDistanceMeters", distance }, { "actorDistanceMeters", distance } });
			Require(boundary.sunDistanceMeters == distance && boundary.torchDistanceMeters == distance && boundary.actorDistanceMeters == distance,
				"A supported slider boundary was rejected");
		}
		const auto independent = C::Parse({ { "torchDistanceMeters", 5 } }, chosen);
		Require(independent.sunDistanceMeters == chosen.sunDistanceMeters && independent.actorDistanceMeters == chosen.actorDistanceMeters && independent.torchDistanceMeters == 5,
			"Torch distance update changed another slider");
		nlohmann::json layer = { { "ShadowCulling", { { "minimumDistanceMeters", 50 }, { "actorDistanceMeters", 75 }, { "torch", false } } } };
		C::MigrateLegacyOverride(layer);
		Require(!layer["ShadowCulling"].contains("minimumDistanceMeters") && !layer["ShadowCulling"].contains("actorDistanceMeters"), "Legacy override retained automatic distances");
		auto merged = nlohmann::json(chosen);
		merged.merge_patch(layer["ShadowCulling"]);
		const auto overridden = merged.get<C::Settings>();
		Require(overridden.sunDistanceMeters == chosen.sunDistanceMeters && overridden.torchDistanceMeters == chosen.torchDistanceMeters && overridden.actorDistanceMeters == chosen.actorDistanceMeters && !overridden.torch,
			"An old override replaced new slider choices or lost its switch preference");
		C::MigrateLegacyOverride(layer);
		Require(layer["ShadowCulling"].size() == 1, "Repeated migration changed the remaining preferences");
		const auto alias = C::Parse({ { "minimumDistanceMeters", 42 } }, settings);
		Require(alias.sunDistanceMeters == 42 && alias.torchDistanceMeters == 42, "Legacy session distance alias changed");
		for (const auto& bad : { nlohmann::json{ { "sunDistanceMeters", 0 } }, nlohmann::json{ { "torchDistanceMeters", 4.999 } }, nlohmann::json{ { "actorDistanceMeters", 0 } }, nlohmann::json{ { "torchDistanceMeters", 301 } },
				 nlohmann::json{ { "sunDistanceMeters", "20" } }, nlohmann::json{ { "minimumDistanceMeters", 50 }, { "sunDistanceMeters", 25 } } }) {
			bool rejected = false;
			try {
				(void)C::Parse(bad);
			} catch (const std::invalid_argument&) {
				rejected = true;
			}
			Require(rejected, "Invalid or ambiguous slider values were accepted");
		}
		C::Demand example{ C::Pass::Sun, { 2448, 2448 }, 2, 40, 1, 1000, 2000, false, false, false };
		Require(C::ShouldCull(settings, example), "SPS object thresholds did not admit a distant barrel-size caster");
		example.cascade = 0;
		Require(!C::ShouldCull(settings, example), "The nearest ordinary sun cascade lost protection");
		example.cascade = 1;
		example.pass = C::Pass::Point;
		example.interior = true;
		Require(C::ShouldCull(settings, example), "Indoor point culling did not default on");
		for (const auto pass : { C::Pass::Sun, C::Pass::Point }) {
			C::Demand boundary{ pass, {}, 2, 16, 1, 1000, 1000, false, false, false };
			const double reach = boundary.radius + C::EyeDistanceMargin + C::MinimumDistance(settings, pass, false);
			const std::array<std::array<double, 3>, 2> eyes{};
			for (const double position : { std::nextafter(reach, 0.0), reach, std::nextafter(reach, INFINITY), reach + 1e-9 }) {
				const std::array<double, 3> center{ position, 0, 0 };
				boundary.eyeDistance.fill(C::SurfaceDistance(center, boundary.radius, eyes[0]) - C::EyeDistanceMargin);
				Require(!C::ShouldCull(settings, boundary) || C::BoundsMayCull(settings, boundary, center, eyes),
					"Rounding at the near-eye threshold changed a culling decision");
			}
		}

		std::mt19937_64 random(0x534841444f57);
		std::uniform_real_distribution<double> coordinate(-30000, 30000), radius(0.01, 400), sun(0.01, 1);
		unsigned accepted = 0, partsAccepted = 0, fastKeeps = 0;
		for (unsigned sample = 0; sample < 200000; ++sample) {
			C::Demand demand;
			demand.pass = sample % 2 ? C::Pass::Sun : C::Pass::Point;
			demand.eyeCount = sample % 3 ? 2 : 1;
			demand.radius = radius(random);
			demand.actorPart = sample % 4 < 2;
			if (demand.actorPart)
				demand.radius /= 40;
			demand.sunElevationSine = sun(random);
			demand.interior = sample % 7 == 0;
			demand.actor = sample % 3 == 0;
			demand.dynamic = sample % 11 == 0;
			demand.protectedGeometry = sample % 13 == 0;
			demand.cascade = sample % 4;
			settings.sun = sample % 5 != 0;
			settings.torch = sample % 9 != 0;
			settings.actors = sample % 4 != 0;
			settings.smallActorParts = sample % 8 != 0;
			settings.interiorTorches = sample % 2 == 0;
			settings.sunDistanceMeters = float(5 + sample % 296);
			settings.torchDistanceMeters = float(300 - sample % 296);
			settings.actorDistanceMeters = float(5 + sample % 296);
			settings.actorPartDistanceMeters = float(sample % 301);
			settings.actorPartReturnDistanceMeters = settings.actorPartDistanceMeters * float(sample % 11) / 10;
			settings.actorPartRadiusCentimeters = float(sample % 501) / 10;
			std::array<double, 3> center{ coordinate(random), coordinate(random), coordinate(random) };
			std::array<double, 3> actorRoot{ coordinate(random), coordinate(random), coordinate(random) };
			std::array<std::array<double, 3>, 2> eyes{ { { 0, 0, 0 }, { 4.4, 0, 0 } } };
			if (sample % 17 == 0)
				eyes[1] = center;
			if (sample % 19 == 0)
				center[0] = std::numeric_limits<float>::max();
			if (sample % 23 == 0)
				center[1] = std::numeric_limits<double>::quiet_NaN();
			const auto* root = demand.actor ? &actorRoot : nullptr;
			for (unsigned eye = 0; eye < demand.eyeCount; ++eye)
				demand.eyeDistance[eye] = C::SurfaceDistance(center, demand.radius, eyes[eye], root) - C::EyeDistanceMargin;
			demand.lightDistance = C::SurfaceDistance(center, demand.radius, { 100, 200, 0 }, root);
			demand.lightRadius = 5000;
			const bool authoritative = C::ShouldCull(settings, demand);
			Require(authoritative == BaselineShouldCull(settings, demand), "Production culling diverged from the policy oracle");
			const bool possible = C::BoundsMayCull(settings, demand, center, eyes);
			Require(!authoritative || possible, "Early keep changed an authoritative culling decision");
			accepted += authoritative;
			partsAccepted += authoritative && demand.actorPart;
			fastKeeps += !possible;
		}
		Require(accepted > 100 && partsAccepted > 100 && accepted > partsAccepted + 100 && fastKeeps > 10000, "Property coverage did not exercise both policies and outcomes");
		std::cout << "PASS: distance migration, independent controls, SPS thresholds and 200000 ordinary/actor-part broad-phase cases\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
