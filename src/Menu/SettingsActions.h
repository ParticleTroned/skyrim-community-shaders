#pragma once

#include <imgui.h>

namespace MenuUI
{
	enum class GlobalSettingsAction
	{
		None,
		Save,
		Load,
		RestoreDefaults,
		ClearShaderCache
	};

	/** Returns why a global action is unavailable, or nullptr when it may run. */
	const char* GetGlobalSettingsActionBlockReason(GlobalSettingsAction action);
	/** Runs a global action after rechecking its availability. */
	void InvokeGlobalSettingsAction(GlobalSettingsAction action);
	/** Draws a global action with consistent availability and scope help. */
	void DrawGlobalSettingsButton(GlobalSettingsAction action, const char* label, ImVec2 size);
	/** Describes which persistent settings an action affects. */
	const char* GetGlobalSettingsActionHelp(GlobalSettingsAction action);
}
