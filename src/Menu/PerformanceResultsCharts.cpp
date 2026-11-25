#include "PerformanceResultsCharts.h"
#include "PerformanceResults.h"
#include "PerformanceResultsFormatting.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>

namespace PerformanceResults::Charts
{
	using namespace ReportFormatting;
	namespace
	{
		constexpr std::size_t kMaximumChartSeries = 16384;
		constexpr std::array<const char*, 2> kMetrics{ "gpu", "cpu" };
		struct Feature
		{
			std::string label;
			std::map<std::size_t, std::array<const Json*, 2>> runs;
		};
		struct Group
		{
			std::string title;
			std::vector<std::size_t> runs;
			std::map<std::pair<std::string, std::string>, Feature> features;
		};
		struct Axis
		{
			double scale = 1, low = 0, high = 1;
			double Position(double value) const { return std::clamp((value / scale - low) / (high - low) * 1000.0, 0.0, 1000.0); }
		};
		const Json* Row(const Feature& feature, std::size_t run, std::size_t metric)
		{
			const auto found = feature.runs.find(run);
			return found == feature.runs.end() ? nullptr : found->second[metric];
		}
		bool Measured(const Json* row) { return row && row->value("value", Json(nullptr)).is_number(); }
		std::string Number(double value) { return Json(value).dump(); }
		Json MeanDifference(const Json* row, const Json* reference)
		{
			if (!Measured(row) || !Measured(reference) || row->value("coverage", Json(nullptr)) != reference->value("coverage", Json(nullptr)))
				return nullptr;
			const auto difference = row->at("value").get<double>() - reference->at("value").get<double>();
			return std::isfinite(difference) ? Json(difference) : Json(nullptr);
		}

		std::vector<Group> Groups(const Json& records, const std::vector<Json>& summaries)
		{
			std::vector<Json> signatures;
			std::vector<std::size_t> cohorts;
			for (const auto& record : records) {
				const auto kind = record.at("kind");
				const auto& context = record.at("context");
				const auto signature = Json::array({ kind == "quick_scan" ? "quick" : kind == "upscaling_sweep" ? "sweep" :
																												  "cost",
					context.value("runtime", Json(nullptr)), context.value("timingDefinitions", Json(nullptr)) });
				const auto found = std::find(signatures.begin(), signatures.end(), signature);
				cohorts.push_back(found - signatures.begin());
				if (found == signatures.end())
					signatures.push_back(signature);
			}
			std::map<std::pair<std::size_t, std::string>, Group> groups;
			for (std::size_t run = 0; run < records.size(); ++run) {
				for (const auto& row : summaries[run]) {
					const auto metric = std::find(kMetrics.begin(), kMetrics.end(), row.at("metric").get<std::string>());
					if (metric == kMetrics.end() || row.at("unit") != "ms")
						continue;
					const auto source = row.value("source", "instrumented");
					auto [where, inserted] = groups.try_emplace({ cohorts[run], source });
					auto& group = where->second;
					if (inserted) {
						const auto kind = records[run].at("kind");
						group.title = source == "csx_whole_frame" ? "Whole-frame timings · CSX" : source == "fpsvr"     ? "Whole-frame timings · fpsVR" :
						                                                                      kind == "quick_scan"      ? "Quick-scan instrumented self-times" :
						                                                                      kind == "upscaling_sweep" ? "Upscaling case costs versus None" :
						                                                                                                  "Feature costs versus Off/None";
						if (source != "instrumented" && source != "csx_whole_frame" && source != "fpsvr")
							group.title = "Recorded timings · " + source;
						group.title += " · " + Text(records[run].at("context").value("runtime", Json(nullptr)));
						for (std::size_t index = 0; index < cohorts.size(); ++index)
							if (cohorts[index] == cohorts[run])
								group.runs.push_back(index);
					}
					const auto relative = row.value("relativeTo", Json(nullptr));
					auto& feature = group.features[{ row.at("id"), relative.dump() }];
					feature.label = row.at("label");
					if (!relative.is_null())
						feature.label += " (vs " + Text(relative) + ")";
					feature.runs[run][metric - kMetrics.begin()] = &row;
				}
			}
			std::vector<Group> result;
			for (auto& [key, group] : groups) {
				std::erase_if(group.features, [](const auto& entry) {
					return std::ranges::none_of(entry.second.runs, [](const auto& run) { return Measured(run.second[0]) || Measured(run.second[1]); });
				});
				if (!group.features.empty())
					result.push_back(std::move(group));
			}
			return result;
		}

