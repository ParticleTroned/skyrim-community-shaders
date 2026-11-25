#include "PerformanceResults.h"
#include "PerformanceResultsCharts.h"
#include "PerformanceResultsFormatting.h"
#include "PerformanceTuningStatistics.h"
#include "ThemePalette.h"
#include "Utils/Statistics.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <vector>

namespace PerformanceResults
{
	using namespace ReportFormatting;
	namespace
	{
		using ThemeColor = MenuUI::ThemePalette::Color;
		namespace Palette = MenuUI::ThemePalette;
		ThemeColor ThemeValue(const Json& theme, const char* section, const char* key, ThemeColor fallback)
		{
			if (!theme.is_object() || !theme.contains(section) || !theme.at(section).is_object() || !theme.at(section).contains(key))
				return fallback;
			const auto& channels = theme.at(section).at(key);
			if (!channels.is_array() || channels.size() != 4 || std::ranges::any_of(channels, [](const Json& value) {
					return !value.is_number() || !std::isfinite(value.get<double>()) || value < 0 || value > 1;
				}))
				return fallback;
			return { channels[0].get<float>(), channels[1].get<float>(), channels[2].get<float>(), channels[3].get<float>() };
		}
		std::string CssColor(ThemeColor color)
		{
			return "rgb(" + std::to_string(std::lround(color.r * 255)) + "," + std::to_string(std::lround(color.g * 255)) + "," + std::to_string(std::lround(color.b * 255)) + ")";
		}
		std::string ThemeStyle(const Json& record)
		{
			const auto presentation = record.value("presentation", Json::object());
			const auto theme = presentation.is_object() ? presentation.value("theme", Json::object()) : Json::object();
			const auto base = ThemeValue(theme, "Palette", "Background", Palette::Background);
			const auto background = ThemeValue(theme, "Colors", "WindowBg", base);
			const auto control = ThemeValue(theme, "Colors", "FrameBg", ThemeValue(theme, "Palette", "FrameBorder", Palette::FrameBorder));
			const auto text = ThemeValue(theme, "Colors", "Text", ThemeValue(theme, "Palette", "Text", Palette::Text));
			const auto accent = ThemeValue(theme, "Colors", "TextLink", ThemeValue(theme, "StatusPalette", "InfoColor", Palette::Info));
			const auto warning = ThemeValue(theme, "StatusPalette", "Warning", Palette::Warning);
			std::string css = ":root{color-scheme:" + std::string(background.r * .2126 + background.g * .7152 + background.b * .0722 > .5 ? "light;" : "dark;");
			for (const auto& [name, color] : std::vector<std::pair<const char*, ThemeColor>>{
					 { "background", background }, { "text", text }, { "panel", ThemeValue(theme, "Colors", "ChildBg", background) },
					 { "control", control }, { "hover", ThemeValue(theme, "Colors", "HeaderHovered", control) },
					 { "line", ThemeValue(theme, "Colors", "TableBorderLight", ThemeValue(theme, "Palette", "Separator", Palette::Separator)) },
					 { "muted", ThemeValue(theme, "Colors", "TextDisabled", ThemeValue(theme, "StatusPalette", "Disable", Palette::Disable)) },
					 { "accent", accent }, { "warning", warning }, { "median", warning },
					 { "series-0", accent }, { "series-1", ThemeValue(theme, "StatusPalette", "RestartNeeded", Palette::RestartNeeded) },
					 { "series-2", ThemeValue(theme, "StatusPalette", "SuccessColor", Palette::Success) }, { "series-3", ThemeValue(theme, "Palette", "ResizeGrip", Palette::ResizeGrip) },
					 { "series-4", ThemeValue(theme, "StatusPalette", "Error", Palette::Error) }, { "series-5", text } })
				css += "--" + std::string(name) + ":" + CssColor(color) + ";";
			return css + "}";
		}
		Json WholeFrameStatistics(const Json& record, const char* metric)
		{
			const auto scans = record.value("fpsVR", Json::object()).value("scans", Json::array());
			std::vector<PerformanceTuningStatistics::Moments> moments;
			for (const auto& scan : scans) moments.push_back({ scan.at(metric).at("meanMs").get<double>(), 1.0 });
			double mean = 0, meanVariance = 0;
			const bool complete = moments.size() == record.value("scanCount", scans.size());
			const bool repeated = complete && PerformanceTuningStatistics::TryGetBlockMeanStatistics(moments, mean, meanVariance);
			std::vector<double> values;
			for (const auto& moment : moments) values.push_back(moment.sum);
			return { { "value", repeated ? Json(mean) : complete && moments.size() == 1 ? Json(moments.front().sum) :
																						  Json(nullptr) },
				{ "standardError", repeated ? Json(std::sqrt(meanVariance)) : Json(nullptr) },
				{ "median", repeated ? Json(Util::Median(std::move(values))) : complete && scans.size() == 1 ? scans.front().at(metric).at("medianMs") :
																											   Json(nullptr) },
				{ "medianBasis", repeated ? Json("scan means") : complete && scans.size() == 1 ? Json("fpsVR samples") :
																								 Json(nullptr) } };
		}

	}

