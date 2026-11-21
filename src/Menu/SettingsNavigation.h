#pragma once

#include <string_view>

namespace MenuUI::SettingsNavigation
{
	constexpr bool IsPerformancePage(std::string_view page)
	{
		return page == "Performance" || page == "PerformancePresets" || page == "Profiling" || page == "PerformanceTuning" || page == "PerformanceOverlay";
	}

	constexpr std::string_view PerformanceSection(std::string_view page)
	{
		return page == "PerformancePresets" ? "presets" : page == "Profiling"      ? "timings" :
		                                              page == "PerformanceTuning"  ? "tuning" :
		                                              page == "PerformanceOverlay" ? "overlay" :
		                                                                             "overview";
	}

	struct Location
	{
		std::string_view page, section;
		bool sectionMoved = false;
	};

	/** Resolve moved controls to their current settings hosts. */
	constexpr Location Resolve(std::string_view page, std::string_view section)
	{
		if (page == "Home" && section == "welcome")
			return { page, "overview", true };
		if (page == "Home" && section == "cache")
			return { "DevTools", "shaders", true };
		if (page == "Profiling" && section == "performance")
			return { "PerformanceTuning", "tuning", true };
		if (page == "PerformanceTuning" && (section == "compare" || section == "quick_scan"))
			return { page, "tuning", true };
		if (page == "PerformanceOverlay" && (section == "counters" || section == "appearance"))
			return { page, "overlay", true };
		if (page == "Screenshot" && (section == "source" || section == "output" || section == "framing" || section == "sequence"))
			return { page, "overview", true };
		if (page == "Wetterness" && section == "shore")
			return { page, "surface", true };
		if (page == "ScreenSpaceGI" && section == "indirect")
			return { page, "look", true };
		if (page == "HairSpecular" && section == "highlights")
			return { page, "look", true };
		if (page == "General" && section == "shaders")
			return { "DevTools", "shaders", true };
		if (page == "General")
			page = "Settings";
		if (page == "Settings" && section == "layout")
			return { page, "behaviour", true };
		if (page == "Settings" && section == "colours")
			return { page, "themes", true };
		if (page == "Advanced")
			page = "DevTools";
		if (page == "RenderDoc" && (section == "overview" || section == "capture" || section == "storage" || section == "files" || section == "renderdoc"))
			return { "DevTools", "renderdoc", true };
		if (page == "DevTools" && section == "diagnostics")
			return { page, "logging", true };
		if (page == "DevTools" && section == "startup")
			return { "Settings", "startup", true };
		if (page == "DevTools" && section == "capture")
			return { page, "renderdoc", true };
		if (page == "VR") {
			if (section == "layout")
				return { "Settings", "vr_menu", true };
			if (section == "controls")
				return { "Settings", "vr_controls", true };
			if (section == "diagnostics")
				return { "DevTools", "vr_diagnostics", true };
		}
		return { page, section };
	}

	/** VR interface controls follow the runtime independently of rendering switches. */
	constexpr bool SectionAvailable(std::string_view page, std::string_view section, bool isVR)
	{
		return isVR || !((page == "Settings" && (section == "vr_menu" || section == "vr_controls" || section == "desktop")) ||
						   (page == "DevTools" && section == "vr_diagnostics"));
	}
}
