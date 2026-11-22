#pragma once

#include <filesystem>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <string_view>

struct Feature;

namespace MenuUI::PerformancePage
{
	/** Draw preset choices within the shared Performance navigation. */
	void RenderPresets();
	/** Top content action aligned to a tool tab; null saves all editable performance choices. */
	void DrawSaveChoice(Feature* a_feature = nullptr, float a_rightEdge = 0);
	/** Persist the editable profile without reapplying its base during settings load. */
	void Save(nlohmann::json& a_settings);
	void Load(const nlohmann::json& a_settings);
	/** Shared UI/DevBench operations; failures leave the saved user profile unchanged. */
	nlohmann::json Apply(std::string_view a_profile);
	nlohmann::json Capture(std::string_view a_feature);
	/** Snapshot all editable choices atomically; skipped features retain existing saved choices. */
	nlohmann::json CaptureAll();
	nlohmann::json SetBase(std::string_view a_profile);
	nlohmann::json Status();
	/** Imports staged choices only; Apply MyPerformance remains explicit. */
	nlohmann::json ImportProfile(const std::filesystem::path& source);
	/** Exports one portable MyPerformance JSON without saving or applying settings. */
	nlohmann::json ExportProfile(const std::filesystem::path& folder, const std::string& name);
	/** Inspect or stage Included Features; none of these actions change live feature settings. */
	nlohmann::json IncludedFeaturesStatus();
	nlohmann::json SelectIncludedFeature(std::string_view a_feature, bool a_enabled);
	nlohmann::json RestoreIncludedFeatures();
	nlohmann::json SaveIncludedFeatures();
}
