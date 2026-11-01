#include "ProfilerDevBenchBridge.h"

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "Api/DevBenchMainThreadDispatch.h"
#	include "BuildProvenance.h"
#	include "Api/AcceptedDrawService.h"
#	include "Api/FeatureService.h"
#	include "Diagnostics/DevBenchCpuSnapshot.h"
#	include "Features/PerformanceOverlay.h"
#	include "Globals.h"
#	include "PresetCompatibility.h"
#	include "Profiler.h"
#	include "State.h"
#	include "Utils/FeatureProfiling.h"

#	include <DevBenchAPI.h>
#	include <nlohmann/json.hpp>

#	include <atomic>
#	include <exception>
#	include <functional>
#	include <stdexcept>
#	include <string>

namespace
{
	using json = nlohmann::json;

	constexpr unsigned int kDevBenchToolExtensionRevision = 5;
	std::atomic_bool g_installAttempted{ false };
	std::atomic_bool g_registered{ false };

	json RunOnMainThread(std::function<json()> a_run)
	{
		return CSX::Api::RunDevBenchMainThreadTask(SKSE::GetTaskInterface(), std::move(a_run));
	}

	json BuildStatus(Profiler& a_profiler)
	{
		a_profiler.RequestCapture(Profiler::CaptureMode::DetailedPasses);

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

		json externalGpuTimings = json::array();
		for (const auto& timing : a_profiler.GetExternalGpuTimings()) {
			const auto statistics = timing.history.GetStatistics();
			externalGpuTimings.push_back({ { "name", timing.name }, { "frameId", timing.frameId },
				{ "gpuMs", timing.history.lastMs }, { "avgMs", statistics.average },
				{ "p95Ms", statistics.p95 }, { "p99Ms", statistics.p99 },
				{ "historyCount", timing.history.count } });
		}
		json featureViews = json::array();
		for (const auto& view : Util::FeatureProfiling::views) {
			json ownedPrefixes = json::array();
			for (auto prefix : view.ownedPrefixes)
				ownedPrefixes.push_back(prefix);
			json sharedPrefixes = json::array();
			for (auto prefix : view.sharedPrefixes)
				sharedPrefixes.push_back(prefix);
			featureViews.push_back({ { "feature", view.feature },
				{ "ownedRoot", view.ownedRoot },
				{ "ownedPrefixes", std::move(ownedPrefixes) },
				{ "ownedGpu", view.HasOwnedTimings(false) },
				{ "ownedCpu", view.HasOwnedTimings(true) },
				{ "sharedPrefixes", std::move(sharedPrefixes) },
				{ "sharedTimingsAreFeatureCost", false },
				{ "coverage", view.coverage } });
		}

		return {
			{ "timingSemantics", "gpu_cpu_self_time" },
			{ "wholeFrame", {
				{ "gpuMs", a_profiler.HasLatestWholeFrameGpuSample() ? json(a_profiler.GetWholeFrameGpuTimeMs()) : json(nullptr) },
				{ "cpuMs", a_profiler.HasLatestWholeFrameCpuSample() ? json(a_profiler.GetWholeFrameCpuTimeMs()) : json(nullptr) },
				{ "sampleId", a_profiler.GetWholeFrameSampleId() },
				{ "gpuSampleId", a_profiler.GetWholeFrameGpuSampleId() },
				{ "cpuSampleId", a_profiler.GetWholeFrameCpuSampleId() },
				{ "presentIntervalSampleId", a_profiler.GetWholeFramePresentIntervalSampleId() },
				{ "skippedCaptures", a_profiler.GetSkippedWholeFrameCaptureCount() },
				{ "semantics", "independent D3D11 whole-frame GPU and QPC render-to-Present CPU" } } },
			{ "presentation", {
				{ "intervalMs", a_profiler.HasLatestPresentIntervalSample() ? json(a_profiler.GetPresentIntervalMs()) : json(nullptr) },
				{ "sampleId", a_profiler.GetPresentIntervalSampleId() },
				{ "synced", a_profiler.WasLatestPresentIntervalSynced() },
				{ "discontinuityEpoch", a_profiler.GetPresentDiscontinuityEpoch() },
				{ "semantics", "accepted game Present-to-Present QPC interval; test and occluded presents are excluded" } } },
			{ "externalGpuCaptureActive", a_profiler.GetGpuCaptureEpoch() != 0 },
			{ "externalGpuTimings", std::move(externalGpuTimings) },
			{ "externalGpuTimingsAreIncludedInTotals", false },
			{ "featureViews", std::move(featureViews) },
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

	json BuildAcceptedDrawSnapshot()
	{
		const auto status = CSX::Api::InspectAcceptedDrawService();
		return {
			{ "action", "accepted_draws" }, { "ready", status.ready },
			{ "subscribers", status.registry.subscribers }, { "events", status.registry.events },
			{ "callbacks", status.registry.callbacks }, { "replays", status.registry.replays },
			{ "rejected_replays", status.registry.rejectedReplays }, { "observer_faults", status.registry.observerFaults },
			{ "filtered_draws", status.filteredDraws }, { "wrong_thread_draws", status.wrongThreadDraws },
			{ "geometry_scope_errors", status.geometryScopeErrors }
		};
	}

	json BuildCpuBurstSnapshot()
	{
		return RunOnMainThread([]() -> json {
			LARGE_INTEGER begin{}, end{}, frequency{};
			if (!QueryPerformanceCounter(&begin) || !QueryPerformanceFrequency(&frequency))
				throw std::runtime_error("diagnostic clock unavailable");
			const auto* api = CSX::Api::GetFeatureService001();
			if (!api)
				throw std::runtime_error("feature service unavailable");
			std::array<CSX::Api::AcceptedDrawRegistry::ObserverSnapshot, 8> observers;
			const bool observerSnapshotComplete = CSX::Api::InspectAcceptedDrawObservers(observers);
			json observerIdentities = json::array();
			for (const auto& observer : observers) {
				if (observer.subscription != 0)
					observerIdentities.push_back({ { "subscription", observer.subscription }, { "callbackAddress", observer.callbackAddress } });
			}
			json result{
				{ "action", "cpu_burst_snapshot" }, { "schema", "csx-cpu-burst-snapshot-v1" },
				{ "devbenchOnly", true }, { "processId", GetCurrentProcessId() },
				{ "mainThreadId", GetCurrentThreadId() }, { "runtime", "se_ae" },
				{ "frame", globals::state ? globals::state->frameCount : 0u },
				{ "qpcBegin", begin.QuadPart }, { "qpcFrequency", frequency.QuadPart },
				{ "featureState", CSX::Diagnostics::CaptureFeatureSettings(*api) },
				{ "presetCompatibility", PresetCompatibility::ToJson(PresetCompatibility::GetPublished()) },
				{ "acceptedDraws", BuildAcceptedDrawSnapshot() },
				{ "acceptedDrawObserversComplete", observerSnapshotComplete },
				{ "acceptedDrawObservers", std::move(observerIdentities) },
				{ "profilerCapturing", globals::profiler && globals::profiler->IsEnabled() },
			};
			if (!QueryPerformanceCounter(&end))
				throw std::runtime_error("diagnostic clock unavailable");
			result["qpcEnd"] = end.QuadPart;
			return result;
		});
	}



	json BuildProfilerResult(const json& a_args)
	{
		const std::string action = a_args.value("action", std::string("status"));
		if (action == "render_targets") {
			if (a_args.contains("enabled") && !a_args.at("enabled").is_boolean())
				return { { "error", "enabled must be a boolean" } };
			return RunOnMainThread([a_args] {
				auto& overlay = globals::features::performanceOverlay;
				if (a_args.contains("enabled"))
					overlay.SetRenderTargetsEnabled(a_args.at("enabled").get<bool>());
				return json{ { "action", "render_targets" }, { "renderTargets", overlay.GetRenderTargetDiagnostics() } };
			});
		}
		if (action == "accepted_draws")
			return BuildAcceptedDrawSnapshot();
		if (action == "cpu_burst_snapshot")
			return BuildCpuBurstSnapshot();
		if (action != "status" && action != "enable" && action != "disable") {
			return {
				{ "error", "unknown action" },
				{ "action", action },
				{ "supported", json::array({ "status", "enable", "disable", "accepted_draws", "cpu_burst_snapshot", "render_targets" }) },
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
			if (auto mismatch = BuildProvenance::ValidateExpectedBuild(args))
				output = std::move(*mismatch);
			else
				output = a_build(args);
		} catch (const std::exception& e) {
			output = { { "error", "invalid request" }, { "detail", e.what() } };
		} catch (...) {
			output = { { "error", "unknown handler error" } };
		}

		BuildProvenance::AttachProducer(output);
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
			{ "actions", json::array({ "status", "enable", "disable", "accepted_draws", "cpu_burst_snapshot", "render_targets" }) },
		};
		BuildProvenance::AttachProducer(result);
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
			R"json({"description":"Inspect and control the SE/AE GPU/CPU profiler. Named timers report self time with profiled descendants excluded; topLevelMs and resolvedTotalMs retain inclusive depth-zero GPU time. CPU totals sum named CPU self time. Separate wholeFrame and presentation objects report real asynchronous D3D11 whole-frame and QPC accepted-Present measurements; these are not sums of named passes. Invalid or missing timing samples are null and retain explicit sample identity. render_targets inspects approximate D3D11 engine-target intervals and optionally stages ShowRenderTargets without saving settings. Collection requires profiling and a visible overlay table or an open profiling target section; closing both releases target queries. Target intervals include intervening work and waits and are excluded from pass totals. cpu_burst_snapshot copies feature settings, preset compatibility, accepted-draw counters and QPC/process/thread identity on the main thread without enabling profiling or stack recording. accepted_draws reports event readiness, callbacks, replays and faults without enabling capture. External NR D3D12 GPU durations sum dispatches and regions for each sampled frame, are excluded from D3D11 totals, and allocate no timestamp resources while GPU capture is inactive. Every response identifies the producing DLL; expectedBuildId rejects stale binaries.","inputSchema":{"type":"object","properties":{"action":{"type":"string","enum":["status","enable","disable","accepted_draws","cpu_burst_snapshot","render_targets"],"default":"status","description":"Status includes owned and shared feature timing coverage. Shared stages include other features and cannot isolate incremental feature cost. Named NR timing excludes ordinary DLSS/FSR. The separate wholeFrame and presentation samples preserve the SE measurement source identity."},"enabled":{"type":"boolean","description":"Optional for render_targets; stages its display/capture toggle without saving settings."},"expectedBuildId":{"type":"string","description":"Exact 64-character CSX Build ID required for this operation."}}},"outputSchema":{"type":"object","properties":{"renderTargets":{"type":"object","properties":{"enabled":{"type":"boolean"},"overlayEnabled":{"type":"boolean"},"profilingEnabled":{"type":"boolean"},"active":{"type":"boolean"},"allocatedQueries":{"type":"integer","minimum":0},"coverage":{"type":"string"},"includedInPassTotals":{"type":"boolean","const":false},"resolvedFrames":{"type":"integer","minimum":0},"invalidFrames":{"type":"integer","minimum":0},"gpuSampleValid":{"type":"boolean"},"timedGpuMs":{"type":["number","null"]},"spanLimitHit":{"type":"boolean"},"bindingLimitHit":{"type":"boolean"},"resourceFailure":{"type":"boolean"},"rows":{"type":"array","items":{"type":"object","properties":{"name":{"type":"string"},"depthStencil":{"type":"integer"},"renderTarget":{"type":"integer"},"drawCalls":{"type":"number"},"gpuMs":{"type":["number","null"]}}}}}},"featureViews":{"type":"array","items":{"type":"object","properties":{"feature":{"type":"string"},"ownedRoot":{"type":"string"},"ownedPrefixes":{"type":"array","items":{"type":"string"},"description":"Additional owned timer prefixes outside ownedRoot. These use selected self-time subtotals in the feature UI."}}}},"status":{"type":"object","properties":{"externalGpuCaptureActive":{"type":"boolean"},"externalGpuTimingsAreIncludedInTotals":{"type":"boolean","const":false},"externalGpuTimings":{"type":"array","items":{"type":"object","properties":{"name":{"type":"string"},"frameId":{"type":"integer"},"gpuMs":{"type":"number"},"avgMs":{"type":"number"},"p95Ms":{"type":"number"},"p99Ms":{"type":"number"},"historyCount":{"type":"integer"}}}}}}}}})json";
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
