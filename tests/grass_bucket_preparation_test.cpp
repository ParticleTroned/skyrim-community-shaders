#include "Features/GrassOptimizations/GrassBucketPreparation.h"
#include <iostream>
#include <memory>
#include <random>

namespace
{
	struct Queue
	{
		bool batching = true;
	};
	struct Group
	{
		uint32_t count = 1;
		std::array<float, 3> lo{}, hi{};
		float minVariance = 0, maxVariance = 0, maxBasisNorm = 0;
		bool boundsKnown = true;
		float birthTime = 0;
	};
	struct Source
	{
		uintptr_t identity;
		std::shared_ptr<Queue> queue;
		int material;
		std::vector<Group> groups;
	};
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
	auto Build(const std::vector<Source>& sources)
	{
		return GrassPreparation::Build(std::span<const Source>(sources), [](const Source&) { return 0; }, [](const Source& a, const Source& b) { return a.material == b.material; });
	}
	void CheckGrouping()
	{
		const auto queue = std::make_shared<Queue>();
		std::vector<Source> sources{ { 4, queue, 0, { Group{} } }, { 2, queue, 1, { Group{} } },
			{ 3, std::make_shared<Queue>(), 0, { Group{} } }, { 1, queue, 0, { Group{} } } };
		std::mt19937 random(73);
		for (int repeat = 0; repeat < 100; ++repeat) {
			std::shuffle(sources.begin(), sources.end(), random);
			const auto result = Build(sources);
			Require(result.size() == 3, "Different materials or queues were combined");
			Require(result[0].sources.size() == 2 && result[0].instances == 2, "Compatible sources did not batch");
			Require(sources[result[0].sources[0]].identity == 1 && sources[result[0].sources[1]].identity == 4,
				"Layout changed with capture order");
			Require(result[0].runs.size() == 2 && result[0].runs[1].record == 1 && result[0].runs[1].source == 1,
				"Runs merged across source origins");
		}
		queue->batching = false;
		Require(Build(sources).size() == sources.size(), "Disabled cross-cell batching combined sources");
		queue->batching = true;
		const auto partitioned = GrassPreparation::Build(std::span<const Source>(sources), [](const Source& source) { return source.identity; }, [](const Source&, const Source&) { return true; });
		Require(partitioned.size() == sources.size(), "Queue lookup lost the captured mesh hash partition");
		Require(Build({}).empty(), "Empty scene produced buckets");
	}
	void CheckRuns()
	{
		Group first{ 2, { -2, -3, -4 }, { 1, 2, 3 }, -2, 1, 2, true, 10 };
		Group second{ 3, { -5, -1, -2 }, { 2, 3, 4 }, -1, 5, 7, true, 10 };
		Group respawn = second;
		respawn.birthTime = 11;
		Group unknown = second;
		unknown.boundsKnown = false;
		const std::vector<Source> sources{ { 1, std::make_shared<Queue>(), 0, { first, second, respawn, unknown, second } } };
		const auto result = Build(sources);
		const auto& runs = result.front().runs;
		Require(runs.size() == 4 && runs[0].firstGroup == 0 && runs[0].lastGroup == 1 && runs[0].count == 5,
			"Compatible runs were not merged or spawn fades were combined");
		Require(runs[0].lo == std::array<float, 3>{ -5, -3, -4 } && runs[0].hi == std::array<float, 3>{ 2, 3, 4 } &&
					runs[0].minVariance == -2 && runs[0].maxVariance == 5 && runs[0].basis == 7,
			"Merged run lost conservative bounds");
		Require(runs[1].record == 5 && runs[2].record == 8 && !runs[2].known && runs[3].record == 11,
			"Unknown bounds or record offsets changed");
	}
	void CheckCapacity()
	{
		const auto queue = std::make_shared<Queue>();
		Group full;
		full.count = GrassPolicy::kMaxBatchInstances;
		std::vector<Source> sources{ { 1, queue, 0, { full } }, { 2, queue, 0, { Group{} } } };
		Require(Build(sources).size() == 2, "Batch instance capacity overflowed");
		sources.front().groups.push_back(Group{});
		bool rejected = false;
		try {
			Build(sources);
		} catch (const std::length_error&) {
			rejected = true;
		}
		Require(rejected, "Oversized source was accepted");
		sources.front().groups.assign(GrassPolicy::kMaxFrameSlices + 1, Group{});
		rejected = false;
		try {
			Build(sources);
		} catch (const std::length_error&) {
			rejected = true;
		}
		Require(rejected, "Oversized group snapshot was accepted");
		sources.assign(GrassPolicy::kMaxFrameSources + 1, Source{ 1, queue, 0, {} });
		rejected = false;
		try {
			Build(sources);
		} catch (const std::length_error&) {
			rejected = true;
		}
		Require(rejected, "Oversized source snapshot was accepted");
	}
}

int main()
{
	try {
		CheckGrouping();
		CheckRuns();
		CheckCapacity();
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
