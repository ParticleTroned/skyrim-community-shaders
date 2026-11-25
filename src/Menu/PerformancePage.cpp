#include "PerformancePage.h"

#include "ExternalSettingsPage.h"
#include "Feature.h"
#include "FeatureListRenderer.h"
#include "Features/Upscaling.h"
#include "Globals.h"
#include "Menu.h"
#include "PerformanceProfiles.h"
#include "PerformanceTuningController.h"
#include "PerformanceTuningRenderer.h"
#include "Presets/LookPack.h"
#include "Presets/LookProfileState.h"
#include "Presets/LookProfiles.h"
#include "ProfileExchange.h"
#include "SettingsActions.h"
#include "SettingsPage.h"
#include "SettingsSearch.h"
#include "SettingsSerialization.h"
#include "State.h"
#include "Utils/FileSystem.h"
#include "Utils/RuntimeToggle.h"
#include "Utils/UI.h"
#include "WeatherManager.h"
#include "WeatherVariableRegistry.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <vector>

namespace MenuUI::PerformancePage
{
	namespace
	{
		ProfileExchangeState exchange;
		using json = nlohmann::json;
		PerformanceProfiles::UserProfile user;
		int selectedProfile = 1;
		ActionFeedback feedback;
		json includedFeatureChoices;
		bool includedFeaturesOpen = false;
		Feature* editedFeature = nullptr;
		json editBaseline;
		json editToggleHistory;

		Feature* FindFeature(std::string_view a_name)
		{
			for (auto* feature : Feature::GetFeatureList())
				if (feature->GetShortName() == a_name)
					return feature;
			return nullptr;
		}

		bool IsAvailable(Feature* a_feature)
		{
			return a_feature && a_feature->loaded && globals::state && !globals::state->IsFeatureDisabled(a_feature->GetShortName());
		}

		bool IsSelectable(Feature* a_feature)
		{
			return a_feature && !a_feature->GetPerformanceProfileReadOnlyReason() && !a_feature->IsHiddenFromUserView() && a_feature->IsInMenu() &&
			       (a_feature == &globals::features::upscaling || a_feature->SupportsPerformanceProfileToggle());
		}

		const char* SelectionAvailabilityBlockReason(Feature* a_feature)
		{
			if (!IsAvailable(a_feature))
				return "Feature is unavailable or disabled for the next startup.";
			if (const auto* reason = a_feature->GetPerformanceProfileReadOnlyReason())
				return reason;
			if (!IsSelectable(a_feature))
				return "Feature is unavailable for performance selections.";
			return nullptr;
		}

		const char* SelectionBlockReason(Feature* a_feature)
		{
			if (const auto* reason = SelectionAvailabilityBlockReason(a_feature))
				return reason;
			if (a_feature == &globals::features::upscaling) {
				if (const auto* reason = globals::features::upscaling.GetUpscalingSelectionBlockReason())
					return reason;
				return PerformanceTuningRenderer::GetFeatureSceneControlBlockReason(a_feature);
			}
			return PerformanceTuningRenderer::GetFeatureControlBlockReason(a_feature);
		}

		const char* MutationBlockReason()
		{
			if (editedFeature)
				return "Return to MyPerformance before changing profiles.";
			if (PerformanceTuningRenderer::HasActiveMeasurements())
				return "Finish the active measurement before changing profiles.";
			if (Util::IsRuntimeToggleBlocked(globals::state))
				return "Load a game and finish loading or saving before changing performance choices.";
			if (!globals::menu)
				return "The settings menu is unavailable.";
			return nullptr;
		}

		Upscaling::UpscaleMethod CurrentUpscalingMethod()
		{
			auto& upscaling = globals::features::upscaling;
			const auto pending = upscaling.GetPendingVRRenderScaleDesiredProfile();
			return pending.HasPendingSettings() ? pending.method : upscaling.GetUpscaleMethod();
		}

		json CaptureUpscaling()
		{
			const auto captured = globals::features::upscaling.CapturePerformanceCostMeasurementState();
			json result = json::object();
			for (const auto* key : { "upscaleMethod", "upscaleMethodNoDLSS", "qualityMode", "dlssPreset", "fsr4RuntimeEnable" })
				result[key] = captured.at(key);
			// A non-DLSS selection is portable; an inactive vendor preference must not replace it.
			const auto method = CurrentUpscalingMethod();
			result["upscaleMethod"] = static_cast<uint32_t>(method);
			if (method != Upscaling::UpscaleMethod::kDLSS)
				result["upscaleMethodNoDLSS"] = static_cast<uint32_t>(method);
			return result;
		}

		bool IsIncludedFeature(Feature* a_feature)
		{
			return IsAvailable(a_feature) && !a_feature->IsHiddenFromUserView() &&
			       (IsSelectable(a_feature) || a_feature->GetPerformanceProfileReadOnlyReason());
		}

		void AppendChoices(json& a_choices, Feature* a_feature)
		{
			const auto name = a_feature->GetShortName();
			if (a_feature == &globals::features::upscaling) {
				a_choices["upscaling"] = CaptureUpscaling();
				return;
			}
			a_choices["features"][name] = a_feature->IsPerformanceToggleEnabled();
			const auto preset = a_feature->GetPerformancePresetName();
			if (!preset.empty())
				a_choices["presets"][name] = preset;
			else if (a_choices.contains("presets"))
				a_choices["presets"].erase(name);
		}

		json Resolve(std::string_view a_profile)
		{
			if (a_profile != PerformanceProfiles::Names[3] && a_profile != "User defined")
				return PerformanceProfiles::BuiltIn(a_profile);
			auto choices = user.Resolve();
			// A feature's saved settings replace its named quality preset.
			if (choices.contains("presets"))
				for (auto* feature : Feature::GetFeatureList())
					if (user.settings.contains(feature->GetName()))
						choices["presets"].erase(feature->GetShortName());
			return choices;
		}

		const char* IncludedFeaturesBlockReason()
		{
			if (!globals::menu)
				return "The settings menu is unavailable.";
			return PerformanceTuningRenderer::HasActiveMeasurements() ? "Finish the active measurement before editing MyPerformance feature choices." : nullptr;
		}

