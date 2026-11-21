#pragma once

#include <string_view>

namespace MenuUI::SettingsNavigation
{
	struct Location
	{
		std::string_view page, section;
		bool sectionMoved = false;
	};

	/** Resolve moved controls while preserving native feature page identities. */
	constexpr Location Resolve(std::string_view page, std::string_view section)
	{
		if (page == "General" && section == "shaders")
			return { "DevTools", "shaders", true };
		if (page == "General")
			page = "Settings";
		if (page == "Advanced")
			page = "DevTools";
		if (page == "DevTools" && section == "startup")
			return { "Settings", "startup", true };
		if (page == "DevTools" && section == "capture")
			return { "RenderDoc", "overview", true };
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
