#include "Features/PerformanceOverlay/ABTesting/ABComparison.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
	using namespace ABComparison;
	void Check(bool valid, const char* message)
	{
		if (!valid)
			throw std::runtime_error(message);
	}

	std::vector<Interval> Bracket(float before, float changed, float after)
	{
		std::vector<Interval> result(3);
		for (size_t i = 0; i < result.size(); ++i) {
			auto& interval = result[i];
			interval.variant = i == 1 ? ABVariant::B : ABVariant::A;
			interval.first = 100 * i + 1;
			interval.last = interval.first + 19;
			interval.closed = true;
			const float value = i == 0 ? before : i == 1 ? changed :
			                                               after;
			for (uint64_t frame = interval.first; frame <= interval.last; ++frame)
				Check(Add(result, frame, { value, value, value }), "fixture overflow");
		}
		return result;
	}

	void BracketingAndCoverage()
	{
		auto intervals = Bracket(10, 8, 10);
		auto result = Compare(intervals, 1);
		Check(result.valid && result.pairs == 1 && result.delta == -2 && result.verdict == -1, "stable improvement lost");
		intervals[0].samples[1].resize(15);
		Check(!Compare(intervals, 1).valid, "one usable A neighbour was accepted");
		Check(Compare(intervals, 0).valid, "missing GPU contaminated independent frame timing");
		intervals[0].samples[1].push_back(10);
		Check(Compare(intervals, 1).valid, "80 percent coverage should be usable");
		intervals[2].closed = false;
		Check(!Compare(intervals, 1).valid, "unfinished baseline was used");
		intervals.pop_back();
		Check(!Compare(intervals, 1).valid, "unbracketed pair was used");
	}

	void NoiseAndDrift()
	{
		const auto equal = Compare(Bracket(10, 10, 10), 0);
		Check(equal.valid && equal.verdict == 0, "equal timings changed verdict");
		Check(Compare(Bracket(10, 10.02f, 10), 1).verdict == 0, "sub-floor difference became a performance claim");
		const auto drift = Compare(Bracket(10, 11, 14), 1);
		Check(drift.valid && drift.baseline == 12 && drift.delta == -1 && drift.uncertainty == 2 && drift.verdict == 0,
			"baseline drift was reported as an improvement");
		auto intervals = Bracket(10, 8, 10);
		auto tail = Bracket(10, 12, 10);
		for (size_t i = 1; i < tail.size(); ++i) {
			tail[i].first += 200;
			tail[i].last += 200;
			intervals.push_back(tail[i]);
		}
		const auto mixed = Compare(intervals, 1);
		Check(mixed.valid && mixed.pairs == 2 && mixed.delta == 0 && mixed.uncertainty == 2 && mixed.verdict == 0,
			"inconsistent repeat deltas were hidden");
		intervals.back().samples[1].clear();
		Check(!Compare(intervals, 1).valid, "failed repeat was dropped from the verdict");
	}

	void SourceIdentityAndBounds()
	{
		std::vector<Interval> intervals(2);
		intervals[0].first = 10;
		intervals[0].last = 20;
		intervals[0].closed = true;
		intervals[1].first = 30;
		intervals[1].variant = ABVariant::B;
		Check(Add(intervals, 18, { 12, 8, 3 }), "late sample rejected");
		Check(Add(intervals, 18, { 12, 8, 3 }), "duplicate should be ignored");
		Check(Add(intervals, 25, { 900, 900, 900 }), "settling sample should be ignored");
		Check(Add(intervals, 31, { 12, 0, std::nullopt }), "zero GPU was rejected");
		Check(Add(intervals, 32, { 0, std::numeric_limits<float>::quiet_NaN(), -1 }), "invalid metrics must be omitted");
		Check(intervals[0].samples[0].size() == 1 && intervals[1].samples[0].size() == 1 &&
				  intervals[1].samples[1].size() == 1 && intervals[1].samples[1][0] == 0 && intervals[1].samples[2].empty(),
			"late, duplicate, settling or invalid samples contaminated a range");
		intervals[1].samples[1].resize(MaximumSamples, 1);
		Check(!Add(intervals, 33, { std::nullopt, 1, std::nullopt }), "storage grew beyond the bound");
		Check(!Compare(Bracket(10, 8, 10), 3).valid, "invalid metric index accepted");
		auto overlap = Bracket(10, 8, 10);
		overlap[1].first = 20;
		Check(!Compare(overlap, 1).valid, "overlapping source frames accepted");
	}
}

int main()
{
	try {
		BracketingAndCoverage();
		NoiseAndDrift();
		SourceIdentityAndBounds();
		std::cout << "A/B source identity, bracketing, uncertainty and capacity cases passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