		json MakeIncludedFeatureChoices(bool a_current)
		{
			auto selections = json::object();
			const auto resolved = user.Resolve();
			for (auto* feature : Feature::GetFeatureList()) {
				if (!IsAvailable(feature) || !IsSelectable(feature))
					continue;
				const auto name = feature->GetShortName();
				bool enabled = feature == &globals::features::upscaling ? CurrentUpscalingMethod() != Upscaling::UpscaleMethod::kNONE : feature->IsPerformanceToggleEnabled();
				if (!a_current) {
					if (feature == &globals::features::upscaling) {
						const auto& upscaling = resolved.at("upscaling");
						enabled = Upscaling::ResolvePerformanceCostMeasurementMethod(upscaling.at("upscaleMethod"), upscaling.at("upscaleMethodNoDLSS")) != Upscaling::UpscaleMethod::kNONE;
					} else if (resolved.at("features").contains(name)) {
						enabled = resolved["features"][name];
					}
				}
				selections[name] = enabled;
			}
			return selections;
		}

		json CaptureEditorSettings(Feature* a_feature)
		{
			auto snapshot = json::object();
			a_feature->SaveBaseSettings(snapshot["settings"]);
			snapshot["runtime"] = a_feature->CapturePerformanceCostMeasurementState();
			snapshot["menu"] = json::object();
			globals::menu->CaptureFeatureMenuSettings(a_feature->GetName(), snapshot["menu"]);
			return snapshot;
		}

		void RestoreEditorSettings(Feature* a_feature, const json& a_snapshot)
		{
			auto settings = a_snapshot.at("settings");
			a_feature->LoadSettings(settings);
			WeatherVariables::GlobalWeatherRegistry::GetSingleton()->CaptureFeatureUserSettings(a_feature->GetShortName());
			a_feature->RestorePerformanceToggleState(a_snapshot.at("runtime"));
			globals::menu->ApplyFeatureMenuSettings(a_feature->GetName(), &a_snapshot.at("menu"));
		}

		bool MatchesUpscaling(const json& a_choices)
		{
			if (a_choices.contains("upscaling") && IsAvailable(&globals::features::upscaling)) {
				const auto current = CaptureUpscaling();
				auto desired = current;
				desired.update(a_choices["upscaling"]);
				if (CurrentUpscalingMethod() != Upscaling::ResolvePerformanceCostMeasurementMethod(desired["upscaleMethod"], desired["upscaleMethodNoDLSS"]))
					return false;
				for (const auto& [key, value] : a_choices["upscaling"].items())
					if (key != "upscaleMethod" && key != "upscaleMethodNoDLSS" && current.at(key) != value)
						return false;
			}
			return true;
		}

		bool Matches(const json& a_choices)
		{
			if (!MatchesUpscaling(a_choices))
				return false;
			for (const auto* category : { "features", "presets" }) {
				if (!a_choices.contains(category))
					continue;
				for (const auto& [name, value] : a_choices[category].items()) {
					auto* feature = FindFeature(name);
					if (!IsAvailable(feature))
						continue;
					if (std::string_view(category) == "features" ? json(feature->IsPerformanceToggleEnabled()) != value : json(feature->GetPerformancePresetName()) != value)
						return false;
				}
			}
			return true;
		}

		bool CollectFeatureTargets(const json& a_choices, std::vector<Feature*>& a_targets, json& a_skipped, std::string& a_error)
		{
			a_targets.clear();
			a_skipped = json::array();
			for (const auto* category : { "features", "presets" }) {
				if (!a_choices.contains(category))
					continue;
				for (const auto& [name, value] : a_choices[category].items()) {
					auto* feature = FindFeature(name);
					if (!IsAvailable(feature)) {
						if (std::ranges::find(a_skipped, json(name)) == a_skipped.end())
							a_skipped.push_back(name);
						continue;
					}
					if (const auto* reason = SelectionAvailabilityBlockReason(feature)) {
						a_error = name + ": " + reason;
						return false;
					}
					if (std::string_view(category) == "presets") {
						const auto names = feature->GetPerformancePresetNames();
						if (std::ranges::find(names, value.get<std::string>()) == names.end()) {
							a_error = "Unsupported feature preset: " + name;
							return false;
						}
					}
					if (std::ranges::find(a_targets, feature) == a_targets.end())
						a_targets.push_back(feature);
				}
			}
			return true;
		}

		bool NeedsPresetChange(Feature* a_feature, const json& a_choices)
		{
			const auto name = a_feature->GetShortName();
			return a_choices.contains("presets") && a_choices["presets"].contains(name) &&
			       a_feature->GetPerformancePresetName() != a_choices["presets"][name].get<std::string>();
		}

		bool NeedsToggleChange(Feature* a_feature, const json& a_choices)
		{
			const auto name = a_feature->GetShortName();
			return a_choices.contains("features") && a_choices["features"].contains(name) &&
			       a_feature->IsPerformanceToggleEnabled() != a_choices["features"][name].get<bool>();
		}

		void Report(const json& a_result)
		{
			feedback.error = !a_result.value("accepted", false);
			feedback.message = a_result.value("message", a_result.value("error", std::string{}));
		}

