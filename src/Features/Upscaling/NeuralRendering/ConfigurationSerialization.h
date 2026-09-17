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
