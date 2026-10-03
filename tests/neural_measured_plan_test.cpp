#include "Features/Upscaling/NeuralRendering/MeasuredPlanJson.h"
#include "Features/Upscaling/NeuralRendering/PipelinePolicy.h"
#include <limits>

#define CHECK(condition)     \
	do {                     \
		if (!(condition))    \
			return __LINE__; \
	} while (false)

int main()
{
	using namespace NeuralRendering;
	using namespace MeasuredPlan;
	Profile profile{ "gpu-driver-runtime", { { "four", { 34, 36, 44, 1, 0, 4000, 32 } }, { "two", { 23, 25, 30, 1, 8, 3000, 32 } }, { "unsteady", { 20, 24, 48, 1, 0, 3000, 32 } } }, 30, 5000 };
	CHECK(profile.Valid());
	std::array candidates{ Candidate{ "four", 4, true, false }, Candidate{ "two", 2, true, false }, Candidate{ "unsteady", 1, true, false } };
	State state;
	auto chosen = Select(candidates, 0, profile.identity, &profile, 100, state);
	CHECK(chosen.index == 1 && state.partition == 2 && state.changedFrame == 100);
	CHECK(Select(candidates, 0, profile.identity, &profile, 101, state).index == 1);
	candidates[1].coverageValid = false;
	CHECK(Select(candidates, 0, profile.identity, &profile, 102, state).index == 0);
	candidates[1].coverageValid = true;
	candidates[1].capacityRejected = true;
	CHECK(Select(candidates, 0, profile.identity, &profile, 200, state).index == 0);
	candidates[1].capacityRejected = false;
	CHECK(Select(candidates, 0, "new-driver", &profile, 201, state).index == 0 && state.partition == 0);
	CHECK(Select(candidates, 0, profile.identity, nullptr, 201, state).index == 0);
	profile.observations[1].cost.gpuUpperMs = 34;
	profile.observations[1].cost.gpuTailMs = 35;
	CHECK(Select(candidates, 0, profile.identity, &profile, 202, state).index == 0);
	profile.observations[1].cost = { 23, 25, 30, 2, 0, 3000, 32 };
	CHECK(Select(candidates, 0, profile.identity, &profile, 240, state).index == 0);
	profile.observations[1].cost.cpuCriticalUpperMs = 1;
	profile.observations[1].cost.residentBytes = 6000;
	CHECK(Select(candidates, 0, profile.identity, &profile, 280, state).index == 0);
	profile.observations[1].cost.residentBytes = 3000;
	profile.observations[1].cost.gpuLowerMs = std::numeric_limits<double>::quiet_NaN();
	CHECK(!profile.Valid());
	profile.observations[1].cost.gpuLowerMs = 23;
	profile.observations.push_back(profile.observations.front());
	CHECK(!profile.Valid());

	CharacterComputeRegionPlan plan;
	plan.count = 4;
	for (unsigned i = 0; i < 4; ++i) {
		plan.regions[i] = { (i % 2) * 1024u, (i / 2) * 1024u, 256, 256 };
		plan.historyKeys[i] = 100 + i;
		plan.clusterIdentities[i] = 200 + i;
		plan.roi[i] = BuildRoiDescriptor(plan.regions[i], plan.regions[i], { 2048, 2048 }, true);
	}
	const auto plans = Candidates(plan, 2048, 2048);
	CHECK(plans.size() > 4 && plans.size() <= 256 && plans.front().plan == plan);
	CHECK(std::ranges::any_of(plans, [](const auto& p) { return p.plan.count == 1; }));
	for (const auto& p : plans) {
		CHECK(GetCharacterRegionSubmissionViolation(0, p.plan, { 0, 0, 2048, 2048 }, 2048, 2048, true).empty());
		for (const auto& original : std::span(plan.regions).first(plan.count))
			CHECK(std::ranges::any_of(std::span(p.plan.regions).first(p.plan.count), [&](const auto& r) { return ContainsComputeSubrect(r, original); }));
	}
	const auto merged = Merge(plans.front(), 0, 1, 2048, 2048);
	CHECK(merged && merged->plan.historyKeys[0] != plan.historyKeys[0]);
	CHECK(merged->plan.historyKeys[1] == plan.historyKeys[2] && merged->plan.regionSlots[1] == plan.regionSlots[2]);
	CHECK(!Merge(plans.front(), 1, 0, 2048, 2048));

	using Json = nlohmann::json;
	Json row{ { "key", Json::array({ { { "geometry", "test-only" } } }) }, { "samples", 32u }, { "gpuLowerMs", 1 },
		{ "gpuUpperMs", 2 }, { "gpuTailMs", 3 }, { "cpuCriticalUpperMs", 1 }, { "transitionUpperMs", 0 }, { "residentBytes", 100u } };
	Json value{ { "schemaVersion", 1 }, { "scope", "experimental" }, { "identity", { { "buildId", "test" } } },
		{ "dwellFrames", 30u }, { "residentBudgetBytes", 1000u }, { "baselineEvidence", { std::string(64, 'a') } },
		{ "heldOutEvidence", { std::string(64, 'b') } }, { "observations", { row } } };
	for (const auto* gate : { "matchedBaselineBrackets", "uniqueFinalizedSources", "stableBaselines", "controlledSceneContent",
			 "heldOutValidation", "transitionCosts", "cpuCriticalPath", "nativeResidency", "unchangedQualityAndCadence" })
		value["qualification"][gate] = true;
	CHECK(ReadProfile(value).Valid());
	const auto rejects = [](const Json& v) { try { (void)ReadProfile(v); return false; } catch (const std::exception&) { return true; } };
	for (const auto& [gate, unused] : value["qualification"].items()) {
		auto bad = value;
		bad["qualification"][gate] = false;
		CHECK(rejects(bad));
	}
	auto bad = value;
	bad["observations"].push_back(row);
	CHECK(rejects(bad));
	bad = value;
	bad["observations"][0]["samples"] = 29u;
	CHECK(rejects(bad));
	bad = value;
	bad["dwellFrames"] = UINT64_MAX;
	CHECK(rejects(bad));
	bad = value;
	bad["observations"][0]["residentBytes"] = -1;
	CHECK(rejects(bad));
	return 0;
}
