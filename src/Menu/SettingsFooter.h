#pragma once

#include <functional>

namespace MenuUI
{
	/** Fixed footer height for the page's available width and current typography. */
	float SettingsFooterHeight(const char* page);
	/** Draws persistent settings status and guarded save/load/default actions. */
	void DrawSettingsFooter(const char* page, const std::function<void(float)>& defaults = {});
	/** Draws the feature-defaults action using the same responsive width as global actions. */
	bool DrawSettingsDefaultButton(float availableWidth);
}
