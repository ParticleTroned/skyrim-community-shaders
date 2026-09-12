#include "SettingsSerialization.h"

#include "PresetCompatibility.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <map>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Feature.h"
#include "FeatureCategories.h"
#include "Utils/FileSystem.h"

namespace
{
	using SettingsGroup = std::vector<std::string>;

	struct FeatureSection
	{
		std::string settingsName;
		std::string displayName;
	};

	void AddPresentGroup(
		std::vector<SettingsGroup>& a_groups,
		const nlohmann::json& a_settings,
		std::initializer_list<std::string_view> a_keys)
	{
		SettingsGroup group;
		for (const auto key : a_keys) {
			if (a_settings.contains(key))
				group.emplace_back(key);
		}
		if (!group.empty())
			a_groups.push_back(std::move(group));
	}

	std::vector<SettingsGroup> BuildSettingsGroups(const nlohmann::json& a_settings)
	{
		std::vector<SettingsGroup> groups;
		std::unordered_set<std::string> knownKeys;

		auto addCoreGroup = [&](std::initializer_list<std::string_view> a_keys) {
			for (const auto key : a_keys)
				knownKeys.emplace(key);
			AddPresentGroup(groups, a_settings, a_keys);
		};

		addCoreGroup({ "Menu" });
		addCoreGroup({ "General" });
		addCoreGroup({ "Advanced", "Disable at Boot", "RenderDoc", "Replace Original Shaders" });
		addCoreGroup({ "Version" });
		addCoreGroup({ PresetCompatibility::kSettingsKey });

		std::map<std::string, std::vector<FeatureSection>> featuresByCategory;
		for (auto* feature : Feature::GetFeatureList()) {
			const auto settingsName = feature->GetName();
			const bool isNewKey = knownKeys.insert(settingsName).second;
			if (!isNewKey || !a_settings.contains(settingsName))
				continue;

			featuresByCategory[std::string(feature->GetCategory())].push_back(
				{ settingsName, feature->GetDisplayName() });
		}

		// Preserve fields supplied by newer versions or external integrations.
		// They cannot be mapped to this build's UI, so keep them in their own group.
		SettingsGroup unknownSettings;
		for (const auto& [key, value] : a_settings.items()) {
			(void)value;
			if (!knownKeys.contains(key))
				unknownSettings.push_back(key);
		}
		if (!unknownSettings.empty())
			groups.push_back(std::move(unknownSettings));

		auto appendCategory = [&](std::string_view a_category) {
			auto categoryIt = featuresByCategory.find(std::string(a_category));
			if (categoryIt == featuresByCategory.end())
				return;

			auto& features = categoryIt->second;
			std::ranges::sort(features, [](const FeatureSection& a_left, const FeatureSection& a_right) {
				return std::tie(a_left.displayName, a_left.settingsName) <
				       std::tie(a_right.displayName, a_right.settingsName);
			});

			SettingsGroup group;
			group.reserve(features.size());
			for (const auto& feature : features)
				group.push_back(feature.settingsName);
			if (!group.empty())
				groups.push_back(std::move(group));
		};

		for (const auto category : FeatureCategories::kMenuOrder)
			appendCategory(category);

		// Match the UI's fallback behavior for categories added by external features.
		for (const auto& [category, features] : featuresByCategory) {
			(void)features;
			if (std::ranges::find(FeatureCategories::kMenuOrder, std::string_view{ category }) ==
				FeatureCategories::kMenuOrder.end()) {
				appendCategory(category);
			}
		}

		return groups;
	}

	std::string SerializeMember(const std::string& a_key, const nlohmann::json& a_value)
	{
		std::string serialized;
		const char* promotedMenuKey = nullptr;
		if (a_key == "Menu" && a_value.is_object()) {
			if (a_value.contains("UI Mode")) {
				promotedMenuKey = "UI Mode";
			} else if (a_value.contains("PerformanceUiMode")) {
				promotedMenuKey = "PerformanceUiMode";
			}
		}

		if (promotedMenuKey) {
			nlohmann::ordered_json orderedMenu = nlohmann::ordered_json::object();
			orderedMenu[promotedMenuKey] = a_value.at(promotedMenuKey);
			for (const auto& [key, value] : a_value.items()) {
				if (key != promotedMenuKey)
					orderedMenu[key] = value;
			}

			nlohmann::ordered_json wrapper = nlohmann::ordered_json::object();
			wrapper[a_key] = std::move(orderedMenu);
			serialized = wrapper.dump(1);
		} else {
			nlohmann::json wrapper = nlohmann::json::object();
			wrapper[a_key] = a_value;
			serialized = wrapper.dump(1);
		}

		const auto firstNewline = serialized.find('\n');
		const auto lastNewline = serialized.rfind('\n');
		if (firstNewline == std::string::npos || lastNewline <= firstNewline)
			throw std::runtime_error("Could not format settings JSON member");
		return serialized.substr(firstNewline + 1, lastNewline - firstNewline - 1);
	}

