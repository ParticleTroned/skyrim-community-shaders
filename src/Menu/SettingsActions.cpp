#include "SettingsActions.h"

#include "Features/LightLimitFix/ParticleLights.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "Menu.h"
#include "PerformanceTuningRenderer.h"
#include "ShaderCache.h"
#include "State.h"
#include "Utils/UI.h"

namespace MenuUI
{
	const char* GetGlobalSettingsActionBlockReason(GlobalSettingsAction action)
	{
		if (action == GlobalSettingsAction::None)
			return nullptr;
		if (!globals::state || !globals::menu || !globals::shaderCache)
			return T("menu.actions.not_ready", "Settings are not ready yet.");
		if (PerformanceTuningRenderer::HasActiveMeasurements())
			return T("menu.actions.comparison_active", "Finish or cancel the performance comparison before changing settings or clearing shaders.");
		if (globals::state->IsPersistentMutationBlocked())
			return T("menu.actions.save_load_active", "Wait for the current game save or load to finish before changing settings.");
		if (action == GlobalSettingsAction::ClearShaderCache) {
			if (globals::shaderCache->IsAwaitingMenuCloseCapture())
				return T("menu.actions.close_menu_capture", "Close the menu to finish the active-shader capture.");
			if (globals::shaderCache->IsCapturingActiveShaders())
				return T("menu.actions.capture_active", "Wait for the active-shader capture to finish.");
		}
		return nullptr;
	}

	const char* GetGlobalSettingsActionHelp(GlobalSettingsAction action)
	{
		switch (action) {
		case GlobalSettingsAction::Save:
			return T("menu.actions.save_help", "Save global CSX settings and theme selection. Theme appearance and editor files have their own Save actions.");
		case GlobalSettingsAction::Load:
			return T("menu.actions.load_help", "Replace current CSX settings with saved settings, including the saved theme selection.");
		case GlobalSettingsAction::RestoreDefaults:
			return T("menu.actions.defaults_help", "Restore installed CSX defaults for the menu, shaders and loaded features. Save CSX settings to keep these changes.");
		case GlobalSettingsAction::ClearShaderCache:
			return Util::GetClearShaderCacheTooltip();
		default:
			return "";
		}
	}

	void InvokeGlobalSettingsAction(GlobalSettingsAction action)
	{
		if (const auto* reason = GetGlobalSettingsActionBlockReason(action)) {
			if (globals::menu)
				globals::menu->ReportSettingsSaveResult(false, reason);
			return;
		}
		switch (action) {
		case GlobalSettingsAction::Save:
			globals::state->Save();
			break;
		case GlobalSettingsAction::Load:
			globals::state->Load();
			globals::features::llf::particleLights.GetConfigs();
			break;
		case GlobalSettingsAction::RestoreDefaults:
			{
				std::string error;
				if (!globals::state->RestoreDefaultSettings(error))
					globals::menu->ReportSettingsSaveResult(false, std::move(error));
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
		const auto* reason = GetGlobalSettingsActionBlockReason(action);
		{
			const auto disabled = Util::DisableGuard(reason != nullptr);
			if (Util::ButtonWithFlash(label, size))
				InvokeGlobalSettingsAction(action);
		}
		Util::AddTooltip(reason ? reason : GetGlobalSettingsActionHelp(action), ImGuiHoveredFlags_AllowWhenDisabled);
	}
}
