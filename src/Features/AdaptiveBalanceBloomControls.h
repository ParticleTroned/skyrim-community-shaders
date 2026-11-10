#pragma once

#include "Bloom.h"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <string>

namespace AdaptiveBalanceBloomControls
{
	struct Field
	{
		const char* name;
		float Bloom::Profile::* member;
		double maximum;
	};
	inline constexpr std::array fields{
		Field{ "EnhancementIntensity", &Bloom::Profile::EnhancementIntensity, Bloom::kEnhancementIntensityMax },
		Field{ "HaloRadius", &Bloom::Profile::HaloRadius, Bloom::kHaloRadiusMax },
		Field{ "HaloSpread", &Bloom::Profile::HaloSpread, 1.0 },
		Field{ "BloomSaturation", &Bloom::Profile::BloomSaturation, Bloom::kBloomSaturationMax },
		Field{ "CompressionThreshold", &Bloom::Profile::CompressionThreshold, Bloom::kCompressionCeilingMax },
		Field{ "CompressionCeiling", &Bloom::Profile::CompressionCeiling, Bloom::kCompressionCeilingMax }
	};

	/** Describes atomic preset selection, reset and active-profile editing. */
	inline nlohmann::json Schema()
	{
		using json = nlohmann::json;
		json properties = json::object();
		for (const auto& field : fields)
			properties[field.name] = { { "type", "number" }, { "minimum", 0 }, { "maximum", field.maximum } };
		properties["BloomTint"] = { { "type", "array" }, { "minItems", 3 }, { "maxItems", 3 },
			{ "items", { { "type", "number" }, { "minimum", 0 }, { "maximum", 1 } } } };
		return { { "type", "object" }, { "minProperties", 1 }, { "additionalProperties", false },
			{ "description", "Global bloom controls. Select preset 0 Default, 1 Fantasy or 2 Dreamy, optionally reset that slot, then apply profile edits. Switching retains edits in the previous slot. Compression threshold is capped to the resulting ceiling. Does not enable bloom or save settings." },
			{ "properties", { { "preset", { { "type", "integer" }, { "minimum", 0 }, { "maximum", Bloom::kPresetCount - 1 } } },
								{ "reset", { { "type", "boolean" } } },
								{ "profile", { { "type", "object" }, { "minProperties", 1 }, { "additionalProperties", false }, { "properties", properties } } } } } };
	}

	/** Reject unknown keys, wrong types, nonfinite values and values beyond UI limits. */
	inline std::string Validate(const nlohmann::json& patch)
	{
		const auto number = [](const nlohmann::json& value, double maximum) {
			return value.is_number() && std::isfinite(value.get<double>()) && value.get<double>() >= 0.0 && value.get<double>() <= maximum;
		};
		if (!patch.is_object() || patch.empty())
			return "bloom must be a nonempty object";
		for (const auto& [name, value] : patch.items()) {
			if (name == "preset") {
				if (!value.is_number_integer() || !number(value, Bloom::kPresetCount - 1))
					return "bloom.preset must be an integer in [0,2]";
			} else if (name == "reset") {
				if (!value.is_boolean())
					return "bloom.reset must be boolean";
			} else if (name == "profile") {
				if (!value.is_object() || value.empty())
					return "bloom.profile must be a nonempty object";
				for (const auto& [key, input] : value.items()) {
					if (key == "BloomTint") {
						if (!input.is_array() || input.size() != 3)
							return "BloomTint requires three channels";
						for (const auto& channel : input)
							if (!number(channel, 1))
								return "BloomTint channels must be finite numbers in [0,1]";
					} else {
						const auto field = std::find_if(fields.begin(), fields.end(), [&](const auto& entry) { return key == entry.name; });
						if (field == fields.end() || !number(input, field->maximum))
							return "Unknown or invalid bloom profile field: " + key;
					}
				}
			} else {
				return "Unknown bloom field: " + name;
			}
		}
		return {};
	}

	/** Validate before touching any saved preset or active values. */
	inline std::string Apply(const nlohmann::json& patch, Bloom::Profile& profile, Bloom::PresetSlots& presets)
	{
		if (auto error = Validate(patch); !error.empty())
			return error;
		if (patch.contains("preset"))
			Bloom::SelectPreset(profile, presets, patch.at("preset").get<uint>());
		if (patch.value("reset", false))
			Bloom::ResetPreset(profile, presets);
		if (patch.contains("profile")) {
			const auto& input = patch.at("profile");
			for (const auto& field : fields)
				if (input.contains(field.name))
					profile.*field.member = input.at(field.name).get<float>();
			if (input.contains("BloomTint")) {
				const auto& tint = input.at("BloomTint");
				profile.BloomTint = { tint.at(0).get<float>(), tint.at(1).get<float>(), tint.at(2).get<float>() };
			}
		}
		Bloom::SanitizeProfile(profile);
		Bloom::SanitizePresets(presets);
		presets.profiles[presets.selected] = profile;
		return {};
	}
}
