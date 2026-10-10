#pragma once

#include "CentralAreaPolicy.h"
#include "DevelopmentDiagnostics.h"
#include "ModelResolutionPolicy.h"

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>

namespace NeuralRendering
{
	/** Rejects fractional, wrapped, or out-of-range integer controls. */
	[[nodiscard]] inline std::optional<std::uint32_t> ParseBoundedInteger(const nlohmann::json& value, std::uint32_t minimum, std::uint32_t maximum)
	{
		if (!value.is_number_integer())
			return std::nullopt;
		if (value.is_number_unsigned()) {
			const auto percent = value.get<std::uint64_t>();
			if (percent < minimum || percent > maximum)
				return std::nullopt;
			return static_cast<std::uint32_t>(percent);
		}
		const auto percent = value.get<std::int64_t>();
		if (percent < minimum || percent > maximum)
			return std::nullopt;
		return static_cast<std::uint32_t>(percent);
	}

	/** Validated independent model scale. */
	[[nodiscard]] inline auto ParseModelResolutionPercent(const nlohmann::json& value)
	{
		return ParseBoundedInteger(value, kMinimumModelResolutionPercent, kMaximumModelResolutionPercent);
	}

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

	/** Strips session-only diagnostics from persisted rendering settings. */
	inline nlohmann::json PersistentRenderingSettings(nlohmann::json rendering)
	{
		if (!rendering.is_object())
			throw std::invalid_argument("rendering settings must be an object");
		if (const auto mode = rendering.find("neuralRenderingMode"); mode != rendering.end() && ParseBoundedInteger(*mode, 1u, 1u))
			rendering["neuralRenderingMode"] = 0u;
		if (const auto legacy = rendering.find("neuralRenderingOptimizedStereoPath"); legacy != rendering.end()) {
			for (const auto* key : { "neuralRenderingBatchedStereo", "neuralRenderingDirectCommit" })
				if (!rendering.contains(key))
					rendering[key] = legacy->get<bool>();
			rendering.erase("neuralRenderingOptimizedStereoPath");
		}
		for (const auto* key : { "neuralCharacterDebugView", "neuralCharacterMaskTestMode",
				 "neuralRenderingFovOnly", "neuralRenderingRenderscaleFov" })
			rendering.erase(key);
#ifdef DEVBENCH_BRIDGE_ENABLED
		rendering.erase("neuralCharacterCurrentContextEnabled");
		rendering.erase("neuralCharacterGpuMaskSupportEnabled");
#endif
		if constexpr (!kDevelopmentDiagnostics) {
			for (const auto* key : { "neuralRenderingBatchedStereo", "neuralRenderingDirectCommit",
					 "neuralRenderingSingleSubrectScale", "neuralCharacterVisualIsolationEnabled",
					 "neuralCharacterVisibilityDepthTestEnabled" })
				rendering.erase(key);
			if (rendering.value("neuralCharacterCropMode", 1u) == 0u)
				rendering.erase("neuralCharacterCropMode");
		}
		return rendering;
	}

	/** Accepts legacy placement and derived coverage while rejecting invalid tuning. */
	inline void ValidateRenderingSettingsNormalization(const nlohmann::json& requested, const nlohmann::json& normalized)
	{
		for (const auto& [name, value] : requested.items()) {
			if (normalized.at(name) == value)
				continue;
			if (name == "neuralRenderingInsertionPoint" && (value == 0u || value == 1u))
				continue;
			if (name == "neuralRenderingMode" && ParseBoundedInteger(value, 1u, 1u) && normalized.at(name) == 0u)
				continue;
			if ((name == "neuralRenderingFovOnly" || name == "neuralRenderingRenderscaleFov") && value.is_boolean())
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
#ifdef DEVBENCH_BRIDGE_ENABLED
		destination.neuralCharacterCurrentContextEnabled = source.neuralCharacterCurrentContextEnabled;
		destination.neuralCharacterGpuMaskSupportEnabled = source.neuralCharacterGpuMaskSupportEnabled;
#endif
	}
}