		void DrawPresets()
		{
			std::array choices{
				Choice{ "performance", "Performance", "Prioritise frame time.", "Click to apply unified Performance switches, upscaling and feature-owned presets. Save CSX settings to keep them." },
				Choice{ "balanced", "Balanced", "Balance cost and quality.", "Click to apply unified Balanced switches, upscaling and feature-owned presets. Save CSX settings to keep them." },
				Choice{ "quality", "Quality", "Prioritise rendering quality.", "Click to apply unified Quality switches, upscaling and feature-owned presets. Save CSX settings to keep them." },
				Choice{ "user", "MyPerformance", "Your saved performance choices.", "Review a base tier plus selections saved from Tuning, then use Apply MyPerformance below to apply them." }
			};
			if (const auto* reason = MutationBlockReason()) {
				for (int index = 0; index < 3; ++index) {
					choices[index].enabled = false;
					choices[index].tooltip = reason;
				}
			}
			const char* profileNames[]{ "Performance", "Balanced", "Quality", "MyPerformance" };
			SettingsSearch::SelectNavigationChoice("Performance preset", selectedProfile, profileNames, 4);
			const int highlighted = selectedProfile == 3 || Matches(Resolve(PerformanceProfiles::Names[selectedProfile])) ? selectedProfile : -1;
			const int clicked = ChoiceCards("Performance preset", highlighted, choices);
			if (clicked == 3) {
				selectedProfile = clicked;
				feedback = {};
			} else if (clicked >= 0) {
				const auto result = Apply(PerformanceProfiles::Names[clicked]);
				Report(result);
				if (result.value("accepted", false))
					selectedProfile = clicked;
			}
			switch (selectedProfile) {
			case 3:
				{
					SectionHeading("Preset selections", "Choose a base and review your saved choices, then use Apply MyPerformance. Saved custom settings replace the base values; individual adjustments outside the preset are preserved.");
					const char* names[]{ "Performance", "Balanced", "Quality" };
					int base = static_cast<int>(std::ranges::find(PerformanceProfiles::Names, user.base) - PerformanceProfiles::Names.begin());
					{
						const auto disabled = Util::DisableGuard(MutationBlockReason() != nullptr);
						if (ChoiceSetting("Base preset", &base, names, 3))
							Report(SetBase(names[base]));
					}
					if (ImGui::Button("Included Features")) {
						includedFeatureChoices = MakeIncludedFeatureChoices(false);
						includedFeaturesOpen = true;
						feedback = {};
						ImGui::SetScrollY(0);
					}
					Util::AddTooltip("Choose which loaded features MyPerformance will turn on or off when applied. Editing these tiles does not change the running features.");
					{
						const auto disabled = Util::DisableGuard(MutationBlockReason() != nullptr);
						if (ImGui::Button("Apply current settings to my preset"))
							Report(CaptureCurrentSettings());
					}
					Util::AddTooltip("Copy ALL current CSX settings into MyPerformance, including global settings and every feature, then save CSX settings.");
					SectionHeading("Saved performance choices");
					for (const auto& [category, values] : user.choices.items()) {
						if (category == "upscaling") {
							const auto method = Upscaling::ResolvePerformanceCostMeasurementMethod(
								values.value("upscaleMethod", 3u), values.value("upscaleMethodNoDLSS", 2u));
							const char* methodName = method == Upscaling::UpscaleMethod::kDLSS ? "NVIDIA DLSS" :
							                         method == Upscaling::UpscaleMethod::kFSR  ? "AMD FSR" :
							                         method == Upscaling::UpscaleMethod::kTAA  ? "TAA" :
							                                                                     "None";
							ImGui::TextWrapped("Upscaling: %s", methodName);
							if (values.contains("qualityMode"))
								ImGui::TextWrapped("Quality: %s", Upscaling::GetQualityModeName(values["qualityMode"], method == Upscaling::UpscaleMethod::kDLSS));
							if (values.contains("dlssPreset"))
								ImGui::TextWrapped("DLSS profile: %s", Upscaling::GetDLSSPresetName(values["dlssPreset"]));
							if (values.contains("fsr4RuntimeEnable"))
								ImGui::TextWrapped("FSR 4: %s", values["fsr4RuntimeEnable"].get<bool>() ? "On when available" : "Off");
							continue;
						}
						for (const auto& [key, value] : values.items()) {
							auto* feature = FindFeature(key);
							if (category == "presets" && feature && user.settings.contains(feature->GetName()))
								continue;
							const auto label = feature ? feature->GetDisplayName() : key;
							if (category == "features")
								ImGui::TextWrapped("%s: %s", label.c_str(), value.get<bool>() ? "On" : "Off");
							else
								ImGui::TextWrapped("%s preset: %s", label.c_str(), value.get_ref<const std::string&>().c_str());
						}
					}
					if (user.choices.empty() && user.settings.empty()) {
						ImGui::TextDisabled("Base preset only");
						Util::AddTooltip("MyPerformance currently uses only its base preset. To add your own choices, open Performance Tuning and use Save settings to MyPerformance for individual features or all editable features.");
					}
					if (!user.settings.empty())
						ImGui::TextWrapped("Custom settings saved for %zu sections.", user.settings.size());
					if (!Matches(Resolve(PerformanceProfiles::Names[3]))) {
						PageNotice("MyPerformance performance choices differ from current settings.", NoticeKind::Pending,
							"Use Apply MyPerformance to apply its base, choices and custom settings, then Save settings to keep the result.");
					}
					{
						const auto disabled = Util::DisableGuard(MutationBlockReason() != nullptr);
						if (ImGui::Button("Apply MyPerformance"))
							Report(Apply(PerformanceProfiles::Names[3]));
					}
					Util::AddTooltip("Applies the chosen base plus your saved performance choices. Save CSX settings to keep the result.");
					DrawProfileExchange(exchange, "Share MyPerformance", Util::PathHelpers::GetCommunityShaderPath() / "Presets", false, MutationBlockReason(), [](const auto& path, const auto&) { Report(ImportProfile(path)); }, [](const auto& folder, const auto& name) { Report(ExportProfile(folder, name)); });
					break;
				}
			default:
				break;
			}
			if (const auto* reason = MutationBlockReason())
				PageNotice(reason, NoticeKind::Unavailable);
			feedback.Draw();
		}

