#include "PerformanceResults.h"
#include "PerformanceFpsVR.h"
#include "PerformanceQuickScan.h"
#include "PerformanceResultsFormatting.h"
#include "PerformanceTuningStatistics.h"
#include "Utils/Statistics.h"
#include "Utils/StringUtils.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace PerformanceResults
{
	using ReportFormatting::Semantics;
	using ReportFormatting::Text;
	namespace
	{
		constexpr std::uintmax_t MaximumFileBytes = 8 * 1024 * 1024;
		bool SafeId(std::string_view id)
		{
			return !id.empty() && id.size() <= 100 && std::ranges::all_of(id, [](char c) {
				return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
			});
		}

		void OptionalObject(const Json& value, const char* key)
		{
			if (value.contains(key) && !value[key].is_object())
				throw std::runtime_error(std::string("Invalid result object: ") + key);
		}

		void OptionalText(const Json& value, const char* key)
		{
			if (value.contains(key) && !value[key].is_null() && (!value[key].is_string() || value[key].get_ref<const std::string&>().size() > 8192))
				throw std::runtime_error(std::string("Invalid result text: ") + key);
		}

		void ValidateScanMetadata(const Json& record)
		{
			if (record.contains("scanCount") && (!record["scanCount"].is_number_integer() || (record["scanCount"] != 1 && record["scanCount"] != PerformanceQuickScan::kBatchScans)))
				throw std::runtime_error("Invalid scan count.");
			for (const auto* key : { "automaticCsv", "sourceUnavailable" })
				if (record.contains(key) && !record[key].is_boolean())
					throw std::runtime_error("Invalid result flag.");
			OptionalObject(record.at("context"), "build");
			OptionalObject(record, "capture");
			if (record.contains("captures") && (!record["captures"].is_array() || record["captures"].size() > PerformanceQuickScan::kBatchScans))
				throw std::runtime_error("Invalid scan captures.");
			OptionalObject(record, "fpsVR");
			if (!record.contains("fpsVR"))
				return;
			const auto& fpsvr = record.at("fpsVR");
			if (fpsvr.contains("recordingStopped") && !fpsvr.at("recordingStopped").is_boolean())
				throw std::runtime_error("Invalid fpsVR recording stop receipt.");
			const auto status = fpsvr.at("status").get<std::string>();
			if (status != "pending" && status != "omitted" && status != "partial" && status != "complete")
				throw std::runtime_error("Invalid fpsVR result status.");
			OptionalText(fpsvr, "reason");
			if (fpsvr.contains("reason") && !fpsvr["reason"].is_string())
				throw std::runtime_error("Invalid fpsVR omission reason.");
			if (fpsvr.contains("scans") && (!fpsvr["scans"].is_array() || fpsvr["scans"].size() > PerformanceQuickScan::kBatchScans))
				throw std::runtime_error("Invalid fpsVR scan results.");
			std::set<unsigned> indices;
			for (const auto& scan : fpsvr.value("scans", Json::array())) {
				const auto& index = scan.at("scanIndex");
				const auto& count = scan.at("samples");
				if (!index.is_number_integer() || index < 1 || index > record.value("scanCount", 1u) || !indices.insert(index.get<unsigned>()).second ||
					!count.is_number_integer() || count < 3 || count > PerformanceFpsVR::kMaximumWindowSamples)
					throw std::runtime_error("Invalid fpsVR scan identity or sample count.");
				for (const auto* mode : { "cpu", "gpu" })
					for (const auto* key : { "meanMs", "medianMs", "p95Ms", "p99Ms" }) {
						const auto& value = scan.at(mode).at(key);
						if (!value.is_number() || !std::isfinite(value.get<double>()) || value.get<double>() < 0)
							throw std::runtime_error("Invalid fpsVR timing statistic.");
					}
			}
			if ((status == "complete" || status == "partial") && indices.empty())
				throw std::runtime_error("fpsVR results claim completion without samples.");
			if ((status == "pending" || status == "omitted") && !indices.empty())
				throw std::runtime_error("Unavailable fpsVR results contain timing samples.");
		}

		std::string Cell(const Json& value)
		{
			auto text = Text(value);
			const auto first = text.find_first_not_of(" \t\r\n");
			if (value.is_string() && first != std::string::npos && std::string_view("=+-@").find(text[first]) != std::string_view::npos)
				text.insert(0, "'");
			std::string result = "\"";
			for (char c : text) {
				if (c == '"')
					result += '"';
				result += c;
			}
			return result + '"';
		}

		Json Summary(const Json& record)
		{
			return { { "id", record.at("id") }, { "started", record.at("started") },
				{ "kind", record.at("kind") }, { "status", record.at("status") }, { "rows", record.at("results").size() } };
		}

		Json BlockMedian(const Json& samples, const std::string& metric)
		{
			if (!samples.is_object() || !samples.contains(metric) || !samples.at(metric).is_object())
				return nullptr;
			const auto& channel = samples.at(metric);
			if (!channel.value("accumulationValid", Json(false)).is_boolean() || channel.value("accumulationValid", Json(false)) != true ||
				!channel.contains("blocks") || !channel.at("blocks").is_array() || channel.at("blocks").size() < 2 || channel.at("blocks").size() > 64)
				return nullptr;
			std::vector<double> values;
			for (const auto& block : channel.at("blocks")) {
				if (!block.is_object() || !block.value("sum", Json(nullptr)).is_number() || !block.value("sampleWeight", Json(nullptr)).is_number())
					return nullptr;
				const auto sum = block.at("sum").get<double>(), weight = block.at("sampleWeight").get<double>();
				if (!std::isfinite(sum) || !std::isfinite(weight) || sum < 0 || weight <= 0 || !std::isfinite(sum / weight))
					return nullptr;
				values.push_back(sum / weight);
			}
			return Util::Median(std::move(values));
		}

		Json MeasuredValue(const Json& metric)
		{
			return metric.value("available", Json(true)) == false ? Json(nullptr) : metric.value("value", Json(nullptr));
		}

		std::string ScanMetricId(const Json& row)
		{
			auto id = row.at("id").get<std::string>();
			const auto index = row.value("scanIndex", Json(nullptr));
			if (index.is_number_integer()) {
				const auto prefix = std::to_string(index.get<unsigned>()) + ":";
				if (id.starts_with(prefix))
					id.erase(0, prefix.size());
			}
			return id;
		}

	}

	std::string Timestamp()
	{
		const auto now = std::chrono::system_clock::now();
		const auto time = std::chrono::system_clock::to_time_t(now);
		std::tm utc{};
		gmtime_s(&utc, &time);
		std::ostringstream text;
		text << std::put_time(&utc, "%Y%m%dT%H%M%S") << std::setfill('0') << std::setw(3)
			 << std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000 << 'Z';
		return text.str();
	}

	std::string NewId()
	{
		static const auto session = [] {
			std::random_device random;
			std::ostringstream value;
			value << std::hex << random() << random();
			return value.str();
		}();
		static std::atomic_uint64_t sequence{ 0 };
		return Timestamp() + "-" + session + "-" + std::to_string(++sequence);
	}

	void Validate(const Json& record)
	{
		if (!record.is_object() || record.value("schema", 0) != 1 || !SafeId(record.at("id").get<std::string>()) ||
			!record.at("started").is_string() || (record.contains("failure") && !record["failure"].is_null() && !record["failure"].is_string()) || !record.at("context").is_object() || !record.at("settings").is_object() ||
			!record.at("results").is_array() || record.at("results").size() > 4096)
			throw std::runtime_error("Invalid or unsupported performance result.");
		const auto kind = record.at("kind").get<std::string>();
		const auto status = record.at("status").get<std::string>();
		if (kind != "quick_scan" && kind != "feature_comparison" && kind != "feature_batch" && kind != "upscaling_sweep")
			throw std::runtime_error("Unknown measurement kind.");
		if (status != "running" && status != "complete" && status != "failed" && status != "cancelled" && status != "interrupted")
			throw std::runtime_error("Unknown measurement status.");
		ValidateScanMetadata(record);
		std::set<std::string> keys;
		struct ScanMetric
		{
			Json semantics;
			std::set<unsigned> scans;
		};
		std::map<std::pair<std::string, std::string>, ScanMetric> scanMetrics;
		for (const auto& row : record.at("results")) {
			if (!row.is_object() || !row.at("label").is_string() || row.at("label").get_ref<const std::string&>().size() > 1024 ||
				!row.at("id").is_string() || row.at("id").get_ref<const std::string&>().size() > 1024 ||
				(row.contains("failure") && !row["failure"].is_null() && !row["failure"].is_string()) ||
				!row.at("metrics").is_object() ||
				!keys.insert(row.at("id").get<std::string>()).second)
				throw std::runtime_error("Invalid or duplicate result row.");
			if (row.contains("source") && (!row.at("source").is_string() || row.at("source").get_ref<const std::string&>().size() > 1024))
				throw std::runtime_error("Invalid measurement source.");
			OptionalText(row, "relativeTo");
			OptionalText(row, "coverage");
			OptionalObject(row, "sampling");
			if (row.contains("sampling")) {
				OptionalText(row["sampling"], "captureRequestedUtc");
				OptionalText(row["sampling"], "captureResolvedUtc");
			}
			if (row.contains("scanIndex") && (!row["scanIndex"].is_number_integer() || row["scanIndex"] < 1 || row["scanIndex"] > record.value("scanCount", 1u)))
				throw std::runtime_error("Invalid result scan index.");
			if (kind == "quick_scan" && record.value("scanCount", 1u) > 1 && !row.contains("scanIndex"))
				throw std::runtime_error("Missing result scan index.");
			for (const auto& [name, metric] : row.at("metrics").items()) {
				if ((name != "frame" && name != "gpu" && name != "cpu" && name != "fps") || !metric.is_object() ||
					(metric.at("unit") != (name == "fps" ? "fps" : "ms")))
					throw std::runtime_error("Invalid measurement metric.");
				if (metric.contains("available") && !metric.at("available").is_boolean())
					throw std::runtime_error("Invalid measurement availability.");
				for (const auto* field : { "value", "standardError", "current", "comparison", "pValue" }) {
					if (metric.contains(field) && !metric[field].is_null() &&
						(!metric[field].is_number() || !std::isfinite(metric[field].get<double>())))
						throw std::runtime_error("Non-finite measurement metric.");
				}
				if ((metric.value("standardError", Json(nullptr)).is_number() && metric.at("standardError") < 0) ||
					(metric.value("pValue", Json(nullptr)).is_number() && (metric.at("pValue") < 0 || metric.at("pValue") > 1)))
					throw std::runtime_error("Invalid measurement uncertainty.");
				if (kind == "quick_scan") {
					auto& group = scanMetrics[{ ScanMetricId(row), name }];
					const Json semantics = Json::array({ row.value("source", Json("instrumented")),
						row.value("coverage", Json(nullptr)), row.value("relativeTo", Json(nullptr)) });
					if (group.semantics.is_null())
						group.semantics = semantics;
					if (group.semantics != semantics)
						throw std::runtime_error("Measurement semantics differ within a scan batch.");
					if (!group.scans.insert(row.value("scanIndex", 1u)).second)
						throw std::runtime_error("Duplicate feature metric within a scan.");
				}
			}
		}
	}

	Json Metrics(const Json& record)
	{
		Json result = Json::array();
		for (const auto& row : record.at("results")) {
			for (const auto& [name, metric] : row.at("metrics").items()) {
				auto entry = metric;
				entry["value"] = MeasuredValue(metric);
				entry["id"] = row.at("id");
				entry["label"] = row.at("label");
				entry["metric"] = name;
				entry["source"] = row.value("source", "instrumented");
				entry["relativeTo"] = row.value("relativeTo", Json(nullptr));
				entry["failure"] = row.value("failure", Json(nullptr));
				entry["coverage"] = row.value("coverage", Json(nullptr));
				entry["scanIndex"] = row.value("scanIndex", Json(nullptr));
				if (record.value("scanCount", 1u) > 1 && entry["scanIndex"].is_number_integer())
					entry["label"] = "Scan " + entry["scanIndex"].dump() + " / " + row.at("label").get<std::string>();
				entry["sampling"] = row.value("sampling", record.value("capture", Json{ { "current", row.value("currentSamples", Json(nullptr)) }, { "comparison", row.value("comparisonSamples", Json(nullptr)) } }));
				entry["median"] = nullptr;
				entry["medianBasis"] = nullptr;
				if (record.at("kind") != "quick_scan" && name != "fps" && entry.value("value", Json(nullptr)).is_number()) {
					const auto current = BlockMedian(row.value("currentSamples", Json(nullptr)), name);
					const auto comparison = BlockMedian(row.value("comparisonSamples", Json(nullptr)), name);
					if (current.is_number() && comparison.is_number()) {
						entry["median"] = current.get<double>() - comparison.get<double>();
						entry["medianBasis"] = "difference of current and comparison block medians";
					}
				}
				result.push_back(std::move(entry));
			}
		}
		return result;
	}

	std::string SceneLabel(const Json& context)
	{
		for (const auto* key : { "cellName", "locationName", "cellEditorId" }) {
			const auto value = context.value(key, Json(nullptr));
			if (value.is_string() && value.get_ref<const std::string&>().find_first_not_of(" \t\r\n") != std::string::npos)
				return value.get<std::string>();
		}
		std::ostringstream name;
		const auto world = context.value("worldspaceName", Json(nullptr));
		if (world.is_string() && !world.get_ref<const std::string&>().empty())
			name << world.get<std::string>() << " / ";
		const auto cell = context.value("cell", Json(nullptr));
		if (cell.is_number_integer() && cell > 0 && cell <= UINT32_MAX)
			name << "Cell 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << cell.get<std::uint32_t>();
		else
			name << "Unknown scene";
		return name.str();
	}

	Json SummaryMetrics(const Json& record)
	{
		Validate(record);
		if (record.at("kind") != "quick_scan")
			return Metrics(record);
		if (record.value("scanCount", 1u) == 1) {
			auto result = Metrics(record);
			for (auto& row : result)
				row["id"] = ScanMetricId(row);
			return result;
		}
		struct Repeats
		{
			Json row;
			std::map<unsigned, Json> values;
		};
		std::map<std::pair<std::string, std::string>, Repeats> groups;
		for (const auto& row : record.at("results")) {
			const auto index = row.at("scanIndex").get<unsigned>();
			const auto id = ScanMetricId(row);
			for (const auto& [name, metric] : row.at("metrics").items()) {
				auto& group = groups[{ id, name }];
				if (group.row.is_null())
					group.row = { { "id", id }, { "label", row.at("label") }, { "metric", name }, { "unit", metric.at("unit") },
						{ "coverage", row.value("coverage", Json(nullptr)) }, { "scanIndex", nullptr },
						{ "relativeTo", row.value("relativeTo", Json(nullptr)) }, { "source", row.value("source", "instrumented") } };
				group.values.emplace(index, MeasuredValue(metric));
			}
		}
		auto result = Json::array();
		for (auto& [key, group] : groups) {
			auto values = Json::array();
			std::vector<PerformanceTuningStatistics::Moments> moments;
			for (unsigned index = 1; index <= record.at("scanCount").get<unsigned>(); ++index) {
				const auto found = group.values.find(index);
				const Json value = found == group.values.end() ? Json(nullptr) : found->second;
				values.push_back(value);
				if (value.is_number())
					moments.push_back({ value.get<double>(), 1.0 });
			}
			double mean = 0, meanVariance = 0;
			const bool complete = moments.size() == values.size() &&
			                      PerformanceTuningStatistics::TryGetBlockMeanStatistics(moments, mean, meanVariance);
			group.row["value"] = complete ? Json(mean) : Json(nullptr);
			group.row["standardError"] = complete ? Json(std::sqrt(meanVariance)) : Json(nullptr);
			std::vector<double> medianValues;
			for (const auto& moment : moments) medianValues.push_back(moment.sum);
			group.row["median"] = complete ? Json(Util::Median(std::move(medianValues))) : Json(nullptr);
			group.row["medianBasis"] = complete ? Json("scan means") : Json(nullptr);
			group.row["repeatValues"] = values;
			group.row["validRepeats"] = moments.size();
			group.row["sampling"] = { { "kind", "within_run_repeats" }, { "repeatValues", std::move(values) },
				{ "validRepeats", moments.size() }, { "expectedRepeats", record.at("scanCount") } };
			result.push_back(std::move(group.row));
		}
		return result;
	}

	Json Compare(const Json& first, const Json& second)
	{
		const auto left = SummaryMetrics(first), right = SummaryMetrics(second);
		const bool sameSemantics = (first.at("kind") == "quick_scan") == (second.at("kind") == "quick_scan") &&
		                           (first.at("kind") == "upscaling_sweep") == (second.at("kind") == "upscaling_sweep") &&
		                           first.at("context").value("timingDefinitions", Json{}) == second.at("context").value("timingDefinitions", Json{}) &&
		                           first.at("context").value("runtime", Json{}) == second.at("context").value("runtime", Json{});
		Json result = { { "compatible", sameSemantics }, { "contextMatches", first.at("context") == second.at("context") },
			{ "settingsMatch", first.at("settings") == second.at("settings") }, { "rows", Json::array() } };
		std::map<std::pair<std::string, std::string>, std::pair<const Json*, const Json*>> pairs;
		for (const auto& row : left)
			pairs[{ row.at("id"), row.at("metric") }].first = &row;
		for (const auto& row : right)
			pairs[{ row.at("id"), row.at("metric") }].second = &row;
		for (const auto& [key, rows] : pairs) {
			const auto& [id, name] = key;
			const auto [a, b] = rows;
			const auto av = a ? a->value("value", Json(nullptr)) : Json(nullptr);
			const auto bv = b ? b->value("value", Json(nullptr)) : Json(nullptr);
			const bool comparable = sameSemantics && a && b && a->at("unit") == b->at("unit") &&
			                        a->at("relativeTo") == b->at("relativeTo") && a->at("source") == b->at("source") &&
			                        a->at("coverage") == b->at("coverage") && av.is_number() && bv.is_number();
			const double difference = comparable ? bv.get<double>() - av.get<double>() : 0;
			result["rows"].push_back({ { "id", id }, { "label", (a ? a : b)->at("label") }, { "metric", name },
				{ "unit", (a ? a : b)->at("unit") }, { "first", av }, { "second", bv },
				{ "difference", comparable && std::isfinite(difference) ? Json(difference) : Json(nullptr) } });
		}
		return result;
	}

	std::string Csv(const Json& record, const Json* comparison)
	{
		Validate(record);
		std::string text = "run,kind,status,feature,metric,unit,value,standard_error,current,comparison,p_value,missing_samples,failure,relative_to,coverage,sampling,difference_to_first,semantics,context,settings,source_commit,build_id,source_dirty,scan_index,scan_count,capture_requested_utc,capture_resolved_utc,run_failure,fpsvr_status,fpsvr_scan,fpsvr_metadata,measurement_source,median,median_basis\r\n";
		const auto compared = comparison ? Compare(record, *comparison) : Json{};
		for (const auto* run : { &record, comparison }) {
			if (!run)
				continue;
			Validate(*run);
			auto rows = SummaryMetrics(*run);
			if (run->at("kind") == "quick_scan" && run->value("scanCount", 1u) > 1)
				for (const auto& raw : Metrics(*run)) rows.push_back(raw);
			if (rows.empty())
				rows.push_back({ { "id", "" }, { "label", "" }, { "metric", "" }, { "unit", "" }, { "failure", run->value("failure", Json(nullptr)) } });
			for (const auto& row : rows) {
				std::vector<Json> cells{ run->at("id"), run->at("kind"), run->at("status"), row.at("label"), row.at("metric"), row.at("unit") };
				for (const auto* key : { "value", "standardError", "current", "comparison", "pValue", "missingSampleCount", "failure", "relativeTo", "coverage", "sampling" })
					cells.push_back(row.value(key, Json(nullptr)));
				Json difference = nullptr;
				if (run == comparison)
					for (const auto& entry : compared.at("rows"))
						if (entry.at("id") == row.at("id") && entry.at("metric") == row.at("metric"))
							difference = entry.at("difference");
				cells.push_back(difference);
				cells.push_back(Semantics(*run));
				cells.push_back(run->at("context").dump());
				cells.push_back(run->at("settings").dump());
				const auto build = run->at("context").value("build", Json::object());
				for (const auto* key : { "sourceCommit", "buildId", "sourceDirty" })
					cells.push_back(build.value(key, Json(nullptr)));
				cells.push_back(row.value("scanIndex", Json(nullptr)));
				cells.push_back(run->value("scanCount", Json(nullptr)));
				const auto sampling = row.value("sampling", Json::object());
				for (const auto* key : { "captureRequestedUtc", "captureResolvedUtc" })
					cells.push_back(sampling.value(key, Json(nullptr)));
				cells.push_back(run->value("failure", Json(nullptr)));
				const auto fpsvr = run->value("fpsVR", Json::object());
				cells.push_back(fpsvr.value("status", "not_requested"));
				Json matched = nullptr;
				for (const auto& scan : fpsvr.value("scans", Json::array()))
					if (scan.at("scanIndex") == row.value("scanIndex", Json(nullptr)))
						matched = scan;
				cells.push_back(matched);
				auto externalMetadata = fpsvr;
				externalMetadata.erase("scans");
				cells.push_back(externalMetadata);
				cells.push_back(row.value("source", "instrumented"));
				cells.push_back(row.value("median", Json(nullptr)));
				cells.push_back(row.value("medianBasis", Json(nullptr)));
				for (std::size_t i = 0; i < cells.size(); ++i) text += (i ? "," : "") + Cell(cells[i]);
				text += "\r\n";
			}
		}
		return text;
	}

	Store::Store(std::filesystem::path logDirectory, Writer write, FolderPicker pick) :
		folder(logDirectory / "CSX" / "Performance"), logPath(logDirectory / "CSX-Performance.log"), writer(std::move(write)), picker(std::move(pick))
	{
		jobs.push_back([this] {
			if (!folder.is_absolute())
				throw std::runtime_error("The SKSE log folder is unavailable.");
			std::filesystem::create_directories(folder);
			writer(logPath, Timestamp() + " CSX performance session started\n", true);
		});
		worker = std::thread([this] { Run(); });
	}

	Store::~Store()
	{
		{
			std::lock_guard lock(mutex);
			stopping = true;
			paused = false;
		}
		wake.notify_one();
		worker.join();
	}

	bool Store::TryPause()
	{
		std::lock_guard lock(mutex);
		if (working || !jobs.empty())
			return false;
		paused = true;
		return true;
	}

	bool Store::Busy() const
	{
		std::lock_guard lock(mutex);
		return working || !jobs.empty();
	}

	void Store::Resume()
	{
		{
			std::lock_guard lock(mutex);
			paused = false;
		}
		wake.notify_one();
	}

	void Store::Submit(std::function<void()> job)
	{
		{
			std::lock_guard lock(mutex);
			jobs.push_back(std::move(job));
		}
		wake.notify_one();
	}

	void Store::Run()
	{
		for (;;) {
			std::function<void()> job;
			{
				std::unique_lock lock(mutex);
				wake.wait(lock, [&] { return stopping || (!paused && !jobs.empty()); });
				if (stopping && jobs.empty())
					return;
				working = true;
				job = std::move(jobs.front());
				jobs.pop_front();
			}
			try {
				job();
			} catch (const std::exception& error) {
				std::lock_guard lock(mutex);
				view.error = error.what();
			}
			{
				std::lock_guard lock(mutex);
				working = false;
			}
		}
	}

	View Store::Snapshot() const
	{
		std::lock_guard lock(mutex);
		auto result = view;
		result.busy = working || !jobs.empty();
		result.unsaved = unsaved.size();
		return result;
	}

	Json Store::Read(const std::string& id) const
	{
		if (!SafeId(id))
			throw std::runtime_error("Invalid result identity.");
		const auto path = folder / (id + ".json");
		if (std::filesystem::file_size(path) > MaximumFileBytes)
			throw std::runtime_error("Performance result exceeds the size limit.");
		std::ifstream input(path, std::ios::binary);
		if (!input)
			throw std::runtime_error("Cannot open performance result.");
		auto record = Json::parse(input, [](int depth, Json::parse_event_t, Json&) {
			if (depth > 128)
				throw std::runtime_error("Performance result nesting exceeds the limit.");
			return true;
		});
		Validate(record);
		if (record.at("id") != id)
			throw std::runtime_error("Result identity does not match its filename.");
		if (record.at("status") == "running")
			record["status"] = "interrupted";
		return record;
	}

	void Store::UpdateHistory(const Json& record)
	{
		std::shared_ptr<const Json> previous;
		{
			std::lock_guard lock(mutex);
			previous = view.history;
		}
		auto history = *previous;
		history.erase(std::remove_if(history.begin(), history.end(), [&](const Json& row) { return row.at("id") == record.at("id"); }), history.end());
		history.push_back(Summary(record));
		std::sort(history.begin(), history.end(), [](const Json& a, const Json& b) { return a.at("id") > b.at("id"); });
		auto next = std::make_shared<const Json>(std::move(history));
		{
			std::lock_guard lock(mutex);
			view.history = std::move(next);
		}
	}

	void Store::WriteRecord(const Json& record)
	{
		Validate(record);
		const auto id = record.at("id").get<std::string>();
		try {
			if (!folder.is_absolute())
				throw std::runtime_error("The SKSE log folder is unavailable.");
			const auto bytes = record.dump(2, ' ', false, Json::error_handler_t::replace);
			if (bytes.size() > MaximumFileBytes)
				throw std::runtime_error("Performance result exceeds the size limit.");
			writer(folder / (id + ".json"), bytes, true);
			if (record.value("automaticCsv", false))
				writer(folder / (id + ".csv"), Csv(record), true);
			if (record.contains("fpsVR"))
				writer(folder / (id + ".html"), Html(record), true);
			if (record.at("status") != "running") {
				const auto existing = std::find_if(sessionEntries.begin(), sessionEntries.end(), [&](const auto& entry) { return entry.at("id") == id; });
				if (existing == sessionEntries.end())
					sessionEntries.push_back(Summary(record));
				else
					*existing = Summary(record);
				const auto reportPath = folder / ("CSX-Performance-session-" + sessionId + ".html");
				writer(reportPath, HtmlSession(SessionRecords()), true);
				std::lock_guard lock(mutex);
				view.sessionReport = Util::PathToUtf8(reportPath);
				view.sessionRuns = sessionEntries.size();
			}
			{
				std::lock_guard lock(mutex);
				unsaved.erase(id);
				view.error.clear();
			}
		} catch (...) {
			std::lock_guard lock(mutex);
			unsaved[id] = record;
			throw;
		}
		UpdateHistory(record);
		std::ofstream log(logPath, std::ios::binary | std::ios::app);
		log << Timestamp() << ' ' << Summary(record).dump() << " failure=" << record.value("failure", Json(nullptr)).dump()
			<< " results=" << Metrics(record).dump(-1, ' ', false, Json::error_handler_t::replace) << '\n';
		log.flush();
		if (!log)
			throw std::runtime_error("Result saved, but the performance session log could not be updated.");
	}

	void Store::Save(Json record, std::function<void(Json&)> finalize)
	{
		Submit([this, record = std::move(record), finalize = std::move(finalize)]() mutable {
			if (finalize) {
				const auto measured = record;
				try {
					finalize(record);
					Validate(record);
				} catch (const std::exception& error) {
					record = measured;
					record["fpsVR"] = { { "status", "omitted" }, { "reason", std::string("External recording finalization failed: ") + error.what() }, { "scans", Json::array() } };
				}
			}
			WriteRecord(record);
		});
	}

	void Store::Refresh()
	{
		Submit([this] {
			Json history = Json::array();
			std::size_t invalid = 0;
			for (const auto& file : std::filesystem::directory_iterator(folder)) {
				if (!file.is_regular_file() || file.path().extension() != ".json")
					continue;
				try {
					history.push_back(Summary(Read(file.path().stem().string())));
				} catch (const std::exception&) {
					++invalid;
				}
			}
			std::sort(history.begin(), history.end(), [](const Json& a, const Json& b) { return a.at("id") > b.at("id"); });
			std::lock_guard lock(mutex);
			view.history = std::make_shared<const Json>(std::move(history));
			view.error = invalid ? std::to_string(invalid) + " unreadable or unsupported result files were skipped." : "";
		});
	}

	void Store::Load(std::string id, bool comparison)
	{
		Submit([this, id = std::move(id), comparison] {
			auto record = id.empty() ? nullptr : std::make_shared<const Json>(Read(id));
			std::lock_guard lock(mutex);
			(comparison ? view.second : view.first) = std::move(record);
			view.error.clear();
		});
	}

	void Store::Retry()
	{
		Submit([this] {
			std::map<std::string, Json> pending;
			{
				std::lock_guard lock(mutex);
				pending = unsaved;
			}
			for (const auto& [id, record] : pending) WriteRecord(record);
		});
	}

	Json Store::SessionRecords() const
	{
		Json records = Json::array();
		std::uintmax_t remaining = 64 * 1024 * 1024;
		for (const auto& entry : sessionEntries) {
			try {
				const auto id = entry.at("id").get<std::string>();
				const auto size = std::filesystem::file_size(folder / (id + ".json"));
				if (size > remaining)
					throw std::runtime_error("Session report size limit reached; open this run separately.");
				remaining -= size;
				records.push_back(Read(id));
			} catch (const std::exception& error) {
				auto unavailable = entry;
				unavailable.update({ { "schema", 1 }, { "sourceUnavailable", true },
					{ "failure", std::string("Saved run data unavailable: ") + error.what() },
					{ "context", Json::object() }, { "settings", Json::object() }, { "results", Json::array() } });
				records.push_back(std::move(unavailable));
			}
		}
		return records;
	}

	void Store::Export(std::string id, std::string comparisonId, std::string format, std::filesystem::path destination, bool session)
	{
		Submit([this, id = std::move(id), comparisonId = std::move(comparisonId), format = std::move(format), destination = std::move(destination), session]() mutable {
			if (format != "html" && format != "csv" && format != "both")
				throw std::runtime_error("Export format must be html, csv or both.");
			if (session && format != "html")
				throw std::runtime_error("Session export requires HTML.");
			if (!session && !comparisonId.empty() && format != "csv") {
				const auto belongsToSession = [&](const std::string& runId) {
					return std::ranges::any_of(sessionEntries, [&](const Json& entry) { return entry.at("id") == runId; });
				};
				if (!belongsToSession(id) || !belongsToSession(comparisonId))
					throw std::runtime_error("HTML comparisons require measurements from the current game session. Older runs can be opened individually.");
			}
			const auto record = session ? SessionRecords() : Read(id);
			const auto comparison = session || comparisonId.empty() ? Json(nullptr) : Read(comparisonId);
			if (destination.empty()) {
				if (!picker)
					throw std::runtime_error("No folder picker is available.");
				{
					std::lock_guard lock(mutex);
					view.message = "Choose an export folder on the desktop.";
				}
				destination = picker(folder);
				if (destination.empty()) {
					std::lock_guard lock(mutex);
					view.message = "Export cancelled.";
					return;
				}
			}
			if (!destination.is_absolute() || !std::filesystem::is_directory(destination))
				throw std::runtime_error("Choose an existing absolute export folder.");
			const auto base = destination / ("CSX-Performance-" + NewId());
			const auto* other = comparison.is_null() ? nullptr : &comparison;
			if (format != "csv") {
				auto path = base;
				path += ".html";
				writer(path, session ? HtmlSession(record) : Html(record, other), false);
			}
			if (format != "html") {
				auto path = base;
				path += ".csv";
				writer(path, Csv(record, other), false);
			}
			std::lock_guard lock(mutex);
			view.error.clear();
			view.message = "Export complete: " + Util::PathToUtf8(destination);
		});
	}
}
