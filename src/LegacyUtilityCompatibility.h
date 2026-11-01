#pragma once

#include <string_view>

struct Feature;

namespace LegacyUtilityCompatibility
{
	/** Legacy utility names address Adaptive Balance's global appearance and DOF settings. */
	bool IsAlias(std::string_view a_name);
	/** Returns an unregistered settings adapter, never a separate runtime feature. */
	Feature* Find(std::string_view a_name, bool a_requireLoaded);
}
