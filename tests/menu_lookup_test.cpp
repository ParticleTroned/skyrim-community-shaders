#include <array>
#include <atomic>
#include <cstddef>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

namespace RE
{
	struct BSFixedString
	{
		static inline unsigned constructions = 0;
		std::string value;
		BSFixedString() = default;
		BSFixedString(std::string_view name) : value(name) { ++constructions; }
		bool operator==(const BSFixedString&) const = default;
	};
	struct IMenu
	{
		bool onStack = false;
		std::weak_ptr<IMenu> owner;
		mutable long ownersDuringQuery = 0;
		bool OnStack() const
		{
			ownersDuringQuery = owner.use_count();
			return onStack;
		}
	};
	struct UI
	{
		struct Entry
		{
			std::shared_ptr<IMenu> menu;
		};
		struct Hash
		{
			size_t operator()(const BSFixedString& name) const { return std::hash<std::string>{}(name.value); }
		};
		std::unordered_map<BSFixedString, Entry, Hash> menuMap;
	};
	struct MainMenu
	{
		static constexpr std::string_view MENU_NAME = "Main Menu";
	};
	struct LoadingMenu
	{
		static constexpr std::string_view MENU_NAME = "Loading Menu";
	};
	struct RaceSexMenu
	{
		static constexpr std::string_view MENU_NAME = "RaceSex Menu";
	};
	struct StatsMenu
	{
		static constexpr std::string_view MENU_NAME = "StatsMenu";
	};
	struct DialogueMenu
	{
		static constexpr std::string_view MENU_NAME = "Dialogue Menu";
	};
}

#include "menu_lookup_under_test.h"

namespace globals
{
	struct State
	{
		bool isMainMenuOpen = false;
		bool isLoadingMenuOpen = false;
	} stateValue;
	State* state = &stateValue;
	namespace game
	{
		RE::UI* ui = nullptr;
		bool isVR = true;
	}
}
namespace
{
	std::atomic_bool g_vrLoadingMenuOpenFromEvent{ false };
	std::atomic_bool g_vrStatsMenuOpenFromEvent{ false };
	std::atomic_bool g_vrDialogueMenuOpenFromEvent{ false };
#include "menu_lookup_loading_under_test.h"
#include "menu_lookup_main_under_test.h"
#include "menu_lookup_names_under_test.h"
#include "menu_lookup_other_contexts_under_test.h"
#include "menu_lookup_presentation_under_test.h"

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
}

