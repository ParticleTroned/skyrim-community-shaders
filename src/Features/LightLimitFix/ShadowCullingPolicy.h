#pragma once

// Shadow-size and actor-part distance policy adapted from SPS, Copyright (c) 2026 dnai92.
// Modified for CSX on 2026-10-30; GPL-3.0-or-later with the packaged SPS exceptions.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace LightLimitFixDetail::ShadowCulling
{
	inline constexpr double MetersPerUnit = 0.01428;
	inline constexpr double EyeDistanceMargin = 512;
	inline constexpr double MinimumSunSine = 0.17364817766693033;
	inline constexpr double ObjectShadowRadius = 150, ObjectAngularSize = 0.035;
	inline constexpr double ActorShadowRadius = 256, ActorAngularSize = 0.02;
	inline constexpr float DefaultActorPartRadiusCentimeters = static_cast<float>(8 * MetersPerUnit * 100);
	inline constexpr float MaximumActorPartRadiusCentimeters = 50;
	inline constexpr std::uint64_t SnapshotMaxAgeMs = 250;
	inline constexpr float DefaultSunDistanceMeters = 20, DefaultTorchDistanceMeters = 15, DefaultActorDistanceMeters = 50;
	inline constexpr float MinimumDistanceMeters = 5, MaximumDistanceMeters = 300;
	inline constexpr float DefaultActorPartDistanceMeters = 7.14f, DefaultActorPartReturnDistanceMeters = 6.426f;

	struct Settings
	{
		bool sun = true, torch = true, actors = true, interiorTorches = true;
		float sunDistanceMeters = DefaultSunDistanceMeters, torchDistanceMeters = DefaultTorchDistanceMeters, actorDistanceMeters = DefaultActorDistanceMeters;
		bool smallActorParts = true;
		float actorPartDistanceMeters = DefaultActorPartDistanceMeters, actorPartReturnDistanceMeters = DefaultActorPartReturnDistanceMeters;
		float actorPartRadiusCentimeters = DefaultActorPartRadiusCentimeters;
		bool operator==(const Settings&) const = default;
		/** Any zero part slider disables the policy without losing its checkbox preference. */
		[[nodiscard]] bool ActorPartsEnabled() const noexcept { return smallActorParts && actorPartDistanceMeters > 0 && actorPartReturnDistanceMeters > 0 && actorPartRadiusCentimeters > 0; }
		[[nodiscard]] bool Active() const noexcept { return sun || torch || actors || ActorPartsEnabled(); }
	};
	inline void Validate(const Settings& value)
	{
		if (!std::isfinite(value.sunDistanceMeters) || value.sunDistanceMeters < MinimumDistanceMeters || value.sunDistanceMeters > MaximumDistanceMeters ||
			!std::isfinite(value.torchDistanceMeters) || value.torchDistanceMeters < MinimumDistanceMeters || value.torchDistanceMeters > MaximumDistanceMeters ||
			!std::isfinite(value.actorDistanceMeters) || value.actorDistanceMeters < MinimumDistanceMeters || value.actorDistanceMeters > MaximumDistanceMeters)
			throw std::invalid_argument("Shadow-culling distances must be 5..300 m");
		if (!std::isfinite(value.actorPartDistanceMeters) || value.actorPartDistanceMeters < 0 || value.actorPartDistanceMeters > MaximumDistanceMeters ||
			!std::isfinite(value.actorPartReturnDistanceMeters) || value.actorPartReturnDistanceMeters < 0 || value.actorPartReturnDistanceMeters > MaximumDistanceMeters ||
			(value.actorPartDistanceMeters > 0 && value.actorPartReturnDistanceMeters > value.actorPartDistanceMeters))
			throw std::invalid_argument("Actor-part distances must be 0..300 m; either 0 disables the policy, otherwise return must not exceed cutoff");
		if (!std::isfinite(value.actorPartRadiusCentimeters) || value.actorPartRadiusCentimeters < 0 || value.actorPartRadiusCentimeters > MaximumActorPartRadiusCentimeters)
			throw std::invalid_argument("Actor-part radius must be 0..50 cm; 0 disables actor-part culling");
	}
	inline Settings Parse(const nlohmann::json& value, Settings result = {}, bool migrateLegacyDistances = false)
	{
		if (!value.is_object())
			throw std::invalid_argument("Shadow culling requires an object");
		const bool legacy = value.contains("minimumDistanceMeters");
		if (legacy && (value.contains("sunDistanceMeters") || value.contains("torchDistanceMeters")))
			throw std::invalid_argument("Use either legacy minimumDistanceMeters or separate sun/torch distances");
		for (const auto& [name, field] : value.items()) {
			bool* flag = name == "sun" ? &result.sun : name == "torch"       ? &result.torch :
			                                       name == "actors"          ? &result.actors :
			                                       name == "interiorTorches" ? &result.interiorTorches :
			                                       name == "smallActorParts" ? &result.smallActorParts :
			                                                                   nullptr;
			if (flag) {
				if (!field.is_boolean())
					throw std::invalid_argument(name + " must be boolean");
				*flag = field.get<bool>();
			} else if (name == "minimumDistanceMeters" || name == "sunDistanceMeters" || name == "torchDistanceMeters" || name == "actorDistanceMeters" ||
					   name == "actorPartDistanceMeters" || name == "actorPartReturnDistanceMeters" || name == "actorPartRadiusCentimeters") {
				if (!field.is_number())
					throw std::invalid_argument(name + " must be numeric");
				const auto number = field.get<double>();
				const bool actorPart = name == "actorPartDistanceMeters" || name == "actorPartReturnDistanceMeters";
				const bool radius = name == "actorPartRadiusCentimeters";
				if (!std::isfinite(number) || number < (actorPart || radius ? 0 : MinimumDistanceMeters) || number > (radius ? MaximumActorPartRadiusCentimeters : MaximumDistanceMeters))
					throw std::invalid_argument(name + " is outside its supported range");
				if (radius) {
					result.actorPartRadiusCentimeters = static_cast<float>(number);
					continue;
				}
				if (actorPart) {
					(name == "actorPartDistanceMeters" ? result.actorPartDistanceMeters : result.actorPartReturnDistanceMeters) = static_cast<float>(number);
					continue;
				}
				if (legacy && migrateLegacyDistances)
					continue;
				if (name == "minimumDistanceMeters")
					result.sunDistanceMeters = result.torchDistanceMeters = static_cast<float>(number);
				else
					(name == "sunDistanceMeters" ? result.sunDistanceMeters : name == "torchDistanceMeters" ? result.torchDistanceMeters :
																											  result.actorDistanceMeters) = static_cast<float>(number);
			} else {
				throw std::invalid_argument("Unknown shadow-culling setting: " + name);
			}
		}
		Validate(result);
		return result;
	}
	/** Validate partial input without assuming live distances; the merged pair is validated before mutation. */
	inline void ValidateOverride(const nlohmann::json& value)
	{
		Settings bounds;
		bounds.actorPartDistanceMeters = MaximumDistanceMeters;
		bounds.actorPartReturnDistanceMeters = 0;
		(void)Parse(value, bounds);
	}
	inline void to_json(nlohmann::json& value, const Settings& settings)
	{
		value = { { "sun", settings.sun }, { "torch", settings.torch }, { "actors", settings.actors }, { "interiorTorches", settings.interiorTorches },
			{ "sunDistanceMeters", settings.sunDistanceMeters }, { "torchDistanceMeters", settings.torchDistanceMeters }, { "actorDistanceMeters", settings.actorDistanceMeters },
			{ "smallActorParts", settings.smallActorParts }, { "actorPartDistanceMeters", settings.actorPartDistanceMeters },
			{ "actorPartReturnDistanceMeters", settings.actorPartReturnDistanceMeters }, { "actorPartRadiusCentimeters", settings.actorPartRadiusCentimeters } };
	}
	inline void from_json(const nlohmann::json& value, Settings& settings) { settings = Parse(value, {}, true); }
	/** Retire automatic legacy distances before merging an override with current slider choices. */
	inline void MigrateLegacyOverride(nlohmann::json& feature)
	{
		auto shadow = feature.find("ShadowCulling");
		if (shadow == feature.end() || !shadow->is_object() || !shadow->contains("minimumDistanceMeters"))
			return;
		(void)Parse(*shadow, {}, true);
		shadow->erase("minimumDistanceMeters");
		shadow->erase("actorDistanceMeters");
	}

	enum class Pass : std::uint8_t
	{
		Unknown,
		Sun,
		Point
	};
	struct Demand
	{
		Pass pass = Pass::Unknown;
		std::array<double, 2> eyeDistance{};
		unsigned eyeCount = 0;
		double radius = 0, sunElevationSine = 0, lightDistance = 0, lightRadius = 0;
		bool actor = false, dynamic = true, interior = false, protectedGeometry = false;
		unsigned cascade = 1;
		bool actorPart = false;
	};
	struct ActorDistanceBand
	{
		double cutoff, returning;
	};
	/** Tiny-part actor thresholds exclude the common eye movement margin. */
	inline ActorDistanceBand ActorPartDistances(const Settings& settings) noexcept
	{
		return { settings.actorPartDistanceMeters / MetersPerUnit, settings.actorPartReturnDistanceMeters / MetersPerUnit };
	}
	inline double MinimumDistance(const Settings& settings, Pass pass, bool actor, bool actorPart = false) noexcept
	{
		return (actorPart ? settings.actorPartReturnDistanceMeters : actor         ? settings.actorDistanceMeters :
																 pass == Pass::Sun ? settings.sunDistanceMeters :
																					 settings.torchDistanceMeters) /
		       MetersPerUnit;
	}
	/** Shared projected-size limit for the broad phase and final caster decision. */
	inline double ShadowRadiusLimit(const Settings& settings, bool actor, bool actorPart = false) noexcept
	{
		return actorPart ? settings.actorPartRadiusCentimeters / (100 * MetersPerUnit) : actor ? ActorShadowRadius :
		                                                                                         ObjectShadowRadius;
	}
	/** Pass-wide keep decisions precede light validation and geometry classification. */
	inline bool PassMayCull(const Settings& settings, const Demand& demand) noexcept
	{
		return (demand.pass == Pass::Sun || demand.pass == Pass::Point) && demand.eyeCount && demand.eyeCount <= demand.eyeDistance.size() &&
		       (demand.actorPart ? settings.ActorPartsEnabled() : settings.actors || (demand.pass == Pass::Sun ? settings.sun : settings.torch)) &&
		       (demand.pass != Pass::Sun || (std::isfinite(demand.sunElevationSine) && demand.sunElevationSine >= MinimumSunSine && demand.sunElevationSine <= 1)) &&
		       (demand.pass != Pass::Point || !demand.interior || settings.interiorTorches);
	}
	inline bool CasterMayCull(const Settings& settings, const Demand& demand) noexcept
	{
		if (demand.protectedGeometry)
			return false;
		if (demand.actorPart)
			return demand.actor && settings.ActorPartsEnabled();
		return demand.actor ? settings.actors : !demand.dynamic && (demand.pass == Pass::Sun ? settings.sun && demand.cascade >= 1 : settings.torch);
	}
	/** Necessary bounds only: failure keeps the caster without inspecting its ownership hierarchy. */
	inline bool BoundsMayCull(const Settings& settings, const Demand& demand, const std::array<double, 3>& center,
		const std::array<std::array<double, 3>, 2>& eyes) noexcept
	{
		if (!PassMayCull(settings, demand) || !std::isfinite(demand.radius) || demand.radius <= 0)
			return false;
		const bool objects = !demand.actorPart && (demand.pass == Pass::Sun ? settings.sun : settings.torch);
		const bool actors = demand.actorPart || settings.actors;
		const double radius = demand.pass == Pass::Sun ? demand.radius / demand.sunElevationSine : demand.radius;
		if (radius >= std::max(objects ? ObjectShadowRadius : 0, actors ? ShadowRadiusLimit(settings, true, demand.actorPart) : 0))
			return false;
		const double minimum = std::min(objects ? MinimumDistance(settings, demand.pass, false) : std::numeric_limits<double>::infinity(),
			actors ? MinimumDistance(settings, demand.pass, true, demand.actorPart) : std::numeric_limits<double>::infinity());
		const double reach = demand.radius + EyeDistanceMargin + minimum;
		for (unsigned eye = 0; eye < demand.eyeCount; ++eye) {
			double squared = 0;
			for (unsigned axis = 0; axis < center.size(); ++axis) {
				if (!std::isfinite(center[axis]) || !std::isfinite(eyes[eye][axis]))
					return false;
				const double delta = center[axis] - eyes[eye][axis];
				squared += delta * delta;
			}
			// Borderline comparisons fall through to the authoritative distance calculation.
			if (squared < reach * reach * (1 - 32 * std::numeric_limits<double>::epsilon()))
				return false;
		}
		return true;
	}
	/** Keep invalid bounds and nearby actor roots conservative before applying shadow distance policy. */
	inline double SurfaceDistance(const std::array<double, 3>& center, double radius,
		const std::array<double, 3>& position, const std::array<double, 3>* actorRoot = nullptr) noexcept
	{
		const auto finite = [](const auto& point) { return std::all_of(point.begin(), point.end(), [](double v) { return std::isfinite(v); }); };
		if (!finite(center) || !finite(position) || !std::isfinite(radius) || radius <= 0 || (actorRoot && !finite(*actorRoot)))
			return std::numeric_limits<double>::quiet_NaN();
		const auto distance = [&](const auto& point) {
			return std::hypot(point[0] - position[0], point[1] - position[1], point[2] - position[2]) - radius;
		};
		const auto result = distance(center);
		return actorRoot ? std::min(result, distance(*actorRoot)) : result;
	}
	/** Admit only tiny distant shadow casters; unknown geometry, projections and close eyes retain their shadows. */
	inline bool ShouldCull(const Settings& settings, const Demand& demand) noexcept
	{
		if (!PassMayCull(settings, demand) || !CasterMayCull(settings, demand) || !std::isfinite(demand.radius) || demand.radius <= 0)
			return false;
		double shadowRadius = demand.radius;
		if (demand.pass == Pass::Point) {
			if (!std::isfinite(demand.lightDistance) || !std::isfinite(demand.lightRadius) || demand.lightDistance <= 1 ||
				demand.lightRadius <= 0 || demand.radius / demand.lightDistance >= 0.1)
				return false;
			shadowRadius *= std::max(1.0, demand.lightRadius / demand.lightDistance);
		} else {
			shadowRadius /= demand.sunElevationSine;
		}
		if (!std::isfinite(shadowRadius) || shadowRadius >= ShadowRadiusLimit(settings, demand.actor, demand.actorPart))
			return false;
		const auto minimum = MinimumDistance(settings, demand.pass, demand.actor, demand.actorPart);
		for (unsigned eye = 0; eye < demand.eyeCount; ++eye) {
			const auto distance = demand.eyeDistance[eye];
			if (!std::isfinite(distance) || distance <= minimum || shadowRadius / distance >= (demand.actor ? ActorAngularSize : ObjectAngularSize))
				return false;
		}
		return true;
	}
}
