#pragma once

#include "StabilizerConfig.h"
#include <array>
#include <charconv>
#include <cmath>
#include <format>

namespace VRFpsStabilizer
{
	inline constexpr std::array kLocationSections{ "Best", "Good", "Default", "Bad", "Worst", "Interior" };
	inline constexpr std::array kEventSections{ "DataLoaded", "PostLoadGame", "AfterLoadGame", "NewGame", "Conditional" };
	enum class SettingKind
	{
		Boolean,
		Integer,
		Number,
		WorldList
	};
	struct Setting
	{
		const char* key;
		const char* label;
		const char* group;
		SettingKind kind;
		const char* suggested;
		double minimum;
		double maximum;
		const char* help;
	};
	inline constexpr std::array kSettings{
		Setting{ "CSVRFadeToBlackDuration", "Render Scale transition fade (seconds)", "Profiles", SettingKind::Number, "0", 0.0, 1000000.0, "Stabilizer's separate timed fade. CSX owns transition coverage, so 0 seconds is recommended. Edit this on the Profiles page." },
		Setting{ "EnableLog", "Logging", "Performance", SettingKind::Boolean, "0", 0.0, 1.0, "Write Stabilizer's configuration changes to its log. Useful while diagnosing settings; leave off for normal play." },
		Setting{ "AutoConfigEnabled", "Automatic quality adjustment", "Performance", SettingKind::Boolean, "1", 0.0, 1.0, "Allow Stabilizer to change quality levels using frame time. Turning this off keeps its console and conditional command features available." },
		Setting{ "CheckSleepDuration", "Check interval (ms)", "Performance", SettingKind::Integer, "12", 12.0, 2147483647.0, "Delay between frame-time checks. Stabilizer requires at least 12 ms. Longer intervals react more slowly." },
		Setting{ "MinTargetFrameTime", "Lower frame-time target (ms)", "Performance", SettingKind::Number, "8.5", 0.001, 1000000.0, "Below this target, Stabilizer may increase quality after the waiting period. Keep below the upper target and allow headroom for your headset." },
		Setting{ "MaxTargetFrameTime", "Upper frame-time target (ms)", "Performance", SettingKind::Number, "9.5", 0.001, 1000000.0, "Above this target, Stabilizer may reduce quality. Must be greater than the lower target. At 90 Hz the full frame budget is about 11.1 ms." },
		Setting{ "FrameTimeChangeThreshold", "Frame-time change threshold (ms)", "Performance", SettingKind::Number, "0.5", 0.0, 1000000.0, "Minimum frame-time change that triggers a check for a more suitable quality level." },
		Setting{ "LevelChangeBackUpWait", "Quality increase delay (cycles)", "Performance", SettingKind::Integer, "80", 0.0, 2147483647.0, "Wait before returning to higher quality. Each cycle contains five checks: interval x 5 x cycles / 1000 gives approximate seconds." },
		Setting{ "LevelChangeBackDownWait", "Quality decrease delay (cycles)", "Performance", SettingKind::Integer, "80", 0.0, 2147483647.0, "Wait before returning to lower quality. Each cycle contains five frame-time checks." },
		Setting{ "LevelChangeBackUpAngle", "Turn before quality increase (degrees)", "Performance", SettingKind::Number, "30", 0.0, 360.0, "Required change in player heading before returning to higher quality." },
		Setting{ "AdjustActorLODFade", "Adjust actor fade distance", "LOD & Grass", SettingKind::Boolean, "1", 0.0, 1.0, "Automatically reduce actor visibility distance when CPU late starts are detected." },
		Setting{ "MinfLODFadeOutMultActors", "Actor fade minimum", "LOD & Grass", SettingKind::Number, "7", 0.0, 1000000.0, "Minimum actor LOD fade multiplier. Lower values hide distant actors sooner." },
		Setting{ "MaxfLODFadeOutMultActors", "Actor fade maximum", "LOD & Grass", SettingKind::Number, "10", 0.0, 1000000.0, "Maximum actor LOD fade multiplier when CPU performance allows it." },
		Setting{ "AdjustObjectLODFade", "Adjust object fade distance", "LOD & Grass", SettingKind::Boolean, "1", 0.0, 1.0, "Automatically reduce object visibility distance when CPU late starts are detected." },
		Setting{ "MinfLODFadeOutMultObjects", "Object fade minimum", "LOD & Grass", SettingKind::Number, "10", 0.0, 1000000.0, "Minimum object LOD fade multiplier. Lower values hide distant objects sooner." },
		Setting{ "MaxfLODFadeOutMultObjects", "Object fade maximum", "LOD & Grass", SettingKind::Number, "14", 0.0, 1000000.0, "Maximum object LOD fade multiplier when CPU performance allows it." },
		Setting{ "AdjustItemLODFade", "Adjust item fade distance", "LOD & Grass", SettingKind::Boolean, "1", 0.0, 1.0, "Automatically reduce item visibility distance when CPU late starts are detected." },
		Setting{ "MinfLODFadeOutMultItems", "Item fade minimum", "LOD & Grass", SettingKind::Number, "4", 0.0, 1000000.0, "Minimum item LOD fade multiplier. Lower values hide distant items sooner." },
		Setting{ "MaxfLODFadeOutMultItems", "Item fade maximum", "LOD & Grass", SettingKind::Number, "8", 0.0, 1000000.0, "Maximum item LOD fade multiplier when CPU performance allows it." },
		Setting{ "RiftenLODFadeOutModifierActors", "Riften actor modifier", "LOD & Grass", SettingKind::Number, "-5", -1000000.0, 1000000.0, "Added to the minimum actor fade multiplier in Riften. Negative values reduce visibility distance." },
		Setting{ "RiftenLODFadeOutModifierObjects", "Riften object modifier", "LOD & Grass", SettingKind::Number, "-6", -1000000.0, 1000000.0, "Added to the minimum object fade multiplier in Riften." },
		Setting{ "RiftenLODFadeOutModifierItems", "Riften item modifier", "LOD & Grass", SettingKind::Number, "-2", -1000000.0, 1000000.0, "Added to the minimum item fade multiplier in Riften." },
		Setting{ "OtherCitiesLODFadeOutModifierActors", "Other cities: actor modifier", "LOD & Grass", SettingKind::Number, "-3", -1000000.0, 1000000.0, "Added to the minimum actor fade multiplier in other cities." },
		Setting{ "OtherCitiesLODFadeOutModifierObjects", "Other cities: object modifier", "LOD & Grass", SettingKind::Number, "-2", -1000000.0, 1000000.0, "Added to the minimum object fade multiplier in other cities." },
		Setting{ "OtherCitiesLODFadeOutModifierItems", "Other cities: item modifier", "LOD & Grass", SettingKind::Number, "-1", -1000000.0, 1000000.0, "Added to the minimum item fade multiplier in other cities." },
		Setting{ "CPULateStartAngleToSwitchBackUp", "Turn before LOD increase (degrees)", "LOD & Grass", SettingKind::Number, "30", 0.0, 360.0, "Required player heading change before restoring higher LOD fade values." },
		Setting{ "CPULateStartDistanceToSwitchBackUp", "Move before LOD increase (game units)", "LOD & Grass", SettingKind::Number, "300", 0.0, 1000000000.0, "Required player movement before restoring higher LOD fade values." },
		Setting{ "CPULateStartChangeThreshold", "CPU late-start threshold (ms)", "LOD & Grass", SettingKind::Number, "2.0", 0.0, 1000000.0, "CPU late-start amount that triggers a check for increasing or decreasing fade distances." },
		Setting{ "CPULateStartCheckFrameCount", "CPU sampling window (frames)", "LOD & Grass", SettingKind::Integer, "30", 1.0, 100.0, "Number of frames checked for CPU late starts. Stabilizer supports at most 100." },
		Setting{ "CPULateStartMinFrameCountToReduce", "Late frames to reduce LOD", "LOD & Grass", SettingKind::Integer, "4", 0.0, 100.0, "Minimum late-frame count needed to reduce fade distances. Cannot exceed the sampling window." },
		Setting{ "CPULateStartMaxFrameCountToIncrease", "Late frames allowed to increase LOD", "LOD & Grass", SettingKind::Integer, "0", 0.0, 100.0, "Maximum late-frame count allowed when increasing fade distances. Cannot exceed the sampling window." },
		Setting{ "LODFadeOutValueUpWait", "LOD increase delay (cycles)", "LOD & Grass", SettingKind::Integer, "30", 0.0, 2147483647.0, "Number of CPU checking cycles to wait before increasing fade distances again." },
		Setting{ "LODFadeOutValueDownWait", "LOD decrease delay (cycles)", "LOD & Grass", SettingKind::Integer, "10", 0.0, 2147483647.0, "Number of CPU checking cycles to wait before reducing fade distances again." },
		Setting{ "GrassChange", "Manage grass by worldspace", "LOD & Grass", SettingKind::Boolean, "1", 0.0, 1.0, "Disable grass in cities, interiors and worlds outside Tamriel, subject to the world lists below. Do not combine with a TOGGLE>tg level command." },
		Setting{ "WithGrassWorlds", "Additional worlds with grass", "LOD & Grass", SettingKind::WorldList, "", 0.0, 0.0, "Comma-separated plugin|hex-form-ID pairs, for example MyWorld.esp|001234. These worlds retain grass. An empty list adds no exceptions." },
		Setting{ "NoGrassWorlds", "Worlds without grass", "LOD & Grass", SettingKind::WorldList, "", 0.0, 0.0, "Comma-separated plugin|hex-form-ID pairs, for example MyWorld.esp|001234. Grass is disabled in these worlds." },
		Setting{ "LODcitychange", "Hide distant LOD in cities", "LOD & Grass", SettingKind::Boolean, "0", 0.0, 1.0, "Turn off distant LOD in cities. This can improve performance but removes distant landscape or object detail." },
		Setting{ "LODinteriorchange", "Hide distant LOD in interiors", "LOD & Grass", SettingKind::Boolean, "0", 0.0, 1.0, "Turn off distant LOD in interiors." },
		Setting{ "RainLODOffset", "Rain LOD distance reduction (game units)", "LOD & Grass", SettingKind::Number, "10000", 0.0, 1000000000.0, "Amount subtracted from LOD distance settings during rain." },
		Setting{ "AdjustDynamicResolution", "Adjust Skyrim dynamic resolution", "Performance", SettingKind::Boolean, "0", 0.0, 1.0, "Experimental Stabilizer control of Skyrim's native dynamic resolution. Requires bEnableAutoDynamicResolution=1 in the game INI. This is separate from CSX upscaling and Render Scale." },
		Setting{ "MinDynamicResolutionRatio", "Dynamic resolution minimum ratio", "Performance", SettingKind::Number, "0.9", 0.01, 1.0, "Lowest native dynamic-resolution ratio. 1 means full resolution; smaller values trade detail for performance." },
		Setting{ "MaxDynamicResolutionRatio", "Dynamic resolution maximum ratio", "Performance", SettingKind::Number, "1.0", 0.01, 1.0, "Highest native dynamic-resolution ratio used from the starting quality level. Must be at least the minimum." },
		Setting{ "DynamicResolutionStartLevel", "Dynamic resolution starting level", "Performance", SettingKind::Integer, "7", 0.0, 9.0, "First quality level using the maximum dynamic-resolution ratio. Earlier levels use 1.0; later levels progressively reduce it." },
	};