		Axis GroupAxis(const Group& group, std::size_t metric)
		{
			Axis axis;
			axis.scale = 0;
			for (const auto& [key, feature] : group.features)
				for (const auto run : group.runs)
					if (const auto* row = Row(feature, run, metric); Measured(row))
						for (const auto* field : { "value", "median", "standardError" })
							if (const auto value = row->value(field, Json(nullptr)); value.is_number())
								axis.scale = std::max(axis.scale, std::abs(value.get<double>()));
			if (axis.scale == 0)
				return {};
			axis.high = 0;
			for (const auto& [key, feature] : group.features)
				for (const auto run : group.runs)
					if (const auto* row = Row(feature, run, metric); Measured(row)) {
						const double mean = row->at("value").get<double>() / axis.scale;
						const double se = row->value("standardError", Json(nullptr)).is_number() ? row->at("standardError").get<double>() / axis.scale : 0;
						axis.low = std::min(axis.low, mean - se);
						axis.high = std::max(axis.high, mean + se);
						if (const auto median = row->value("median", Json(nullptr)); median.is_number()) {
							axis.low = std::min(axis.low, median.get<double>() / axis.scale);
							axis.high = std::max(axis.high, median.get<double>() / axis.scale);
						}
					}
			return axis;
		}

		std::string Bar(const Json& row, const Axis& axis)
		{
			const auto mean = row.at("value").get<double>();
			const auto zero = axis.Position(0), position = axis.Position(mean);
			std::string text = "<svg class=\"bar-chart\" viewBox=\"0 0 1000 28\" preserveAspectRatio=\"none\" role=\"img\" aria-label=\"" + Escape("Mean " + Display(mean) + " ms; median " + Display(row.value("median", Json(nullptr)))) + "\">";
			text += "<line class=\"zero\" x1=\"" + Number(zero) + "\" x2=\"" + Number(zero) + "\" y1=\"0\" y2=\"28\"/>";
			text += "<rect class=\"mean-bar\" x=\"" + Number(std::min(zero, position)) + "\" y=\"7\" width=\"" + Number(std::abs(position - zero)) + "\" height=\"14\"/>";
			text += "<circle class=\"mean-dot\" cx=\"" + Number(position) + "\" cy=\"14\" r=\"3\"/>";
			if (const auto se = row.value("standardError", Json(nullptr)); se.is_number()) {
				const auto halfWidth = se.get<double>() / axis.scale / (axis.high - axis.low) * 1000.0;
				text += "<path class=\"se-whisker\" d=\"M " + Number(position - halfWidth) + " 14 H " + Number(position + halfWidth) + " M " + Number(position - halfWidth) + " 3 V 25 M " + Number(position + halfWidth) + " 3 V 25\"/>";
			}
			if (const auto median = row.value("median", Json(nullptr)); median.is_number()) {
				const auto x = Number(axis.Position(median.get<double>()));
				text += "<line class=\"median-halo\" x1=\"" + x + "\" x2=\"" + x + "\" y1=\"0\" y2=\"28\"/>";
				text += "<line class=\"median-marker\" x1=\"" + x + "\" x2=\"" + x + "\" y1=\"0\" y2=\"28\"><title>Median: " + Escape(Display(median)) + " ms · " + Escape(Text(row.value("medianBasis", Json(nullptr)))) + "</title></line>";
			}
			return text + "</svg>";
		}