		void DrawIncludedFeatures()
		{
			const auto toolbarStart = ImGui::GetCursorScreenPos();
			const float toolbarWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
			const auto& style = ImGui::GetStyle();
			const float restoreSize = ImGui::GetFrameHeight();
			const float applyWidth = ImGui::CalcTextSize("Apply to MyPerformance").x + style.FramePadding.x * 2;
			const float actionsWidth = applyWidth + style.ItemSpacing.x + restoreSize;
			if (ImGui::Button("Back to MyPerformance")) {
				includedFeaturesOpen = false;
				selectedProfile = 3;
				feedback = {};
				ImGui::SetScrollY(0);
				return;
			}
			Util::AddTooltip("Return to MyPerformance. Use Apply to MyPerformance first to keep edited selections.");
			const float actionsLeft = toolbarStart.x + std::max(0.0f, toolbarWidth - actionsWidth);
			if (actionsLeft >= ImGui::GetItemRectMax().x + style.ItemSpacing.x) {
				ImGui::SameLine();
				ImGui::SetCursorScreenPos({ actionsLeft, toolbarStart.y });
			} else {
				ImGui::SetCursorScreenPos({ actionsLeft, ImGui::GetCursorScreenPos().y });
			}
			const auto* blocked = IncludedFeaturesBlockReason();
			{
				const auto disabled = Util::DisableGuard(blocked != nullptr);
				if (ImGui::Button("Apply to MyPerformance"))
					Report(SaveIncludedFeatures());
				Util::AddTooltip(blocked ? blocked : "Store these On/Off choices in MyPerformance. Live features stay unchanged. Apply MyPerformance on the previous page to use them; Save settings persists the preset.");
				if (actionsWidth <= toolbarWidth)
					ImGui::SameLine();
				if (ImGui::Button("##RestoreIncludedFeatures", { restoreSize, restoreSize }))
					Report(RestoreIncludedFeatures());
				Util::DrawActionGlyph(ImGui::GetWindowDrawList(), Util::ActionGlyph::LoadSettings, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::GetColorU32(ImGuiCol_Text));
				Util::AddTooltip(blocked ? blocked : "Reset the draft to current Enabled settings. This does not change MyPerformance or any running feature.");
			}
			ImGui::SetCursorScreenPos({ toolbarStart.x, ImGui::GetCursorScreenPos().y });
			SectionHeading("Included Features", "Click once to switch On or Off. Double-click to open a feature's settings. Tile switches are saved to MyPerformance; the feature editor changes the running game.");
			feedback.Draw();
			if (includedFeatureChoices.is_null())
				includedFeatureChoices = MakeIncludedFeatureChoices(false);
			std::vector<Feature*> features;
			for (auto* feature : Feature::GetFeatureList())
				if (IsIncludedFeature(feature))
					features.push_back(feature);
			std::ranges::sort(features, {}, [](Feature* feature) { return feature->GetDisplayName(); });
			if (features.empty())
				ImGui::TextDisabled("No loaded features with Enabled controls.");
			for (size_t categoryIndex = 0; categoryIndex < FeatureCategories::kMenuOrder.size(); ++categoryIndex) {
				const auto category = FeatureCategories::kMenuOrder[categoryIndex];
				struct CardText
				{
					std::string id, name, description, help;
					bool enabled, selected;
				};
				std::vector<CardText> text;
				for (auto* feature : features) {
					if (feature->GetCategory() != category)
						continue;
					auto description = feature->GetFeatureSummary().first;
					const auto* readOnlyReason = feature->GetPerformanceProfileReadOnlyReason();
					const auto help = description + "\n\n" + (readOnlyReason ? readOnlyReason : "Click to stage this feature On or Off in MyPerformance. Double-click to edit its live settings.");
					description = description.substr(0, description.find_first_of("\r\n"));
					if (const auto sentence = description.find(". "); sentence != std::string::npos)
						description.resize(sentence + 1);
					text.push_back({ feature->GetShortName(), feature->GetDisplayName(), std::move(description), help,
						!blocked && !readOnlyReason, readOnlyReason ? feature->IsPerformanceToggleEnabled() : includedFeatureChoices.value(feature->GetShortName(), false) });
				}
				if (text.empty())
					continue;
				ImVec4 color{ 0, 0, 0, 1 };
				ImGui::ColorConvertHSVtoRGB(std::fmod(.58f + static_cast<float>(categoryIndex) * .618034f, 1.0f), .55f, .9f, color.x, color.y, color.z);
				SectionHeading(category.data());
				std::vector<Choice> cards;
				for (const auto& item : text)
					cards.push_back({ item.id.c_str(), item.name.c_str(), item.description.c_str(), item.help.c_str(), item.enabled, item.selected, color, true });
				int doubleClicked = -1;
				const int clicked = ChoiceCards(category.data(), -1, cards, &doubleClicked);
				if (doubleClicked >= 0) {
					// Undo the first click's switch before entering the editor.
					Report(SelectIncludedFeature(text[doubleClicked].id, !includedFeatureChoices.value(text[doubleClicked].id, false)));
					Report(BeginFeatureEdit(text[doubleClicked].id));
					return;
				} else if (clicked >= 0)
					Report(SelectIncludedFeature(text[clicked].id, !includedFeatureChoices.value(text[clicked].id, false)));
			}
		}

	}

	void Save(nlohmann::json& a_settings) { a_settings["Performance Profiles"] = user.Save(); }

	void Load(const nlohmann::json& a_settings)
	{
		editedFeature = nullptr;
		includedFeatureChoices = nullptr;
		if (!a_settings.contains("Performance Profiles"))
			user = {};
		else if (!user.Load(a_settings["Performance Profiles"])) {
			logger::warn("Performance Profiles: invalid saved selections; retaining the last valid user profile");
			return;
		}
		for (auto* feature : Feature::GetFeatureList()) {
			if (!feature->GetPerformanceProfileReadOnlyReason())
				continue;
			for (const auto* category : { "features", "presets" }) {
				const auto found = user.choices.find(category);
				if (found == user.choices.end())
					continue;
				found->erase(feature->GetShortName());
				if (found->empty())
					user.choices.erase(found);
			}
		}
	}

	nlohmann::json SetBase(std::string_view a_profile)
	{
		if (PerformanceProfiles::BuiltIn(a_profile).is_null())
			return { { "accepted", false }, { "error", "Unknown base profile." } };
		if (const auto* reason = MutationBlockReason())
			return { { "accepted", false }, { "error", reason } };
		user.base = a_profile;
		globals::menu->RequestSettingsDirtyCheck();
		return { { "accepted", true }, { "message", "MyPerformance base updated. Save CSX settings in the header to keep it." } };
	}

	nlohmann::json Capture(std::string_view a_name)
	{
		if (const auto* reason = MutationBlockReason())
			return { { "accepted", false }, { "error", reason } };
		auto* feature = FindFeature(a_name);
		if (const auto* reason = SelectionBlockReason(feature))
			return { { "accepted", false }, { "error", reason } };
		auto candidate = user.choices;
		AppendChoices(candidate, feature);
		if (!PerformanceProfiles::ValidateChoices(candidate))
			return { { "accepted", false }, { "error", "These selections cannot be stored in a performance profile." } };
		user.choices = std::move(candidate);
		user.settings.erase(feature->GetName());
		globals::menu->RequestSettingsDirtyCheck();
		return { { "accepted", true }, { "message", "Performance choices added to MyPerformance. Custom visual tuning is excluded. Save CSX settings in the header to keep MyPerformance." } };
	}

