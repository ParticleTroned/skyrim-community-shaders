#include "Diagnostics/NvPerf.h"

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "Api/DevBenchJson.h"
#	include "Api/ServiceFoundation.h"
#	include "BuildProvenance.h"
#	include "Utils/StringUtils.h"
#	include <DevBenchAPI.h>
#	include <atomic>
#	include <algorithm>
#	include <cmath>
#	include <format>
#	include <mutex>
#	include <nlohmann/json.hpp>
#	include <set>
#	include <stdexcept>

namespace
{
	using json = nlohmann::json;
	std::atomic_bool registered{ false };

	CSX::Api::ServiceFoundation& Foundation()
	{
		static CSX::Api::ServiceFoundation foundation({ "communityshaders.nvperf", 1, 0, 1 });
		static std::once_flag metadata;
		std::call_once(metadata, [&] { foundation.SetServerMetadataProvider([] { return BuildProvenance::GetProducer(); }); });
		return foundation;
	}

	const char* StateName(NvPerf::CaptureState state)
	{
		switch (state) {
		case NvPerf::CaptureState::Waiting:
			return "waiting";
		case NvPerf::CaptureState::Capturing:
			return "capturing";
		case NvPerf::CaptureState::Decoding:
			return "decoding";
		case NvPerf::CaptureState::Done:
			return "done";
		case NvPerf::CaptureState::Failed:
			return "failed";
		default:
			return "idle";
		}
	}

	json StatusJson(const NvPerf::Status& status)
	{
		return { { "state", StateName(status.state) }, { "captureId", status.captureId }, { "sample", status.sample },
			{ "samples", status.samples }, { "pass", status.pass }, { "passes", status.passes }, { "error", status.error } };
	}

	json Coverage()
	{
		return { { "api", "D3D11" }, { "context", "immediate" }, { "d3d12EyeQueuesInstrumented", false },
			{ "parallelEyeSchedulingChanged", false }, { "deterministicReplay", false }, { "repeatabilityVerified", false },
			{ "summaryCounters", "sum per sample" }, { "summaryRates", "time weighted when valid duration is requested; otherwise unweighted" },
			{ "note", "Live multi-frame counters require comparable frame workloads. D3D12 NR work and queue overlap are not attributed. Captures perturb timing." } };
	}

