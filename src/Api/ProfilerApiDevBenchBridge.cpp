#include "Api/ProfilerApiDevBenchBridge.h"

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "Api/DevBenchMainThreadDispatch.h"
#	include "Api/ProfilerService.h"
#	include "Api/ServiceFoundation.h"
#	include "BuildProvenance.h"

#	include <DevBenchAPI.h>
#	include <nlohmann/json.hpp>

#	include <atomic>
#	include <exception>
#	include <functional>
#	include <limits>
#	include <mutex>
#	include <set>
#	include <stdexcept>
#	include <string>

namespace
{
	using json = nlohmann::json;
	using CSX::ProfilerAPI::CaptureMode;
	using CSX::ProfilerAPI::CaptureProgress001;
	using CSX::ProfilerAPI::CaptureRequest001;
	using CSX::ProfilerAPI::CaptureState;
	using CSX::ProfilerAPI::CpuSnapshot001;
	using CSX::ProfilerAPI::Snapshot001;
	using CSX::ProfilerAPI::Status;
	using CSX::ProfilerAPI::TimerDescriptor001;
	using CSX::ProfilerAPI::TimingDomain;
	std::atomic_bool g_registered{ false };
	std::mutex g_terminalEventMutex;
	std::set<std::uint64_t> g_reportedTerminalCaptures;

	CSX::Api::ServiceFoundation& Foundation()
	{
		static CSX::Api::ServiceFoundation foundation({ CSX::ProfilerAPI::ServiceName,
			CSX::ProfilerAPI::ServiceMajor, CSX::ProfilerAPI::SourceServiceMinor, CSX::ProfilerAPI::SourceSchemaRevision });
		static std::once_flag metadataInitialized;
		std::call_once(metadataInitialized, [&] {
			foundation.SetServerMetadataProvider([] {
				auto producer = BuildProvenance::GetProducer();
				producer["serviceSessionId"] = Foundation().SessionId();
				return producer;
			});
		});
		return foundation;
	}

	const char* StatusName(Status a_status)
	{
		switch (a_status) {
		case Status::kSuccess: return "success";
		case Status::kInvalidArgument: return "invalid_argument";
		case Status::kStructureTooSmall: return "structure_too_small";
		case Status::kUnavailable: return "unavailable";
		case Status::kWrongThread: return "wrong_thread";
		case Status::kDisabled: return "disabled";
		case Status::kBusy: return "busy";
		case Status::kCaptureNotFound: return "capture_not_found";
		case Status::kTimerNotFound: return "timer_not_found";
		default: return "internal_error";
		}
	}

	const char* CaptureStateName(CaptureState a_state)
	{
		switch (a_state) {
		case CaptureState::kRunning: return "running";
		case CaptureState::kCompleted: return "completed";
		case CaptureState::kCancelled: return "cancelled";
		default: return "none";
		}
	}

	json RunOnMainThread(std::function<json()> a_run)
	{
		return CSX::Api::RunDevBenchMainThreadTask(SKSE::GetTaskInterface(), std::move(a_run), CSX::Api::DevBenchDispatchErrorFormat::profiler);
	}

	json ProgressJson(const CaptureProgress001& a_progress)
	{
		return {
			{ "captureId", a_progress.captureId },
			{ "state", CaptureStateName(a_progress.state) },
			{ "requestedFrames", a_progress.requestedFrames },
			{ "submittedFrames", a_progress.submittedFrames },
			{ "resolvedFrames", a_progress.resolvedFrames },
		};
	}

	json ApiFailure(Status a_status)
	{
		return { { "_apiError", StatusName(a_status) }, { "_apiStatus", static_cast<std::uint32_t>(a_status) } };
	}