	nlohmann::json CaptureAll()
	{
		if (const auto* reason = MutationBlockReason())
			return { { "accepted", false }, { "error", reason } };
		auto candidate = user.choices;
		auto captured = json::array();
		auto skipped = json::object();
		for (auto* feature : Feature::GetFeatureList()) {
			if (!IsSelectable(feature))
				continue;
			const auto name = feature->GetShortName();
			if (const auto* reason = SelectionBlockReason(feature)) {
				skipped[name] = reason;
				continue;
			}
			AppendChoices(candidate, feature);
			captured.push_back(name);
		}
		if (captured.empty())
			return { { "accepted", false }, { "skippedFeatures", skipped }, { "error", "No editable performance choices are available to save." } };
		if (!PerformanceProfiles::ValidateChoices(candidate))
			return { { "accepted", false }, { "error", "These selections cannot be stored in a performance profile." } };
		user.choices = std::move(candidate);
		for (const auto& name : captured)
			user.settings.erase(FindFeature(name.get<std::string>())->GetName());
		globals::menu->RequestSettingsDirtyCheck();
		std::string message = "Saved settings for " + std::to_string(captured.size()) + " features to MyPerformance.";
		if (!skipped.empty())
			message += " Skipped " + std::to_string(skipped.size()) + " unavailable or controlled features; their saved choices were kept.";
		message += " Save CSX settings in the header to keep MyPerformance.";
		return { { "accepted", true }, { "capturedFeatures", captured }, { "skippedFeatures", skipped }, { "message", message } };
	}

	nlohmann::json CaptureCurrentSettings()
	{
		if (const auto* reason = MutationBlockReason())
			return { { "accepted", false }, { "error", reason } };
		const auto previous = user;
		try {
			json snapshot;
			std::string error;
			if (!globals::state->CaptureSettingsSnapshot(snapshot, error))
				return { { "accepted", false }, { "error", error } };
			if (!PerformanceProfiles::ValidateSnapshot(snapshot))
				return { { "accepted", false }, { "error", "The current settings cannot be stored in a preset." } };
			auto choices = json::object();
			for (auto* feature : Feature::GetFeatureList())
				if (IsAvailable(feature) && IsSelectable(feature))
					AppendChoices(choices, feature);
			user.choices = std::move(choices);
			user.settings = std::move(snapshot);
			if (!globals::state->Save()) {
				user = previous;
				return { { "accepted", false }, { "error", globals::menu->GetSettingsSaveMessage() } };
			}
			includedFeatureChoices = nullptr;
			return { { "accepted", true }, { "message", "All current CSX settings copied to MyPerformance and saved." } };
		} catch (const std::exception& error) {
			user = previous;
			return { { "accepted", false }, { "error", error.what() } };
		}
	}

	nlohmann::json Apply(std::string_view a_profile)
	{
		if (const auto* reason = MutationBlockReason())
			return { { "accepted", false }, { "error", reason } };
		const auto choices = Resolve(a_profile);
		if (choices.is_null() || !PerformanceProfiles::ValidateChoices(choices))
			return { { "accepted", false }, { "error", "Unknown or invalid performance profile." } };
		std::vector<Feature*> targets;
		const auto settings = a_profile == "MyPerformance" || a_profile == "User defined" ? user.settings : json::object();
		auto skipped = json::array();
		std::string error;
		if (!CollectFeatureTargets(choices, targets, skipped, error))
			return { { "accepted", false }, { "error", error } };
		auto& upscaling = globals::features::upscaling;
		bool applied = false;
		bool transactionRestored = true;
		json previousSettings;
		const auto previousToggleHistory = PerformanceTuningRenderer::CaptureFeatureToggleHistory();
		try {
			if (!settings.empty()) {
				if (!globals::state->CaptureSettingsSnapshot(previousSettings, error) || !globals::state->ApplySettingsSnapshot(settings, error)) {
					PerformanceTuningRenderer::RestoreFeatureToggleHistory(previousToggleHistory);
					return { { "accepted", false }, { "error", error } };
				}
				if (!CollectFeatureTargets(choices, targets, skipped, error))
					throw std::runtime_error(error);
			}
			const bool applyUpscaling = choices.contains("upscaling") && IsAvailable(&upscaling);
			if (choices.contains("upscaling") && !IsAvailable(&upscaling))
				skipped.push_back("Upscaling");
			auto history = PerformanceTuningRenderer::CaptureFeatureToggleHistory();
			for (auto* feature : targets)
				if (settings.contains(feature->GetName()) || (choices.contains("presets") && choices["presets"].contains(feature->GetShortName())))
					history.erase(feature->GetShortName());
			std::erase_if(targets, [&](Feature* feature) { return !NeedsPresetChange(feature, choices) && !NeedsToggleChange(feature, choices); });
			for (auto* feature : targets)
				if (const auto* reason = SelectionBlockReason(feature))
					throw std::runtime_error(feature->GetShortName() + ": " + reason);
			applied = PerformanceTuningRenderer::ApplyFeatureSettingsTransaction(targets, [&] {
				PerformanceTuningRenderer::RestoreFeatureToggleHistory(history);
				for (auto* feature : targets) {
					const auto name = feature->GetShortName();
					if ((NeedsPresetChange(feature, choices) && !feature->ApplyPerformancePreset(choices["presets"][name].get<std::string>())) ||
						(NeedsToggleChange(feature, choices) &&
							!PerformanceTuningRenderer::SetFeatureEnabled(feature, choices["features"][name]))) {
						error = "Feature rejected its selections: " + name;
						return false;
					}
				}
				if (applyUpscaling && !MatchesUpscaling(choices)) {
					if (const auto* reason = SelectionBlockReason(&upscaling)) {
						error = std::string("Upscaling: ") + reason;
						return false;
					}
					auto desiredUpscaling = CaptureUpscaling();
					desiredUpscaling.update(choices["upscaling"]);
					const auto method = Upscaling::ResolvePerformanceCostMeasurementMethod(desiredUpscaling["upscaleMethod"], desiredUpscaling["upscaleMethodNoDLSS"]);
					const auto transition = upscaling.ApplyCSMenuUpscalingTransition(method, upscaling.GetVRRenderScaleModePreference(),
						desiredUpscaling["qualityMode"], desiredUpscaling["dlssPreset"], "performance profile", Upscaling::VRUpscalingTransitionOrigin::CSMenu, 0, desiredUpscaling["fsr4RuntimeEnable"].get<bool>());
					if (transition.disposition == Upscaling::UpscalingTransitionApplyDisposition::Rejected) {
						error = "The upscaling transition was rejected.";
						return false;
					}
				}
				return true;
			});
		} catch (const PerformanceTuningController::SettingsRollbackError& exception) {
			transactionRestored = false;
			error = exception.what();
		} catch (const std::exception& exception) {
			error = std::string("Performance preset failed: ") + exception.what();
		} catch (...) {
			error = "Performance preset failed unexpectedly.";
		}
		if (!applied) {
			bool restored = transactionRestored;
			if (!previousSettings.is_null()) {
				std::string restoreError;
				try {
					restored = globals::state->ApplySettingsSnapshot(previousSettings, restoreError);
				} catch (const std::exception& failure) {
					restored = false;
					restoreError = failure.what();
				} catch (...) {
					restored = false;
					restoreError = "Unexpected restoration failure.";
				}
				if (!restored)
					error += "; rollback failed: " + restoreError;
			}
			PerformanceTuningRenderer::RestoreFeatureToggleHistory(previousToggleHistory);
			globals::menu->RequestSettingsDirtyCheck();
			return { { "accepted", false }, { "error", error + (restored ? " Previous feature settings and toggle history were restored." : "") } };
		}
		globals::menu->RequestSettingsDirtyCheck();
		return { { "accepted", true }, { "skippedUnavailableFeatures", skipped }, { "message", skipped.empty() ? "Performance profile accepted. Upscaling may finish asynchronously. Save CSX settings to keep it." : "Performance profile accepted for available features. Unloaded or startup-disabled features were skipped. Save CSX settings to keep it." } };
	}

