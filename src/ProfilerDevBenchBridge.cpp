#include "ProfilerDevBenchBridge.h"

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "Globals.h"
#	include "Profiler.h"
#	include "State.h"

#	include <DevBenchAPI.h>
#	include <nlohmann/json.hpp>

#	include <atomic>
#	include <chrono>
#	include <functional>
#	include <future>
#	include <memory>
#	include <stdexcept>
#	include <string>

namespace
{
	using json = nlohmann::json;

	constexpr auto kMainThreadTimeout = std::chrono::milliseconds(5000);
	constexpr unsigned int kDevBenchToolExtensionRevision = 5;
	std::atomic_bool g_installAttempted{ false };
	std::atomic_bool g_registered{ false };

	json RunOnMainThread(std::function<json()> a_run)
	{
		auto* taskInterface = SKSE::GetTaskInterface();
		if (!taskInterface)
			return { { "error", "SKSE task interface unavailable" } };

		auto promise = std::make_shared<std::promise<json>>();
		auto cancelled = std::make_shared<std::atomic_bool>(false);
		auto future = promise->get_future();
		taskInterface->AddTask([promise, cancelled, run = std::move(a_run)]() mutable {
			if (cancelled->load(std::memory_order_acquire))
				return;
			try {
				promise->set_value(run());
			} catch (const std::exception& e) {
				promise->set_value(json{ { "error", "main-thread task failed" }, { "detail", e.what() } });
			} catch (...) {
				promise->set_value(json{ { "error", "main-thread task failed" } });
			}
		});

		if (future.wait_for(kMainThreadTimeout) != std::future_status::ready) {
			cancelled->store(true, std::memory_order_release);
			return { { "error", "main thread did not run within 5000ms" } };
		}
		return future.get();
	}

	json BuildStatus(Profiler& a_profiler)
	{
		a_profiler.RequestCapture();

		json timers = json::array();
		for (const auto& result : a_profiler.GetResults()) {
			if (!result.valid)
				continue;
			timers.push_back({
				{ "name", result.name },
				{ "gpuMs", result.gpuTimeMs },
				{ "topLevelMs", result.topLevelMs },
				{ "avgMs", result.avgMs },
				{ "p95Ms", result.p95Ms },
				{ "p99Ms", result.p99Ms },
				{ "cpuMs", result.cpuTimeMs },
				{ "cpuAvgMs", result.cpuAvgMs },
				{ "cpuP95Ms", result.cpuP95Ms },
				{ "cpuP99Ms", result.cpuP99Ms },
				{ "hasGpu", result.hasGpu },
				{ "hasCpu", result.hasCpu },
				{ "activeGpu", result.activeGpu },
				{ "activeCpu", result.activeCpu },
				{ "historyHead", result.historyHead },
				{ "historyCount", result.historyCount },
				{ "cpuHistoryHead", result.cpuHistoryHead },
				{ "cpuHistoryCount", result.cpuHistoryCount },
			});
		}

		return {
			{ "enabled", a_profiler.IsUserEnabled() },
			{ "capturing", a_profiler.IsEnabled() },
			{ "frame_count", globals::state ? globals::state->frameCount : 0u },
			{ "capturedFrameCount", a_profiler.GetCapturedFrameCount() },
			{ "totalMs", a_profiler.GetTotalTimeMs() },
			{ "cpuTotalMs", a_profiler.GetCpuTotalTimeMs() },
			{ "resolvedTotalMs", a_profiler.GetResolvedTotalTimeMs() },
			{ "resolvedCpuTotalMs", a_profiler.GetResolvedCpuTotalTimeMs() },
			{ "acquiredSlots", a_profiler.GetAcquiredSlots() },
			{ "peakAcquiredSlots", a_profiler.GetPeakAcquiredSlots() },
			{ "slotRefusals", a_profiler.GetSlotRefusals() },
			{ "maxTimers", Profiler::kMaxTimers },
			{ "timers", std::move(timers) },
		};
	}

	json BuildProfilerResult(const json& a_args)
	{
		const std::string action = a_args.value("action", std::string("status"));
		if (action != "status" && action != "enable" && action != "disable") {
			return {
				{ "error", "unknown action" },
				{ "action", action },
				{ "supported", json::array({ "status", "enable", "disable" }) },
			};
		}

		return RunOnMainThread([action]() -> json {
			auto* profiler = globals::profiler;
			if (!profiler)
				return { { "error", "profiler unavailable" } };
			if (action == "enable") {
				profiler->SetUserEnabled(true);
				return { { "action", "enable" }, { "enabled", true } };
			}
			if (action == "disable") {
				profiler->SetUserEnabled(false);
				return { { "action", "disable" }, { "enabled", false } };
			}
			return { { "action", "status" }, { "status", BuildStatus(*profiler) } };
		});
	}

