#include "Diagnostics/D3DTextureLifetimeDevBenchBridge.h"

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "Api/DevBenchMainThreadDispatch.h"
#	include "Api/ServiceFoundation.h"
#	include "BuildProvenance.h"
#	include "Diagnostics/D3DTextureLifetimeTracker.h"
#	include <DevBenchAPI.h>
#	include <atomic>
#	include <functional>
#	include <mutex>
#	include <set>
#	include <stdexcept>
#	include <string>
#	include <string_view>

namespace
{
	using json = nlohmann::json;
	namespace Tracker = Diagnostics::D3DTextureLifetimeTracker;
	std::atomic_bool registered{ false };

	CSX::Api::ServiceFoundation& Foundation()
	{
		static CSX::Api::ServiceFoundation foundation({ "communityshaders.texture_lifetime", 1, 0, 1 });
		static std::once_flag metadata;
		std::call_once(metadata, [&] { foundation.SetServerMetadataProvider([] { return BuildProvenance::GetProducer(); }); });
		return foundation;
	}

	json Handle(const json& args)
	{
		const auto invalid = [&](std::string message, std::string field) {
			return Foundation().MakeError(args, "invalid_request", std::move(message), "validation", false, std::move(field));
		};
		static const std::set<std::string_view> allowed{
			"contractMajor", "contractMinor", "clientId", "commandId", "expectedBuildId", "action", "expectedSessionID"
		};
		for (const auto& item : args.items())
			if (!allowed.contains(item.key()))
				return invalid("unsupported field: " + item.key(), item.key());
		if (args.contains("contractMinor") && (!args.at("contractMinor").is_number_integer() || args.at("contractMinor") != 0))
			return invalid("contractMinor must be the integer 0", "contractMinor");
		const auto action = args.value("action", std::string("status"));
		if (action != "start" && action != "status" && action != "checkpoint" && action != "stop" && action != "reset")
			return invalid("unknown action", "action");
		std::uint64_t expected = 0;
		if (args.contains("expectedSessionID")) {
			const auto& value = args.at("expectedSessionID");
			if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<std::int64_t>() <= 0))
				return invalid("expectedSessionID must be a positive integer", "expectedSessionID");
			expected = value.get<std::uint64_t>();
			if (!expected)
				return invalid("expectedSessionID must be a positive integer", "expectedSessionID");
		}
		if (action != "status" && action != "start" && !expected)
			return invalid("checkpoint, stop and reset require expectedSessionID", "expectedSessionID");
		const auto result = CSX::Api::RunDevBenchMainThreadTask(SKSE::GetTaskInterface(), [action, expected] {
			bool accepted = true;
			if (action == "start")
				accepted = Tracker::Start();
			else if (action == "stop")
				accepted = Tracker::Stop(expected);
			else if (action == "reset")
				accepted = Tracker::Reset(expected);
			else if (action == "checkpoint")
				accepted = Tracker::Checkpoint(expected);
			return json{ { "action", action }, { "accepted", accepted }, { "capture", Tracker::BuildStatus() } };
		});
		if (result.contains("error"))
			return Foundation().MakeError(args, "main_thread_dispatch_failed", result.dump(), "execution", true);
		auto response = result.at("accepted").get<bool>() ? Foundation().MakeEnvelope(args, true) :
		                                                    Foundation().MakeError(args, "capture_state_conflict", "Capture state or expectedSessionID does not permit this action.", "execution");
		response["result"] = result;
		return response;
	}

	void ToolHandler(void*, const char* input, void* sink, DevBenchAPI::WriteFn write) noexcept
	{
		try {
			const std::string_view text = input ? input : "";
			if (text.size() > 32768)
				throw std::invalid_argument("request exceeds 32 KiB");
			const auto args = json::parse(text);
			if (auto mismatch = BuildProvenance::ValidateExpectedBuild(args)) {
				const auto response = Foundation().MakeError(args, "producer_mismatch", "loaded build differs from expectedBuildId").dump();
				write(sink, response.c_str());
				return;
			}
			const auto response = Foundation().Dispatch(args, &Handle).dump();
			write(sink, response.c_str());
		} catch (const std::exception& error) {
			try {
				const auto response = Foundation().MakeError(json::object(), "invalid_request", error.what()).dump();
				write(sink, response.c_str());
			} catch (...) {
				write(sink, R"({"ok":false,"error":{"code":"serialization_failed"}})");
			}
		} catch (...) {
			write(sink, R"({"ok":false,"error":{"code":"internal_error"}})");
		}
	}
}
#endif

namespace D3DTextureLifetimeDevBenchBridge
{
	void Install()
	{
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (registered.load(std::memory_order_acquire))
			return;
		auto* host = DevBenchAPI::GetDevBenchInterface001();
		if (!host)
			return;
		constexpr const char* descriptor = R"json({
            "description":"Opt-in SE/AE D3D11 texture-lifetime capture. A private COM sentinel records creation and final release for the hooked game D3D11 device without retaining the texture. Counts cover successfully tracked objects; capacity drops and attachment failures are explicit. Stop freezes statistics; old-session sentinels cannot mutate a later capture. Checkpoint advances the cohort used to identify retained older allocations. Native ownership enrichment traverses bounded reachable world/preview materials on the main thread under renderer ownership, with a four-millisecond work budget. Unmatched textures have unknown ownership, not proven leaks. Typed FaceGen tint materials are correlated when reachable; raw assignment callsites and exact head-part identity are not instrumented. Creation-origin observations are historical, bounded, and retain no Ni/COM references. SDK/driver allocations and D3D12 frame-generation or neural-rendering resources are outside this hook. All commands use the main-thread dispatcher and per-command idempotency. Checkpoint/stop/reset require the sessionID returned by capture status; reset requires the capture to be stopped.",
            "inputSchema":{"type":"object","additionalProperties":false,"required":["contractMajor","clientId","commandId","action"],"properties":{
                "contractMajor":{"type":"integer","const":1},"contractMinor":{"type":"integer","const":0},
                "clientId":{"type":"string","minLength":1,"maxLength":128},"commandId":{"type":"string","minLength":1,"maxLength":128},
                "expectedBuildId":{"type":"string"},"action":{"type":"string","enum":["start","status","checkpoint","stop","reset"]},
                "expectedSessionID":{"type":"integer","minimum":1}
            }}
        })json";
		host->RegisterTool("communityshaders.texture_lifetime", descriptor, &ToolHandler, nullptr);
		registered.store(true, std::memory_order_release);
#endif
	}
}
