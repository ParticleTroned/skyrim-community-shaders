#pragma once

#include "AdaptiveBalancePresets.h"
#include <functional>

namespace MenuUI
{
	struct ProfileExchangeState
	{
		int mode = 0;
		std::string name;
		std::string nameDraft;
		bool upperCase = false;
		std::filesystem::path source;
		AdaptiveBalancePresets::ExportFolder importPicker, exportPicker;
	};
	/** One complete profile per transfer; callbacks own validation and mutation. */
	void DrawProfileExchange(ProfileExchangeState& state, const char* name, const std::filesystem::path& root,
		bool acceptsPacks, const char* blocked,
		const std::function<void(const std::filesystem::path&, const std::string&)>& importProfile,
		const std::function<void(const std::filesystem::path&, const std::string&)>& exportProfile);
}
