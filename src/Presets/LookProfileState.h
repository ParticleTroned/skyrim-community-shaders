#pragma once

#include "LookScenes.h"
#include "SettingsDomains.h"
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

namespace Presets
{
	/** Filters interior entries without changing their pause state or silently accepting duplicate keys. */
	inline Json InteriorEditsForDomain(const Json& entries, bool appearance)
	{
		if (!entries.is_array() || entries.size() > 4096)
			throw std::runtime_error("Invalid personal interior edits");
		Json result = Json::array();
		std::set<std::pair<std::string, std::string>> keys;
		for (const auto& entry : entries) {
			const auto feature = entry.at("feature").get<std::string>();
			const auto key = entry.at("setting").get<std::string>();
			if (!keys.emplace(feature, key).second)
				throw std::runtime_error("Duplicate personal interior setting");
			const Json patch{ { key, entry.at("value") } };
			auto values = SelectDomain(feature, patch, appearance ? SettingsDomain::Appearance : SettingsDomain::Performance);
			if (!appearance)
				OverlayScene(values, SelectDomain(feature, patch, SettingsDomain::Local));
			if (!values.contains(key))
				continue;
			auto selected = entry;
			selected["value"] = values.at(key);
			result.push_back(std::move(selected));
		}
		return result;
	}

	/** Changes only appearance entries; interior performance and local choices remain shared. */
	inline Json ReplaceInteriorAppearance(const Json& current, const Json& profile)
	{
		auto result = InteriorEditsForDomain(current, false);
		std::map<std::pair<std::string, std::string>, size_t> indices;
		for (size_t i = 0; i < result.size(); ++i)
			indices.emplace(std::pair{ result[i].at("feature").get<std::string>(), result[i].at("setting").get<std::string>() }, i);
		for (const auto& entry : InteriorEditsForDomain(profile, true)) {
			const auto key = std::pair{ entry.at("feature").get<std::string>(), entry.at("setting").get<std::string>() };
			if (const auto found = indices.find(key); found != indices.end()) {
				// A shared entry's pause state also owns its performance fields.
				auto& existing = result[found->second];
				Json values{ { key.second, existing["value"] } };
				OverlayScene(values, Json{ { key.second, entry["value"] } });
				existing["value"] = values.at(key.second);
			} else
				result.push_back(entry);
		}
		return result;
	}

	/** Keeps personal edits per profile; settings from unavailable features remain recoverable. */
	inline void StoreProfileEdits(Json& state, const std::string& id, const Json& appearance, const Json& baseline, const Json& interior)
	{
		auto& personal = state["csxProfiles"][id];
		auto& edits = personal["edits"];
		if (!edits.is_object())
			edits = Json::object();
		for (const auto& [feature, values] : appearance.items()) {
			const auto delta = Difference(values, baseline.value(feature, Json::object()));
			if (delta.empty())
				edits.erase(feature);
			else
				edits[feature] = delta;
		}
		personal["interior"] = InteriorEditsForDomain(interior, true);
	}

	/** Copies one feature's personal profile state without accepting another feature's edits. */
	inline void CopyFeatureProfileState(Json& target, const Json& source, const std::string& feature)
	{
		if (!target.contains("Presets") || !source.contains("Presets"))
			return;
		auto& destination = target["Presets"];
		const auto& origin = source["Presets"];
		if (!destination.is_object() || !origin.is_object())
			return;
		const auto id = origin.value("activePackId", std::string{});
		if (id != destination.value("activePackId", std::string{}))
			return;
		const auto profiles = origin.find("csxProfiles");
		if (profiles == origin.end() || !profiles->is_object() || !profiles->contains(id))
			return;
		const auto& personal = profiles->at(id);
		const auto edits = personal.value("edits", Json::object());
		auto& saved = destination["csxProfiles"][id];
		if (!saved["edits"].is_object())
			saved["edits"] = Json::object();
		if (edits.contains(feature))
			saved["edits"][feature] = edits[feature];
		else
			saved["edits"].erase(feature);
		const auto copyInterior = [&](Json& result, const Json& current) {
			if (!result.is_array())
				result = Json::array();
			std::erase_if(result.get_ref<Json::array_t&>(), [&](const Json& entry) { return entry.value("feature", "") == feature; });
			for (const auto& entry : current)
				if (entry.value("feature", "") == feature)
					result.push_back(entry);
		};
		copyInterior(saved["interior"], personal.value("interior", Json::array()));
		copyInterior(destination["csxInteriorEdits"], origin.value("csxInteriorEdits", Json::array()));
	}
}