	nlohmann::json ParseWithUniqueObjectKeys(const std::string& a_contents)
	{
		// Loading retains nlohmann's legacy last-value behavior for duplicate keys,
		// but automatic reformatting must not collapse ambiguous source text.
		std::vector<std::unordered_set<std::string>> objectKeys;
		bool duplicateKeyFound = false;
		const nlohmann::json::parser_callback_t callback =
			[&](int, nlohmann::json::parse_event_t a_event, nlohmann::json& a_parsed) {
				switch (a_event) {
				case nlohmann::json::parse_event_t::object_start:
					objectKeys.emplace_back();
					break;
				case nlohmann::json::parse_event_t::key:
					if (!objectKeys.empty() && !objectKeys.back().insert(a_parsed.get<std::string>()).second)
						duplicateKeyFound = true;
					break;
				case nlohmann::json::parse_event_t::object_end:
					if (!objectKeys.empty())
						objectKeys.pop_back();
					break;
				default:
					break;
				}
				return true;
			};

		auto parsed = nlohmann::json::parse(a_contents, callback);
		if (duplicateKeyFound)
			throw std::runtime_error("Settings JSON contains duplicate object keys");
		return parsed;
	}
}

std::string SettingsSerialization::Serialize(const nlohmann::json& a_settings)
{
	if (!a_settings.is_object())
		throw std::invalid_argument("Settings JSON must contain an object");

	const auto groups = BuildSettingsGroups(a_settings);
	size_t memberCount = 0;
	for (const auto& group : groups)
		memberCount += group.size();
	if (memberCount != a_settings.size())
		throw std::runtime_error("Canonical settings groups do not cover every JSON member exactly once");

	std::ostringstream output;
	output << "{\n";

	size_t emittedMembers = 0;
	for (size_t groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
		if (groupIndex > 0)
			output << '\n';

		for (const auto& key : groups[groupIndex]) {
			output << SerializeMember(key, a_settings.at(key));
			++emittedMembers;
			if (emittedMembers < memberCount)
				output << ',';
			output << '\n';
		}
	}

	output << "}\n";
	auto contents = output.str();
	const auto roundTripSettings = nlohmann::json::parse(contents);
	if (roundTripSettings != a_settings)
		throw std::runtime_error("Canonical settings JSON failed semantic round-trip validation");
	return contents;
}

bool SettingsSerialization::WriteFileAtomic(
	const std::filesystem::path& a_path,
	const nlohmann::json& a_settings,
	std::string& o_errorMessage)
{
	try {
		const auto contents = Serialize(a_settings);
		return Util::FileHelpers::WriteTextFileAtomic(a_path, contents, o_errorMessage);
	} catch (const std::exception& e) {
		o_errorMessage = std::format("Could not write settings JSON: {}", e.what());
		return false;
	}
}

SettingsSerialization::CanonicalizationResult SettingsSerialization::CanonicalizeFile(
	const std::filesystem::path& a_path,
	const nlohmann::json& a_settings,
	std::string& o_errorMessage)
{
	o_errorMessage.clear();

	try {
		const auto canonicalContents = Serialize(a_settings);
		std::ifstream input(a_path, std::ios::binary);
		if (!input.is_open()) {
			o_errorMessage = "Could not reopen the settings file for canonicalization";
			return CanonicalizationResult::Error;
		}

		const std::string existingContents{
			std::istreambuf_iterator<char>(input),
			std::istreambuf_iterator<char>()
		};
		if (input.bad()) {
			o_errorMessage = "An I/O error occurred while checking settings order";
			return CanonicalizationResult::Error;
		}
		if (existingContents == canonicalContents)
			return CanonicalizationResult::Unchanged;

		const auto currentSettings = ParseWithUniqueObjectKeys(existingContents);
		if (currentSettings != a_settings) {
			o_errorMessage = "Settings changed while their order was being checked; preserving the newer file";
			return CanonicalizationResult::Error;
		}

		if (!Util::FileHelpers::WriteTextFileAtomic(a_path, canonicalContents, o_errorMessage))
			return CanonicalizationResult::Error;

		return CanonicalizationResult::Rewritten;
	} catch (const std::exception& e) {
		o_errorMessage = std::format("Could not canonicalize settings JSON: {}", e.what());
		return CanonicalizationResult::Error;
	}
}
