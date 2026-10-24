#pragma once

#include "CentralAreaPolicy.h"
#include "DevelopmentDiagnostics.h"
#include "GuidedReconstructionPolicy.h"
#include "PeripheralCompressionPolicy.h"
#include "TuningLimits.h"

#include <algorithm>
#include <array>
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

	/** Named filters form the public DevBench reconstruction contract. */
	[[nodiscard]] inline std::optional<ReconstructionFilter> ParseReconstructionFilter(const nlohmann::json& value)
	{
		if (!value.is_string())
			return std::nullopt;
		if (value == "bilinear")
			return ReconstructionFilter::Bilinear;
		if (value == "lanczos3")
			return ReconstructionFilter::Lanczos3;
		return std::nullopt;
	}

#ifdef DEVBENCH_BRIDGE_ENABLED
	template <class Mode>
	struct ComparisonRequest
	{
		Mode mode = Mode::Auto;
		std::uint32_t frames = 0;
		std::uint64_t expectedRevision = 0;
	};
	using ReconstructionComparisonRequest = ComparisonRequest<GuidedReconstructionOverride>;
	using PeripheralComparisonRequest = ComparisonRequest<PeripheralCompressionOverride>;

	/** Validate a complete comparison lease before renderer ownership or mutation. */
	template <class Lease, class Mode, std::size_t Count, class Name>
	[[nodiscard]] inline std::optional<ComparisonRequest<Mode>> ParseComparisonLease(const nlohmann::json& args,
		const char* action, const char* modeField, const char* framesField, const char* revisionField,
		const std::array<Mode, Count>& modes, Name name)
	{
		if (!args.is_object())
			return std::nullopt;
		for (const auto& item : args.items())
			if (item.key() != "action" && item.key() != "expectedBuildId" && item.key() != modeField && item.key() != framesField && item.key() != revisionField)
				return std::nullopt;
		if (args.value("action", nlohmann::json{}) != action)
			return std::nullopt;
		const auto mode = args.find(modeField), frames = args.find(framesField), revision = args.find(revisionField);
		if (mode == args.end() || !mode->is_string() || frames == args.end() || revision == args.end() || !revision->is_number_integer())
			return std::nullopt;
		const auto selected = std::ranges::find_if(modes, [&](Mode candidate) { return *mode == name(candidate); });
		if (selected == modes.end())
			return std::nullopt;
		ComparisonRequest<Mode> result;
		result.mode = *selected;
		const auto boundedFrames = ParseBoundedInteger(*frames, 0, Lease::kMaximumFrames);
		if (!boundedFrames || (result.mode == Mode::Auto ? *boundedFrames != 0 : *boundedFrames == 0) ||
			(!revision->is_number_unsigned() && revision->get<std::int64_t>() <= 0))
			return std::nullopt;
		result.expectedRevision = revision->get<std::uint64_t>();
		if (result.expectedRevision == 0 || result.expectedRevision == std::numeric_limits<std::uint64_t>::max())
			return std::nullopt;
		result.frames = *boundedFrames;
		return result;
	}
	/** Parse the guided reconstruction comparison without persisting benchmark state. */
	[[nodiscard]] inline std::optional<ReconstructionComparisonRequest> ParseReconstructionComparison(const nlohmann::json& args)
	{
		using enum GuidedReconstructionOverride;
		return ParseComparisonLease<GuidedReconstructionExperiment>(args, "nr_reconstruction_compare", "reconstructionOverride", "reconstructionOverrideFrames",
			"expectedReconstructionOverrideRevision", std::array{ Auto, Basic, GuidedExperiment }, GetGuidedReconstructionOverrideName);
	}
	/** Parse peripheral baseline, identity and compressed-profile comparison leases. */
	[[nodiscard]] inline std::optional<PeripheralComparisonRequest> ParsePeripheralComparison(const nlohmann::json& args)
	{
		using enum PeripheralCompressionOverride;
		return ParseComparisonLease<PeripheralCompressionExperiment>(args, "nr_peripheral_compare", "peripheralOverride", "peripheralOverrideFrames",
			"expectedPeripheralOverrideRevision", std::array{ Auto, Ordinary, Identity, CompressedExperiment }, GetPeripheralCompressionOverrideName);
	}
#endif

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

	/** Accepts legacy placement, coverage and provider range reductions; rejects invalid tuning. */
	inline void ValidateRenderingSettingsNormalization(const nlohmann::json& requested, const nlohmann::json& normalized, TuningLimits limits = {})
	{
		const auto isProviderRangeReduction = [&](const std::string& name, const nlohmann::json& value) {
			if (!value.is_number())
				return false;
			float maximum = 2.0f;
			if (name == "neuralRenderingIntensity")
				maximum = limits.intensity;
			else if (name == "neuralRenderingLocalStructure")
				maximum = limits.localStructureStrength;
			else if (name == "neuralRenderingSkinStructure")
				maximum = limits.skinStructureStrength;
			const double strength = value.get<double>();
			return strength > maximum && strength <= 2.0 && normalized.at(name) == maximum;
		};
		for (const auto& [name, value] : requested.items()) {
			if (normalized.at(name) == value)
				continue;
			if (name == "neuralRenderingInsertionPoint" && (value == 0u || value == 1u))
				continue;
			if (name == "neuralRenderingMode" && ParseBoundedInteger(value, 1u, 1u) && normalized.at(name) == 0u)
				continue;
			if ((name == "neuralRenderingFovOnly" || name == "neuralRenderingRenderscaleFov") && value.is_boolean())
				continue;
			if (isProviderRangeReduction(name, value))
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
