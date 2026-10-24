#include "Api/MeshBlendingDevBenchPolicy.h"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

using json = nlohmann::json;

namespace globals
{
	struct StateBoundary
	{
		bool blocked = false;
		bool developer = true;
		bool IsPersistentMutationBlocked() const { return blocked; }
		bool IsDeveloperMode() const { return developer; }
	};
	StateBoundary* state = nullptr;
}

// Settings I/O and engine availability are boundaries; the adapter and its
// diagnostic member layout below are extracted from production source.
struct MeshBlending
{
#include "mesh_blending_devbench_members.h"
	Diagnostics diagnostics{};
	bool loaded = true;
	bool runtimeEnabled = true;
	bool capture = false;
	std::uint32_t loads = 0u;
	std::uint32_t captureWrites = 0u;
	std::atomic_bool ruleFileMutationAllowed = true;
	std::string ruleFileLoadError;
	json settings{ { "Enabled", 1u }, { "NifEnabled", 1u }, { "BlendStrength", 0.75f }, { "BlendWidth", 12.0f } };
	void SaveSettings(json& value) { value = settings; }
	void LoadSettings(json& value)
	{
		settings = value;
		++loads;
	}
	void SetDiscoveryCaptureEnabled(bool value)
	{
		capture = value;
		++captureWrites;
	}
	bool IsRuntimeEnabled() const { return runtimeEnabled; }
	bool IsDiscoveryCaptureActive() const { return capture; }
	json ControlDevBench(const json& request);
};

#include "mesh_blending_devbench_under_test.h"

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	struct Fixture
	{
		globals::StateBoundary state;
		MeshBlending feature;
		Fixture()
		{
			globals::state = &state;
			feature.diagnostics.drawCalls = std::uint64_t{ 1 } << 40u;
			feature.diagnostics.currentFrame = 19u;
			feature.diagnostics.traversalLimitRejects = 7u;
		}
		~Fixture() { globals::state = nullptr; }
	};

	json MutationState(const MeshBlending& feature)
	{
		return { feature.settings, feature.capture, feature.loads, feature.captureWrites,
			feature.diagnostics.drawCalls, feature.diagnostics.currentFrame, feature.diagnostics.traversalLimitRejects };
	}

	void TestStatusAndMutation()
	{
		Fixture fixture;
		auto& feature = fixture.feature;
		feature.ruleFileMutationAllowed.store(false);
		feature.ruleFileLoadError = "read-only policy";
		const auto before = MutationState(feature);
		const auto status = feature.ControlDevBench(json::object());
		Require(status.is_object() && status.size() == CSX::Api::MeshBlendingDevBench::StatusSchema().at("properties").size(),
			"status fields do not match the public response shape");
		Require(status.at("available") == true && status.at("persisted") == false &&
					status.at("ruleFileWritable") == false && status.at("ruleFileError") == "read-only policy",
			"status lost availability or policy metadata");
		const auto& counters = status.at("diagnostics");
		Require(counters.is_object() && counters.at("drawCalls") == (std::uint64_t{ 1 } << 40u) &&
					counters.at("frame") == 19u && counters.at("traversalLimitRejects") == 7u && !status.contains("drawCalls"),
			"diagnostics were flattened, truncated or omitted");
		Require(MutationState(feature) == before, "status request mutated feature state");
		feature.ruleFileMutationAllowed.store(true);
		const auto changed = feature.ControlDevBench({ { "settings", { { "NifEnabled", false }, { "BlendWidth", 24.0f } } },
			{ "discoveryCapture", true }, { "resetDiagnostics", true } });
		Require(feature.loads == 1u && feature.captureWrites == 1u && changed.at("discoveryCapture") == true &&
					changed.at("settings").at("NifEnabled") == 0u && changed.at("settings").at("BlendWidth") == 24.0f &&
					changed.at("settings").at("BlendStrength") == 0.75f && changed.at("persisted") == false,
			"valid mixed request did not apply each session change exactly once");
		for (const auto& value : changed.at("diagnostics"))
			Require(value == 0u, "reset did not clear reported diagnostics");
	}

	void TestInvalidRequestsAreAtomic()
	{
		Fixture fixture;
		const auto before = MutationState(fixture.feature);
		for (const auto& request : {
				 json::array(),
				 json{ { "settings", { { "BlendWidth", 24.0f } } }, { "discoveryCapture", "invalid" } },
				 json{ { "settings", { { "BlendWidth", 24.0f } } }, { "unknown", true } },
				 json{ { "settings", { { "Enabled", false }, { "BlendWidth", 0.0f } } },
					 { "discoveryCapture", true }, { "resetDiagnostics", true } } }) {
			bool rejected = false;
			try {
				(void)fixture.feature.ControlDevBench(request);
			} catch (const std::invalid_argument&) {
				rejected = true;
			}
			Require(rejected && MutationState(fixture.feature) == before,
				"invalid request was accepted or partially changed feature state");
		}
	}

	void TestUnavailableMutationGates()
	{
		for (unsigned condition = 0u; condition < 3u; ++condition) {
			Fixture fixture;
			fixture.feature.loaded = condition != 0u;
			fixture.state.blocked = condition == 1u;
			if (condition == 2u)
				globals::state = nullptr;
			const auto before = MutationState(fixture.feature);
			const auto status = fixture.feature.ControlDevBench({ { "resetDiagnostics", false } });
			Require(!status.contains("errorCode") && status.at("available") == fixture.feature.loaded,
				"read-only status was blocked by runtime availability");
			for (const auto& request : {
					 json{ { "settings", { { "BlendWidth", 24.0f } } }, { "discoveryCapture", true }, { "resetDiagnostics", true } },
					 json{ { "discoveryCapture", false } } }) {
				const auto response = fixture.feature.ControlDevBench(request);
				Require(response.at("errorCode") == (condition == 0u ? "feature_unavailable" : "runtime_unavailable") &&
							MutationState(fixture.feature) == before,
					"unavailable runtime admitted a mutation");
			}
		}
	}
}

int main()
{
	try {
		TestStatusAndMutation();
		TestInvalidRequestsAreAtomic();
		TestUnavailableMutationGates();
		std::cout << "Production Mesh DevBench adapter status, mutation and rejection checks passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
