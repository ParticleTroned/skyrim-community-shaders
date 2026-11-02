#pragma once
#include "MonoPolicy.h"
#include <functional>
#include <nlohmann/json.hpp>
#include <string>

namespace NeuralRendering
{
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(MonoSettings, enabled, mode, modelResolutionPercent, peripheralCompressionPercent, network, style,
		intensity, localTone, localStructure, skinStructure, guidedReconstruction, guidedConfidenceRejectionPercent, pressureResolution, prewarm, pauseInCombat,
		colorMode, detailStrength, lightingPreservation, exposure, charactersOnly, sceneCategoryStrengths, categoryStrengths,
		categoryMask, actorGroupMask, maximumDistanceMeters, minimumFacePixelSize, holdFrames, adaptiveActorSelection,
		actorFocus, focusScale, cropToCharacters, packedCharacters, captureExposure, exposureSource, regionPercent, featherPixels, horizontalScale, regionOffset);
}

namespace NeuralRendering
{
	/** Strict atomic automation patches; persisted profiles still use Sanitize for migration. */
	[[nodiscard]] inline std::string ApplyMonoSettingsPatch(const nlohmann::json& patch, MonoSettings& settings)
	{
		using Json = nlohmann::json;
		if (!patch.is_object() || patch.empty())
			return "settings must be a nonempty object";
		Json merged = settings;
		std::function<bool(const Json&, Json&)> convert = [&](const Json& value, Json& target) {
			if (target.is_boolean()) {
				if (!value.is_boolean())
					return false;
				target = value;
				return true;
			}
			if (target.is_number_unsigned()) {
				if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<std::int64_t>() < 0) ||
					value.get<std::uint64_t>() > UINT32_MAX)
					return false;
				target = value.get<std::uint32_t>();
				return true;
			}
			if (target.is_number_float()) {
				if (!value.is_number())
					return false;
				const auto converted = value.get<float>();
				if (!Util::IsFinite(converted))
					return false;
				target = converted;
				return true;
			}
			if (target.is_array()) {
				if (!value.is_array() || target.size() != value.size())
					return false;
				for (std::size_t index = 0; index < target.size(); ++index)
					if (!convert(value[index], target[index]))
						return false;
				return true;
			}
			return false;
		};
		for (const auto& [key, value] : patch.items()) {
			if (!merged.contains(key))
				return "unknown setting: " + key;
			if (!convert(value, merged[key]))
				return "invalid value type: " + key;
		}
		auto candidate = merged.get<MonoSettings>();
		Sanitize(candidate);
		if (Json(candidate) != merged)
			return "settings exceed the declared mono ranges";
		settings = candidate;
		return {};
	}
}
