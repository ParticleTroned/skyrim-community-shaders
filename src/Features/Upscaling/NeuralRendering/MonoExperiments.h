#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "ReconstructionTuning.h"
#	include <nlohmann/json.hpp>
#	include <string>

namespace NeuralRendering
{
	/** Explicit session controls; production presets never serialize these experiments. */
	struct MonoExperiments
	{
		ReconstructionTuning reconstruction{};
		bool nvidiaReferenceColor = false, nativeControlMask = false, nativeJitter = false;
		bool operator==(const MonoExperiments&) const = default;
	};

	[[nodiscard]] inline nlohmann::json DescribeMonoExperiments(const MonoExperiments& value)
	{
		const auto& tuning = value.reconstruction;
		return { { "shadowTransferPercent", tuning.shadowTransferPercent }, { "faceDetailProtectionPercent", tuning.faceDetailProtectionPercent },
			{ "footprintSubdivisions", tuning.footprintSubdivisions }, { "perChannelClamping", tuning.perChannelClamping },
			{ "depthSampleMotion", tuning.depthSampleMotion }, { "nvidiaReferenceColor", value.nvidiaReferenceColor },
			{ "nativeControlMask", value.nativeControlMask }, { "nativeJitter", value.nativeJitter } };
	}

	/** Reject the whole patch before any renderer state changes. */
	[[nodiscard]] inline std::string ApplyMonoExperimentsPatch(const nlohmann::json& patch, MonoExperiments& value)
	{
		if (!patch.is_object() || patch.empty())
			return "experiments must be a nonempty object";
		auto merged = DescribeMonoExperiments(value);
		for (const auto& [key, item] : patch.items()) {
			if (!merged.contains(key))
				return "unknown experiment: " + key;
			if (merged[key].is_boolean()) {
				if (!item.is_boolean())
					return "experiment must be boolean: " + key;
			} else if (!item.is_number_integer() || (!item.is_number_unsigned() && item.get<std::int64_t>() < 0) || item.get<std::uint64_t>() > UINT32_MAX)
				return "experiment must be an unsigned integer: " + key;
			merged[key] = item;
		}
		auto candidate = value;
		auto& tuning = candidate.reconstruction;
		tuning.shadowTransferPercent = merged["shadowTransferPercent"].get<std::uint32_t>();
		tuning.faceDetailProtectionPercent = merged["faceDetailProtectionPercent"].get<std::uint32_t>();
		tuning.footprintSubdivisions = merged["footprintSubdivisions"].get<std::uint32_t>();
		tuning.perChannelClamping = merged["perChannelClamping"].get<bool>();
		tuning.depthSampleMotion = merged["depthSampleMotion"].get<bool>();
		candidate.nvidiaReferenceColor = merged["nvidiaReferenceColor"].get<bool>();
		candidate.nativeControlMask = merged["nativeControlMask"].get<bool>();
		candidate.nativeJitter = merged["nativeJitter"].get<bool>();
		if (!tuning.Valid())
			return "experiments exceed the declared reconstruction ranges";
		value = candidate;
		return {};
	}
}
#endif