		std::string Series(const Json* row, const Axis& axis, std::size_t run, std::size_t metric, const Json* reference, std::size_t referenceRun)
		{
			const auto difference = MeanDifference(row, reference);
			std::string text = "<div class=\"chart-series color-" + std::to_string(run % 6) + "\" data-run=\"" + std::to_string(run + 1) + "\" data-metric=\"" + kMetrics[metric] + "\"";
			if (Measured(row)) {
				text += " data-value=\"" + Number(row->at("value").get<double>()) + "\"";
				if (const auto se = row->value("standardError", Json(nullptr)); se.is_number())
					text += " data-se=\"" + Number(se.get<double>()) + "\"";
				if (difference.is_number())
					text += " data-change=\"" + Number(difference.get<double>()) + "\"";
			}
			text += "><a href=\"#run-" + std::to_string(run + 1) + "\">Run " + std::to_string(run + 1) + " details</a>";
			if (!Measured(row))
				return text + "<span class=\"muted\">Unavailable</span></div>";
			text += Bar(*row, axis) + "<div class=\"chart-value\">Mean " + Escape(Display(row->at("value"))) + " ms";
			if (const auto se = row->value("standardError", Json(nullptr)); se.is_number())
				text += " &plusmn; " + Escape(Display(se)) + " ms SE";
			else
				text += " · SE unavailable";
			text += "<br><span class=\"median-label\">Median " + Escape(Display(row->value("median", Json(nullptr)))) + "</span>";
			if (row->value("median", Json(nullptr)).is_number())
				text += " ms <small>(" + Escape(Text(row->at("medianBasis"))) + ")</small>";
			else
				text += " <small>(valid saved samples required)</small>";
			if (run != referenceRun) {
				text += "<br><small>Mean &Delta; vs Run " + std::to_string(referenceRun + 1) + ": " +
				        (difference.is_number() ? Escape(Display(difference)) + " ms" : "Unavailable") + "</small>";
			}
			return text + "</div></div>";
		}

		std::string GroupCharts(const Group& group, const Json& records, std::size_t& seriesCount, bool& truncated)
		{
			std::string text = "<section class=\"chart-group\"><h3>" + Escape(group.title) + "</h3>";
			const auto& first = records[group.runs.front()];
			const bool mismatched = std::ranges::any_of(group.runs, [&](auto run) {
				return records[run].at("settings") != first.at("settings") || records[run].at("context") != first.at("context");
			});
			if (mismatched)
				text += "<p class=\"comparison-notice\">Run context or settings differ. Inspect the linked scene, build and settings before interpreting a performance change.</p>";
			text += "<div class=\"chart-cards\">";
			const std::array<Axis, 2> axes{ GroupAxis(group, 0), GroupAxis(group, 1) };
			for (const auto& [key, feature] : group.features) {
				if (group.runs.size() > (kMaximumChartSeries - seriesCount) / 2) {
					truncated = true;
					break;
				}
				seriesCount += group.runs.size() * 2;
				text += "<article class=\"feature-chart\" data-label=\"" + Escape(feature.label) + "\"><h4>" + Escape(feature.label) + "</h4><div class=\"metric-pair\">";
				for (std::size_t metric = 0; metric < kMetrics.size(); ++metric) {
					const bool available = std::ranges::any_of(group.runs, [&](auto run) { return Measured(Row(feature, run, metric)); });
					text += "<section class=\"metric-chart\" data-metric=\"" + std::string(kMetrics[metric]) + "\"" + (available ? "" : " hidden") + "><h5>" + (metric == 0 ? "GPU" : "CPU") + " · ms</h5>";
					for (const auto run : group.runs) text += Series(Row(feature, run, metric), axes[metric], run, metric, Row(feature, group.runs.front(), metric), group.runs.front());
					text += "</section>";
				}
				text += "</div></article>";
			}
			return text + "</div></section>";
		}
	}