	std::uint64_t Unsigned(const json& args, const char* key, std::uint64_t fallback, std::uint64_t minimum, std::uint64_t maximum)
	{
		if (!args.contains(key))
			return fallback;
		const auto& value = args.at(key);
		if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<std::int64_t>() < 0))
			throw std::invalid_argument(std::format("{} must be an unsigned integer", key));
		const auto number = value.get<std::uint64_t>();
		if (number < minimum || number > maximum)
			throw std::invalid_argument(std::format("{} must be in {}..{}", key, minimum, maximum));
		return number;
	}

	json ResultJson(const NvPerf::CaptureResult& result, bool detail, std::size_t offset, std::size_t limit)
	{
		json metrics = json::array();
		for (const auto& metric : result.metrics) metrics.push_back(NvPerf::FormatMetric(metric));
		json unsupported = json::array();
		for (const auto& metric : result.unsupportedMetrics) unsupported.push_back(NvPerf::FormatMetric(metric));
		const auto values = [](const std::vector<double>& numbers) {
			json array = json::array();
			for (double value : numbers) array.push_back(std::isfinite(value) ? json(value) : json(nullptr));
			return array;
		};
		json summary = json::array();
		for (const auto& entry : result.summary)
			summary.push_back({ { "range", entry.name }, { "perFrame", entry.occurrencesPerFrame },
				{ "incomplete", entry.incomplete }, { "values", values(entry.values) } });
		json frames = json::array();
		for (const auto& frame : result.frames)
			frames.push_back({ { "sample", frame.sample }, { "pass", frame.pass }, { "frame", frame.frame } });
		json out{ { "captureId", result.id }, { "success", result.success }, { "error", result.error }, { "buildId", result.buildId },
			{ "chip", result.chipName }, { "startFrame", result.startFrame }, { "endFrame", result.endFrame }, { "samples", result.samples },
			{ "passesPerSample", result.passesPerSample }, { "passFrames", std::move(frames) }, { "coverage", Coverage() },
			{ "droppedRanges", result.droppedRanges }, { "droppedTraceBytes", result.droppedTraceBytes },
			{ "incompleteRanges", result.incompleteRanges }, { "unbalancedRanges", result.unbalancedRanges },
			{ "metrics", std::move(metrics) }, { "unsupportedMetrics", std::move(unsupported) }, { "summary", std::move(summary) },
			{ "csvPath", Util::PathToUtf8(result.csvPath) }, { "csvWritten", result.csvWritten }, { "exportError", result.exportError },
			{ "totalRanges", result.rangeResults.size() } };
		if (detail) {
			json ranges = json::array();
			const auto end = std::min(result.rangeResults.size(), offset + limit);
			for (auto index = offset; index < end; ++index) {
				const auto& range = result.rangeResults[index];
				ranges.push_back({ { "sample", range.sample }, { "range", range.name }, { "path", range.path },
					{ "occurrence", range.occurrence }, { "values", values(range.values) } });
			}
			out["rangeResults"] = std::move(ranges);
			out["offset"] = offset;
			out["nextOffset"] = end < result.rangeResults.size() ? json(end) : json(nullptr);
		}
		return out;
	}

	json Handle(const json& args)
	{
		Unsigned(args, "contractMinor", 0, 0, 0);
		const auto action = args.value("action", std::string{});
		std::set<std::string_view> allowed{ "contractMajor", "contractMinor", "clientId", "commandId", "expectedBuildId", "action" };
		if (action == "capture")
			allowed.insert({ "metrics", "ranges", "samples", "delayFrames", "nestingLevels" });
		else if (action == "result")
			allowed.insert({ "captureId", "detail", "offset", "limit" });
		else if (action == "cancel")
			allowed.insert("captureId");
		else if (action == "events")
			allowed.insert({ "afterEventId", "limit" });
		else if (action != "status" && action != "metrics")
			throw std::invalid_argument("unknown action");
		for (const auto& item : args.items())
			if (!allowed.contains(item.key()))
				throw std::invalid_argument("unsupported field: " + item.key());

		json result;
		if (action == "status") {
			NvPerf::Probe();
			const auto device = NvPerf::GetDeviceInfo();
			result = { { "built", device.built }, { "probed", device.probed }, { "available", device.available },
				{ "gpuSupported", device.gpuSupported }, { "device", device.deviceName }, { "chip", device.chipName },
				{ "deviceError", device.error }, { "capture", StatusJson(NvPerf::GetStatus()) }, { "coverage", Coverage() } };
		} else if (action == "metrics") {
			result["defaults"] = json::array();
			for (const auto& metric : NvPerf::DefaultMetrics()) result["defaults"].push_back(NvPerf::FormatMetric(metric));
		} else if (action == "capture") {
			NvPerf::CaptureConfiguration config;
			for (const char* key : { "metrics", "ranges" }) {
				if (!args.contains(key))
					continue;
				const auto& list = args.at(key);
				if (!list.is_array() || list.size() > NvPerf::MaxMetrics || (std::string_view(key) == "metrics" && list.empty()))
					throw std::invalid_argument("metrics/ranges require at most 32 strings; metrics cannot be empty");
				for (const auto& value : list) {
					if (!value.is_string() || value.get_ref<const std::string&>().size() > NvPerf::MaxNameLength)
						throw std::invalid_argument("metric/filter length exceeds 240 characters");
					if (std::string_view(key) == "ranges")
						config.ranges.push_back(value.get<std::string>());
					else {
						NvPerf::MetricRequest metric;
						std::string error;
						if (!NvPerf::ParseMetric(value.get_ref<const std::string&>(), metric, error))
							throw std::invalid_argument(error);
						config.metrics.push_back(std::move(metric));
					}
				}
			}
			config.samples = static_cast<std::uint32_t>(Unsigned(args, "samples", 1, 1, NvPerf::MaxSamples));
			config.delayFrames = static_cast<std::uint32_t>(Unsigned(args, "delayFrames", 0, 0, NvPerf::MaxDelayFrames));
			config.nestingLevels = static_cast<std::uint16_t>(Unsigned(args, "nestingLevels", 1, 1, NvPerf::MaxNestingLevels));
			std::uint64_t id = 0;
			std::string error;
			if (!NvPerf::RequestCapture(config, id, error))
				return Foundation().MakeError(args, "capture_rejected", error, "execution");
			result = { { "queued", true }, { "captureId", id }, { "coverage", Coverage() } };
		} else if (action == "cancel") {
			if (!args.contains("captureId"))
				throw std::invalid_argument("captureId is required for cancel");
			const auto id = Unsigned(args, "captureId", 0, 1, UINT64_MAX);
			result = { { "cancelRequested", NvPerf::CancelCapture(id) }, { "captureId", id } };
		} else if (action == "events") {
			result = Foundation().PollEvents(Unsigned(args, "afterEventId", 0, 0, UINT64_MAX),
				static_cast<std::uint32_t>(Unsigned(args, "limit", 64, 1, 128)));
		} else {
			const auto latest = NvPerf::LatestResult();
			const auto status = NvPerf::GetStatus();
			const auto id = Unsigned(args, "captureId", status.captureId, 1, UINT64_MAX);
			if (args.contains("detail") && !args.at("detail").is_boolean())
				throw std::invalid_argument("detail must be boolean");
			const auto offset = Unsigned(args, "offset", 0, 0, 16384);
			const auto limit = Unsigned(args, "limit", 64, 1, 128);
			if (latest && latest->id == id)
				result = ResultJson(*latest, args.value("detail", false), offset, limit);
			else if (id && status.captureId == id && (status.state == NvPerf::CaptureState::Waiting || status.state == NvPerf::CaptureState::Capturing || status.state == NvPerf::CaptureState::Decoding))
				result = { { "pending", true }, { "capture", StatusJson(status) } };
			else
				return Foundation().MakeError(args, "capture_not_retained", "Only the latest completed capture is retained; export it before another capture.");
		}
		auto response = Foundation().MakeEnvelope(args, true);
		response["result"] = std::move(result);
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
				const auto response = CSX::Api::DumpForDevBench(Foundation().MakeError(args, "producer_mismatch", "loaded build differs from expectedBuildId"));
				write(sink, response.c_str());
				return;
			}
			const auto response = CSX::Api::DumpForDevBench(Foundation().Dispatch(args, &Handle));
			write(sink, response.c_str());
		} catch (const std::exception& error) {
			try {
				const auto response = CSX::Api::DumpForDevBench(Foundation().MakeError(json::object(), "invalid_request", error.what()));
				write(sink, response.c_str());
			} catch (...) {
				write(sink, R"({"ok":false,"error":{"code":"serialization_failed"}})");
			}
		} catch (...) {
			write(sink, R"({"ok":false,"error":{"code":"internal_error"}})");
		}
	}
}