	static std::string FpsVrStatus(const Json& run)
	{
		const auto fpsvr = run.value("fpsVR", Json::object());
		return fpsvr.value("status", "not requested") + std::string(fpsvr.contains("recordingStopped") && !fpsvr.at("recordingStopped").get<bool>() ? " / stop unconfirmed" : "");
	}

	static std::string QuickScanTable(const Json& run, const Json& summary, bool wholeFrame)
	{
		const auto count = run.value("scanCount", 1u);
		auto rows = summary;
		std::erase_if(rows.get_ref<Json::array_t&>(), [&](const Json& row) {
			const auto values = row.value("repeatValues", Json::array({ row.value("value", Json(nullptr)) }));
			return row.value("source", "instrumented") != (wholeFrame ? "csx_whole_frame" : "instrumented") ||
			       std::ranges::none_of(values, [](const Json& value) { return value.is_number(); });
		});
		if (rows.empty())
			return {};
		std::string text = wholeFrame ? "<h3>CSX Performance Tuning whole-frame timings</h3>" : "<h3>Instrumented feature self-times</h3>";
		text += "<p>Mean and standard error use only the repeats within this experiment. SE = sample standard deviation / square root of repeat count. Incomplete triplicates have no aggregate.</p><div class=\"scroll\"><table><tr><th>Feature</th><th>Metric</th>";
		for (unsigned index = 1; index <= count; ++index)
			text += "<th>Scan " + std::to_string(index) + "</th>";
		text += "<th>Mean</th><th>SE</th><th>Unit</th><th>Coverage</th></tr>";
		for (const auto& row : rows) {
			text += "<tr><td>" + Escape(Text(row.at("label"))) + "</td><td>" + Escape(Text(row.at("metric"))) + "</td>";
			for (const auto& value : row.value("repeatValues", Json::array({ row.value("value", Json(nullptr)) })))
				text += "<td>" + Escape(Display(value)) + "</td>";
			for (const auto* key : { "value", "standardError", "unit", "coverage" })
				text += "<td>" + Escape(Display(row.value(key, Json(nullptr)))) + "</td>";
			text += "</tr>";
		}
		return text + "</table></div>";
	}

	static std::string ExperimentTable(const Json& records, const std::vector<Json>& summaries)
	{
		std::vector<std::pair<std::size_t, Json>> experiments;
		std::map<std::pair<std::string, std::string>, Json> keys;
		for (std::size_t i = 0; i < records.size(); ++i) {
			if (records[i].at("kind") != "quick_scan")
				continue;
			auto rows = summaries[i];
			for (const auto& row : rows)
				if (row.value("value", Json(nullptr)).is_number())
					keys.emplace(std::make_pair(row.at("id").get<std::string>(), row.at("metric").get<std::string>()), row);
			experiments.emplace_back(i, std::move(rows));
		}
		if (experiments.size() < 2 || keys.empty())
			return {};
		std::string text = "<h2>Experiments side by side</h2><p>Each column is a separate run. Each SE uses only that run's triplicate; experiments are never pooled. Check each run's scene and settings before comparing.</p><div class=\"scroll\"><table><tr><th>Feature / source</th><th>Metric</th><th>Unit</th>";
		for (const auto& [index, rows] : experiments)
			text += "<th><a href=\"#run-" + std::to_string(index + 1) + "\">Experiment " + std::to_string(index + 1) + "</a><br><small>" + Escape(SceneLabel(records[index].at("context"))) + "</small><br>Mean &plusmn; SE</th>";
		text += "</tr>";
		for (const auto& [key, rowInfo] : keys) {
			text += "<tr><td>" + Escape(Text(rowInfo.at("label"))) + "</td><td>" + Escape(key.second) + "</td><td>" + Escape(Text(rowInfo.at("unit"))) + "</td>";
			for (const auto& [index, rows] : experiments) {
				const auto row = std::find_if(rows.begin(), rows.end(), [&](const Json& value) { return value.at("id") == key.first && value.at("metric") == key.second; });
				text += "<td>";
				if (row == rows.end() || !row->value("value", Json(nullptr)).is_number())
					text += "Unavailable";
				else {
					text += Escape(Display(row->at("value")));
					if (row->value("standardError", Json(nullptr)).is_number())
						text += " &plusmn; " + Escape(Display(row->at("standardError")));
				}
				text += "</td>";
			}
			text += "</tr>";
		}
		return text + "</table></div>";
	}

