#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <nlohmann/json.hpp>

namespace ReflexDiagnostics::Suite
{
	/** One restoration receipt spans every condition and never adopts a test override. */
	struct SavedSettings
	{
		bool lowLatency = false, boost = false, cap = false;
		float fps = 60, hour = 12, timeScale = 20;
		bool valid = false;

		template <class Settings>
		void Capture(const Settings& settings, float gameHour, float scale)
		{
			if (!valid)
				*this = { settings.reflexLowLatencyMode, settings.reflexLowLatencyBoost, settings.reflexUseFPSLimit,
					settings.reflexFPSLimit, gameHour, scale, true };
		}
		template <class Settings>
		void Restore(Settings& settings, float& gameHour, float& scale)
		{
			if (!valid)
				return;
			settings.reflexLowLatencyMode = lowLatency;
			settings.reflexLowLatencyBoost = boost;
			settings.reflexUseFPSLimit = cap;
			settings.reflexFPSLimit = fps;
			gameHour = hour;
			scale = timeScale;
			valid = false;
		}
		void Preserve(nlohmann::json& settings) const
		{
			if (!valid)
				return;
			settings["reflexLowLatencyMode"] = lowLatency;
			settings["reflexLowLatencyBoost"] = boost;
			settings["reflexUseFPSLimit"] = cap;
			settings["reflexFPSLimit"] = fps;
		}
	};
}
#endif