	json ReadSnapshot(const CSX::ProfilerAPI::Interface001& a_api)
	{
		Snapshot001 snapshot;
		const auto status = a_api.GetSnapshot(a_api.context, &snapshot);
		if (status != Status::kSuccess && status != Status::kUnavailable)
			return ApiFailure(status);
		return {
			{ "status", StatusName(status) },
			{ "available", snapshot.available != 0 },
			{ "enabled", snapshot.enabled != 0 },
			{ "capturing", snapshot.capturing != 0 },
			{ "timerCount", snapshot.timerCount },
			{ "limits", { { "historyCapacity", snapshot.historyCapacity }, { "maximumTimers", snapshot.maximumTimers }, { "frameLatency", snapshot.frameLatency } } },
			{ "frame", { { "captured", snapshot.capturedFrameCount }, { "acquiredSlots", snapshot.acquiredSlots }, { "peakAcquiredSlots", snapshot.peakAcquiredSlots }, { "slotRefusals", snapshot.slotRefusals } } },
			{ "totalsMs", { { "gpu", snapshot.gpuTotalMs }, { "cpu", snapshot.cpuTotalMs }, { "resolvedGpu", snapshot.resolvedGpuTotalMs }, { "resolvedCpu", snapshot.resolvedCpuTotalMs } } },
			{ "capabilities", snapshot.capabilities },
			{ "buildId", snapshot.buildId ? snapshot.buildId : "" },
		};
	}

	json ReadCpuSnapshot(const CSX::ProfilerAPI::Interface002& a_api)
	{
		CpuSnapshot001 snapshot;
		const auto status = a_api.GetCpuSnapshot(a_api.paired.context, &snapshot);
		if (status != Status::kSuccess && status != Status::kUnavailable)
			return ApiFailure(status);
		return {
			{ "status", StatusName(status) }, { "view", "independent_cpu" },
			{ "available", snapshot.available != 0 }, { "enabled", snapshot.enabled != 0 },
			{ "capturing", snapshot.capturing != 0 }, { "timerCount", snapshot.timerCount },
			{ "capturedFrameCount", snapshot.capturedFrameCount }, { "publicationCount", snapshot.publicationCount },
			{ "historyCapacity", snapshot.historyCapacity }, { "maximumTimersPerFrame", snapshot.maximumTimersPerFrame },
			{ "slotRefusals", snapshot.slotRefusals }, { "resolvedTotalMs", snapshot.resolvedTotalMs },
			{ "buildId", snapshot.buildId ? snapshot.buildId : "" }
		};
	}

	json ReadProgress(const CSX::ProfilerAPI::Interface001& a_api, std::uint64_t a_captureId)
	{
		CaptureProgress001 progress;
		const auto status = a_api.GetCaptureProgress(a_api.context, a_captureId, &progress);
		if (status != Status::kSuccess)
			return ApiFailure(status);
		if (progress.state == CaptureState::kCompleted || progress.state == CaptureState::kCancelled) {
			std::lock_guard lock(g_terminalEventMutex);
			if (g_reportedTerminalCaptures.insert(progress.captureId).second) {
				Foundation().AppendEvent(std::to_string(progress.captureId), 1,
					progress.state == CaptureState::kCompleted ? "capture.completed" : "capture.cancelled",
					ProgressJson(progress));
			}
		}
		return ProgressJson(progress);
	}

