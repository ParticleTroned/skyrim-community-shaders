#include "Api/ProfilerApiDevBenchBridge.h"

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "Api/ProfilerService.h"
#	include "Api/ServiceFoundation.h"
#	include "BuildProvenance.h"

#	include <DevBenchAPI.h>
#	include <nlohmann/json.hpp>

#	include <atomic>
#	include <chrono>
#	include <functional>
#	include <future>
#	include <limits>
#	include <memory>
#	include <mutex>
#	include <set>
#	include <stdexcept>
#	include <string>

namespace
{
	using json = nlohmann::json;
	using CSX::ProfilerAPI::CaptureProgress001;
	using CSX::ProfilerAPI::CaptureRequest001;
	using CSX::ProfilerAPI::CaptureState;
	using CSX::ProfilerAPI::Snapshot001;
	using CSX::ProfilerAPI::Status;
	using CSX::ProfilerAPI::TimerDescriptor001;
	using CSX::ProfilerAPI::TimingDomain;
	constexpr auto kMainThreadTimeout = std::chrono::milliseconds(5000);
	std::atomic_bool g_registered{ false };
	std::mutex g_terminalEventMutex;
	std::set<std::uint64_t> g_reportedTerminalCaptures;

	CSX::Api::ServiceFoundation& Foundation()
	{
		static CSX::Api::ServiceFoundation foundation({ CSX::ProfilerAPI::ServiceName, 1, 0, 1 });
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
		auto* tasks = SKSE::GetTaskInterface();
		if (!tasks)
			return { { "_dispatchError", "SKSE task interface unavailable" } };
		auto promise = std::make_shared<std::promise<json>>();
		auto cancelled = std::make_shared<std::atomic_bool>(false);
		auto future = promise->get_future();
		tasks->AddTask([promise, cancelled, run = std::move(a_run)]() mutable {
			if (cancelled->load(std::memory_order_acquire))
				return;
			try {
				promise->set_value(run());
			} catch (const std::exception& e) {
				promise->set_value(json{ { "_dispatchError", e.what() } });
			} catch (...) {
				promise->set_value(json{ { "_dispatchError", "unknown main-thread failure" } });
			}
		});
		if (future.wait_for(kMainThreadTimeout) != std::future_status::ready) {
			cancelled->store(true, std::memory_order_release);
			return { { "_dispatchError", "main thread did not run within 5000ms" } };
		}
		return future.get();
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
		const bool known = action == "registry" || action == "snapshot" || action == "timers" || action == "history" ||
			action == "set_enabled" || action == "clear_history" || action == "start_capture" ||
			action == "capture_status" || action == "cancel_capture" || action == "events" || action == "acknowledge_events";
		if (!known)
			return Foundation().MakeError(a_args, "unknown_action", "action is not supported", "validation", false, "action");

		if (action == "registry") {
			auto response = Foundation().MakeEnvelope(a_args, true);
			response["result"] = {
				{ "service", CSX::ProfilerAPI::ServiceName },
				{ "major", CSX::ProfilerAPI::ServiceMajor },
				{ "minor", CSX::ProfilerAPI::ServiceMinor },
				{ "schemaRevision", CSX::ProfilerAPI::SchemaRevision },
				{ "capabilities", CSX::ProfilerAPI::ServiceCapabilities },
				{ "mainThreadAffine", true },
				{ "registryMainThreadAffine", false },
				{ "capture", { { "minimumFrames", 1 }, { "maximumFrames", 300 }, { "singleActiveSession", true }, { "requiresEnabled", true } } },
				{ "actions", json::array({ "registry", "snapshot", "timers", "history", "set_enabled", "clear_history", "start_capture", "capture_status", "cancel_capture", "events", "acknowledge_events" }) },
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
		if (action == "history") {
			if (!a_args.contains("timerIndex") || !a_args["timerIndex"].is_number_unsigned())
				return Foundation().MakeError(a_args, "invalid_field", "timerIndex must be an unsigned integer", "validation", false, "timerIndex");
			const auto domain = a_args.value("domain", std::string("gpu"));
			if (domain != "gpu" && domain != "cpu")
				return Foundation().MakeError(a_args, "invalid_field", "domain must be gpu or cpu", "validation", false, "domain");
		}

		auto result = RunOnMainThread([action, a_args] {
			const auto* api = CSX::Api::GetProfilerService001();
			if (!api)
				return json{ { "_dispatchError", "profiler API unavailable" } };
			if (action == "snapshot")
				return ReadSnapshot(*api);
			if (action == "timers") {
				const auto prefix = a_args.value("prefix", std::string{});
				const auto captureId = a_args.value("captureId", 0ull);
				if (captureId != 0) {
					CaptureProgress001 progress;
					const auto captureStatus = api->GetCaptureProgress(api->context, captureId, &progress);
					if (captureStatus != Status::kSuccess)
						return ApiFailure(captureStatus);
				}
				json timers = json::array();
				const auto count = captureId != 0 ? api->GetCaptureTimerCount(api->context, captureId) : api->GetTimerCount(api->context);
				for (std::uint32_t index = 0; index < count; ++index) {
					TimerDescriptor001 timer;
					const auto timerStatus = captureId != 0 ?
						api->GetCaptureTimerDescriptor(api->context, captureId, index, &timer) :
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
				return json{ { "captureId", captureId == 0 ? json(nullptr) : json(captureId) }, { "catalogCount", count }, { "returnedCount", timers.size() }, { "prefix", prefix }, { "timers", std::move(timers) } };
			}
			if (action == "history") {
				const auto timerIndex = a_args.value("timerIndex", std::numeric_limits<std::uint32_t>::max());
				const auto domainName = a_args.value("domain", std::string("gpu"));
				const auto domain = domainName == "cpu" ? TimingDomain::kCpu : TimingDomain::kGpu;
				const auto captureId = a_args.value("captureId", 0ull);
				TimerDescriptor001 timer;
				const auto timerStatus = captureId != 0 ?
					api->GetCaptureTimerDescriptor(api->context, captureId, timerIndex, &timer) :
					api->GetTimerDescriptor(api->context, timerIndex, &timer);
				if (timerStatus != Status::kSuccess)
					return ApiFailure(timerStatus);
				const auto count = domain == TimingDomain::kCpu ? timer.cpuHistoryCount : timer.gpuHistoryCount;
				const auto offset = std::min(a_args.value("offset", 0u), count);
				const auto limit = std::min(a_args.value("limit", count), 300u);
				json samples = json::array();
				for (std::uint32_t sample = offset; sample < count && samples.size() < limit; ++sample) {
					float value = 0.0f;
					const auto sampleStatus = captureId != 0 ?
						api->GetCaptureHistorySample(api->context, captureId, timerIndex, domain, sample, &value) :
						api->GetHistorySample(api->context, timerIndex, domain, sample, &value);
					if (sampleStatus == Status::kSuccess)
						samples.push_back(value);
				}
				return json{ { "captureId", captureId == 0 ? json(nullptr) : json(captureId) }, { "timerIndex", timerIndex }, { "name", timer.name ? timer.name : "" }, { "domain", domainName }, { "historyCount", count }, { "offset", offset }, { "samplesMs", std::move(samples) } };
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
			"description":"Versioned CSX profiler API with non-mutating inspection, timer histories, and bounded capture sessions. The legacy communityshaders.profiler tool remains available.",
			"inputSchema":{"type":"object","required":["contractMajor","clientId","commandId","action"],"properties":{
				"contractMajor":{"type":"integer","const":1},"clientId":{"type":"string","minLength":1,"maxLength":128},
				"commandId":{"type":"string","minLength":1,"maxLength":128},"expectedBuildId":{"type":"string"},
				"action":{"type":"string","enum":["registry","snapshot","timers","history","set_enabled","clear_history","start_capture","capture_status","cancel_capture","events","acknowledge_events"]},
				"prefix":{"type":"string"},"timerIndex":{"type":"integer","minimum":0},"domain":{"type":"string","enum":["gpu","cpu"]},
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