	std::string Overview(const Json& records, const std::vector<Json>& summaries)
	{
		const auto groups = Groups(records, summaries);
		if (groups.empty())
			return "<section id=\"graphical-overview\"><h2>Graphical overview</h2><p>No complete CPU/GPU summary is available. Partial measurements, run status and details follow.</p></section>";
		std::string text = "<section id=\"graphical-overview\"><h2>Graphical overview</h2><p>Bar = mean &nbsp;·&nbsp; Whisker = &plusmn;1 SE &nbsp;·&nbsp; Highlighted line = labelled median. Values are milliseconds.</p>";
		text += "<div class=\"chart-controls\"><label>Show <select disabled data-control=\"run\"><option value=\"all\">All runs side by side</option>";
		for (std::size_t run = 0; run < records.size(); ++run)
			text += "<option value=\"" + std::to_string(run + 1) + "\">Run " + std::to_string(run + 1) + " · " + Escape(SceneLabel(records[run].at("context"))) + "</option>";
		text += "</select></label><label>Sort features <select disabled data-control=\"sort\"><option value=\"gpu\">GPU · highest first</option><option value=\"cpu\">CPU · highest first</option><option value=\"gpu-change\">GPU change · largest magnitude</option><option value=\"cpu-change\">CPU change · largest magnitude</option><option value=\"gpu-se\">GPU uncertainty · largest SE</option><option value=\"cpu-se\">CPU uncertainty · largest SE</option><option value=\"name\">Feature name</option></select></label></div><details><summary>How to read these charts</summary><p>Quick-scan medians use scan means. On/Off markers show the difference of the saved current and comparison block medians. A single fpsVR capture uses its stored sample median. Each basis is printed beside the value; missing medians are never estimated from a mean.</p><p>Run-to-run mean differences are descriptive, without a new significance test. Independent feature costs are not added together. CPU and GPU work overlap. Check the linked scene, build, settings and coverage before comparing.</p><p>Mean and uncertainty sorting use the largest available value among visible runs within each measurement group. Change sorting ranks the absolute mean difference from the first compatible run in that group. Changes remain unavailable when the reference or matching coverage is missing. Each group and metric has a shared scale that stays fixed while filtering. All raw measurements and uncertainty remain below.</p></details><noscript><p>Enable JavaScript for sorting and run filtering. All charts, values and detail links remain readable without it.</p></noscript>";
		std::size_t seriesCount = 0;
		text += "<nav class=\"run-key\" aria-label=\"Measurement status and details\">";
		for (std::size_t run = 0; run < records.size(); ++run)
			text += "<a class=\"color-" + std::to_string(run % 6) + "\" href=\"#run-" + std::to_string(run + 1) + "\">Run " + std::to_string(run + 1) + " · " + Escape(Text(records[run].at("status"))) + "</a>";
		text += "</nav>";
		bool truncated = false;
		for (const auto& group : groups) text += GroupCharts(group, records, seriesCount, truncated);
		if (truncated)
			text += "<p>Large-report chart limit reached. All saved values remain in the measurement details below.</p>";
		return text + "<p data-empty hidden>No complete CPU/GPU summary is available for the selected run. Its status and details follow below.</p></section>";
	}