	json BuildResult(const json& a_args)
	{
		const auto action = a_args.value("action", std::string{});
		const bool cpuView = action == "cpu_snapshot" || action == "cpu_timers" || action == "cpu_history";
		const bool known = action == "registry" || action == "snapshot" || action == "timers" || action == "history" ||
		                   action == "set_enabled" || action == "clear_history" || action == "start_capture" ||
		                   action == "capture_status" || action == "cancel_capture" || action == "events" || action == "acknowledge_events" ||
		                   cpuView || action == "request_capture";
		if (!known)
			return Foundation().MakeError(a_args, "unknown_action", "action is not supported", "validation", false, "action");

		if (action == "registry") {
			auto response = Foundation().MakeEnvelope(a_args, true);
			response["result"] = {
				{ "service", CSX::ProfilerAPI::ServiceName },
				{ "major", CSX::ProfilerAPI::ServiceMajor },
				{ "minor", CSX::ProfilerAPI::SourceServiceMinor },
				{ "schemaRevision", CSX::ProfilerAPI::SourceSchemaRevision },
				{ "timingSemantics", "gpu_cpu_self_time" },
				{ "capabilities", CSX::ProfilerAPI::ServiceCapabilities | CSX::ProfilerAPI::kCapabilityIndependentCpu },
				{ "mainThreadAffine", true },
				{ "registryMainThreadAffine", false },
				{ "capture", { { "minimumFrames", 1 }, { "maximumFrames", 300 }, { "singleActiveSession", true }, { "requiresEnabled", true } } },
				{ "actions", json::array({ "registry", "snapshot", "timers", "history", "set_enabled", "clear_history", "start_capture", "capture_status", "cancel_capture", "events", "acknowledge_events", "request_capture", "cpu_snapshot", "cpu_timers", "cpu_history" }) },
			};
			return response;
		}

		if (action == "events" || action == "acknowledge_events") {
			auto response = Foundation().MakeEnvelope(a_args, true);
			if (action == "events") {
				std::string captureFilter;
				if (const auto found = a_args.find("captureId"); found != a_args.end() && found->is_number_unsigned())
					captureFilter = std::to_string(found->get<std::uint64_t>());
				response["result"] = Foundation().PollEvents(a_args.value("afterEventId", 0ull), a_args.value("limit", 100u), captureFilter);
			} else {
				response["result"] = { { "acknowledgedThroughEventId", Foundation().AcknowledgeEvents(a_args.value("throughEventId", 0ull)) }, { "journal", Foundation().JournalStatus() } };
			}
			return response;
		}

		if ((action == "start_capture") && (!a_args.contains("frameCount") || !a_args["frameCount"].is_number_unsigned()))
			return Foundation().MakeError(a_args, "invalid_field", "frameCount must be an unsigned integer", "validation", false, "frameCount");
		if ((action == "capture_status" || action == "cancel_capture") && (!a_args.contains("captureId") || !a_args["captureId"].is_number_unsigned()))
			return Foundation().MakeError(a_args, "invalid_field", "captureId must be an unsigned integer", "validation", false, "captureId");
		if (cpuView && a_args.contains("captureId"))
			return Foundation().MakeError(a_args, "invalid_field", "independent CPU views do not accept captureId", "validation", false, "captureId");
		if (action == "request_capture") {
			const auto mode = a_args.value("mode", std::string("both"));
			if (mode != "cpu" && mode != "gpu" && mode != "both")
				return Foundation().MakeError(a_args, "invalid_field", "mode must be cpu, gpu or both", "validation", false, "mode");
		}
		if (action == "history" || action == "cpu_history") {
			if (!a_args.contains("timerIndex") || !a_args["timerIndex"].is_number_unsigned())
				return Foundation().MakeError(a_args, "invalid_field", "timerIndex must be an unsigned integer", "validation", false, "timerIndex");
			const auto domain = a_args.value("domain", std::string(cpuView ? "cpu" : "gpu"));
			if ((domain != "gpu" && domain != "cpu") || (cpuView && domain != "cpu"))
				return Foundation().MakeError(a_args, "invalid_field", "domain must be gpu or cpu", "validation", false, "domain");
		}

		auto result = RunOnMainThread([action, a_args, cpuView] {
			const auto* api = CSX::Api::GetProfilerService001();
			if (!api)
				return json{ { "_dispatchError", "profiler API unavailable" } };
			const auto* sourceApi = CSX::Api::GetProfilerService002();
			if ((cpuView || action == "request_capture") && !sourceApi)
				return ApiFailure(Status::kUnavailable);
			if (action == "cpu_snapshot")
				return ReadCpuSnapshot(*sourceApi);
			if (action == "request_capture") {
				const auto mode = a_args.value("mode", std::string("both"));
				const auto sources = mode == "cpu" ? CaptureMode::kCpu : mode == "gpu" ? CaptureMode::kGpu :
				                                                                         CaptureMode::kBoth;
				const auto status = sourceApi->RequestCapture(sourceApi->paired.context, sources);
				return status == Status::kSuccess ? json{ { "requested", true }, { "mode", mode } } : ApiFailure(status);
			}
			if (action == "snapshot")
				return ReadSnapshot(*api);
			if (action == "timers" || action == "cpu_timers") {
				const auto prefix = a_args.value("prefix", std::string{});
				const auto captureId = a_args.value("captureId", 0ull);
				if (captureId != 0) {
					CaptureProgress001 progress;
					const auto captureStatus = api->GetCaptureProgress(api->context, captureId, &progress);
					if (captureStatus != Status::kSuccess)
						return ApiFailure(captureStatus);
				}
				json timers = json::array();
				const auto count = cpuView        ? sourceApi->GetCpuTimerCount(api->context) :
				                   captureId != 0 ? api->GetCaptureTimerCount(api->context, captureId) :
				                                    api->GetTimerCount(api->context);
				for (std::uint32_t index = 0; index < count; ++index) {
					TimerDescriptor001 timer;
					const auto timerStatus = cpuView ? sourceApi->GetCpuTimerDescriptor(api->context, index, &timer) : captureId != 0 ? api->GetCaptureTimerDescriptor(api->context, captureId, index, &timer) :
					                                                                                                                    api->GetTimerDescriptor(api->context, index, &timer);
					if (timerStatus != Status::kSuccess)
						continue;
					const std::string name = timer.name ? timer.name : "";
					if (!prefix.empty() && !name.starts_with(prefix))
						continue;
					timers.push_back({ { "index", index }, { "name", name }, { "hasGpu", timer.hasGpu != 0 }, { "hasCpu", timer.hasCpu != 0 },
						{ "activeGpu", timer.activeGpu != 0 }, { "activeCpu", timer.activeCpu != 0 },
						{ "gpu", { { "ms", timer.gpuMs }, { "topLevelMs", timer.gpuTopLevelMs }, { "averageMs", timer.gpuAverageMs }, { "p95Ms", timer.gpuP95Ms }, { "p99Ms", timer.gpuP99Ms }, { "historyCount", timer.gpuHistoryCount } } },
						{ "cpu", { { "ms", timer.cpuMs }, { "averageMs", timer.cpuAverageMs }, { "p95Ms", timer.cpuP95Ms }, { "p99Ms", timer.cpuP99Ms }, { "historyCount", timer.cpuHistoryCount } } } });
				}
				json response{ { "captureId", captureId == 0 ? json(nullptr) : json(captureId) }, { "catalogCount", count }, { "returnedCount", timers.size() }, { "prefix", prefix }, { "timers", std::move(timers) } };
				if (cpuView)
					response["snapshot"] = ReadCpuSnapshot(*sourceApi);
				return response;
			}
			if (action == "history" || action == "cpu_history") {
				const auto timerIndex = a_args.value("timerIndex", std::numeric_limits<std::uint32_t>::max());
				const auto domainName = a_args.value("domain", std::string(cpuView ? "cpu" : "gpu"));
				const auto domain = domainName == "cpu" ? TimingDomain::kCpu : TimingDomain::kGpu;
				const auto captureId = a_args.value("captureId", 0ull);
				TimerDescriptor001 timer;
				const auto timerStatus = cpuView ? sourceApi->GetCpuTimerDescriptor(api->context, timerIndex, &timer) : captureId != 0 ? api->GetCaptureTimerDescriptor(api->context, captureId, timerIndex, &timer) :
				                                                                                                                         api->GetTimerDescriptor(api->context, timerIndex, &timer);
				if (timerStatus != Status::kSuccess)
					return ApiFailure(timerStatus);
				const auto count = domain == TimingDomain::kCpu ? timer.cpuHistoryCount : timer.gpuHistoryCount;
				const auto offset = std::min(a_args.value("offset", 0u), count);
				const auto limit = std::min(a_args.value("limit", count), 300u);
				json samples = json::array();
				for (std::uint32_t sample = offset; sample < count && samples.size() < limit; ++sample) {
					float value = 0.0f;
					const auto sampleStatus = cpuView ? sourceApi->GetCpuHistorySample(api->context, timerIndex, sample, &value) : captureId != 0 ? api->GetCaptureHistorySample(api->context, captureId, timerIndex, domain, sample, &value) :
					                                                                                                                                api->GetHistorySample(api->context, timerIndex, domain, sample, &value);
					if (sampleStatus == Status::kSuccess)
						samples.push_back(value);
				}
				json response{ { "captureId", captureId == 0 ? json(nullptr) : json(captureId) }, { "timerIndex", timerIndex }, { "name", timer.name ? timer.name : "" }, { "domain", domainName }, { "historyCount", count }, { "offset", offset }, { "samplesMs", std::move(samples) } };
				if (cpuView)
					response["snapshot"] = ReadCpuSnapshot(*sourceApi);
				return response;
			}
			if (action == "set_enabled") {
				if (!a_args.contains("enabled") || !a_args["enabled"].is_boolean())
					return json{ { "_apiError", "enabled_must_be_boolean" }, { "_apiStatus", static_cast<std::uint32_t>(Status::kInvalidArgument) } };
				const auto status = api->SetEnabled(api->context, a_args["enabled"].get<bool>() ? 1u : 0u);
				if (status != Status::kSuccess)
					return ApiFailure(status);
				return json{ { "enabled", a_args["enabled"] }, { "snapshot", ReadSnapshot(*api) } };
			}
			if (action == "clear_history") {
				const auto status = api->ClearHistory(api->context);
				return status == Status::kSuccess ? json{ { "cleared", true }, { "snapshot", ReadSnapshot(*api) } } : ApiFailure(status);
			}
			if (action == "start_capture") {
				CaptureRequest001 request;
				request.frameCount = a_args["frameCount"].get<std::uint32_t>();
				request.clearHistory = a_args.value("clearHistory", false) ? 1u : 0u;
				CaptureProgress001 progress;
				const auto status = api->StartCapture(api->context, &request, &progress);
				if (status != Status::kSuccess)
					return ApiFailure(status);
				Foundation().AppendEvent(std::to_string(progress.captureId), 0, "capture.started", ProgressJson(progress));
				return ProgressJson(progress);
			}
			if (action == "capture_status")
				return ReadProgress(*api, a_args["captureId"].get<std::uint64_t>());
			if (action == "cancel_capture") {
				CaptureProgress001 progress;
				const auto status = api->CancelCapture(api->context, a_args["captureId"].get<std::uint64_t>(), &progress);
				if (status != Status::kSuccess)
					return ApiFailure(status);
				return ReadProgress(*api, progress.captureId);
			}
			return json{ { "_dispatchError", "validated action was not dispatched" } };
		});

		if (result.contains("_dispatchError"))
			return Foundation().MakeError(a_args, "main_thread_dispatch_failed", result.value("_dispatchError", std::string("dispatch failed")), "dispatch", true);
		if (result.contains("_apiError"))
			return Foundation().MakeError(a_args, result.value("_apiError", std::string("profiler_error")), "profiler operation failed", "execution", result.value("_apiError", std::string{}) == "busy");
		auto response = Foundation().MakeEnvelope(a_args, true);
		response["result"] = std::move(result);
		return response;
	}

