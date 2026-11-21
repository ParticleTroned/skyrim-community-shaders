#include "PerformancePage.h"

#include "Feature.h"
#include "Features/Upscaling.h"
#include "Globals.h"
#include "Menu.h"
#include "PerformanceProfiles.h"
#include "PerformanceTuningRenderer.h"
#include "SettingsActions.h"
#include "SettingsPage.h"
#include "SettingsSearch.h"
#include "State.h"
#include "Utils/RuntimeToggle.h"
#include "Utils/UI.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <vector>

namespace MenuUI::PerformancePage
{
	namespace
	{
		using json = nlohmann::json;
		PerformanceProfiles::UserProfile user;
		int selectedProfile = 1;
		ActionFeedback feedback;
		json includedFeatureChoices;
		bool includedFeaturesOpen = false;

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

		const char* SelectionBlockReason(Feature* a_feature)
		{
			if (!IsAvailable(a_feature))
				return "Feature is unavailable or disabled for the next startup.";
			if (const auto* reason = a_feature->GetPerformanceProfileReadOnlyReason())
				return reason;
			if (a_feature == &globals::features::upscaling) {
				if (const auto* reason = globals::features::upscaling.GetUpscalingSelectionBlockReason())
					return reason;
				return PerformanceTuningRenderer::GetFeatureSceneControlBlockReason(a_feature);
			}
			if (!a_feature->SupportsPerformanceProfileToggle() || a_feature->IsHiddenFromUserView() || !a_feature->IsInMenu())
				return "Feature is unavailable for performance selections.";
			return PerformanceTuningRenderer::GetFeatureControlBlockReason(a_feature);
		}