namespace NvPerf
{
	void InstallDevBench()
	{
		if (registered.load(std::memory_order_acquire))
			return;
		auto* host = DevBenchAPI::GetDevBenchInterface001();
		if (!host)
			return;
		constexpr const char* descriptor = R"({
            "description":"Reply and event strings replace invalid UTF-8 with U+FFFD; request validation remains strict. Optional developer-only NVIDIA hardware counters for CSX D3D11 immediate-context ranges. Requires NVPERF_SUPPORT and SDK. D3D12 NR eye queues are not instrumented and parallel-eye scheduling is unchanged. One SDK pass per live frame, not deterministic replay: compare only stable workloads. Capture may perturb timings. status queues render-thread probing; repeat with a new commandId. capture is idempotent per clientId/commandId and returns captureId. cancel requires that id. result retains the latest completed capture; detail returns at most 128 ranges per page with nextOffset. Values follow metrics order; nonfinite values are null and mark capture incomplete. Counters are summed per sample, rates time-weighted when duration is included. CSV export errors are separate from capture validity; returned csvPath values are UTF-8. events journals terminal outcomes. Requests expire after five minutes of progressing render frames. Engine ranges require their existing annotation hooks; CSX pass ranges work independently of timing capture.",
            "inputSchema":{"type":"object","required":["contractMajor","clientId","commandId","action"],"additionalProperties":false,"properties":{
                "contractMajor":{"type":"integer","const":1},"contractMinor":{"type":"integer","minimum":0,"maximum":0},
                "clientId":{"type":"string","minLength":1,"maxLength":128},"commandId":{"type":"string","minLength":1,"maxLength":128},"expectedBuildId":{"type":"string"},
                "action":{"type":"string","enum":["status","metrics","capture","cancel","result","events"]},
                "metrics":{"type":"array","minItems":1,"maxItems":32,"items":{"type":"string","minLength":1,"maxLength":240}},
                "ranges":{"type":"array","maxItems":32,"items":{"type":"string","minLength":1,"maxLength":240}},
                "samples":{"type":"integer","minimum":1,"maximum":64},"delayFrames":{"type":"integer","minimum":0,"maximum":600},
                "nestingLevels":{"type":"integer","minimum":1,"maximum":8},"captureId":{"type":"integer","minimum":1},"detail":{"type":"boolean"},
                "offset":{"type":"integer","minimum":0,"maximum":16384},"limit":{"type":"integer","minimum":1,"maximum":128},"afterEventId":{"type":"integer","minimum":0}
            }},"outputSchema":{"description":"Invalid UTF-8 in output strings is replaced with U+FFFD; valid UTF-8 output is unchanged.","type":"object","required":["ok"],"properties":{"ok":{"type":"boolean"},"result":{"type":"object","properties":{"csvPath":{"type":"string","description":"UTF-8 filesystem path to the capture CSV."}}},"error":{"type":"object"}}}
        })";
		host->RegisterTool("communityshaders.nvperf", descriptor, &ToolHandler, nullptr);
		SetCompletionCallback([](const CaptureResult& result) {
			json payload{ { "captureId", result.id }, { "success", result.success }, { "error", result.error }, { "buildId", result.buildId } };
			Foundation().AppendEvent(std::to_string(result.id), 0, "completed", payload);
			if (auto* current = DevBenchAPI::GetDevBenchInterface001()) {
				const auto text = CSX::Api::DumpForDevBench(payload);
				current->EmitEvent("communityshaders.nvperf.completed", text.c_str());
			}
		});
		registered.store(true, std::memory_order_release);
	}
}
#endif