	void RunHandler(
		json (*a_build)(const json&),
		const char* a_argsJson,
		void* a_sink,
		DevBenchAPI::WriteFn a_write) noexcept
	{
		json output;
		try {
			json args = json::object();
			if (a_argsJson && *a_argsJson)
				args = json::parse(a_argsJson);
			if (!args.is_object())
				throw std::runtime_error("arguments must be a JSON object");
			output = a_build(args);
		} catch (const std::exception& e) {
			output = { { "error", "invalid request" }, { "detail", e.what() } };
		} catch (...) {
			output = { { "error", "unknown handler error" } };
		}

		try {
			const std::string serialized = output.dump();
			a_write(a_sink, serialized.c_str());
		} catch (...) {
			a_write(a_sink, R"({"error":"response serialization failed"})");
		}
	}

	void ProfilerToolHandler(
		void*,
		const char* a_argsJson,
		void* a_sink,
		DevBenchAPI::WriteFn a_write)
	{
		RunHandler(&BuildProfilerResult, a_argsJson, a_sink, a_write);
	}

	void ProfilerInspectExtensionHandler(
		void*,
		const char*,
		void* a_sink,
		DevBenchAPI::WriteFn a_write)
	{
		json result{
			{ "registered", g_registered.load(std::memory_order_acquire) },
			{ "tool", "communityshaders.profiler" },
			{ "usage", R"(Invoke the top-level devbench tool with {"action":"status"} when exposed. If the client has not exposed dynamic tools, dispatch it through devbench scenario with a tool step: {"tool":"communityshaders.profiler","args":{"action":"status"}}.)" },
			{ "actions", json::array({ "status", "enable", "disable" }) },
		};
		const auto serialized = result.dump();
		a_write(a_sink, serialized.c_str());
	}

	DevBenchAPI::IDevBenchInterface001* GetDevBenchToolExtensionInterface()
	{
		const auto messaging = SKSE::GetMessagingInterface();
		if (!messaging)
			return nullptr;

		DevBenchAPI::DevBenchMessage message;
		messaging->Dispatch(
			DevBenchAPI::DevBenchMessage::kMessage_GetInterface,
			&message,
			sizeof(DevBenchAPI::DevBenchMessage*),
			DevBenchAPI::DevBenchPluginName);
		if (!message.GetApiFunction)
			return nullptr;

		return static_cast<DevBenchAPI::IDevBenchInterface001*>(
			message.GetApiFunction(kDevBenchToolExtensionRevision));
	}
}

namespace ProfilerDevBenchBridge
{
	void Install()
	{
		if (g_installAttempted.exchange(true, std::memory_order_acq_rel))
			return;

		auto* devBench = DevBenchAPI::GetDevBenchInterface001();
		if (!devBench) {
			logger::info("ProfilerDevBenchBridge: devbench host not present; profiler tool not registered");
			return;
		}

		static constexpr const char* descriptor =
			R"({"description":"Inspect and control the CSX GPU/CPU profiler. status requests a capture and returns live UI totals, resolve-consistent totals, the captured engine frame, timer capacity diagnostics, and per-timer current and rolling values. Results normally resolve after three frames and can be older while capture is idle or the game is paused; compare frame_count with capturedFrameCount. Use resolvedTotalMs with topLevelMs for nesting-correct totals. enable and disable change the profiling preference.","inputSchema":{"type":"object","properties":{"action":{"type":"string","enum":["status","enable","disable"],"default":"status"}}}})";
		devBench->RegisterTool(
			"communityshaders.profiler",
			descriptor,
			&ProfilerToolHandler,
			nullptr);
		if (devBench->GetBuildNumber() >= 10500) {
			static constexpr const char* inspectDescriptor =
				R"({"description":"Reports the Community Shaders profiler diagnostic tool registration and points callers to the top-level communityshaders.profiler tool."})";
			if (auto* extensionDevBench = GetDevBenchToolExtensionInterface()) {
				const bool inserted = extensionDevBench->RegisterToolExtension(
					"inspect",
					"communityshaders.profiler",
					inspectDescriptor,
					&ProfilerInspectExtensionHandler,
					nullptr);
				logger::info(
					"ProfilerDevBenchBridge: registered inspect extension communityshaders.profiler with devbench build {}{}",
					extensionDevBench->GetBuildNumber(),
					inserted ? "" : " (replaced existing handler)");
			} else {
				logger::warn("ProfilerDevBenchBridge: devbench revision-5 interface unavailable; inspect extension not registered");
			}
		}
		g_registered.store(true, std::memory_order_release);
		logger::info(
			"ProfilerDevBenchBridge: registered communityshaders.profiler with devbench build {}",
			devBench->GetBuildNumber());
	}

	bool IsBuilt()
	{
		return true;
	}

	bool IsRegistered()
	{
		return g_registered.load(std::memory_order_acquire);
	}
}

#else

namespace ProfilerDevBenchBridge
{
	void Install() {}

	bool IsBuilt()
	{
		return false;
	}

	bool IsRegistered()
	{
		return false;
	}
}

#endif