		const char* MutationBlockReason()
		{
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

		bool IsSelectable(Feature* a_feature)
		{
			return a_feature && !a_feature->GetPerformanceProfileReadOnlyReason() && !a_feature->IsHiddenFromUserView() && a_feature->IsInMenu() &&
			       (a_feature == &globals::features::upscaling || a_feature->SupportsPerformanceProfileToggle());
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
			return (a_profile == PerformanceProfiles::Names[3] || a_profile == "User defined") ? user.Resolve() : PerformanceProfiles::BuiltIn(a_profile);
		}

		const char* IncludedFeaturesBlockReason()
		{
			if (!globals::menu)
				return "The settings menu is unavailable.";
			return PerformanceTuningRenderer::HasActiveMeasurements() ? "Finish the active measurement before editing MyPreset feature choices." : nullptr;
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

		bool Matches(const json& a_choices)
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
				Choice{ "user", "MyPreset", "Your saved performance choices.", "Review a base tier plus selections saved from Tuning, then use Apply MyPreset below to apply them." }
			};
			if (const auto* reason = MutationBlockReason()) {
				for (int index = 0; index < 3; ++index) {
					choices[index].enabled = false;
					choices[index].tooltip = reason;
				}
			}
			const char* profileNames[]{ "Performance", "Balanced", "Quality", "MyPreset" };
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
					SectionHeading("Preset selections", "Choose a base and review your saved choices, then use Apply MyPreset. Applies upscaling choices, feature On/Off states and named feature presets. Other individual visual adjustments are preserved.");
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
					Util::AddTooltip("Choose which loaded features MyPreset will turn on or off when applied. Editing these tiles does not change the running features.");
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
							const auto label = feature ? feature->GetDisplayName() : key;
							if (category == "features")
								ImGui::TextWrapped("%s: %s", label.c_str(), value.get<bool>() ? "On" : "Off");
							else
								ImGui::TextWrapped("%s preset: %s", label.c_str(), value.get_ref<const std::string&>().c_str());
						}
					}
					if (user.choices.empty()) {
						ImGui::TextDisabled("Base preset only");
						Util::AddTooltip("MyPreset currently uses only its base preset. To add your own choices, open Performance Tuning and use Save settings to MyPreset for individual features or all editable features.");
					}
					if (!Matches(user.Resolve())) {
						PageNotice("MyPreset changes not applied.", NoticeKind::Pending,
							"Current settings differ from MyPreset. Use Apply MyPreset to apply its base and saved choices, then Save settings to keep the result.");
					}
					{
						const auto disabled = Util::DisableGuard(MutationBlockReason() != nullptr);
						if (ImGui::Button("Apply MyPreset"))
							Report(Apply(PerformanceProfiles::Names[3]));
					}
					Util::AddTooltip("Applies the chosen base plus your saved performance choices. Save CSX settings to keep the result.");
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
			const float applyWidth = ImGui::CalcTextSize("Apply to MyPreset").x + style.FramePadding.x * 2;
			const float actionsWidth = applyWidth + style.ItemSpacing.x + restoreSize;
			if (ImGui::Button("Back to MyPreset")) {
				includedFeaturesOpen = false;
				selectedProfile = 3;
				feedback = {};
				ImGui::SetScrollY(0);
				return;
			}
			Util::AddTooltip("Return to MyPreset. Use Apply to MyPreset first to keep edited selections.");
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
				if (ImGui::Button("Apply to MyPreset"))
					Report(SaveIncludedFeatures());
				Util::AddTooltip(blocked ? blocked : "Store these On/Off choices in MyPreset. Live features stay unchanged. Apply MyPreset on the previous page to use them; Save settings persists the preset.");
				if (actionsWidth <= toolbarWidth)
					ImGui::SameLine();
				if (ImGui::Button("##RestoreIncludedFeatures", { restoreSize, restoreSize }))
					Report(RestoreIncludedFeatures());
				Util::DrawActionGlyph(ImGui::GetWindowDrawList(), Util::ActionGlyph::LoadSettings, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::GetColorU32(ImGuiCol_Text));
				Util::AddTooltip(blocked ? blocked : "Reset the draft to current Enabled settings. This does not change MyPreset or any running feature.");
			}
			ImGui::SetCursorScreenPos({ toolbarStart.x, ImGui::GetCursorScreenPos().y });
			SectionHeading("Included Features", "Highlighted editable tiles will be On when MyPreset is applied; unhighlighted tiles will be Off. Restart-only features show their selected state and cannot be changed here. These choices do not change the running features.");
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
					const auto help = description + "\n\n" + (readOnlyReason ? readOnlyReason : "Click to stage this feature On or Off in MyPreset. The running feature is unchanged.");
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
					cards.push_back({ item.id.c_str(), item.name.c_str(), item.description.c_str(), item.help.c_str(), item.enabled, item.selected, color });
				const int clicked = ChoiceCards(category.data(), -1, cards);
				if (clicked >= 0)
					Report(SelectIncludedFeature(text[clicked].id, !includedFeatureChoices.value(text[clicked].id, false)));
			}
		}

	}

	void Save(nlohmann::json& a_settings) { a_settings["Performance Profiles"] = user.Save(); }

	void Load(const nlohmann::json& a_settings)
	{
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
		return { { "accepted", true }, { "message", "MyPreset base updated. Save CSX settings in the header to keep it." } };
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
		globals::menu->RequestSettingsDirtyCheck();
		return { { "accepted", true }, { "message", "Performance choices added to MyPreset. Custom visual tuning is excluded. Save CSX settings in the header to keep MyPreset." } };
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
		globals::menu->RequestSettingsDirtyCheck();
		std::string message = "Saved settings for " + std::to_string(captured.size()) + " features to MyPreset.";
		if (!skipped.empty())
			message += " Skipped " + std::to_string(skipped.size()) + " unavailable or controlled features; their saved choices were kept.";
		message += " Save CSX settings in the header to keep MyPreset.";
		return { { "accepted", true }, { "capturedFeatures", captured }, { "skippedFeatures", skipped }, { "message", message } };
	}

	nlohmann::json Apply(std::string_view a_profile)
	{
		if (const auto* reason = MutationBlockReason())
			return { { "accepted", false }, { "error", reason } };
		const auto choices = Resolve(a_profile);
		if (choices.is_null() || !PerformanceProfiles::ValidateChoices(choices))
			return { { "accepted", false }, { "error", "Unknown or invalid performance profile." } };
		std::vector<Feature*> targets;
		auto skipped = json::array();
		for (const auto* category : { "features", "presets" }) {
			if (!choices.contains(category))
				continue;
			for (const auto& [name, value] : choices[category].items()) {
				auto* feature = FindFeature(name);
				if (!IsAvailable(feature)) {
					if (std::ranges::find(skipped, json(name)) == skipped.end())
						skipped.push_back(name);
					continue;
				}
				if (const auto* reason = SelectionBlockReason(feature))
					return { { "accepted", false }, { "error", name + ": " + reason } };
				if (std::string_view(category) == "presets") {
					const auto names = feature->GetPerformancePresetNames();
					if (std::ranges::find(names, value.get<std::string>()) == names.end())
						return { { "accepted", false }, { "error", "Unsupported feature preset: " + name } };
				}
				if (std::ranges::find(targets, feature) == targets.end())
					targets.push_back(feature);
			}
		}
		auto& upscaling = globals::features::upscaling;
		const bool applyUpscaling = choices.contains("upscaling") && IsAvailable(&upscaling);
		if (applyUpscaling) {
			if (const auto* reason = SelectionBlockReason(&upscaling))
				return { { "accepted", false }, { "error", std::string("Upscaling: ") + reason } };
		} else if (choices.contains("upscaling")) {
			skipped.push_back("Upscaling");
		}
		std::string error;
		bool applied = false;
		try {
			applied = PerformanceTuningRenderer::ApplyFeatureSettingsTransaction(targets, [&] {
				for (auto* feature : targets) {
					const auto name = feature->GetShortName();
					if ((choices.contains("presets") && choices["presets"].contains(name) &&
							!feature->ApplyPerformancePreset(choices["presets"][name].get<std::string>())) ||
						(choices.contains("features") && choices["features"].contains(name) &&
							!PerformanceTuningRenderer::SetFeatureEnabled(feature, choices["features"][name]))) {
						error = "Feature rejected its selections: " + name;
						return false;
					}
				}
				if (applyUpscaling) {
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
		} catch (const std::exception& exception) {
			return { { "accepted", false }, { "error", std::string("Performance preset failed: ") + exception.what() } };
		} catch (...) {
			return { { "accepted", false }, { "error", "Performance preset failed unexpectedly." } };
		}
		if (!applied)
			return { { "accepted", false }, { "error", error + " Previous feature settings and toggle history were restored." } };
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
		return { { "features", features }, { "open", includedFeaturesOpen } };
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
			return { { "accepted", false }, { "error", "These feature selections cannot be stored in MyPreset." } };
		user.choices = std::move(candidate);
		globals::menu->RequestSettingsDirtyCheck();
		return { { "accepted", true }, { "message", "Feature choices stored in MyPreset. Save settings to keep them." } };
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
		const char* label = "Save settings to MyPreset###SaveToMyPreset";
		if (ImGui::CalcTextSize(label, nullptr, true).x + padding > available)
			label = "Save settings\nto MyPreset###SaveToMyPreset";
		const float width = std::min(available, ImGui::CalcTextSize(label, nullptr, true).x + padding);
		const float right = a_rightEdge > 0 ? std::min(a_rightEdge, start.x + available) : start.x + available;
		ImGui::SetCursorScreenPos({ std::max(start.x, right - width), start.y });
		{
			const auto disabled = Util::DisableGuard(reason != nullptr);
			if (ImGui::Button(label, { width, 0 }))
				Report(a_feature ? Capture(a_feature->GetShortName()) : CaptureAll());
		}
		Util::AddTooltip(reason ? reason : a_feature ? "Adds this feature's current On/Off state and named preset, or Upscaling choices, to MyPreset. Other features and individual visual adjustments are excluded. Save CSX settings in the header to keep MyPreset." :
													   "Adds current On/Off states, named feature presets and Upscaling choices for all editable features to MyPreset, including features switched off. Unavailable or controlled features keep their previously saved choices. Individual visual adjustments are excluded. Save CSX settings in the header to keep MyPreset.");
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
}
