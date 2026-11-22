#include "SettingsActions.h"

#include "Feature.h"
#include "Features/LightLimitFix/ParticleLights.h"
#include "Globals.h"
#include "Menu.h"
#include "PerformanceTuningRenderer.h"
#include "SettingsPage.h"
#include "ShaderCache.h"
#include "State.h"
#include "Utils/UI.h"
#include "WeatherManager.h"
#include "WeatherVariableRegistry.h"

#include <stdexcept>
#include <unordered_map>

namespace MenuUI
{
	namespace
	{
		std::unordered_map<Feature*, ActionFeedback> featureFeedback;
	}

	const ActionFeedback& GetFeatureSettingsFeedback(Feature* feature) { return featureFeedback[feature]; }

	const char* GetFeatureMutationBlockReason(Feature* feature)
	{
		if (const auto* reason = GetGlobalSettingsActionBlockReason(GlobalSettingsAction::Save))
			return reason;
		if (!feature || !feature->loaded || globals::state->IsFeatureDisabled(feature->GetShortName()))
			return "This feature's settings are unavailable in this session.";
		return PerformanceTuningRenderer::GetFeatureSceneControlBlockReason(feature);
	}

	const char* GetFeatureSettingsActionBlockReason(Feature* feature)
	{
		if (const auto* reason = GetFeatureMutationBlockReason(feature))
			return reason;
		return feature->HasFeatureSettings() ? nullptr : "This feature has no saved settings.";
	}

	bool InvokeFeatureSettingsAction(GlobalSettingsAction action, Feature* feature)
	{
		auto& feedback = featureFeedback[feature];
		feedback.error = true;
		if (const auto* reason = GetFeatureSettingsActionBlockReason(feature)) {
			feedback.message = reason;
			return false;
		}
		try {
			if (!globals::menu->PrepareSettingsMutation())
				throw std::runtime_error("Could not preserve the saved settings baseline.");
			if (action == GlobalSettingsAction::Save) {
				const bool saved = globals::state->Save(State::ConfigMode::USER, feature);
				feedback.message = saved ? "This feature's settings were saved." : globals::menu->GetSettingsSaveMessage();
				feedback.error = !saved;
				return saved;
			}
			json saved;
			if (action == GlobalSettingsAction::Load && !globals::menu->CopySavedFeatureSettings(feature->GetName(), saved))
				throw std::runtime_error("No saved settings are available for this feature.");
			if (action != GlobalSettingsAction::Load && action != GlobalSettingsAction::RestoreDefaults)
				throw std::runtime_error("Unknown feature settings action.");
			json previous;
			feature->SaveSettings(previous);
			json previousMenu = json::object();
			globals::menu->CaptureFeatureMenuSettings(feature->GetName(), previousMenu);
			const bool applied = PerformanceTuningRenderer::ApplyFeatureSettingsTransaction({ feature }, [&] {
				try {
					if (action == GlobalSettingsAction::Load)
						feature->LoadSettings(saved.at(feature->GetName()));
					else
						feature->RestoreDefaultSettings();
					globals::menu->ApplyFeatureMenuSettings(feature->GetName(), action == GlobalSettingsAction::Load ? &saved : nullptr);
				} catch (...) {
					try {
						globals::menu->ApplyFeatureMenuSettings(feature->GetName(), &previousMenu);
						feature->LoadSettings(previous);
					} catch (...) {
						throw std::runtime_error("The feature action failed and its previous settings could not be recovered.");
					}
					throw;
				}
				return true; }, true);
			if (!applied)
				throw std::runtime_error("The feature rejected the settings change.");
			auto* registry = WeatherVariables::GlobalWeatherRegistry::GetSingleton();
			if (registry->HasWeatherSupport(feature->GetShortName())) {
				registry->CaptureFeatureUserSettings(feature->GetShortName());
				WeatherManager::GetSingleton()->NotifyUserSettingsChanged();
				WeatherManager::GetSingleton()->RefreshFeatureOverrides();
			}
			globals::menu->RequestSettingsDirtyCheck();
			feedback.message = action == GlobalSettingsAction::Load ? "This feature's last saved settings were restored." : "Defaults restored for this feature. Save to keep them.";
			feedback.error = false;
			return true;
		} catch (const std::exception& error) {
			feedback.message = error.what();
		} catch (...) {
			feedback.message = "The feature action failed. Check CSX.log.";
		}
		logger::warn("Feature settings action failed for {}: {}", feature->GetShortName(), feedback.message);
		globals::menu->RequestSettingsDirtyCheck();
		return false;
	}