	std::string_view Style()
	{
		return R"css(
[hidden]{display:none!important}.run-key{display:flex;flex-wrap:wrap;gap:10px 24px;margin:16px 0}.run-key a{color:var(--series)}.chart-controls{display:flex;flex-wrap:wrap;gap:20px;padding:16px 0}.chart-controls label{max-width:100%;min-width:0;display:flex;align-items:center;gap:10px}select{max-width:100%;min-width:0;font:inherit;padding:9px;border:1px solid var(--line);border-radius:6px;background:var(--control);color:inherit}.feature-chart{border:1px solid var(--line);border-radius:10px;margin:12px 0;padding:16px}.feature-chart h4{overflow-wrap:anywhere;margin:0 0 12px;font-size:17px}.metric-pair{display:grid;grid-template-columns:1fr 1fr;gap:24px}.metric-chart h5{margin:0 0 12px;color:var(--muted);font-size:13px}.chart-series{display:grid;grid-template-columns:90px minmax(70px,1fr) minmax(130px,1fr);gap:10px;align-items:center;margin:10px 0;--series:var(--series-0)}.color-1{--series:var(--series-1)}.color-2{--series:var(--series-2)}.color-3{--series:var(--series-3)}.color-4{--series:var(--series-4)}.color-5{--series:var(--series-5)}.bar-chart{width:100%;height:30px;overflow:visible;background:var(--background)}.zero{vector-effect:non-scaling-stroke;stroke:var(--muted);stroke-width:1;stroke-dasharray:3}.mean-bar,.mean-dot{fill:var(--series)}.se-whisker{vector-effect:non-scaling-stroke;fill:none;stroke:var(--text);stroke-width:2}.median-halo{stroke:var(--background);stroke-width:7;vector-effect:non-scaling-stroke}.median-marker{vector-effect:non-scaling-stroke;stroke:var(--median);stroke-width:3}.median-label{color:var(--median)}.chart-value{font-variant-numeric:tabular-nums;font-size:13px}.chart-value small{font-size:11px}.comparison-notice{border-left:3px solid var(--warning);padding:8px 14px;background:var(--control)}@media(max-width:1050px){.metric-pair{grid-template-columns:1fr}}@media(max-width:550px){.chart-series{grid-template-columns:76px 1fr}.chart-value{grid-column:2}.chart-controls label{align-items:flex-start;flex-direction:column}}@media print{.chart-controls{display:none}.feature-chart{break-inside:avoid}.median-label{color:#8a5900}}
)css";
	}

	std::string_view Script()
	{
		return R"js(<script>
(() => {
  'use strict';
  const root = document.getElementById('graphical-overview');
  if (!root || !root.querySelector('[data-control="run"]')) return;
  const run = root.querySelector('[data-control="run"]');
  const sort = root.querySelector('[data-control="sort"]');
  run.disabled = sort.disabled = false;
  const visible = series => run.value === 'all' || series.dataset.run === run.value;
  const number = (series, field = 'value') => series.hasAttribute(`data-${field}`) && Number.isFinite(Number(series.dataset[field])) ? Number(series.dataset[field]) : null;
  const score = card => {
    const [metric, statistic = 'value'] = sort.value.split('-');
    let result = null;
    card.querySelectorAll('.chart-series').forEach(series => {
      const sample = number(series, statistic);
      const value = sample === null ? null : statistic === 'change' ? Math.abs(sample) : sample;
      if (visible(series) && series.dataset.metric === metric && value !== null && (result === null || value > result)) result = value;
    });
    return result;
  };
  function update() {
    let anyGroup = false;
    root.querySelectorAll('.chart-group').forEach(group => {
      const container = group.querySelector('.chart-cards');
      const cards = Array.from(container.children);
      cards.forEach(card => {
        let measured = false;
        card.querySelectorAll('.metric-chart').forEach(metric => {
          let available = false;
          metric.querySelectorAll('.chart-series').forEach(series => {
            series.hidden = !visible(series);
            available ||= !series.hidden && number(series) !== null;
          });
          metric.hidden = !available;
          measured ||= available;
        });
        card.hidden = !measured;
      });
      const scores = new Map(cards.map(card => [card, score(card)]));
      cards.sort((a, b) => {
        if (sort.value !== 'name') {
          const left = scores.get(a), right = scores.get(b);
          if (left === null && right !== null) return 1;
          if (right === null && left !== null) return -1;
          if (left !== null && right !== null && left !== right) return left > right ? -1 : 1;
        }
        return a.dataset.label.localeCompare(b.dataset.label);
      }).forEach(card => container.appendChild(card));
      group.hidden = cards.every(card => card.hidden);
      anyGroup ||= !group.hidden;
    });
    root.querySelector('[data-empty]').hidden = anyGroup;
  }
  run.addEventListener('change', update);
  sort.addEventListener('change', update);
  update();
})();
</script>)js";
	}
}
