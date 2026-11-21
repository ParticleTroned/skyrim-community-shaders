#include "PerformanceResults.h"
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
		std::set<std::string> keys;
		for (const auto& row : record.at("results")) {
			if (!row.is_object() || !row.at("label").is_string() || row.at("label").get_ref<const std::string&>().size() > 1024 ||
				(row.contains("failure") && !row["failure"].is_null() && !row["failure"].is_string()) ||
				!row.at("metrics").is_object() ||
				!keys.insert(row.at("id").get<std::string>()).second)
				throw std::runtime_error("Invalid or duplicate result row.");
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
				entry["sampling"] = record.value("capture", Json{ { "current", row.value("currentSamples", Json(nullptr)) }, { "comparison", row.value("comparisonSamples", Json(nullptr)) } });
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
		std::string text = "run,kind,status,feature,metric,unit,value,standard_error,current,comparison,p_value,missing_samples,failure,relative_to,coverage,sampling,difference_to_first,semantics,context,settings\r\n";
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
				for (std::size_t i = 0; i < cells.size(); ++i) text += (i ? "," : "") + Cell(cells[i]);
				text += "\r\n";
			}
		}
		return text;
	}

	std::string Html(const Json& record, const Json* comparison)
	{
		Validate(record);
		std::string text = "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width\"><title>CSX performance results</title><style>body{font:16px system-ui;max-width:1200px;margin:2em auto;padding:0 1em;color:#eee;background:#171b20}table{border-collapse:collapse;width:100%}td,th{padding:.6em;text-align:left;border-bottom:1px solid #555}h1,h2{color:#ffb52b}pre{white-space:pre-wrap;overflow-wrap:anywhere}details{margin:1em 0}</style><h1>CSX performance results</h1>";
		for (const auto* run : { &record, comparison }) {
			if (!run)
				continue;
			Validate(*run);
			text += "<h2>" + Escape(Text(run->at("kind"))) + "</h2><p>" + Escape(Text(run->at("started"))) + " · " + Escape(Text(run->at("status"))) + "</p><p>" + Semantics(*run) + "</p>";
			if (run->contains("failure") && !run->at("failure").is_null())
				text += "<p>" + Escape(Text(run->at("failure"))) + "</p>";
			text += "<table><tr><th>Feature / case</th><th>Metric</th><th>Value</th><th>Unit</th><th>Standard error</th><th>Current</th><th>Comparison</th><th>p-value</th><th>Compared with</th><th>Coverage</th></tr>";
			for (const auto& row : Metrics(*run)) {
				text += "<tr>";
				for (const auto* key : { "label", "metric", "value", "unit", "standardError", "current", "comparison", "pValue", "relativeTo", "coverage" })
					text += "<td>" + Escape(Text(row.value(key, Json(nullptr)))) + "</td>";
				text += "</tr>";
			}
			text += "</table><details><summary>Context, settings and sample validity</summary><pre>" + Escape(run->dump(2)) + "</pre></details>";
		}
		if (comparison) {
			const auto compared = Compare(record, *comparison);
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
		return text + "</html>";
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

	void Store::Save(Json record)
	{
		Submit([this, record = std::move(record)] { WriteRecord(record); });
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

	void Store::Export(std::string id, std::string comparisonId, std::string format, std::filesystem::path destination)
	{
		Submit([this, id = std::move(id), comparisonId = std::move(comparisonId), format = std::move(format), destination = std::move(destination)]() mutable {
			if (format != "html" && format != "csv" && format != "both")
				throw std::runtime_error("Export format must be html, csv or both.");
			const auto record = Read(id);
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
				writer(path, Html(record, other), false);
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
