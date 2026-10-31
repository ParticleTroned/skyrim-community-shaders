#include "Api/MeshBlendingDevBenchPolicy.h"

#include <cassert>
#include <limits>

int main()
{
	using nlohmann::json;
	using namespace CSX::Api::MeshBlendingDevBench;
	json current = json::object();
	for (const auto& field : Settings) {
		current[std::string(field.name)] = field.minimum;
	}
	current["Enabled"] = 1u;
	current["NifEnabled"] = 1u;
	current["BlendStrength"] = 0.75f;
	current["BlendWidth"] = 12.0f;
	const auto original = current;
	const auto proposed = ApplySettingsPatch(current, { { "NifEnabled", false }, { "BlendWidth", 24.0 } });
	assert(current == original);
	assert(proposed.at("Enabled") == 1u);
	assert(proposed.at("NifEnabled") == 0u);
	assert(proposed.at("BlendStrength") == 0.75f);
	assert(proposed.at("BlendWidth") == 24.0f);
	assert(ApplySettingsPatch(current, json::object()) == current);

	auto rejects = [&](const json& patch) {
		bool rejected = false;
		try {
			(void)ApplySettingsPatch(current, patch);
		} catch (const std::invalid_argument&) {
			rejected = true;
		}
		assert(rejected);
		assert(current == original);
	};
	rejects(json::array());
	rejects({ { "Unknown", true } });
	rejects({ { "BlendWidth", "12" } });
	rejects({ { "BlendWidth", false } });
	rejects({ { "BlendWidth", std::numeric_limits<double>::infinity() } });
	rejects({ { "BlendWidth", std::numeric_limits<double>::quiet_NaN() } });
	rejects({ { "BlendWidth", 0.0 } });
	rejects({ { "BlendWidth", 257.0 } });
	rejects({ { "DetectionMode", 1.5 } });
	rejects({ { "DetectionMode", -1 } });
	rejects({ { "DetectionMode", 3 } });
	rejects({ { "Enabled", 2 } });
	rejects({ { "Enabled", 1.0 } });
	rejects({ { "Enabled", false }, { "MaximumDistance", 65537 } });

	const auto schema = Schema();
	const auto& fields = schema.at("properties").at("settings").at("properties");
	assert(fields.size() == std::size(Settings));
	for (const auto& field : Settings) {
		assert(fields.contains(field.name));
	}
	assert(!schema.at("additionalProperties").get<bool>());
	assert(!schema.at("properties").at("settings").at("additionalProperties").get<bool>());
	const auto status = StatusSchema();
	assert(!status.at("additionalProperties").get<bool>());
	assert(status.at("properties").at("settings") == schema.at("properties").at("settings"));
	assert(status.at("properties").at("persisted").at("const") == false);
	assert(status.at("properties").at("diagnostics").at("additionalProperties").at("minimum") == 0);
}
