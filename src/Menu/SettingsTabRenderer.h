#pragma once

#include "Utils/Input.h"
#include <functional>
#include <vector>

// Forward declarations
class Menu;

class SettingsTabRenderer
{
public:
	// Settings state passed from Menu
	struct SettingsState
	{
		bool& settingToggleKey;
		bool& settingsEffectsToggle;
		bool& settingNeuralRenderingToggleKey;
		bool& settingSkipCompilationKey;
		bool& settingOverlayToggleKey;
		bool& settingShaderBlockPrevKey;      // Debug: shader block previous key
		bool& settingShaderBlockNextKey;      // Debug: shader block next key
		bool& settingCSEditorToggleKey;  // CS Editor toggle key
		bool& settingScreenshotKey;           // Screenshot capture key
	};

	/** Draw shared General sections while retaining native SE font and keyboard controls. */
	static void RenderGeneralSettings(
		SettingsState& state);

private:
	static void RenderShadersTab();
	static void RenderKeybindingsTab(
		SettingsState& state);

	static void RenderThemesTab();

	// Shared page sections
	static void RenderBehaviorTab();
	static void RenderFontsTab();
	static void RenderStylingTab();
	static void RenderColorsTab();
};