	void ToolHandler(void*, const char* a_argsJson, void* a_sink, DevBenchAPI::WriteFn a_write) noexcept
	{
		json output;
		try {
			json args = a_argsJson && *a_argsJson ? json::parse(a_argsJson) : json::object();
			if (auto mismatch = BuildProvenance::ValidateExpectedBuild(args)) {
				output = Foundation().MakeError(args, mismatch->value("code", std::string("producer_mismatch")), mismatch->value("error", std::string("loaded CSX build does not match the request")), "validation", false, "expectedBuildId");
			} else {
				output = Foundation().Dispatch(args, &BuildResult);
			}
		} catch (const std::exception& e) {
			output = Foundation().MakeError(json::object(), "invalid_request", e.what());
		} catch (...) {
			output = Foundation().MakeError(json::object(), "internal_error", "unknown profiler API error", "dispatch", true);
		}
		try {
			const auto serialized = output.dump();
			a_write(a_sink, serialized.c_str());
		} catch (...) {
			a_write(a_sink, R"({"ok":false,"error":{"code":"serialization_failed"}})");
		}
	}
}

namespace CSX::Api::ProfilerApiDevBenchBridge
{
	void Install()
	{
		if (g_registered.load(std::memory_order_acquire))
			return;
		auto* devBench = DevBenchAPI::GetDevBenchInterface001();
		if (!devBench) {
			logger::info("ProfilerApiDevBenchBridge: devbench host not present; profiler API tool not registered");
			return;
		}
		const char* descriptor = R"({
			"description":"Versioned CSX profiler API with paired snapshots and bounded captures, plus an independent CPU view. All timer values and histories are self time. snapshot/timers/history preserve their paired-frame semantics. cpu_snapshot/cpu_timers/cpu_history publish at each CPU capture frame boundary without waiting for GPU queries; use their own capturedFrameCount and publicationCount, and separate catalog indices. CPU views retain the last publication while idle and do not accept captureId. request_capture queues one next-frame request for mode cpu, gpu or both; requests combine, so another consumer or a bounded capture can require both sources. CPU-only requests issue no GPU timestamps unless another consumer requests GPU work. All inspection is non-mutating. Bounded captures and the legacy communityshaders.profiler tool remain unchanged.",
			"inputSchema":{"type":"object","required":["contractMajor","clientId","commandId","action"],"properties":{
				"contractMajor":{"type":"integer","const":1},"clientId":{"type":"string","minLength":1,"maxLength":128},
				"commandId":{"type":"string","minLength":1,"maxLength":128},"expectedBuildId":{"type":"string"},
				"action":{"type":"string","enum":["registry","snapshot","timers","history","set_enabled","clear_history","start_capture","capture_status","cancel_capture","events","acknowledge_events","request_capture","cpu_snapshot","cpu_timers","cpu_history"]},
				"mode":{"type":"string","enum":["cpu","gpu","both"],"default":"both","description":"Sources requested by request_capture for one next frame; concurrent requests combine."},
				"prefix":{"type":"string"},"timerIndex":{"type":"integer","minimum":0},"domain":{"type":"string","enum":["gpu","cpu"],"description":"Timing domain for self-time history samples."},
				"offset":{"type":"integer","minimum":0},"limit":{"type":"integer","minimum":1,"maximum":300},"enabled":{"type":"boolean"},
				"frameCount":{"type":"integer","minimum":1,"maximum":300},"clearHistory":{"type":"boolean"},"captureId":{"type":"integer","minimum":1},
				"afterEventId":{"type":"integer","minimum":0},"throughEventId":{"type":"integer","minimum":0}
			}}
		})";
		devBench->RegisterTool("communityshaders.profiler_api", descriptor, &ToolHandler, nullptr);
		g_registered.store(true, std::memory_order_release);
		logger::info("ProfilerApiDevBenchBridge: registered communityshaders.profiler_api with devbench build {}", devBench->GetBuildNumber());
	}

	bool IsRegistered() { return g_registered.load(std::memory_order_acquire); }
}

#else

namespace CSX::Api::ProfilerApiDevBenchBridge
{
	void Install() {}
	bool IsRegistered() { return false; }
}

#endif
