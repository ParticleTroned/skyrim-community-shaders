#pragma once

#include <imgui.h>

struct Feature;

namespace MenuUI
{
	struct ActionFeedback;
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
	/** Shared availability for feature switches and persistent edits in every UI entry point. */
	const char* GetFeatureMutationBlockReason(Feature* feature);
	/** Reuses native feature persistence and setters without changing other sections. */
	bool InvokeFeatureSettingsAction(GlobalSettingsAction action, Feature* feature);
	/** Returns the same feature availability used by its footer and automation. */
	const char* GetFeatureSettingsActionBlockReason(Feature* feature);
	/** Last scoped action result, independent of other features and global saves. */
	const ActionFeedback& GetFeatureSettingsFeedback(Feature* feature);
}