	static std::string SessionOverview(const Json& records, const std::vector<Json>& summaries)
	{
		constexpr std::array<const char*, 11> headings{ "Scene", "Measurement", "Status", "CSX FPS", "CSX Frame mean ms", "CSX CPU mean ms", "CSX GPU mean ms", "fpsVR", "fpsVR CPU mean ms", "fpsVR GPU mean ms", "Source commit" };
		std::vector<std::array<Json, headings.size()>> values;
		std::array<bool, headings.size()> visible{ true, true, true, false, false, false, false, false, false, false, true };
		for (std::size_t i = 0; i < records.size(); ++i) {
			const auto& run = records[i];
			const auto& context = run.at("context");
			const auto nativeMean = [&](const char* metric) -> Json {
				for (const auto& row : summaries[i])
					if (row.value("source", "instrumented") == "csx_whole_frame" && row.at("metric") == metric)
						return row.value("value", Json(nullptr));
				return nullptr;
			};
			values.push_back({ SceneLabel(context), run.at("kind"), run.value("sourceUnavailable", false) ? Json("Data unavailable") : run.at("status"),
				nativeMean("fps"), nativeMean("frame"), nativeMean("cpu"), nativeMean("gpu"), FpsVrStatus(run),
				WholeFrameStatistics(run, "cpu").at("value"), WholeFrameStatistics(run, "gpu").at("value"), context.value("build", Json::object()).value("sourceCommit", Json(nullptr)) });
			for (std::size_t column = 3; column <= 9; ++column) visible[column] = visible[column] || (column == 7 ? run.contains("fpsVR") : values.back()[column].is_number());
		}
		std::string text = "<h2>Session overview</h2><p>Each row is one measurement. Compare matching scenes, settings and rendering resolutions. Columns unavailable in every run are hidden. Independent feature costs must not be added together.</p><div class=\"scroll\"><table><thead><tr><th>Run</th>";
		for (std::size_t column = 0; column < headings.size(); ++column)
			if (visible[column])
				text += "<th>" + std::string(headings[column]) + "</th>";
		text += "</tr></thead><tbody>";
		for (std::size_t i = 0; i < records.size(); ++i) {
			text += "<tr><td><a href=\"#run-" + std::to_string(i + 1) + "\">Run " + std::to_string(i + 1) + "</a><br><small>" + Escape(Text(records[i].at("started"))) + "</small></td>";
			for (std::size_t column = 0; column < headings.size(); ++column)
				if (visible[column])
					text += "<td>" + Escape(Display(values[i][column])) + "</td>";
			text += "</tr>";
		}
		return text + "</tbody></table></div>";
	}

