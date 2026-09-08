#pragma once

#include "CharacterSettings.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>

#include <nlohmann/json.hpp>

namespace NeuralRendering
{
	namespace CharacterSettingsJsonDetail
	{
		template <class Settings, class Visitor>
		void VisitFields(Settings& settings, Visitor&& visit)
		{
			visit("enabled", settings.enabled);
			visit("faces", settings.faces);
			visit("skin", settings.skin);
			visit("hair", settings.hair);
			visit("faceStrength", settings.faceStrength);
			visit("skinStrength", settings.skinStrength);
			visit("hairStrength", settings.hairStrength);
			visit("maximumDistanceMeters", settings.maximumDistanceMeters);
			visit("adaptiveRoiSelection", settings.adaptiveRoiSelection);
			visit("multiRoi", settings.multiRoi);
			visit("minimumFacePixelSize", settings.minimumFacePixelSize);
			visit("roiMargin", settings.roiMargin);
			visit("roiHoldFrames", settings.roiHoldFrames);
			visit("depthAwareFeather", settings.depthAwareFeather);
			visit("visibilityDepthTest", settings.visibilityDepthTest);
			visit("featherRadius", settings.featherRadius);
			visit("featherDepthThreshold", settings.featherDepthThreshold);
			visit("debugView", settings.debugView);
			visit("maskTestMode", settings.maskTestMode);
		}

		template <class Value>
		void ReadValue(const nlohmann::json& json, Value& value)
		{
			if constexpr (std::is_same_v<Value, std::uint32_t> || std::is_enum_v<Value>) {
				if (!json.is_number_integer())
					throw nlohmann::json::type_error::create(302,
						"character counts and modes require integer values", &json);
				// Preserve the range direction before narrowing; the policy sanitizer
				// remains responsible for each setting's bounds and enum fallback.
				constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
				const auto bounded = json.is_number_unsigned() ?
				                         std::min<std::uint64_t>(json.get<std::uint64_t>(), maximum) :
				                         static_cast<std::uint64_t>(std::clamp<std::int64_t>(
											 json.get<std::int64_t>(), 0, maximum));
				value = static_cast<Value>(bounded);
			} else if constexpr (std::is_same_v<Value, float>) {
				if (!json.is_number())
					throw nlohmann::json::type_error::create(302,
						"character strengths and ranges require numeric values", &json);
				const auto number = json.get<double>();
				constexpr double maximum = std::numeric_limits<float>::max();
				value = static_cast<float>(std::isfinite(number) ?
											   std::clamp(number, -maximum, maximum) :
											   number);
			} else {
				json.get_to(value);
			}
		}
	}

	/** Serialize the stable persisted character setting names. */
	inline void to_json(nlohmann::json& json, const CharacterSettings& settings)
	{
		json = nlohmann::json::object();
		CharacterSettingsJsonDetail::VisitFields(settings,
			[&](const char* key, const auto& value) { json[key] = value; });
	}

	/** Read defaults plus known fields, committing only after type validation. */
	inline void from_json(const nlohmann::json& json, CharacterSettings& settings)
	{
		if (!json.is_object())
			throw nlohmann::json::type_error::create(302,
				"character settings require an object", &json);
		CharacterSettings parsed{};
		CharacterSettingsJsonDetail::VisitFields(parsed,
			[&](const char* key, auto& value) {
				if (const auto found = json.find(key); found != json.end())
					CharacterSettingsJsonDetail::ReadValue(*found, value);
			});
		SanitizeCharacterSettings(parsed);
		settings = parsed;
	}
}
