#include "LookScenes.h"
#include "Utils/JsonCleanup.h"
#include "Utils/StringUtils.h"
#include <charconv>
#include <cstdint>
#include <stdexcept>

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>
#include <vector>

namespace Presets
{
	std::string SceneFormKey(std::string_view key)
	{
		const auto separator = key.find('~');
		auto hex = key.substr(0, separator);
		if (hex.starts_with("0x") || hex.starts_with("0X"))
			hex.remove_prefix(2);
		if (hex.empty() || (separator != key.npos && (separator + 1 == key.size() || key.find('~', separator + 1) != key.npos)))
			throw std::runtime_error("Invalid scene form key: " + std::string(key));
		std::uint32_t id = 0;
		const auto parsed = std::from_chars(hex.data(), hex.data() + hex.size(), id, 16);
		if (hex.empty() || parsed.ec != std::errc{} || parsed.ptr != hex.data() + hex.size())
			throw std::runtime_error("Invalid scene form key: " + std::string(key));
		char digits[8];
		const auto encoded = std::to_chars(digits, digits + sizeof(digits), id, 16);
		return std::string("0x") + std::string(digits, encoded.ptr) +
		       (separator == key.npos ? std::string{} : "~" + Util::ToLowerAscii(key.substr(separator + 1)));
	}

	namespace
	{
		void OverlaySceneFlattened(Json& destination, const Json& patch)
		{
			auto flat = destination.flatten();
			flat.erase("");
			const auto updates = patch.flatten();
			for (const auto& [path, value] : updates.items())
				if (!path.empty() && !value.is_null())
					flat[path] = value;
			destination = flat.unflatten();
		}

		/** Collect empty leaves while rejecting shapes that unflatten can reinterpret. */
		bool PreflightSceneShape(Json& value, std::vector<Json*>& emptyContainers)
		{
			if (value.is_object() && value.contains("0"))
				return false;
			if (!value.is_structured())
				return value.is_primitive();
			if (value.empty())
				emptyContainers.push_back(&value);
			for (auto& child : value)
				if (!PreflightSceneShape(child, emptyContainers))
					return false;
			return true;
		}
	}

	void OverlayScene(Json& destination, const Json& patch)
	{
		std::vector<Json*> emptyContainers;
		if (!PreflightSceneShape(destination, emptyContainers)) {
			OverlaySceneFlattened(destination, patch);
			return;
		}
		auto updates = patch.flatten();
		const Util::JsonCleanupGuard cleanup(updates);
		std::vector<std::pair<Json*, Json*>> assignments;
		assignments.reserve(updates.size());
		for (auto& [path, value] : updates.items()) {
			if (path.empty() || value.is_null())
				continue;
			Json* target = nullptr;
			try {
				const Json::json_pointer pointer(path);
				if (destination.contains(pointer))
					target = &destination.at(pointer);
			} catch (const Json::exception&) {
				// Preserve the flat-map fallback for object keys invalid as array indices.
			}
			if (!value.is_primitive() || !target || (target->is_structured() && !target->empty())) {
				OverlaySceneFlattened(destination, patch);
				return;
			}
			assignments.emplace_back(target, &value);
		}
		// All allocation precedes mutation; empty-leaf normalization preserves target addresses.
		if (destination.is_primitive())
			destination = nullptr;
		for (auto* container : emptyContainers)
			*container = nullptr;
		for (const auto& [target, value] : assignments)
			target->swap(*value);
	}

	std::array<float, 6> ScenePeriodWeights(std::array<float, 6> starts, float hour, float blendHours, float ease)
	{
		if (!std::isfinite(hour))
			hour = 12;
		if (!std::isfinite(blendHours))
			blendHours = 0;
		blendHours = std::clamp(blendHours, 0.0f, 6.0f);
		constexpr std::array shifts{ .5f, 0.f, -.5f, .5f, 0.f, -.5f };
		for (size_t i = 0; i < starts.size(); ++i) {
			const auto next = (i + 1) % starts.size();
			const auto length = (next ? starts[next] : starts[0] + 24) - starts[i];
			if (!std::isfinite(length) || length <= 0)
				return { 0, 0, 1, 0, 0, 0 };
			const auto scale = 1 + shifts[i] - shifts[next];
			if (scale > 0)
				blendHours = std::min(blendHours, length / scale);
		}
		for (size_t i = 0; i < starts.size(); ++i) starts[i] += shifts[i] * blendHours;
		hour = std::fmod(std::fmod(hour - blendHours * .5f, 24.f) + 24.f, 24.f);
		std::array<float, 6> result{};
		for (size_t i = 0; i < starts.size(); ++i) {
			const auto next = (i + 1) % starts.size();
			const auto end = next ? starts[next] : starts[0] + 24;
			const auto current = !next && hour < starts[i] ? hour + 24 : hour;
			if (current < starts[i] || current >= end)
				continue;
			float weight = blendHours > 0 ? std::clamp((end - current) / blendHours, 0.f, 1.f) : 1;
			const auto smooth = weight * weight * weight * (weight * (weight * 6 - 15) + 10);
			weight = std::lerp(weight, smooth, std::clamp(ease, 0.f, 1.f));
			result[i] = weight;
			result[next] = 1 - weight;
			return result;
		}
		return { 0, 0, 1, 0, 0, 0 };
	}

