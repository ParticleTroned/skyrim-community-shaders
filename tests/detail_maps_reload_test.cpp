#include <fmt/ranges.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace logger
{
	template <class... Args>
	void warn(Args&&...)
	{}
}

// Inject content-loader failures while exercising the production transaction.
struct DetailMaps
{
	std::vector<int> sets, rules, excludes, entries;
	std::vector<std::string> ruleErrors;
	int failure = 0;
	bool activePass = true;
	void EndPass() { activePass = false; }
	void LoadSets()
	{
		sets = { 2 };
		if (failure == 1)
			throw std::runtime_error("DDS read failed");
	}
	void LoadRules()
	{
		rules = { 3 };
		excludes = { 4 };
		if (failure == 2)
			ruleErrors.push_back("Malformed rule");
	}
	void Reload();
};
#include "detail_maps_reload_under_test.h"

int main()
{
	try {
		for (int failure : { 1, 2 }) {
			DetailMaps feature;
			feature.sets = feature.rules = feature.excludes = feature.entries = { 1 };
			feature.failure = failure;
			feature.Reload();
			if (feature.sets != std::vector{ 1 } || feature.rules != std::vector{ 1 } || feature.excludes != std::vector{ 1 } ||
				feature.activePass || !feature.entries.empty() || feature.ruleErrors.empty())
				throw std::runtime_error("Failed reload discarded valid content or retained stale draw state");
			feature.failure = 0;
			feature.Reload();
			if (feature.sets != std::vector{ 2 } || feature.rules != std::vector{ 3 } || feature.excludes != std::vector{ 4 } || !feature.ruleErrors.empty())
				throw std::runtime_error("Successful reload failed to publish new content");
		}
		DetailMaps firstLoad;
		firstLoad.failure = 2;
		firstLoad.Reload();
		if (!firstLoad.sets.empty() || !firstLoad.rules.empty() || !firstLoad.excludes.empty())
			throw std::runtime_error("Failed first load published partial content");
		std::cout << "Reload rollback, recovery and failed first load passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
