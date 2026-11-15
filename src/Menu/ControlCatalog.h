#pragma once

#include <span>
#include <string_view>

namespace MenuUI::ControlCatalog
{
	/** Immutable build-generated navigation metadata; never owns setting values. */
	struct Entry
	{
		std::string_view id;
		std::string_view feature;
		std::string_view page;
		std::string_view section;
		std::string_view label;
		std::string_view rawLabel;
		std::string_view selector;
		std::string_view choice;
		std::string_view group;
	};

	/** Includes controls on pages that have never been drawn. */
	std::span<const Entry> Entries();
}
