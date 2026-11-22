#pragma once

#include "SettingsDomains.h"
#include <array>
#include <map>
#include <vector>

namespace Presets
{
	inline constexpr std::array<const char*, 6> ScenePeriods{ "Dawn", "Sunrise", "Day", "Sunset", "Dusk", "Night" };
	struct LookSceneContext
	{
		bool interior = false;
		std::array<float, 6> periods{ 0, 0, 1, 0, 0, 0 };
		std::string previousWeather, weather;
		float weatherWeight = 1;
		std::vector<std::string> locations;
		std::string worldspace, cell;
		bool operator==(const LookSceneContext&) const = default;
	};
	/** Canonical matching for file-key contexts, independent of plugin case and hex padding. */
	std::string SceneFormKey(std::string_view key);
	/** Overlays covered leaf fields without replacing neighbouring array elements. */
	void OverlayScene(Json& destination, const Json& patch);
	/** Resolves just location ownership above a supplied time/weather baseline. */
	Json ResolveLookLocations(const std::map<std::string, Json>& layers, const LookSceneContext& context, const Json& base, const std::map<std::string, bool>& periodicContexts = {});
	/** Climate-based period weights using the native scene-pack boundary convention. */
	std::array<float, 6> ScenePeriodWeights(std::array<float, 6> starts, float hour, float blendHours, float ease);
	/** Blends only covered fields, restoring absent endpoints from the authored base. */
	Json BlendSceneLayers(const Json& base, const Json& first, const Json& second, float weight);
	/** Resolves periods, weather and broad-to-specific places without flattening authored rules. */
	Json ResolveLookScenes(const std::map<std::string, Json>& layers, const LookSceneContext& context, const Json& base, const std::map<std::string, bool>& periodicContexts = {});
}
