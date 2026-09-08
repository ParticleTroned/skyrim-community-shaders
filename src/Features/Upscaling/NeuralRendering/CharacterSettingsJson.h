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
		inline constexpr auto kVisualIsolationKey = "neuralCharacterVisualIsolationEnabled";

		/** Keep the existing flat VR schema and policy-member mapping in one place. */
		template <class Settings, class Policy, class Visitor>
		void VisitFields(Settings& settings, Policy& policy, Visitor&& visit)
		{
			visit("neuralCharacterRenderingEnabled", settings.neuralCharacterRenderingEnabled, policy.enabled);
			visit("neuralCharacterFacesEnabled", settings.neuralCharacterFacesEnabled, policy.faces);
			visit("neuralCharacterSkinEnabled", settings.neuralCharacterSkinEnabled, policy.skin);
			visit("neuralCharacterHairEnabled", settings.neuralCharacterHairEnabled, policy.hair);
			visit("neuralCharacterFaceStrength", settings.neuralCharacterFaceStrength, policy.faceStrength);
			visit("neuralCharacterSkinStrength", settings.neuralCharacterSkinStrength, policy.skinStrength);
			visit("neuralCharacterHairStrength", settings.neuralCharacterHairStrength, policy.hairStrength);
			visit("neuralCharacterMaximumDistanceMeters", settings.neuralCharacterMaximumDistanceMeters, policy.maximumDistanceMeters);
			visit("neuralCharacterAdaptiveRoiSelectionEnabled", settings.neuralCharacterAdaptiveRoiSelectionEnabled, policy.adaptiveRoiSelection);
			visit("neuralCharacterMultiRoiEnabled", settings.neuralCharacterMultiRoiEnabled, policy.multiRoi);
			visit("neuralCharacterMinimumFacePixelSize", settings.neuralCharacterMinimumFacePixelSize, policy.minimumFacePixelSize);
			visit("neuralCharacterRoiMargin", settings.neuralCharacterRoiMargin, policy.roiMargin);
			visit("neuralCharacterRoiHoldFrames", settings.neuralCharacterRoiHoldFrames, policy.roiHoldFrames);
			visit("neuralCharacterDepthAwareFeatherEnabled", settings.neuralCharacterDepthAwareFeatherEnabled, policy.depthAwareFeather);
			visit("neuralCharacterVisibilityDepthTestEnabled", settings.neuralCharacterVisibilityDepthTestEnabled, policy.visibilityDepthTest);
			visit("neuralCharacterFeatherRadius", settings.neuralCharacterFeatherRadius, policy.featherRadius);
			visit("neuralCharacterDepthThreshold", settings.neuralCharacterDepthThreshold, policy.featherDepthThreshold);
		}

		template <class Value>
		void ReadValue(const nlohmann::json& json, Value& value)
		{
			if constexpr (std::is_same_v<Value, std::uint32_t>) {
				if (!json.is_number_integer())
					throw nlohmann::json::type_error::create(302,
						"character counts require integer values", &json);
				// Preserve range direction before narrowing. The policy sanitizer
				// applies the individual setting's bounds afterwards.
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

	/** Copy VR's flat controls without applying the master rendering switch. */
	template <class Settings>
	[[nodiscard]] CharacterSettings GetUpscalingCharacterSettings(const Settings& settings)
	{
		CharacterSettings policy{};
		CharacterSettingsJsonDetail::VisitFields(settings, policy,
			[](const char*, const auto& source, auto& destination) { destination = source; });
		policy.debugView = static_cast<CharacterDebugView>(settings.neuralCharacterDebugView);
		policy.maskTestMode = static_cast<CharacterMaskTestMode>(settings.neuralCharacterMaskTestMode);
		return policy;
	}

	/** Write a sanitized policy back without changing VR's visual-isolation switch. */
	template <class Settings>
	void ApplyUpscalingCharacterSettings(Settings& settings, const CharacterSettings& policy)
	{
		CharacterSettingsJsonDetail::VisitFields(settings, policy,
			[](const char*, auto& destination, const auto& source) { destination = source; });
		settings.neuralCharacterDebugView = static_cast<std::uint32_t>(policy.debugView);
		settings.neuralCharacterMaskTestMode = static_cast<std::uint32_t>(policy.maskTestMode);
	}

	/** Append the persisted VR character fields; developer modes remain transient. */
	template <class Settings>
	void WriteUpscalingCharacterSettingsJson(nlohmann::json& json, const Settings& settings)
	{
		const CharacterSettings policy{};
		CharacterSettingsJsonDetail::VisitFields(settings, policy,
			[&](const char* key, const auto& value, const auto&) { json[key] = value; });
		json[CharacterSettingsJsonDetail::kVisualIsolationKey] = settings.neuralCharacterVisualIsolationEnabled;
	}

	/** Read defaults and validate all persisted fields before changing controls. */
	template <class Settings>
	void ReadUpscalingCharacterSettingsJson(const nlohmann::json& json, Settings& settings)
	{
		if (!json.is_object())
			throw nlohmann::json::type_error::create(302,
				"character settings require an object", &json);
		CharacterSettings parsed{};
		CharacterSettingsJsonDetail::VisitFields(settings, parsed,
			[&](const char* key, const auto&, auto& value) {
				if (const auto found = json.find(key); found != json.end())
					CharacterSettingsJsonDetail::ReadValue(*found, value);
			});
		bool visualIsolation = CharacterPolicy::kDefaultVisualIsolation;
		if (const auto found = json.find(CharacterSettingsJsonDetail::kVisualIsolationKey); found != json.end())
			CharacterSettingsJsonDetail::ReadValue(*found, visualIsolation);
		SanitizeCharacterSettings(parsed);
		CharacterSettingsJsonDetail::VisitFields(settings, parsed,
			[](const char*, auto& destination, const auto& source) { destination = source; });
		settings.neuralCharacterVisualIsolationEnabled = visualIsolation;
	}
}