	nlohmann::json Status()
	{
		auto profiles = json::array();
		for (const auto name : PerformanceProfiles::Names) {
			const auto choices = Resolve(name);
			profiles.push_back({ { "name", name }, { "choices", choices }, { "matches", Matches(choices) } });
		}
		return { { "userDefined", user.Save() }, { "profiles", profiles } };
	}

	nlohmann::json IncludedFeaturesStatus()
	{
		auto features = json::array();
		const auto selections = includedFeatureChoices.is_null() ? MakeIncludedFeatureChoices(false) : includedFeatureChoices;
		const auto current = MakeIncludedFeatureChoices(true);
		for (auto* feature : Feature::GetFeatureList()) {
			const auto name = feature->GetShortName();
			if (!IsIncludedFeature(feature))
				continue;
			const auto* readOnlyReason = feature->GetPerformanceProfileReadOnlyReason();
			const bool enabled = readOnlyReason ? feature->IsPerformanceToggleEnabled() : current.at(name).get<bool>();
			features.push_back({ { "feature", name }, { "name", feature->GetDisplayName() }, { "category", feature->GetCategory() },
				{ "currentEnabled", enabled }, { "selected", readOnlyReason ? enabled : selections.value(name, enabled) },
				{ "editable", !readOnlyReason && !IncludedFeaturesBlockReason() }, { "readOnlyReason", readOnlyReason ? json(readOnlyReason) : json(nullptr) } });
		}
		return { { "features", features }, { "open", includedFeaturesOpen }, { "editingFeature", editedFeature ? json(editedFeature->GetShortName()) : json(nullptr) } };
	}

	nlohmann::json SelectIncludedFeature(std::string_view a_name, bool a_enabled)
	{
		if (const auto* reason = IncludedFeaturesBlockReason())
			return { { "accepted", false }, { "error", reason } };
		auto* feature = FindFeature(a_name);
		if (feature && feature->GetPerformanceProfileReadOnlyReason())
			return { { "accepted", false }, { "error", feature->GetPerformanceProfileReadOnlyReason() } };
		if (!IsAvailable(feature) || !IsSelectable(feature))
			return { { "accepted", false }, { "error", "Feature is unloaded, startup-disabled or has no Enabled control." } };
		if (includedFeatureChoices.is_null())
			includedFeatureChoices = MakeIncludedFeatureChoices(false);
		includedFeatureChoices[std::string(a_name)] = a_enabled;
		return { { "accepted", true }, { "message", "" } };
	}

	nlohmann::json RestoreIncludedFeatures()
	{
		if (const auto* reason = IncludedFeaturesBlockReason())
			return { { "accepted", false }, { "error", reason } };
		includedFeatureChoices = MakeIncludedFeatureChoices(true);
		return { { "accepted", true }, { "message", "Selection restored from current settings." } };
	}

	nlohmann::json SaveIncludedFeatures()
	{
		if (const auto* reason = IncludedFeaturesBlockReason())
			return { { "accepted", false }, { "error", reason } };
		if (!includedFeatureChoices.is_object() || includedFeatureChoices.empty())
			return { { "accepted", false }, { "error", "No feature selections to save." } };
		auto candidate = user.choices;
		for (const auto& [name, enabled] : includedFeatureChoices.items()) {
			auto* feature = FindFeature(name);
			if (!IsAvailable(feature) || !IsSelectable(feature) || !enabled.is_boolean())
				return { { "accepted", false }, { "error", "Feature availability changed. Restore the selection from current settings." } };
			if (feature != &globals::features::upscaling) {
				candidate["features"][name] = enabled;
				continue;
			}
			auto upscaling = user.Resolve().at("upscaling");
			if (!enabled.get<bool>()) {
				upscaling["upscaleMethod"] = 0;
				upscaling["upscaleMethodNoDLSS"] = 0;
			} else if (Upscaling::ResolvePerformanceCostMeasurementMethod(upscaling.at("upscaleMethod"), upscaling.at("upscaleMethodNoDLSS")) == Upscaling::UpscaleMethod::kNONE) {
				upscaling = CurrentUpscalingMethod() != Upscaling::UpscaleMethod::kNONE ? CaptureUpscaling() : PerformanceProfiles::BuiltIn(user.base).at("upscaling");
			}
			candidate["upscaling"] = std::move(upscaling);
		}
		if (!PerformanceProfiles::ValidateChoices(candidate))
			return { { "accepted", false }, { "error", "These feature selections cannot be stored in MyPerformance." } };
		user.choices = std::move(candidate);
		globals::menu->RequestSettingsDirtyCheck();
		return { { "accepted", true }, { "message", "Feature choices stored in MyPerformance. Save settings to keep them." } };
	}