	namespace
	{
		/** Matches a non-root flatten leaf without materializing unrelated base fields. */
		const Json* FindSceneFlatLeaf(const Json& base, const std::string& path)
		{
			if (path.empty())
				return nullptr;
			try {
				const Json::json_pointer pointer(path);
				if (!base.contains(pointer))
					return nullptr;
				const auto& value = base.at(pointer);
				if (!value.is_structured())
					return &value;
				if (value.is_structured() && value.empty()) {
					static const Json emptyLeaf;
					return &emptyLeaf;
				}
			} catch (const Json::exception&) {
				// A patch's object key need not be a valid index in the base's array.
			}
			return nullptr;
		}
	}

	Json BlendSceneLayers(const Json& base, const Json& first, const Json& second, float weight)
	{
		const auto one = first.flatten(), two = second.flatten();
		std::set<std::string> paths;
		for (const auto* values : { &one, &two })
			for (const auto& [path, value] : values->items())
				if (!path.empty() && !value.is_null())
					paths.insert(path);
		Json result = Json::object();
		weight = std::clamp(weight, 0.f, 1.f);
		for (const auto& path : paths) {
			const auto* fallback = FindSceneFlatLeaf(base, path);
			if (!fallback)
				continue;
			const auto& a = one.contains(path) ? one[path] : *fallback;
			const auto& b = two.contains(path) ? two[path] : *fallback;
			if (a.is_number() && b.is_number() && (a.is_number_float() || b.is_number_float()))
				result[path] = std::lerp(a.get<double>(), b.get<double>(), static_cast<double>(weight));
			else
				result[path] = weight < .5f ? a : b;
		}
		return result.unflatten();
	}

	namespace
	{
		Json PeriodLayer(const std::map<std::string, Json>& layers, const std::string& root, const LookSceneContext& context, const Json& base, const std::map<std::string, bool>& periodicContexts)
		{
			const auto flat = layers.find(root);
			const bool periodic = root == "TimeOfDay" || (periodicContexts.contains(root) ? periodicContexts.at(root) : flat == layers.end() || flat->second.empty());
			if (!periodic)
				return flat == layers.end() ? Json::object() : flat->second;

			Json blended = Json::object();
			float accumulated = 0;
			const Json emptyPeriod = Json::object();
			for (size_t i = 0; i < ScenePeriods.size(); ++i) {
				const auto weight = context.periods[i];
				if (weight <= 0)
					continue;
				const auto period = layers.find(root + "/" + ScenePeriods[i]);
				const Json& values = period == layers.end() ? emptyPeriod : period->second;
				blended = BlendSceneLayers(base, blended, values, weight / (accumulated + weight));
				accumulated += weight;
			}
			return blended;
		}
	}

	Json ResolveLookLocations(const std::map<std::string, Json>& layers, const LookSceneContext& context, const Json& base, const std::map<std::string, bool>& periodicContexts, const std::map<std::string, Json>& locationOverrides)
	{
		Json result = Json::object();
		for (const auto& key : context.locations) {
			auto locationBase = base;
			OverlayScene(locationBase, result);
			OverlayScene(result, PeriodLayer(layers, "Locations/" + key, context, locationBase, periodicContexts));
			// Personal values win at their scope; more specific locations still inherit and refine them.
			if (const auto found = locationOverrides.find(key); found != locationOverrides.end())
				OverlayScene(result, found->second);
		}
		return result;
	}

	Json ResolveLookScenes(const std::map<std::string, Json>& layers, const LookSceneContext& context, const Json& base, const std::map<std::string, bool>& periodicContexts)
	{
		Json result = Json::object();
		if (!context.interior) {
			result = PeriodLayer(layers, "TimeOfDay", context, base, periodicContexts);
			auto weatherBase = base;
			OverlayScene(weatherBase, result);
			const auto previous = PeriodLayer(layers, "Weather/" + context.previousWeather, context, weatherBase, periodicContexts);
			const auto current = PeriodLayer(layers, "Weather/" + context.weather, context, weatherBase, periodicContexts);
			OverlayScene(result, BlendSceneLayers(weatherBase, previous, current, context.weatherWeight));
		}
		auto locationBase = base;
		OverlayScene(locationBase, result);
		OverlayScene(result, ResolveLookLocations(layers, context, locationBase, periodicContexts));
		return result;
	}
}
