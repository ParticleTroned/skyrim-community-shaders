#pragma once
#include "PerformanceResults.h"

namespace MenuUI::PerformanceResultsPage
{
	/** Initializes the session log and background result store at plugin startup. */
	PerformanceResults::Store& GetStore();
	void Draw();
	/** Shared asynchronous history/load/export actions for the optional DevBench bridge. */
	nlohmann::json Action(const nlohmann::json& args);
}
