#include "PerformanceResults.h"
#include "PerformanceFpsVR.h"
#include "PerformanceQuickScan.h"
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
	namespace
	{
		constexpr std::uintmax_t MaximumFileBytes = 8 * 1024 * 1024;
		bool SafeId(std::string_view id)
		{
			return !id.empty() && id.size() <= 100 && std::ranges::all_of(id, [](char c) {
				return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
			});
		}

		std::string Text(const Json& value)
		{
			return value.is_null() ? "Unavailable" : value.is_string() ? value.get<std::string>() :
			                                                             value.dump();
		}

		std::string Display(const Json& value)
		{
			if (!value.is_number_float())
				return Text(value);
			std::ostringstream number;
			number << std::fixed << std::setprecision(3) << value.get<double>();
			return number.str();
		}

		Json WholeFrameMean(const Json& record, const char* metric)
		{
			double mean = 0, count = 0;
			for (const auto& scan : record.value("fpsVR", Json::object()).value("scans", Json::array())) {
				const double samples = scan.at("samples").get<double>();
				mean += (scan.at(metric).at("meanMs").get<double>() - mean) * (samples / (count + samples));
				count += samples;
			}
			return count > 0 ? Json(mean) : Json(nullptr);
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
				if (!index.is_number_integer() || index < 1 || index > record.value("scanCount", PerformanceQuickScan::kBatchScans) || !indices.insert(index.get<unsigned>()).second ||
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

		std::string Escape(std::string_view text)
		{
			std::string result;
			for (char c : text) {
				switch (c) {
				case '&':
					result += "&amp;";
					break;
				case '<':
					result += "&lt;";
					break;
				case '>':
					result += "&gt;";
					break;
				case '"':
					result += "&quot;";
					break;
				case '\'':
					result += "&#39;";
					break;
				default:
					result += c;
					break;
				}
			}
			return result;
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

		const char* Semantics(const Json& record)
		{
			return record.at("kind") == "quick_scan" ?
			           "Instrumented self-time per frame, not an On/Off saving. Shared and uninstrumented work is excluded." :
			           "Current minus comparison (Off/None), with standard error. FPS uses comparison minus current. Independent feature costs must not be added together.";
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
		for (const auto& row : record.at("results")) {
			if (!row.is_object() || !row.at("label").is_string() || row.at("label").get_ref<const std::string&>().size() > 1024 ||
				(row.contains("failure") && !row["failure"].is_null() && !row["failure"].is_string()) ||
				!row.at("metrics").is_object() ||
				!keys.insert(row.at("id").get<std::string>()).second)
				throw std::runtime_error("Invalid or duplicate result row.");
			OptionalObject(row, "sampling");
			if (row.contains("sampling")) {
				OptionalText(row["sampling"], "captureRequestedUtc");
				OptionalText(row["sampling"], "captureResolvedUtc");
			}
			if (row.contains("scanIndex") && (!row["scanIndex"].is_number_integer() || row["scanIndex"] < 1 || row["scanIndex"] > record.value("scanCount", PerformanceQuickScan::kBatchScans)))
				throw std::runtime_error("Invalid result scan index.");
			for (const auto& [name, metric] : row.at("metrics").items()) {
				if ((name != "frame" && name != "gpu" && name != "cpu" && name != "fps") || !metric.is_object() ||
					(metric.at("unit") != (name == "fps" ? "fps" : "ms")))
					throw std::runtime_error("Invalid measurement metric.");
				for (const auto* field : { "value", "standardError", "current", "comparison", "pValue" }) {
					if (metric.contains(field) && !metric[field].is_null() &&
						(!metric[field].is_number() || !std::isfinite(metric[field].get<double>())))
						throw std::runtime_error("Non-finite measurement metric.");
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
				entry["id"] = row.at("id");
				entry["label"] = row.at("label");
				entry["metric"] = name;
				entry["relativeTo"] = row.value("relativeTo", Json(nullptr));
				entry["failure"] = row.value("failure", Json(nullptr));
				entry["coverage"] = row.value("coverage", Json(nullptr));
				entry["scanIndex"] = row.value("scanIndex", Json(nullptr));
				if (record.value("scanCount", 1u) > 1 && entry["scanIndex"].is_number_integer())
					entry["label"] = "Scan " + entry["scanIndex"].dump() + " / " + row.at("label").get<std::string>();
				entry["sampling"] = row.value("sampling", record.value("capture", Json{ { "current", row.value("currentSamples", Json(nullptr)) }, { "comparison", row.value("comparisonSamples", Json(nullptr)) } }));
				result.push_back(std::move(entry));
			}
		}
		return result;
	}

	Json Compare(const Json& first, const Json& second)
	{
		Validate(first);
		Validate(second);
		const bool sameSemantics = (first.at("kind") == "quick_scan") == (second.at("kind") == "quick_scan") &&
		                           (first.at("kind") == "upscaling_sweep") == (second.at("kind") == "upscaling_sweep") &&
		                           first.at("context").value("timingDefinitions", Json{}) == second.at("context").value("timingDefinitions", Json{}) &&
		                           first.at("context").value("runtime", Json{}) == second.at("context").value("runtime", Json{});
		Json result = { { "compatible", sameSemantics }, { "contextMatches", first.at("context") == second.at("context") },
			{ "settingsMatch", first.at("settings") == second.at("settings") }, { "rows", Json::array() } };
		const auto left = Metrics(first), right = Metrics(second);
		std::set<std::pair<std::string, std::string>> keys;
		for (const auto* rows : { &left, &right })
			for (const auto& row : *rows) keys.emplace(row.at("id"), row.at("metric"));
		for (const auto& [id, name] : keys) {
			const auto find = [&](const Json& rows) -> const Json* {
				for (const auto& row : rows)
					if (row.at("id") == id && row.at("metric") == name)
						return &row;
				return nullptr;
			};
			const auto* a = find(left);
			const auto* b = find(right);
			const auto av = a ? a->value("value", Json(nullptr)) : Json(nullptr);
			const auto bv = b ? b->value("value", Json(nullptr)) : Json(nullptr);
			const bool comparable = sameSemantics && a && b && a->at("unit") == b->at("unit") &&
			                        a->at("relativeTo") == b->at("relativeTo") && av.is_number() && bv.is_number();
			result["rows"].push_back({ { "id", id }, { "label", (a ? a : b)->at("label") }, { "metric", name },
				{ "unit", (a ? a : b)->at("unit") }, { "first", av }, { "second", bv },
				{ "difference", comparable ? Json(bv.get<double>() - av.get<double>()) : Json(nullptr) } });
		}
		return result;
	}

	std::string Csv(const Json& record, const Json* comparison)
	{
		Validate(record);
		std::string text = "run,kind,status,feature,metric,unit,value,standard_error,current,comparison,p_value,missing_samples,failure,relative_to,coverage,sampling,difference_to_first,semantics,context,settings,source_commit,build_id,source_dirty,scan_index,scan_count,capture_requested_utc,capture_resolved_utc,run_failure,fpsvr_status,fpsvr_scan,fpsvr_metadata\r\n";
		const auto compared = comparison ? Compare(record, *comparison) : Json{};
		for (const auto* run : { &record, comparison }) {
			if (!run)
				continue;
			Validate(*run);
			auto rows = Metrics(*run);
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
				for (std::size_t i = 0; i < cells.size(); ++i) text += (i ? "," : "") + Cell(cells[i]);
				text += "\r\n";
			}
		}
		return text;
	}

	static std::string Report(const Json& records, bool comparison)
	{
		if (!records.is_array() || records.empty())
			throw std::runtime_error("No measurements in this session yet.");
		std::string text = R"html(<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>CSX performance report</title><style>
:root{color-scheme:dark;font:15px system-ui;--line:#344358;--muted:#a6b6cb;--accent:#75d6cc}body{margin:0;background:#0e1521;color:#e7edf6}main{max-width:1440px;margin:auto;padding:32px 24px}header{padding:20px 0 28px}h1{font-size:36px;margin:8px 0}h2{margin:0 0 12px}h3{margin-top:28px}p{line-height:1.6}a{color:var(--accent)}small,.muted{color:var(--muted)}.eyebrow{text-transform:uppercase;letter-spacing:.15em;color:var(--accent)}.run{background:#172233;border:1px solid var(--line);border-radius:14px;padding:24px;margin:24px 0;scroll-margin-top:16px}.scroll{overflow:auto}table{border-collapse:collapse;width:100%;font-variant-numeric:tabular-nums}td,th{padding:12px 10px;text-align:left;border-bottom:1px solid var(--line);vertical-align:top}th{font-size:12px;text-transform:uppercase;letter-spacing:.04em;color:var(--muted)}tbody tr:hover{background:#203049}pre{white-space:pre-wrap;overflow-wrap:anywhere;max-height:550px;overflow:auto;font-size:12px}details{margin:16px 0;padding:14px;background:#101a29;border:1px solid var(--line);border-radius:8px}summary{cursor:pointer;color:var(--accent);font-weight:600}.metadata{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:16px}.metadata details{margin:0}meter{display:block;width:100%;min-width:70px;height:8px;margin-top:5px;accent-color:var(--accent)}@media(max-width:700px){main{padding:16px 10px}.run{padding:14px}h1{font-size:28px}}@media print{body{background:white;color:black}.run{break-inside:avoid}details{background:white}a{color:#245e59}}
</style></head><body><main><header><div class="eyebrow">Community Shaders Expanded</div><h1>Performance report</h1><p class="muted">Compare recorded costs, check whole-frame timings, and inspect the settings behind every run.</p></header><h2>Session overview</h2><p>Each row is a separate measurement. Lower times mean less work, but compare matching scenes, settings and rendering resolutions. Independent On/Off feature costs must not be added together.</p><div class="scroll"><table><thead><tr><th>Run</th><th>Scene</th><th>Measurement</th><th>Status</th><th>fpsVR</th><th>Whole CPU mean ms</th><th>Whole GPU mean ms</th><th>Source commit</th></tr></thead><tbody>)html";
		for (std::size_t i = 0; i < records.size(); ++i) {
			const auto& run = records[i];
			Validate(run);
			const auto& context = run.at("context");
			text += "<tr><td><a href=\"#run-" + std::to_string(i + 1) + "\">Run " + std::to_string(i + 1) + "</a><br><small>" + Escape(Text(run.at("started"))) + "</small></td>";
			for (const auto& value : { context.value("cellName", Json(nullptr)), run.at("kind"), run.value("sourceUnavailable", false) ? Json("Data unavailable") : run.at("status"),
					 run.value("fpsVR", Json::object()).value("status", Json("not requested")), WholeFrameMean(run, "cpu"), WholeFrameMean(run, "gpu"), context.value("build", Json::object()).value("sourceCommit", Json(nullptr)) })
				text += "<td>" + Escape(Display(value)) + "</td>";
			text += "</tr>";
		}
		text += "</tbody></table></div>";
		for (std::size_t i = 0; i < records.size(); ++i) {
			const auto* run = &records[i];
			Validate(*run);
			text += "<section class=\"run\" id=\"run-" + std::to_string(i + 1) + "\"><div class=\"eyebrow\">Run " + std::to_string(i + 1) + "</div><h2>" + Escape(Text(run->at("context").value("cellName", run->at("kind")))) + "</h2><p>" + Escape(Text(run->at("started"))) + " · " + Escape(Text(run->at("status"))) + "</p><p>" + Semantics(*run) + "</p>";
			if (run->contains("failure") && !run->at("failure").is_null())
				text += "<p>" + Escape(Text(run->at("failure"))) + "</p>";
			const auto build = run->at("context").value("build", Json::object());
			text += "<p>Commit: " + Escape(Text(build.value("sourceCommit", Json(nullptr)))) + " · Build ID: " +
			        Escape(Text(build.value("buildId", Json(nullptr)))) + " · Modified source: " + Escape(Text(build.value("sourceDirty", Json(nullptr)))) + "</p>";
			text += "<div class=\"metadata\"><details><summary>Settings for this run</summary>";
			for (const auto& [feature, settings] : run->at("settings").items())
				text += "<details><summary>" + Escape(feature) + "</summary><pre>" + Escape(settings.dump(2)) + "</pre></details>";
			text += "</details><details><summary>Scene, hardware and build</summary><pre>" + Escape(run->at("context").dump(2)) + "</pre></details></div>";
			if (run->contains("fpsVR")) {
				const auto& fpsvr = run->at("fpsVR");
				text += "<h3>fpsVR whole-frame timings</h3><p>" + Escape(fpsvr.value("status", "omitted")) + " · " + Escape(fpsvr.value("reason", "")) + "</p>";
				text += "<p>Matched using the latest completed SteamVR timestamps at capture request and resolution. Boundaries are approximate and include profiler query completion latency. Warm-up and gaps outside those boundaries are excluded. CPU and GPU times overlap and must not be added to the profiler totals.</p>";
				text += "<div class=\"scroll\"><table><tr><th>Scan</th><th>Samples</th><th>Metric</th><th>Mean ms</th><th>Median ms</th><th>p95 ms</th><th>p99 ms</th></tr>";
				for (const auto& scan : fpsvr.value("scans", Json::array())) {
					for (const auto* metric : { "cpu", "gpu" }) {
						text += "<tr><td>" + Escape(Text(scan.at("scanIndex"))) + "</td><td>" + Escape(Text(scan.at("samples"))) + "</td><td>" + metric + "</td>";
						for (const auto* key : { "meanMs", "medianMs", "p95Ms", "p99Ms" }) text += "<td>" + Escape(Display(scan.at(metric).at(key))) + "</td>";
						text += "</tr>";
					}
				}
				text += "</table></div>";
			}
			text += "<h3>" + std::string(run->at("kind") == "quick_scan" ? "Instrumented feature self-times" : "Measured On/Off costs") + "</h3><div class=\"scroll\"><table><tr><th>Feature / case</th><th>Metric</th><th>Value</th><th>Unit</th><th>Standard error</th><th>Current</th><th>Comparison</th><th>p-value</th><th>Compared with</th><th>Coverage</th></tr>";
			auto rows = Metrics(*run);
			double maximum = 0;
			for (const auto& row : rows)
				if (row.value("value", Json(nullptr)).is_number() && row.at("unit") == "ms")
					maximum = std::max(maximum, std::abs(row.at("value").get<double>()));
			for (const auto& row : rows) {
				text += "<tr>";
				for (const auto* key : { "label", "metric", "value", "unit", "standardError", "current", "comparison", "pValue", "relativeTo", "coverage" }) {
					text += "<td>" + Escape(Display(row.value(key, Json(nullptr))));
					if (std::string_view(key) == "value" && maximum > 0 && row.at("unit") == "ms" && row.value(key, Json(nullptr)).is_number())
						text += "<meter title=\"Magnitude relative to this run's largest ms value\" min=\"0\" max=\"" + std::to_string(maximum) + "\" value=\"" + std::to_string(std::abs(row.at(key).get<double>())) + "\"></meter>";
					text += "</td>";
				}
				text += "</tr>";
			}
			text += "</table></div><details><summary>Complete run metadata and sample validity</summary><pre>" + Escape(run->dump(2)) + "</pre></details></section>";
		}
		if (comparison) {
			const auto compared = Compare(records.at(0), records.at(1));
			text += "<h2>Comparison: second minus first</h2><p>These are differences between recorded measurements, not predictions or a new significance test. Check scene, hardware and settings before interpreting them.</p>";
			if (!compared.at("compatible").get<bool>())
				text += "<p>Different measurement definitions; differences are unavailable.</p>";
			if (!compared.at("contextMatches").get<bool>() || !compared.at("settingsMatch").get<bool>())
				text += "<p>Context or settings differ.</p>";
			text += "<table><tr><th>Feature / case</th><th>Metric</th><th>First</th><th>Second</th><th>Difference</th><th>Unit</th></tr>";
			for (const auto& row : compared.at("rows")) {
				text += "<tr>";
				for (const auto* key : { "label", "metric", "first", "second", "difference", "unit" }) text += "<td>" + Escape(Text(row.at(key))) + "</td>";
				text += "</tr>";
			}
			text += "</table>";
		}
		return text + "</main></body></html>";
	}

	std::string Html(const Json& record, const Json* comparison)
	{
		auto records = Json::array({ record });
		if (comparison)
			records.push_back(*comparison);
		return Report(records, comparison != nullptr);
	}

	std::string HtmlSession(const Json& records) { return Report(records, false); }

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
			const auto record = session ? SessionRecords() : Read(id);
			const auto comparison = comparisonId.empty() ? Json(nullptr) : Read(comparisonId);
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
