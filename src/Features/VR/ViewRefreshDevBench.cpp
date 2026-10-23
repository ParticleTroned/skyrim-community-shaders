#include "ViewRefresh.h"

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "Api/DevBenchMainThreadDispatch.h"
#	include "BuildProvenance.h"
#	include <DevBenchAPI.h>
#	include <nlohmann/json.hpp>

namespace CSX::ViewRefresh
{
	namespace
	{
		using json = nlohmann::json;

		json Execute(const json& args)
		{
			if (!args.is_object())
				return { { "error", "Expected an object" } };
			for (const auto& [key, value] : args.items()) {
				if (key != "action" && key != "enabled" && key != "sceneReuse" && key != "latePose" && key != "expectedBuildId")
					return { { "error", "Unknown property: " + key } };
				if ((key == "enabled" || key == "sceneReuse" || key == "latePose") && !value.is_boolean())
					return { { "error", key + " must be boolean" } };
				if ((key == "action" || key == "expectedBuildId") && !value.is_string())
					return { { "error", key + " must be a string" } };
			}
			const auto action = args.value("action", std::string("status"));
			if (action == "status" && (args.contains("enabled") || args.contains("sceneReuse") || args.contains("latePose")))
				return { { "error", "Settings require the configure action" } };
			if (action == "configure") {
				if (!REL::Module::IsVR())
					return { { "error", "View Refresh requires Skyrim VR" } };
				auto settings = GetSettings();
				settings.enabled = args.value("enabled", settings.enabled);
				settings.reuseScenes = args.value("sceneReuse", settings.reuseScenes);
				settings.latePose = args.value("latePose", settings.latePose);
				Configure(settings);
			} else if (action != "status") {
				return { { "error", "action must be status or configure" } };
			}
			return { { "status", Status() } };
		}

		void Handler(void*, const char* text, void* sink, DevBenchAPI::WriteFn write) noexcept
		{
			json output;
			try {
				const auto args = text && *text ? json::parse(text) : json::object();
				if (auto mismatch = BuildProvenance::ValidateExpectedBuild(args))
					output = std::move(*mismatch);
				else
					output = CSX::Api::RunDevBenchMainThreadTask(SKSE::GetTaskInterface(), [args] { return Execute(args); });
			} catch (const std::exception& error) {
				output = { { "error", error.what() } };
			} catch (...) {
				output = { { "error", "View Refresh request failed" } };
			}
			try {
				BuildProvenance::AttachProducer(output);
				const auto serialized = output.dump();
				write(sink, serialized.c_str());
			} catch (...) {
				write(sink, R"({"error":"View Refresh response serialization failed"})");
			}
		}
	}

	void InstallDevBench()
	{
		auto* host = DevBenchAPI::GetDevBenchInterface001();
		if (!host)
			return;
		static constexpr const char* descriptor = R"({"description":"Inspect or configure experimental CSX View Refresh in Skyrim VR. enabled controls the feature; sceneReuse allows exactly one headset-pose-refreshed display frame between fresh world renders while stationary; latePose additionally corrects freshly rendered frames on SteamVR. Reuse supports the game's existing SteamVR/OpenComposite pose cycle, never adds pose waits or a submit thread, and requires a complete accepted stereo source no older than 35 ms. Stable native resolution without Neural Rendering is required; scene reuse additionally requires engine TAA to be off. Menus, overlays, locomotion, large pose deltas, transitions and invalid resources force fresh rendering. World animation and hands update at scene cadence. configure changes current settings and retries latched graphics failures; identical healthy settings preserve allocations and history. Settings properties require configure. Disabling releases graphics after any prepared pair finishes. Normal CSX settings saving persists preferences. status reports admission reason and actual completed rendered, reused, corrected and failed stereo pair counts. No performance improvement or visual qualification is implied by counters. expectedBuildId rejects a different producer.","inputSchema":{"type":"object","additionalProperties":false,"required":["action"],"properties":{"action":{"type":"string","enum":["status","configure"]},"enabled":{"type":"boolean"},"sceneReuse":{"type":"boolean"},"latePose":{"type":"boolean"},"expectedBuildId":{"type":"string"}}},"outputSchema":{"type":"object","properties":{"status":{"type":"object","properties":{"enabled":{"type":"boolean"},"sceneReuse":{"type":"boolean"},"latePose":{"type":"boolean"},"reason":{"type":"string"},"sourceReady":{"type":"boolean"},"sourceFrame":{"type":"integer"},"sourceCycle":{"type":"integer"},"poseCycle":{"type":"integer"},"renderedPairs":{"type":"integer"},"reusedPairs":{"type":"integer"},"correctedPairs":{"type":"integer"},"failedPairs":{"type":"integer"}}},"error":{"type":"string"}}}})";
		host->RegisterTool("communityshaders.view_refresh", descriptor, &Handler, nullptr);
	}
}
#else
namespace CSX::ViewRefresh
{
	void InstallDevBench() {}
}
#endif
