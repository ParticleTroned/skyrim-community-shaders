#include "Features/Upscaling/NeuralRendering/MeasuredPlanSearch.h"

#include <iostream>
#include <set>

#define CHECK(condition)                              \
	do {                                              \
		if (!(condition)) {                           \
			std::cerr << "line " << __LINE__ << '\n'; \
			return __LINE__;                          \
		}                                             \
	} while (false)

using namespace NeuralRendering;
using namespace NeuralRendering::MeasuredPlan;

static SearchInput Fixture(unsigned count = 4)
{
	SearchInput input;
	input.width = input.height = 2048;
	input.enclosure = { 0, 0, 2048, 2048 };
	input.generation = 7;
	input.sourceFrame = 100;
	input.regionLimit = count;
	for (unsigned i = 0; i < count; ++i) {
		const unsigned x = 192 + (i % 2) * 1200, y = 192 + (i / 2) * (count > 4 ? 420 : 1200);
		const CharacterRect rect{ x, y, x + 100, y + 100 };
		input.actors.push_back({ 10u + i, rect });
		input.eligibility.push_back(rect);
	}
	return input;
}

static bool Covers(const SearchCandidate& candidate, const SearchInput& input)
{
	if (!candidate.plan.count)
		return candidate.operation == std::string_view("keep");
	if (!GetCharacterRegionSubmissionViolation(input.logicalSlot, candidate.plan, input.enclosure, input.width, input.height, true).empty())
		return false;
	for (const auto& rect : input.eligibility) {
		const auto required = CharacterMultiRoiDetail::Required(rect, input.width, input.height);
		unsigned owners = 0;
		for (unsigned i = 0; i < candidate.plan.count; ++i)
			owners += ContainsComputeSubrect(candidate.plan.regions[i], required);
		if (owners != 1)
			return false;
	}
	return true;
}

