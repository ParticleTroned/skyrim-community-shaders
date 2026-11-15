#include "Menu/SettingsSearchPolicy.h"
#include <iostream>
#include <stdexcept>

int main()
{
	using namespace MenuUI::SettingsSearch;
	try {
		auto require = [](bool condition, const char* message) {
			if (!condition)
				throw std::runtime_error(message);
		};
		const auto words = Policy::Words("  bloom\tstrength\r\n");
		require(words.size() == 2 && words[0] == "bloom" && words[1] == "strength", "Tokenize whitespace");
		require(Policy::Score("strength", "adaptive balance bloom", words) > 0, "Match title and breadcrumb together");
		require(Policy::Score("strength", "water", words) < 0, "Require every query word");
		require(Policy::Score("strength", "bloom", Policy::Words("   ")) < 0, "Blank query must not enumerate everything");
		const auto bloom = Policy::Words("bloom");
		require(Policy::Score("bloom strength", "global", bloom) > Policy::Score("strength", "bloom", bloom), "Prefer title over location");
		require(Policy::Score("bloom strength", "", bloom) > Policy::Score("global bloom strength", "", bloom), "Prefer title prefix");
		require(Policy::Score("exposure", "global", Policy::Words("exposureglobal")) < 0, "Do not match across field boundaries");
		require(Policy::Score("x", "", Policy::Words("query longer than the label")) < 0, "Reject unmatched long query");
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
