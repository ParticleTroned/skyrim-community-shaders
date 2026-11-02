#include "PresetCompatibility.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <iostream>

int main()
{
	using namespace PresetCompatibility;
	assert(Evaluate(nlohmann::json::object(), "CSX 2.2-SE").disposition == Disposition::kUnmarked);

	nlohmann::json settings{
		{ kSettingsKey, {
							{ "contractVersion", kContractVersion },
							{ "presetId", "boundary-test" },
							{ "presetVersion", "1" },
							{ "target", {
											{ "runtime", "SE" },
											{ "minimumVersion", "2.1" },
											{ "maximumVersionExclusive", "2.3" },
										} },
							{ "settingsContract", {
													  { "revision", kSettingsContractRevision },
													  { "sourceTreeSha256", std::string(64, 'a') },
												  } },
						} },
	};
	for (const auto* version : { "CSX 2.1-SE", "CSX 2.2-SE", "CSX 2.2.1-SE" })
		assert(Evaluate(settings, version).disposition == Disposition::kCompatible);
	for (const auto* version : { "CSX 2.0-SE", "CSX 2.3-SE", "CSX 3.0-SE", "invalid" })
		assert(!Evaluate(settings, version).ShouldApply());

	auto& contract = settings[kSettingsKey];
	contract["target"]["runtime"] = "Unsupported";
	assert(!Evaluate(settings, "CSX 2.2-SE").ShouldApply());
	contract["target"]["runtime"] = "SE";
	contract["settingsContract"]["revision"] = kSettingsContractRevision - 1;
	assert(!Evaluate(settings, "CSX 2.2-SE").ShouldApply());
	contract["settingsContract"]["revision"] = kSettingsContractRevision;
	contract["settingsContract"]["sourceTreeSha256"] = "invalid";
	assert(!Evaluate(settings, "CSX 2.2-SE").ShouldApply());
	std::cout << "PASS: preset version boundaries, runtime, revision and malformed identity\n";
}
