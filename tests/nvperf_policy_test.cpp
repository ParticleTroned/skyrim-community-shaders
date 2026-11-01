#define DEVBENCH_BRIDGE_ENABLED
#include "nvperf_policy_extracted.h"

#include <cassert>
#include <iostream>
#include <limits>

namespace
{
	NvPerf::MetricRequest Parse(std::string_view a_text)
	{
		NvPerf::MetricRequest result;
		std::string error;
		assert(NvPerf::ParseMetric(a_text, result, error));
		return result;
	}

	void CheckMetricContract()
	{
		const auto defaults = NvPerf::DefaultMetrics();
		assert(!defaults.empty() && defaults.size() <= NvPerf::MaxMetrics);
		assert(defaults.front().name == "gpu__time_duration");
		for (const auto& metric : defaults) {
			const auto roundTrip = Parse(NvPerf::FormatMetric(metric));
			assert(roundTrip.name == metric.name);
			assert(roundTrip.metricType == metric.metricType);
			assert(roundTrip.rollupOp == metric.rollupOp);
			assert(roundTrip.submetric == metric.submetric);
		}
		assert(NvPerf::MetricLabel(Parse("gpu__time_duration")) == "gpu__time_duration.sum");
		assert(NvPerf::MetricLabel(Parse("sm__throughput:throughput")) == "sm__throughput.avg.pct_of_peak_sustained_elapsed");
		assert(NvPerf::MetricLabel(Parse("stalls:ratio")) == "stalls.ratio");
		assert(NvPerf::MetricLabel(Parse("warps:active")) == "warps.avg.pct_of_peak_sustained_active");
		assert(NvPerf::MetricLabel(Parse("cycles:elapsed")) == "cycles.avg.pct_of_peak_sustained_elapsed");
		assert(NvPerf::MetricLabel(Parse("instructions:percycle")) == "instructions.avg.per_cycle_active");
		assert(Parse("  metric : counter \t").name == "metric");
		assert(Parse(std::string(NvPerf::MaxNameLength, 'x')).name.size() == NvPerf::MaxNameLength);
		const std::vector<std::string> invalid{
			"", " \t", ":counter", "metric:", "metric:unknown", "metric:ratio:counter", "me\ntric", "metric\x80",
			std::string(NvPerf::MaxNameLength + 1, 'x')
		};
		for (const auto& text : invalid) {
			auto untouched = Parse("sentinel");
			std::string error;
			assert(!NvPerf::ParseMetric(text, untouched, error));
			assert(!error.empty() && untouched.name == "sentinel");
		}
	}

	void CheckFiltersAndCsv()
	{
		assert(NvPerf::NameMatches("Water*", "Water::Reflections"));
		assert(NvPerf::NameMatches("Water*", "Water"));
		assert(NvPerf::NameMatches("*", "Lighting"));
		assert(!NvPerf::NameMatches("Water", "Water::Reflections"));
		assert(!NvPerf::NameMatches("*Water", "UnderWater"));
		assert(!NvPerf::NameMatches("water*", "Water"));
		const std::vector<std::string> filters{ "Water::Main", "Water*" };
		assert(*NvPerf::MatchingFilter(filters, "Water::Main") == filters.front());
		assert(*NvPerf::MatchingFilter(filters, "Water::Reflections") == filters.back());
		assert(NvPerf::MatchingFilter(filters, "Sky") == nullptr);
		assert(NvPerf::CsvEscape("safe") == "safe");
		assert(NvPerf::CsvEscape("") == "");
		assert(NvPerf::CsvEscape("a,b") == "\"a,b\"");
		assert(NvPerf::CsvEscape("a\"b") == "\"a\"\"b\"");
		assert(NvPerf::CsvEscape("a\r\nb") == "\"a\r\nb\"");
	}

	NvPerf::RangeResult Range(std::string a_name, std::vector<double> a_values, std::vector<std::string> a_enclosing = {})
	{
		NvPerf::RangeResult range;
		range.name = std::move(a_name);
		range.values = std::move(a_values);
		range.enclosing = std::move(a_enclosing);
		return range;
	}

	void CheckResultAggregation()
	{
		NvPerf::CaptureResult result;
		result.samples = 2;
		result.metrics = { Parse("gpu__time_duration"), Parse("instructions"), Parse("sm__throughput:throughput") };
		result.ranges = { "Water*" };
		result.rangeResults = {
			Range("Water::Main", { 10.0, 100.0, 20.0 }),
			Range("Water::Reflections", { 30.0, 300.0, 60.0 }),
			Range("Water::Nested", { 1000.0, 10000.0, 99.0 }, { "Water::Main" })
		};
		auto summary = NvPerf::Summarize(result);
		assert(summary.size() == 1 && summary.front().name == "Water*");
		assert(summary.front().occurrencesPerFrame == 1.0);
		assert(summary.front().values[0] == 20.0);
		assert(summary.front().values[1] == 200.0);
		assert(summary.front().values[2] == 50.0);
		assert(summary.front().incomplete == 0);

		result.rangeResults.push_back(Range("Water::Incomplete", { std::numeric_limits<double>::quiet_NaN(), 40.0, 90.0 }));
		summary = NvPerf::Summarize(result);
		assert(summary.front().incomplete == 1);
		assert(summary.front().occurrencesPerFrame == 1.5);
		assert(summary.front().values[0] == 20.0 && summary.front().values[1] == 220.0);
		assert(summary.front().values[2] == 50.0);

		result.ranges.clear();
		result.rangeResults = { Range("Other", { 0.0, 10.0, 99.0 }), Range("First", { 1.0, 5.0, 20.0 }) };
		summary = NvPerf::Summarize(result);
		assert(summary.size() == 2 && summary[0].name == "Other" && summary[1].name == "First");
		assert(summary[0].incomplete == 1 && std::isnan(summary[0].values[2]));
		assert(summary[1].values[2] == 20.0);

		result.samples = 0;
		result.metrics = { Parse("ratio:ratio") };
		result.rangeResults = { Range("One", { 10.0 }), Range("One", { 30.0 }) };
		summary = NvPerf::Summarize(result);
		assert(summary.front().occurrencesPerFrame == 2.0 && summary.front().values[0] == 20.0);
	}
}

int main()
{
	CheckMetricContract();
	CheckFiltersAndCsv();
	CheckResultAggregation();
	std::cout << "NvPerf metric validation, filter/CSV handling, and time-weighted summary tests passed\n";
}
