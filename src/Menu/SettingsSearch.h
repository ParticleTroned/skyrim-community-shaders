#pragma once

#include <string>
#include <string_view>
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <nlohmann/json_fwd.hpp>
#endif

namespace MenuUI::SettingsSearch
{
	/** Draw cached results in the sidebar; returns false for an empty query. */
	bool Draw(std::string_view a_query);
	/** Refresh registry metadata lazily on the next search after opening the menu. */
	void Invalidate();
	/** Stop a pending reveal when closing the menu, preserving observed results. */
	void CancelReveal();
	/** Queue navigation only, without executing a setting or button action. */
	bool Navigate(std::string_view a_id, std::string& a_error);

	/** Bounds control discovery to the selected page's ordinary UI draw. */
	class PageScope
	{
	public:
		explicit PageScope(std::string_view a_page);
		~PageScope();
		PageScope(const PageScope&) = delete;
		PageScope& operator=(const PageScope&) = delete;

	private:
		std::string_view previous;
	};

	/** Observe an existing control's final rectangle without changing its value. */
	void NotifyControl(const char* a_label);
	/** Only for UI navigation selectors; never pass a feature setting here. */
	bool SelectNavigationChoice(const char* a_label, int& a_selected, const char* const a_items[], int a_count);
	/** Expand a cataloged UI group before drawing its collapsing header. */
	void PrepareGroup(const char* a_label);

#ifdef DEVBENCH_BRIDGE_ENABLED
	/** Same cached results and navigation IDs used by the sidebar. */
	nlohmann::json Query(std::string_view a_query);
	/** Reports queued, revealed or conditional navigation without inferring success. */
	nlohmann::json Status();
#endif
}
