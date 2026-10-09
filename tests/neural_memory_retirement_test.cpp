#include "Features/Upscaling/NeuralRendering/MemoryRetirementPolicy.h"

#include <cstdlib>
#include <iostream>

using namespace NeuralRendering;
namespace
{
	void Require(bool value, const char* message)
	{
		if (!value) {
			std::cerr << message << '\n';
			std::exit(1);
		}
	}
	MemoryBudgetSample Sample(std::uint64_t time)
	{
		return { 16384 * MemoryRecoveryPolicy::kMiB, 15000 * MemoryRecoveryPolicy::kMiB, true, 0, false, time };
	}
}
int main()
{
	MemoryRetirementPolicy policy;
	Require(policy.Observe(Sample(1000), 1000, true, false) == MemoryRetirementReason::None, "Absent backend was evicted");
	policy.Retained(1000);
	Require(policy.backendRetained && policy.warmRetirements == 1, "Warm retirement not recorded");
	policy.Retained(1100);
	Require(policy.retiredAtMs == 1000 && policy.warmRetirements == 1, "Repeated reset extended retention");
	Require(policy.Observe(Sample(999), 1000, true, true) == MemoryRetirementReason::None, "Pre-retirement sample counted");
	for (std::uint64_t time = 1000; time < 2000; time += 250)
		Require(policy.Observe(Sample(time), time, true, true) == MemoryRetirementReason::None, "Cold eviction skipped pressure dwell");
	Require(policy.Observe(Sample(1750), 2000, true, true) == MemoryRetirementReason::None, "Cached sample completed dwell");
	Require(policy.Observe(Sample(2000), 2000, true, true) == MemoryRetirementReason::Headroom, "Sustained pressure did not evict");
	policy.Released(MemoryRetirementReason::Headroom);
	Require(!policy.backendRetained && policy.coldEvictions == 1, "Cold eviction counters incorrect");
	Require(policy.Observe(Sample(2250), 2250, true, true) == MemoryRetirementReason::None, "Cold backend kept evicting");

	policy.Retained(3000);
	(void)policy.Observe(Sample(3000), 3000, true, true);
	(void)policy.Observe(Sample(3250), 3250, true, true);
	Require(policy.Observe(Sample(4000), 4000, true, true) == MemoryRetirementReason::None, "Observation gap counted as pressure");
	Require(policy.Observe(Sample(4250), 4250, false, true) == MemoryRetirementReason::None, "Cooldown alone evicted backend");
	Require(policy.Observe(Sample(4500), 4500, true, true) == MemoryRetirementReason::None, "Healthy sample did not clear pressure window");
	Require(policy.Observe(Sample(5001), 5000, true, true) == MemoryRetirementReason::None, "Future sample authorized eviction");
	Require(policy.Observe(Sample(4000), 5000, true, true) == MemoryRetirementReason::None, "Stale sample authorized eviction");
	Require(policy.Observe(Sample(5500), 5500, true, true) == MemoryRetirementReason::None, "Invalid samples retained pressure continuity");
	Require(policy.Observe(Sample(2000), 2000, true, true) == MemoryRetirementReason::None, "Clock reversal authorized eviction");
	Require(policy.Observe({}, 32999, false, false) == MemoryRetirementReason::None, "Idle backend expired early");
	Require(policy.Observe({}, 33000, false, false) == MemoryRetirementReason::IdleTimeout, "Invalid budget permitted unbounded idle retention");
	Require(policy.Observe({}, 33000, false, true) == MemoryRetirementReason::None, "Recovery cannot retain through the maximum cooldown");
	Require(policy.Observe({}, 123000, false, true) == MemoryRetirementReason::IdleTimeout, "Recovery retained an unusable backend indefinitely");
	policy.Released();
	Require(policy.coldEvictions == 1, "Ordinary reuse was counted as memory eviction");
	std::cout << "Warm retirement policy passed\n";
}