	inline const Setting* FindSetting(std::string_view key)
	{
		const auto found = std::ranges::find_if(kSettings, [&](const Setting& setting) { return Equal(key, setting.key); });
		return found == kSettings.end() ? nullptr : &*found;
	}

	inline bool ParseNumber(std::string_view text, double& value)
	{
		text = Trim(text);
		if (text.empty())
			return false;
		const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
		return result.ec == std::errc{} && result.ptr == text.data() + text.size() && std::isfinite(value);
	}

	/** Validate an explicit edit against the installed Stabilizer configuration contract. */
	inline bool ValidateSetting(const Setting& setting, std::string_view text, std::string& error)
	{
		if (setting.kind == SettingKind::WorldList) {
			auto remaining = Trim(text);
			while (!remaining.empty()) {
				const auto comma = remaining.find(',');
				const auto entry = Trim(remaining.substr(0, comma));
				const auto pipe = entry.find('|');
				const auto plugin = Trim(entry.substr(0, pipe));
				const auto form = pipe == entry.npos ? std::string_view{} : Trim(entry.substr(pipe + 1));
				if (form.empty() || form.size() > 8) {
					error = std::string(setting.label) + ": each world needs a plugin name and a 1-8 digit hexadecimal form ID.";
					return false;
				}
				unsigned int id = 0;
				const auto parsed = std::from_chars(form.data(), form.data() + form.size(), id, 16);
				if (plugin.empty() || plugin.find_first_of("|/\\\\:#;[]") != plugin.npos ||
					!(plugin.size() >= 4 && (Equal(plugin.substr(plugin.size() - 4), ".esp") || Equal(plugin.substr(plugin.size() - 4), ".esm") || Equal(plugin.substr(plugin.size() - 4), ".esl"))) ||
					parsed.ec != std::errc{} || parsed.ptr != form.data() + form.size()) {
					error = std::string(setting.label) + ": use comma-separated Plugin.esp|001234 entries.";
					return false;
				}
				if (comma == remaining.npos)
					break;
				remaining = Trim(remaining.substr(comma + 1));
				if (remaining.empty()) {
					error = "A world list cannot end with a comma.";
					return false;
				}
			}
			return true;
		}
		double value = 0;
		if (setting.kind == SettingKind::Integer || setting.kind == SettingKind::Boolean) {
			text = Trim(text);
			if (text.empty()) {
				error = std::string(setting.label) + ": enter a whole decimal number.";
				return false;
			}
			int integer = 0;
			const auto parsed = std::from_chars(text.data(), text.data() + text.size(), integer);
			if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
				error = std::string(setting.label) + ": enter a whole decimal number.";
				return false;
			}
		}
		if (!ParseNumber(text, value) || value < setting.minimum || value > setting.maximum ||
			(setting.kind != SettingKind::Number && std::floor(value) != value)) {
			error = std::format("{}: enter a finite value from {} to {}.", setting.label, setting.minimum, setting.maximum);
			return false;
		}
		return true;
	}

	/** Check edited fields and related limits without normalizing untouched custom settings. */
	inline bool ValidateSettings(const IniDocument& document, std::string& error)
	{
		const IniDocument baseline{ document.original, document.original };
		const auto changed = [&](std::string_view key) { return document.Get("Settings", key) != baseline.Get("Settings", key); };
		for (const auto& setting : kSettings) {
			const auto value = document.Get("Settings", setting.key);
			if (value && changed(setting.key) && !ValidateSetting(setting, *value, error))
				return false;
		}
		constexpr std::array pairs{
			std::pair{ "MinTargetFrameTime", "MaxTargetFrameTime" },
			std::pair{ "MinfLODFadeOutMultActors", "MaxfLODFadeOutMultActors" },
			std::pair{ "MinfLODFadeOutMultObjects", "MaxfLODFadeOutMultObjects" },
			std::pair{ "MinfLODFadeOutMultItems", "MaxfLODFadeOutMultItems" },
			std::pair{ "MinDynamicResolutionRatio", "MaxDynamicResolutionRatio" },
			std::pair{ "CPULateStartMinFrameCountToReduce", "CPULateStartCheckFrameCount" },
			std::pair{ "CPULateStartMaxFrameCountToIncrease", "CPULateStartCheckFrameCount" }
		};
		for (const auto& [lowerKey, upperKey] : pairs) {
			if (!changed(lowerKey) && !changed(upperKey))
				continue;
			const auto lower = document.Get("Settings", lowerKey), upper = document.Get("Settings", upperKey);
			double minValue = 0, maxValue = 0;
			if (!lower || !upper || !ValidateSetting(*FindSetting(lowerKey), *lower, error) ||
				!ValidateSetting(*FindSetting(upperKey), *upper, error) ||
				!ParseNumber(*lower, minValue) || !ParseNumber(*upper, maxValue) ||
				minValue > maxValue || (Equal(lowerKey, "MinTargetFrameTime") && minValue == maxValue)) {
				error = std::string(lowerKey) + " and " + upperKey + " must both be configured with valid lower/upper limits.";
				return false;
			}
		}
		return true;
	}
}
