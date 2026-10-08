#pragma once

#include "Feature.h"
#include <array>
#include <functional>

namespace MenuUI
{
	struct SettingsAction
	{
		const char* label;
		const char* help;
		bool enabled;
		std::function<void()> invoke;
		const char* confirmation = nullptr;
		const char* compactLabel = nullptr;
	};

	struct SettingsFooter
	{
		bool dirty = false;
		bool error = false;
		std::string status;
		std::string detail;
		std::array<SettingsAction, 3> actions;
	};

	/** Menu-only companion settings; never registered with the renderer or CSX JSON persistence. */
	struct ExternalSettingsPage : Feature
	{
		virtual bool IsAvailable() const = 0;
		virtual bool HasUnsavedChanges() const = 0;
		virtual SettingsFooter GetSettingsFooter() = 0;
	};
}
