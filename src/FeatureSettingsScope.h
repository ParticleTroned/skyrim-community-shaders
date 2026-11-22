#pragma once

#include "SettingsMigrations.h"

#include <string>
#include <string_view>

namespace FeatureSettingsScope
{
	inline constexpr std::string_view kScreenshot = "Screenshot";
	inline constexpr const char* kScreenshotKey = "ScreenshotKey";

	/** Locate the shortcut owned by Screenshot without moving its saved Menu key. */
	inline const nlohmann::json* ScreenshotShortcut(const nlohmann::json& settings)
	{
		const auto menu = settings.find("Menu");
		if (menu == settings.end() || !menu->is_object())
			return nullptr;
		const auto key = menu->find(kScreenshotKey);
		return key == menu->end() ? nullptr : &*key;
	}

	/** Replace one feature's persisted values, retaining all other owners' settings. */
	inline void Copy(nlohmann::json& target, const nlohmann::json& source, std::string_view name)
	{
		const std::string key(name);
		auto value = source.find(key);
		if (value == source.end() && name == SettingsMigrations::kEditorSettingsName)
			value = source.find(SettingsMigrations::kLegacyEditorSettingsName);
		if (value != source.end())
			target[key] = *value;
		else
			target.erase(key);
		if (name == SettingsMigrations::kEditorSettingsName)
			target.erase(std::string(SettingsMigrations::kLegacyEditorSettingsName));
		if (name == SettingsMigrations::kAdaptiveBalanceSettingsName)
			for (const auto alias : SettingsMigrations::kLegacyAppearanceSettingsNames)
				target.erase(std::string(alias));
		if (name == kScreenshot) {
			if (const auto* shortcut = ScreenshotShortcut(source)) {
				target["Menu"][kScreenshotKey] = *shortcut;
			} else if (auto menu = target.find("Menu"); menu != target.end() && menu->is_object()) {
				menu->erase(kScreenshotKey);
				if (menu->empty())
					target.erase(menu);
			}
		}
	}

	/** Compare a feature's values stored outside its own root section. */
	inline bool MenuSettingsEqual(const nlohmann::json& left, const nlohmann::json& right, std::string_view name)
	{
		if (name != kScreenshot)
			return true;
		const auto* a = ScreenshotShortcut(left);
		const auto* b = ScreenshotShortcut(right);
		return a && b ? *a == *b : a == b;
	}
}
