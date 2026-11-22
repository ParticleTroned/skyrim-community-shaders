#pragma once

#include <functional>

class SettingsTabRenderer
{
public:
	// Settings state passed from Menu
	struct SettingsState
	{
		bool& settingToggleKey;
		bool& settingsEffectsToggle;
		bool& settingNeuralRenderingToggleKey;
		bool& settingOverlayToggleKey;
	};

	static void RenderGeneralSettings(SettingsState& state, const std::function<void()>& drawStartup);
	/** Draw shared shader configuration inside Dev Tools. */
	static void RenderShaderSettings();
	static void RenderShaderCacheActions();
	static void RenderShaderShortcuts();
	/** Draw developer-owned shortcuts at their tool's point of use. */
	static void RenderDeveloperShortcuts();
	static void RenderCaptureShortcuts();

private:
	static void DrawRenderDocShortcut();
	static void RenderShadersTab();
	static void RenderKeybindingsTab(
		SettingsState& state);

	// Interface detail panels
	static void RenderBehaviorTab();
	static void RenderThemesTab();
	static void RenderFontsTab();
	static void RenderStylingTab();
	static void RenderColorsTab();
};