	const char* GetGlobalSettingsActionBlockReason(GlobalSettingsAction action)
	{
		if (action == GlobalSettingsAction::None)
			return nullptr;
		if (PerformanceTuningRenderer::HasActiveMeasurements())
			return "Finish or cancel the performance comparison before changing settings or clearing shaders.";
		if (globals::state->IsPersistentMutationBlocked())
			return "Wait for the current game save or load to finish before changing settings.";
		if (action == GlobalSettingsAction::ClearShaderCache) {
			if (globals::shaderCache->IsAwaitingMenuCloseCapture())
				return "Close the menu to finish the active-shader capture.";
			if (globals::shaderCache->IsCapturingActiveShaders())
				return "Wait for the active-shader capture to finish.";
		}
		return nullptr;
	}

	const char* GetGlobalSettingsActionHelp(GlobalSettingsAction action)
	{
		switch (action) {
		case GlobalSettingsAction::Save:
			return "Save global CSX settings, menu zoom and theme selection. Theme appearance, Stabilizer profiles and editor files have their own Save actions.";
		case GlobalSettingsAction::Load:
			return "Replace current CSX settings with saved settings, including the saved theme selection.";
		case GlobalSettingsAction::RestoreDefaults:
			return "Restore installed CSX defaults for the menu, shaders and loaded features. Save CSX settings to keep these changes.";
		case GlobalSettingsAction::ClearShaderCache:
			return Util::GetClearShaderCacheTooltip();
		default:
			return "";
		}
	}

	void InvokeGlobalSettingsAction(GlobalSettingsAction action)
	{
		if (const auto* reason = GetGlobalSettingsActionBlockReason(action)) {
			globals::menu->ReportSettingsSaveResult(false, reason);
			return;
		}
		switch (action) {
		case GlobalSettingsAction::Save:
			if (globals::state->Save())
				featureFeedback.clear();
			break;
		case GlobalSettingsAction::Load:
			globals::state->Load();
			featureFeedback.clear();
			globals::features::llf::particleLights.GetConfigs();
			break;
		case GlobalSettingsAction::RestoreDefaults:
			{
				std::string error;
				if (!globals::state->RestoreDefaultSettings(error))
					globals::menu->ReportSettingsSaveResult(false, std::move(error));
				else
					featureFeedback.clear();
				break;
			}
		case GlobalSettingsAction::ClearShaderCache:
			Util::RequestClearShaderCacheConfirmation(Util::ResolveShaderCacheClearScope());
			break;
		case GlobalSettingsAction::None:
			break;
		}
	}

	void DrawGlobalSettingsButton(GlobalSettingsAction action, const char* label, ImVec2 size)
	{
		const bool dirty = action == GlobalSettingsAction::Save && globals::menu->HasUnsavedSettings();
		ImGui::PushStyleColor(ImGuiCol_Text, dirty ? globals::menu->GetTheme().StatusPalette.Warning : ImGui::GetStyleColorVec4(ImGuiCol_Text));
		const SKSE::stl::scope_exit restoreText([] { ImGui::PopStyleColor(); });
		const auto* reason = GetGlobalSettingsActionBlockReason(action);
		{
			const auto disabled = Util::DisableGuard(reason != nullptr);
			if (Util::ButtonWithFlash(label, size))
				InvokeGlobalSettingsAction(action);
		}
		Util::AddTooltip(reason ? reason : GetGlobalSettingsActionHelp(action), ImGuiHoveredFlags_AllowWhenDisabled);
	}
}