int main()
try {
	Require(!Util::IsMenuOpen<RE::MainMenu>(nullptr), "missing UI admitted a menu");
	Require(!IsMainOrLoadingMenuContextActive() && !IsSkyrimMenuPresentationContextActive(nullptr), "missing UI admitted a context");
	Require(!IsRaceSexMenuContextActive(nullptr) && !IsStatsMenuContextActive() && !IsDialogueMenuContextActive(), "missing UI admitted another context");
	Require(!IsVRActivationPromptContextActive(), "missing UI admitted an activation prompt");
	Require(RE::BSFixedString::constructions == 0, "menu keys were interned before UI availability");

	RE::UI ui;
	globals::game::ui = &ui;
	Require(!IsMainOrLoadingMenuContextActive() && !IsSkyrimMenuPresentationContextActive(&ui), "empty menu map admitted a context");
	const RE::BSFixedString mainKey{ RE::MainMenu::MENU_NAME };
	ui.menuMap[mainKey] = {};
	Require(!IsMainMenuContextActive(), "registered null menu admitted a context");
	auto main = std::make_shared<RE::IMenu>();
	main->owner = main;
	ui.menuMap[mainKey].menu = main;
	Require(!IsMainMenuContextActive(), "registered off-stack menu appeared open");
	main->onStack = true;
	Require(IsMainMenuContextActive(), "live main-menu open was delayed");
	const auto constructions = RE::BSFixedString::constructions;
	const auto references = main.use_count();
	for (unsigned query = 0; query < 1000; ++query) {
		main->onStack = (query % 2) != 0;
		Require(IsMainOrLoadingMenuContextActive() == main->onStack, "same-frame transition was cached");
	}
	Require(main->ownersDuringQuery == references + 1, "query did not retain its menu while reading live state");
	Require(main.use_count() == references, "queries retained menu objects");
	Require(RE::BSFixedString::constructions == constructions, "repeated context checks re-interned names");
	ui.menuMap.erase(mainKey);
	Require(!IsMainMenuContextActive(), "removed menu remained cached");
	ui.menuMap[mainKey].menu = std::make_shared<RE::IMenu>();
	Require(!IsMainMenuContextActive(), "replacement inherited the old menu state");
	ui.menuMap[mainKey].menu->onStack = true;
	Require(IsMainMenuContextActive(), "replacement menu was not queried");
	ui.menuMap.clear();

	const RE::BSFixedString loadingKey{ RE::LoadingMenu::MENU_NAME };
	auto loading = std::make_shared<RE::IMenu>();
	ui.menuMap[loadingKey].menu = loading;
	loading->onStack = true;
	Require(IsMainOrLoadingMenuContextActive(), "live loading open was delayed");
	loading->onStack = false;
	Require(!IsMainOrLoadingMenuContextActive(), "live loading close was delayed");
	g_vrLoadingMenuOpenFromEvent = true;
	Require(IsLoadingMenuContextActive(), "loading event no longer overrides its closed mirror");
	g_vrLoadingMenuOpenFromEvent = false;
	globals::state->isLoadingMenuOpen = true;
	Require(IsLoadingMenuContextActive(), "cached loading guard was discarded");
	globals::state->isLoadingMenuOpen = false;
	globals::state->isMainMenuOpen = true;
	Require(IsMainMenuContextActive(), "cached main-menu guard was discarded");
	globals::state->isMainMenuOpen = false;
	ui.menuMap.clear();

	for (const auto name : kSkyrimPresentationMenuNames) {
		const RE::BSFixedString key{ name };
		auto menu = std::make_shared<RE::IMenu>();
		ui.menuMap[key].menu = menu;
		const auto before = RE::BSFixedString::constructions;
		for (unsigned query = 0; query < 10; ++query) {
			menu->onStack = (query % 2) != 0;
			Require(IsSkyrimMenuPresentationContextActive(&ui) == menu->onStack, "presentation-menu transition was cached or omitted");
		}
		Require(RE::BSFixedString::constructions == before, "presentation scan re-interned names");
		ui.menuMap.clear();
		Require(!IsSkyrimMenuPresentationContextActive(&ui), "presentation scan retained a removed menu");
	}
	auto custom = std::make_shared<RE::IMenu>();
	for (const auto name : { "CustomMenu", "FavoritesMenu", "GiftMenu", "Credits Menu", "CalibrationOptionMenu" }) {
		ui.menuMap.clear();
		ui.menuMap[RE::BSFixedString{ name }].menu = custom;
		for (bool open : { true, false, true }) {
			custom->onStack = open;
			Require(IsSkyrimMenuPresentationContextActive(&ui) == open, "required menu host was not recognized");
		}
	}
	ui.menuMap.clear();
	ui.menuMap[RE::BSFixedString{ "AlternatePerspectiveMenu" }].menu = custom;
	Require(!IsSkyrimMenuPresentationContextActive(&ui), "SWF asset name was treated as the SKSE menu host");
	ui.menuMap.clear();
	ui.menuMap[RE::BSFixedString{ "Unrelated Menu" }].menu = main;
	Require(!IsSkyrimMenuPresentationContextActive(&ui), "unrelated open menu changed presentation eligibility");
	for (auto name : { RE::RaceSexMenu::MENU_NAME, RE::StatsMenu::MENU_NAME, RE::DialogueMenu::MENU_NAME }) {
		ui.menuMap.clear();
		auto menu = std::make_shared<RE::IMenu>();
		ui.menuMap[RE::BSFixedString{ name }].menu = menu;
		for (bool open : { false, true, false }) {
			menu->onStack = open;
			Require(IsRaceSexMenuContextActive(&ui) == (open && name == RE::RaceSexMenu::MENU_NAME), "race-menu live state changed");
			Require(IsStatsMenuContextActive() == (open && name == RE::StatsMenu::MENU_NAME), "stats-menu live state changed");
			Require(IsDialogueMenuContextActive() == (open && name == RE::DialogueMenu::MENU_NAME), "dialogue-menu live state changed");
		}
	}
	g_vrStatsMenuOpenFromEvent = g_vrDialogueMenuOpenFromEvent = true;
	Require(IsStatsMenuContextActive() && IsDialogueMenuContextActive(), "existing event guards were lost");
	g_vrStatsMenuOpenFromEvent = g_vrDialogueMenuOpenFromEvent = false;

	ui.menuMap.clear();
	const RE::BSFixedString activationKey{ "WSActivateRollover" };
	auto activation = std::make_shared<RE::IMenu>();
	ui.menuMap[activationKey].menu = activation;
	Require(!IsVRActivationPromptContextActive(), "off-stack activation prompt appeared open");
	const auto activationConstructions = RE::BSFixedString::constructions;
	for (bool open : { true, false, true }) {
		activation->onStack = open;
		Require(IsVRActivationPromptContextActive() == open, "activation prompt transition was cached");
		Require(!IsSkyrimMenuPresentationContextActive(&ui), "gameplay prompt entered the full-menu policy");
	}
	Require(RE::BSFixedString::constructions == activationConstructions, "activation checks re-interned their key");
	globals::game::isVR = false;
	Require(!IsVRActivationPromptContextActive(), "activation overlay admitted a non-VR runtime");
	globals::game::isVR = true;
	ui.menuMap.erase(activationKey);
	Require(!IsVRActivationPromptContextActive(), "removed activation prompt remained active");
	ui.menuMap[activationKey].menu = activation;

	RE::UI replacement;
	globals::game::ui = &replacement;
	globals::state = nullptr;
	Require(!IsMainOrLoadingMenuContextActive() && !IsSkyrimMenuPresentationContextActive(&replacement), "UI replacement reused old state");
	Require(!IsVRActivationPromptContextActive(), "UI replacement retained an old activation prompt");
	globals::game::ui = nullptr;
	Require(!IsMainOrLoadingMenuContextActive(), "teardown retained menu state");
	std::cout << "Menu lookup live state, key reuse, lifetime and context checks passed\n";
} catch (const std::exception& error) {
	std::cerr << error.what() << '\n';
	return 1;
}