	Feature* EditedFeature() { return editedFeature; }

	nlohmann::json BeginFeatureEdit(std::string_view a_name)
	{
		if (const auto* reason = MutationBlockReason())
			return { { "accepted", false }, { "error", reason } };
		auto* feature = FindFeature(a_name);
		if (!IsAvailable(feature) || !IsSelectable(feature))
			return { { "accepted", false }, { "error", "This feature is unavailable for profile editing." } };
		if (const auto* reason = SelectionBlockReason(feature))
			return { { "accepted", false }, { "error", reason } };
		if (!globals::menu->PrepareSettingsMutation())
			return { { "accepted", false }, { "error", "Could not preserve saved settings." } };
		try {
			auto baseline = json::object();
			for (auto* item : Feature::GetFeatureList()) {
				if (!item->loaded)
					continue;
				baseline[item->GetShortName()] = CaptureEditorSettings(item);
			}
			editBaseline = std::move(baseline);
			editToggleHistory = PerformanceTuningRenderer::CaptureFeatureToggleHistory();
			editedFeature = feature;
			globals::menu->OpenMenu();
			includedFeaturesOpen = true;
			selectedProfile = 3;
			return { { "accepted", true }, { "message", "Editing live settings. Apply saves to MyPerformance and CSX; Restore returns to the opening settings." } };
		} catch (const std::exception& error) {
			return { { "accepted", false }, { "error", error.what() } };
		}
	}

	nlohmann::json EndFeatureEdit()
	{
		editedFeature = nullptr;
		editBaseline = editToggleHistory = nullptr;
		includedFeaturesOpen = true;
		selectedProfile = 3;
		globals::menu->SelectFeatureMenu(FeatureListRenderer::GetBuiltInPageMenuEntryId("PerformancePresets"));
		return { { "accepted", true }, { "message", "Returned to MyPerformance. Current game settings were kept." } };
	}

	nlohmann::json RestoreFeatureEdit()
	{
		if (!editedFeature)
			return { { "accepted", false }, { "error", "No profile feature editor is open." } };
		if (const auto* reason = GetFeatureSettingsActionBlockReason(editedFeature))
			return { { "accepted", false }, { "error", reason } };
		try {
			std::vector<Feature*> targets;
			auto previous = json::object();
			for (auto* feature : Feature::GetFeatureList()) {
				if (!feature->loaded || !editBaseline.contains(feature->GetShortName()))
					continue;
				auto current = CaptureEditorSettings(feature);
				if (current != editBaseline[feature->GetShortName()]) {
					targets.push_back(feature);
					previous[feature->GetShortName()] = std::move(current);
				}
			}
			for (auto* feature : targets)
				if (const auto* reason = PerformanceTuningRenderer::GetFeatureSceneControlBlockReason(feature))
					return { { "accepted", false }, { "error", reason } };
			const bool restored = PerformanceTuningRenderer::ApplyFeatureSettingsTransaction(targets, [&] {
				try {
					for (auto* feature : targets)
						RestoreEditorSettings(feature, editBaseline.at(feature->GetShortName()));
				} catch (const std::exception& error) {
					std::string message = error.what();
					for (auto* feature : targets) {
						try {
							RestoreEditorSettings(feature, previous.at(feature->GetShortName()));
						} catch (const std::exception& rollback) {
							message += "; restoring " + feature->GetName() + " also failed: " + rollback.what();
						}
					}
					throw std::runtime_error(message);
				}
				return true;
			});
			if (!restored)
				return { { "accepted", false }, { "error", "The opening settings could not be restored." } };
			PerformanceTuningRenderer::RestoreFeatureToggleHistory(editToggleHistory);
			WeatherManager::GetSingleton()->NotifyUserSettingsChanged();
			WeatherManager::GetSingleton()->RefreshFeatureOverrides();
			globals::menu->RequestSettingsDirtyCheck();
			return { { "accepted", true }, { "message", "Opening settings restored. Apply to save this restored state." } };
		} catch (const std::exception& error) {
			return { { "accepted", false }, { "error", error.what() } };
		}
	}

	nlohmann::json ApplyFeatureEdit()
	{
		if (!editedFeature)
			return { { "accepted", false }, { "error", "No profile feature editor is open." } };
		if (const auto* reason = GetFeatureSettingsActionBlockReason(editedFeature))
			return { { "accepted", false }, { "error", reason } };
		const auto previous = user;
		try {
			json settings;
			editedFeature->SaveBaseSettings(settings);
			if (!settings.is_object() || !PerformanceProfiles::ValidateSettings(settings))
				return { { "accepted", false }, { "error", "The feature settings cannot be stored in this profile." } };
			AppendChoices(user.choices, editedFeature);
			user.settings[editedFeature->GetName()] = std::move(settings);
			globals::menu->CaptureFeatureMenuSettings(editedFeature->GetName(), user.settings);
			json lookSettings = json::object();
			Presets::LookProfiles::Get().SaveSelection(lookSettings);
			Presets::CopyFeatureProfileState(user.settings, lookSettings, editedFeature->GetShortName());
			if (!globals::state->Save()) {
				user = previous;
				return { { "accepted", false }, { "error", globals::menu->GetSettingsSaveMessage() } };
			}
			if (includedFeatureChoices.is_object())
				includedFeatureChoices[editedFeature->GetShortName()] = editedFeature == &globals::features::upscaling ? CurrentUpscalingMethod() != Upscaling::UpscaleMethod::kNONE : editedFeature->IsPerformanceToggleEnabled();
			return { { "accepted", true }, { "message", "Saved to MyPerformance and CSX settings." } };
		} catch (const std::exception& error) {
			user = previous;
			return { { "accepted", false }, { "error", error.what() } };
		}
	}