int main()
{
	auto input = Fixture();
	CHECK(ValidSearchInput(input));
	CHECK(input.Matches(100, 7, 0));
	CHECK(!input.Matches(101, 7, 0) && !input.Matches(100, 8, 0) && !input.Matches(100, 7, 1));
	const auto search = Search({}, input.enclosure, input.width, input.height, &input);
	CHECK(search.inputValid && search.visited > 0 && search.candidates.size() <= kMaximumCandidatesPerEye);
	CHECK(search.candidates.front().plan.count == 0);
	std::set<unsigned> counts;
	unsigned two = 0;
	for (const auto& candidate : search.candidates) {
		CHECK(Covers(candidate, input));
		CHECK(candidate.plan.count <= input.regionLimit);
		counts.insert(candidate.plan.count);
		two += candidate.plan.count == 2;
	}
	CHECK(counts == std::set<unsigned>({ 0, 1, 2, 3, 4 }) && two == 2);
	// The two equal-area partitions split different actor memberships, not just a previous merge.
	const auto firstTwo = std::ranges::find_if(search.candidates, [](const auto& c) { return c.plan.count == 2; });
	const auto secondTwo = std::ranges::find_if(firstTwo + 1, search.candidates.end(), [](const auto& c) { return c.plan.count == 2; });
	CHECK(firstTwo->plan.clusterIdentities != secondTwo->plan.clusterIdentities);
	const auto repartition = Search(firstTwo->plan, input.enclosure, input.width, input.height, &input);
	CHECK(std::ranges::any_of(repartition.candidates, [&](const auto& c) { return c.plan == secondTwo->plan; }));
	CHECK(std::ranges::any_of(repartition.candidates, [](const auto& c) { return c.operation == std::string_view("local_merge"); }));

	const auto four = std::ranges::find_if(search.candidates, [](const auto& c) { return c.plan.count == 4; });
	auto retained = four->plan;
	retained.regionSlots = { 3, 2, 1, 0 };
	for (unsigned i = 0; i < retained.count; ++i) retained.historyKeys[i] = 900 + i;
	retained.regions[0].width += 128;
	retained.roi[0] = BuildRoiDescriptor(retained.roi[0].samplingSupport, retained.regions[0], { input.width, input.height }, true);
	const auto anchored = Reanchor(retained, input);
	CHECK(anchored && anchored->regions[0].Area() < retained.regions[0].Area());
	CHECK(anchored->historyKeys == retained.historyKeys && anchored->regionSlots == retained.regionSlots);
	CHECK(anchored->roi[0].samplingSupport == retained.roi[0].samplingSupport);
	const auto fresh = Search(retained, input.enclosure, input.width, input.height, &input);
	for (const auto& candidate : fresh.candidates) {
		CHECK(Covers(candidate, input));
		for (unsigned i = 0; i < candidate.plan.count; ++i)
			for (unsigned old = 0; old < retained.count; ++old)
				if (candidate.plan.clusterIdentities[i] == retained.clusterIdentities[old]) {
					CHECK(candidate.plan.historyKeys[i] == retained.historyKeys[old]);
					CHECK(candidate.plan.regionSlots[i] == retained.regionSlots[old]);
				}
	}
	auto permuted = input;
	std::ranges::reverse(permuted.actors);
	const auto reordered = Search({}, input.enclosure, input.width, input.height, &permuted);
	CHECK(reordered.candidates.size() == search.candidates.size());
	for (unsigned i = 0; i < search.candidates.size(); ++i) CHECK(reordered.candidates[i].id == search.candidates[i].id);

	for (unsigned limit = 1; limit <= kMaximumRegionsPerEye; ++limit) {
		auto many = Fixture(8);
		many.regionLimit = limit;
		const auto result = Search({}, many.enclosure, many.width, many.height, &many);
		CHECK(result.inputValid && result.candidates.size() <= kMaximumCandidatesPerEye);
		CHECK(result.visited <= CharacterMultiRoiDetail::kPartitionCandidateBudget && result.coverageChecks <= kCoverageCheckBudget);
		for (const auto& candidate : result.candidates) {
			CHECK(candidate.plan.count <= limit && Covers(candidate, many));
		}
		CHECK(std::ranges::any_of(result.candidates, [&](const auto& c) { return c.plan.count == limit; }));
	}

	// Alignment/edge padding and bridge-shaped coverage may invalidate a tempting split.
	auto bridge = input;
	bridge.eligibility.push_back({ 290, 230, 1394, 260 });
	const auto bridged = Search({}, bridge.enclosure, bridge.width, bridge.height, &bridge);
	CHECK(bridged.rejected > 0);
	for (const auto& candidate : bridged.candidates) CHECK(Covers(candidate, bridge));
	for (unsigned width : { 511u, 777u, 1001u }) {
		auto edge = Fixture(2);
		edge.width = width;
		edge.height = 777;
		edge.enclosure = { 0, 0, width, 777 };
		edge.actors[0].rect = { 0, 0, 80, 80 };
		edge.actors[1].rect = { width - 80, 690, width, 777 };
		edge.eligibility = { edge.actors[0].rect, edge.actors[1].rect };
		for (const auto& candidate : Search({}, edge.enclosure, width, 777, &edge).candidates) CHECK(Covers(candidate, edge));
	}

	for (unsigned error = 0; error < 8; ++error) {
		auto bad = input;
		if (error == 0)
			bad.actors[1].identity = bad.actors[0].identity;
		if (error == 1)
			bad.actors[0].identity = 0;
		if (error == 2)
			bad.actors[0].rect.maxX = UINT32_MAX;
		if (error == 3)
			bad.eligibility.clear();
		if (error == 4)
			bad.regionLimit = kEnabledRegionsPerEye + 1;
		if (error == 5)
			bad.generation = 0;
		if (error == 6)
			bad.enclosure.width = UINT32_MAX;
		if (error == 7)
			bad.width = kCharacterMaskRoiMaximumExtent + 1;
		CHECK(!ValidSearchInput(bad));
		const auto safe = Search({}, input.enclosure, input.width, input.height, &bad);
		CHECK(!safe.inputValid && safe.candidates.size() == 1 && safe.candidates.front().plan.count == 0);
	}
	auto dense = Fixture(8);
	dense.eligibility.resize(kMaximumCoverageRectangles, dense.eligibility.front());
	const auto budgeted = Search({}, dense.enclosure, dense.width, dense.height, &dense);
	CHECK(budgeted.inputValid && budgeted.truncated && budgeted.coverageChecks <= kCoverageCheckBudget);
	CHECK(budgeted.visited < CharacterMultiRoiDetail::kPartitionCandidateBudget);
	for (const auto& candidate : budgeted.candidates) CHECK(Covers(candidate, dense));
	auto invalidPlan = retained;
	invalidPlan.regionSlots[0] = kEnabledRegionsPerEye;
	CHECK(Search(invalidPlan, input.enclosure, input.width, input.height, &input).candidates.empty());
	CHECK(Search({}, {}, 2048, 2048, &input).candidates.empty());
	const auto unknown = Search({}, input.enclosure, input.width, input.height, nullptr);
	CHECK(unknown.candidates.size() == 1 && unknown.candidates[0].plan.count == 0);

	// The selector may prefer a larger measured layout; pixel area does not supply milliseconds.
	Profile profile{ "fixture", {}, 30, 10000 };
	std::vector<Candidate> choices;
	for (unsigned i = 0; i < search.candidates.size(); ++i) {
		const auto key = std::to_string(search.candidates[i].id);
		choices.push_back({ key, search.candidates[i].id, true, false });
		profile.observations.push_back({ key, { 9, 10, 11, 1, 0, 1000, 32 } });
	}
	const auto target = static_cast<std::size_t>(firstTwo - search.candidates.begin());
	profile.observations[target].cost = { 3, 4, 5, 1, 1, 2000, 32 };
	State state;
	CHECK(Select(choices, 0, profile.identity, &profile, 1, state).index == target);
	CHECK(Select(choices, 0, "stale", &profile, 2, state).index == 0);
	CHECK(Select(choices, 0, profile.identity, nullptr, 3, state).index == 0);
	choices[target].capacityRejected = true;
	CHECK(Select(choices, 0, profile.identity, &profile, 4, state).index == 0);
	std::cout << "Split/repartition, re-anchor, coverage, identity, bounds and measured selection passed\n";
	return 0;
}
