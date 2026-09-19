#pragma once

#include <nlohmann/json.hpp>
#include <stdexcept>

namespace NeuralRendering
{
	/** Selects feature-owned fields from an ordinary or legacy upscaling profile. */
	inline nlohmann::json RenderingSettings(const nlohmann::json& settings)
	{
		nlohmann::json result = nlohmann::json::object();
		if (settings.is_object())
			for (const auto& [name, value] : settings.items())
				if (name.starts_with("neural"))
					result[name] = value;
		return result;
	}

	/** Preserves legacy colour boot-disable without disabling independent rendering. */
	inline nlohmann::json LegacyColourSettings(const nlohmann::json& settings)
	{
		auto colour = settings.value("Neural Rendering Colour", nlohmann::json::object());
		const auto disabled = settings.find("Disable at Boot");
		if (disabled != settings.end() && disabled->is_object()) {
			const auto legacy = disabled->find("NeuralColor");
			if (legacy != disabled->end() && legacy->is_boolean() && legacy->get<bool>() && colour.is_object())
				colour["enabled"] = false;
		}
		return colour;
	}

	/** Migrates legacy lane selection without persisting session-only overrides. */
	inline nlohmann::json PersistentRenderingSettings(nlohmann::json rendering)
	{
		if (!rendering.is_object())
			throw std::invalid_argument("rendering settings must be an object");
		if (const auto legacy = rendering.find("neuralRenderingOptimizedStereoPath"); legacy != rendering.end()) {
			for (const auto* key : { "neuralRenderingBatchedStereo", "neuralRenderingDirectCommit" })
				if (!rendering.contains(key))
					rendering[key] = legacy->get<bool>();
			rendering.erase("neuralRenderingOptimizedStereoPath");
		}
		for (const auto* key : { "neuralCharacterMultiRoiEnabled", "neuralCharacterMultiRoiSavingsGateEnabled",
				 "neuralCharacterDebugView", "neuralCharacterMaskTestMode" })
			rendering.erase(key);
		return rendering;
	}

	/** Accepts migrated placement while rejecting other out-of-range persisted values. */
	inline void ValidateRenderingSettingsNormalization(const nlohmann::json& requested, const nlohmann::json& normalized)
	{
		for (const auto& [name, value] : requested.items()) {
			if (normalized.at(name) == value)
				continue;
			if (name == "neuralRenderingInsertionPoint" && (value == 0u || value == 1u))
				continue;
			throw std::invalid_argument("Neural Rendering setting is outside its valid range: " + name);
		}
	}

	/** Keeps an independent feature's live settings intact during an upscaler edit. */
	template <class Settings>
	void CopyRenderingSettings(Settings& destination, const Settings& source)
	{
		nlohmann::json merged = destination;
		merged.update(RenderingSettings(source));
		destination = merged.template get<Settings>();
		destination.neuralCharacterDebugView = source.neuralCharacterDebugView;
		destination.neuralCharacterMaskTestMode = source.neuralCharacterMaskTestMode;
	}
}