	static std::string Report(const Json& records, bool comparison)
	{
		if (!records.is_array() || records.empty())
			throw std::runtime_error("No measurements in this session yet.");
		std::vector<Json> summaries;
		for (const auto& record : records) {
			auto rows = SummaryMetrics(record);
			if (record.value("fpsVR", Json::object()).value("status", "omitted") == "complete")
				for (const auto* metric : { "cpu", "gpu" }) {
					auto row = WholeFrameStatistics(record, metric);
					row.update({ { "id", "$fpsvr" }, { "label", "Whole frame (fpsVR)" }, { "metric", metric },
						{ "unit", "ms" }, { "source", "fpsvr" }, { "relativeTo", nullptr } });
					rows.push_back(std::move(row));
				}
			summaries.push_back(std::move(rows));
		}
		std::string text = R"html(<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>CSX performance report</title><style>
:root{color-scheme:dark;font:15px system-ui;}body{margin:0;background:var(--background);color:var(--text)}main{max-width:1440px;margin:auto;padding:32px 24px}header{padding:20px 0 28px}h1{font-size:36px;margin:8px 0}h2{margin:0 0 12px}h3{margin-top:28px}p{line-height:1.6}a{color:var(--accent)}small,.muted{color:var(--muted)}.eyebrow{text-transform:uppercase;letter-spacing:.15em;color:var(--accent)}.run{background:var(--panel);border:1px solid var(--line);border-radius:14px;padding:24px;margin:24px 0;scroll-margin-top:16px}.scroll{overflow:auto}table{border-collapse:collapse;width:100%;font-variant-numeric:tabular-nums}td,th{padding:12px 10px;text-align:left;border-bottom:1px solid var(--line);vertical-align:top}th{font-size:12px;text-transform:uppercase;letter-spacing:.04em;color:var(--muted)}tbody tr:hover{background:var(--hover)}pre{white-space:pre-wrap;overflow-wrap:anywhere;max-height:550px;overflow:auto;font-size:12px}details{margin:16px 0;padding:14px;background:var(--control);border:1px solid var(--line);border-radius:8px}summary{cursor:pointer;color:var(--accent);font-weight:600}.metadata{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:16px}.metadata details{margin:0}meter{display:block;width:100%;min-width:70px;height:8px;margin-top:5px;accent-color:var(--accent)}@media(max-width:700px){main{padding:16px 10px}.run{padding:14px}h1{font-size:28px}}@media print{body{background:white;color:black}.run{break-inside:avoid}details{background:white}a{color:#705000}}
</style></head><body><main><header><div class="eyebrow">Community Shaders Expanded</div><h1>Performance report</h1><p class="muted">Compare recorded costs, check whole-frame timings, and inspect the settings behind every run.</p></header>)html";
		const Json* themeRun = &records.front();
		for (const auto& record : records)
			if (record.value("presentation", Json::object()).is_object() && record.value("presentation", Json::object()).contains("theme"))
				themeRun = &record;
		text.insert(text.find("</style>"), ThemeStyle(*themeRun) + std::string(Charts::Style()));
		text += Charts::Overview(records, summaries);
		text += SessionOverview(records, summaries);
		if (const auto experiments = ExperimentTable(records, summaries); !experiments.empty())
			text += "<details><summary>Numeric experiment comparison · mean and SE</summary>" + experiments + "</details>";
		for (std::size_t i = 0; i < records.size(); ++i) {
			const auto* run = &records[i];
			Validate(*run);
			text += "<section class=\"run\" id=\"run-" + std::to_string(i + 1) + "\"><div class=\"eyebrow\">Run " + std::to_string(i + 1) + "</div><h2>" + Escape(SceneLabel(run->at("context"))) + "</h2><p>" + Escape(Text(run->at("started"))) + " · " + Escape(Text(run->at("status"))) + "</p><p>" + Semantics(*run) + "</p>";
			if (run->contains("failure") && !run->at("failure").is_null())
				text += "<p>" + Escape(Text(run->at("failure"))) + "</p>";
			const auto build = run->at("context").value("build", Json::object());
			text += "<p>Commit: " + Escape(Text(build.value("sourceCommit", Json(nullptr)))) + " · Build ID: " +
			        Escape(Text(build.value("buildId", Json(nullptr)))) + " · Modified source: " + Escape(Text(build.value("sourceDirty", Json(nullptr)))) + "</p>";
			text += "<div class=\"metadata\"><details><summary>Settings for this run</summary>";
			for (const auto& [feature, settings] : run->at("settings").items())
				text += "<details><summary>" + Escape(feature) + "</summary><pre>" + Escape(settings.dump(2)) + "</pre></details>";
			text += "</details><details><summary>Scene, hardware and build</summary><pre>" + Escape(run->at("context").dump(2)) + "</pre></details></div>";
			if (run->at("kind") == "quick_scan")
				text += QuickScanTable(*run, summaries[i], true);
			if (run->contains("fpsVR")) {
				const auto& fpsvr = run->at("fpsVR");
				text += "<h3>fpsVR whole-frame timings</h3><p>" + Escape(fpsvr.value("status", "omitted")) + " · " + Escape(fpsvr.value("reason", "")) + "</p>";
				if (!fpsvr.value("scans", Json::array()).empty()) {
					text += "<div class=\"scroll\"><table><tr><th>Metric</th><th>Mean of scan means ms</th><th>Within-run SE ms</th></tr>";
					for (const auto* metric : { "cpu", "gpu" }) {
						const auto summary = WholeFrameStatistics(*run, metric);
						text += "<tr><td>" + std::string(metric) + "</td><td>" + Escape(Display(summary.at("value"))) + "</td><td>" + Escape(Display(summary.at("standardError"))) + "</td></tr>";
					}
					text += "</table></div>";
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
			}
			if (run->at("kind") == "quick_scan")
				text += QuickScanTable(*run, summaries[i], false);
			else {
				text += "<h3>Measured On/Off costs</h3><div class=\"scroll\"><table><tr><th>Feature / case</th><th>Metric</th><th>Value</th><th>Unit</th><th>Standard error</th><th>Current</th><th>Comparison</th><th>p-value</th><th>Compared with</th><th>Coverage</th></tr>";
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
				text += "</table></div>";
			}
			text += "<details><summary>Complete run metadata and sample validity</summary><pre>" + Escape(run->dump(2)) + "</pre></details></section>";
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
				if (!row.at("first").is_number() && !row.at("second").is_number())
					continue;
				text += "<tr>";
				for (const auto* key : { "label", "metric", "first", "second", "difference", "unit" }) text += "<td>" + Escape(Display(row.at(key))) + "</td>";
				text += "</tr>";
			}
			text += "</table>";
		}
		return text + std::string(Charts::Script()) + "</main></body></html>";
	}

	std::string Html(const Json& record, const Json* comparison)
	{
		auto records = Json::array({ record });
		if (comparison)
			records.push_back(*comparison);
		return Report(records, comparison != nullptr);
	}

	std::string HtmlSession(const Json& records) { return Report(records, false); }

}