	SettingsFooter FeatureEditFooter()
	{
		const auto* reason = GetFeatureSettingsActionBlockReason(editedFeature);
		return { globals::menu->HasUnsavedSettings(), feedback.error, "MyPerformance feature", feedback.message,
			{ SettingsAction{ "Back to MyPerformance", "Return to the tiles and keep current game settings.", true, [] { Report(EndFeatureEdit()); }, nullptr, "Back" },
				SettingsAction{ "Apply to profile", reason ? reason : "Save these feature settings to MyPerformance and normal CSX settings.", !reason, [] { Report(ApplyFeatureEdit()); }, nullptr, "Apply" },
				SettingsAction{ "Restore", reason ? reason : "Restore the game settings from before opening this feature.", !reason, [] { Report(RestoreFeatureEdit()); } } } };
	}

	void DrawSaveChoice(Feature* a_feature, float a_rightEdge)
	{
		if (a_feature && !IsSelectable(a_feature))
			return;
		const auto* reason = MutationBlockReason();
		if (!reason && a_feature)
			reason = SelectionBlockReason(a_feature);
		if (!reason && !a_feature && std::ranges::none_of(Feature::GetFeatureList(), [](Feature* feature) { return IsSelectable(feature) && !SelectionBlockReason(feature); }))
			reason = "No editable performance choices are available to save.";
		const auto start = ImGui::GetCursorScreenPos();
		const float available = std::max(1.0f, ImGui::GetContentRegionAvail().x);
		const float padding = ImGui::GetStyle().FramePadding.x * 2;
		const char* label = "Save settings to MyPerformance###SaveToMyPerformance";
		if (ImGui::CalcTextSize(label, nullptr, true).x + padding > available)
			label = "Save settings\nto MyPerformance###SaveToMyPerformance";
		const float width = std::min(available, ImGui::CalcTextSize(label, nullptr, true).x + padding);
		const float right = a_rightEdge > 0 ? std::min(a_rightEdge, start.x + available) : start.x + available;
		ImGui::SetCursorScreenPos({ std::max(start.x, right - width), start.y });
		{
			const auto disabled = Util::DisableGuard(reason != nullptr);
			if (ImGui::Button(label, { width, 0 }))
				Report(a_feature ? Capture(a_feature->GetShortName()) : CaptureAll());
		}
		Util::AddTooltip(reason ? reason : a_feature ? "Adds this feature's current On/Off state and named preset, or Upscaling choices, to MyPerformance. Other features and individual visual adjustments are excluded. Save CSX settings in the header to keep MyPerformance." :
													   "Adds current On/Off states, named feature presets and Upscaling choices for all editable features to MyPerformance, including features switched off. Unavailable or controlled features keep their previously saved choices. Individual visual adjustments are excluded. Save CSX settings in the header to keep MyPerformance.");
		ImGui::SetCursorScreenPos({ start.x, ImGui::GetCursorScreenPos().y });
		feedback.Draw();
	}

	void RenderPresets()
	{
		if (includedFeaturesOpen)
			DrawIncludedFeatures();
		else
			DrawPresets();
	}
	nlohmann::json ImportProfile(const std::filesystem::path& source)
	{
		if (const auto* reason = MutationBlockReason())
			return { { "accepted", false }, { "error", reason } };
		try {
			const auto document = Presets::ReadProfileDocument(source);
			PerformanceProfiles::UserProfile candidate;
			if (!PerformanceProfiles::ReadExport(document, candidate))
				throw std::runtime_error("Choose a valid MyPerformance profile.");
			std::vector<std::string> unavailable;
			for (const auto* category : { "features", "presets" }) {
				if (!candidate.choices.contains(category))
					continue;
				for (const auto& [id, value] : candidate.choices[category].items()) {
					auto* feature = FindFeature(id);
					if (feature && !IsSelectable(feature))
						throw std::runtime_error(id + ": this feature cannot be changed by a performance profile.");
					if (!IsAvailable(feature)) {
						unavailable.push_back(id);
						continue;
					}
					const auto names = feature->GetPerformancePresetNames();
					if (std::string_view(category) == "presets" && !candidate.settings.contains(feature->GetName()) && std::ranges::find(names, value.get<std::string>()) == names.end())
						throw std::runtime_error(id + ": unsupported quality preset.");
				}
			}
			if (!globals::menu->PrepareSettingsMutation())
				throw std::runtime_error("Could not preserve saved settings.");
			user = std::move(candidate);
			selectedProfile = 3;
			includedFeatureChoices = nullptr;
			globals::menu->RequestSettingsDirtyCheck();
			return { { "accepted", true }, { "unavailableFeatures", unavailable },
				{ "message", unavailable.empty() ? "Imported. Use Apply MyPerformance to use these choices." : "Imported with unavailable features. They will be skipped when applied." } };
		} catch (const std::exception& error) {
			return { { "accepted", false }, { "error", error.what() } };
		}
	}

	nlohmann::json ExportProfile(const std::filesystem::path& folder, const std::string& name)
	{
		try {
			if (!Presets::IsPackId(name))
				throw std::runtime_error("Choose a valid profile name.");
			if (!folder.is_absolute())
				throw std::runtime_error("Choose an absolute export folder.");
			std::filesystem::create_directories(folder);
			const auto path = folder / std::filesystem::path(std::u8string(name.begin(), name.end()) + u8".MyPerformance.json");
			if (std::filesystem::exists(path))
				throw std::runtime_error("That profile already exists. Choose another name.");
			std::string error;
			if (!Util::FileHelpers::WriteTextFileAtomic(path, SettingsSerialization::Serialize(PerformanceProfiles::MakeExport(user, Plugin::VERSION_LABEL)), error, false, false))
				throw std::runtime_error(error);
			return { { "accepted", true }, { "path", Util::PathToUtf8(path) }, { "message", "MyPerformance exported." } };
		} catch (const std::exception& error) {
			return { { "accepted", false }, { "error", error.what() } };
		}
	}

}
