#pragma once

#include "RE/U/UI.h"

namespace Util
{
	/** Read current menu state with an interned key, retaining the menu for the query. */
	[[nodiscard]] inline bool IsMenuOpen(RE::UI* a_ui, const RE::BSFixedString& a_menuName)
	{
		if (!a_ui)
			return false;
		const auto found = a_ui->menuMap.find(a_menuName);
		if (found == a_ui->menuMap.end())
			return false;
		const auto menu = found->second.menu;
		return menu && menu->OnStack();
	}

	/** Intern a built-in menu name on first valid UI access; retain only the key. */
	template <class Menu>
	[[nodiscard]] bool IsMenuOpen(RE::UI* a_ui)
	{
		if (!a_ui)
			return false;
		static const RE::BSFixedString menuName{ Menu::MENU_NAME };
		return IsMenuOpen(a_ui, menuName);
	}
}
