#include "Features/Upscaling/NeuralRendering/MemoryResolutionPolicy.h"
#include "Features/Upscaling/NeuralRendering/NativeMemoryPolicy.h"

#include <iostream>
#include <stdexcept>

using namespace NeuralRendering;

namespace
{
	void Require(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}

	MemoryBudgetSample Sample(std::uint64_t usage, std::uint64_t time)
	{
		return { 16384 * NativeMemoryPolicy::kMiB, usage, true, 0, false, time };
	}

	void CheckDemand()
	{
		NativeMemoryPolicy policy;
		NativeMemoryKey full{ .runtimeVersion = 310009, .network = 2, .creation = { { 2000, 2000 }, { 2000, 2000 } }, .color = { 2000, 2000 } };
		const auto original = policy.Project(full);
		Require(original && !original->observed, "Cold estimate incorrectly claims a measurement");
		auto reduced = full;
		reduced.creation = { { 1000, 1000 }, { 1000, 1000 } };
		reduced.color = { 1000, 1000 };
		Require(policy.Project(reduced)->bytes < original->bytes, "Creation dimensions do not affect admission");
		// Evaluation rectangles are intentionally absent: a crop retains its native creation capacity.
		Require(policy.Project(full)->bytes == original->bytes, "An unallocated crop changed full-grid capacity");
		auto invalid = full;
		invalid.creation.input.width = 0;
		Require(!policy.Project(invalid), "Zero creation dimension was admitted");
		invalid.creation.input.width = UINT32_MAX;
		Require(!policy.Project(invalid), "Overflowing creation dimension was admitted");
		const auto growth = original->bytes * 2;
		policy.Observe(full, growth);
		const auto learned = policy.Project(full);
		Require(learned->observed && learned->observedProcessGrowth == growth && learned->bytes > growth,
			"Observed application growth lost its uncertainty allowance");
		policy.Observe(full, 0);
		Require(policy.Project(full)->bytes == learned->bytes, "A low/noisy observation reduced the high-water estimate");
		for (unsigned mode = 0; mode < 8; ++mode) {
			auto changed = full;
			switch (mode) {
			case 0:
				++changed.runtimeVersion;
				break;
			case 1:
				++changed.network;
				break;
			case 2:
				++changed.creation.input.width;
				break;
			case 3:
				++changed.creation.output.height;
				break;
			case 4:
				++changed.color.width;
				break;
			case 5:
				changed.providerBlending = true;
				break;
			case 6:
				changed.controlMask = true;
				break;
			case 7:
				++changed.colorFormat;
				break;
			}
			Require(!policy.Project(changed)->observed, "Learned demand escaped its native configuration");
		}
		for (unsigned index = 0; index < NativeMemoryPolicy::kCapacity + 1; ++index) {
			auto changed = full;
			changed.runtimeVersion += index + 1;
			policy.Observe(changed, growth);
		}
		Require(!policy.Project(full)->observed, "Session estimate cache is not bounded");
		policy.Observe(full, UINT64_MAX);
		Require(policy.Project(full)->bytes == UINT64_MAX, "Uncertainty arithmetic overflowed");
	}

	void CheckStartup()
	{
		NativeStartupObservation observation;
		observation.Begin({}, 2, Sample(1000, 10), 10, true);
		observation.Sample(Sample(1500, 11), 11);
		observation.Sample(Sample(1200, 12), 12);
		Require(observation.valid && observation.peak == 1500, "Startup lost its initial evaluation peak");
		LifetimeFenceSnapshot fence{ .fence = 9, .issued = 4, .completed = 4, .initialized = true, .completedKnown = true };
		Require(!observation.Completed(fence), "An unsubmitted native call became ready");
		observation.fence = 9;
		observation.issued = 4;
		fence.completed = 3;
		Require(!observation.Completed(fence), "Pending work permitted private resource reuse");
		observation.reservedBytes = 500;
		Require(observation.RemainingReservation(1200, fence, Sample(1500, 12), 12) == 1200,
			"Pending startup released its pair allowance");
		fence.completed = 4;
		Require(observation.Completed(fence), "Completed private work never became ready");
		Require(observation.RemainingReservation(1200, fence, Sample(1500, 12), 12) == 700,
			"Completed startup double-counted resident usage or released the other eye");
		Require(observation.RemainingReservation(1200, fence, Sample(1500, 12), 513) == 1200,
			"Stale usage replaced a live memory allowance");
		Require(observation.RemainingReservation(400, fence, Sample(1500, 12), 12) == 400,
			"A replaced reservation underflowed on completion");
		observation.evaluated = false;
		Require(observation.RemainingReservation(1200, fence, Sample(1500, 12), 12) == 1200,
			"Creation-only prewarm consumed initial evaluation allowance");
		observation.evaluated = true;
		fence.deviceRemoved = true;
		Require(!observation.Completed(fence), "Device removal reported startup completion");
		fence.deviceRemoved = false;
		fence.fence = 10;
		Require(!observation.Completed(fence), "A replacement fence satisfied an old submission");
		observation.Sample(Sample(9999, 13), 514);
		Require(!observation.valid && observation.peak == 1500, "Stale budget data trained the estimator");
		auto simulated = Sample(9999, 20);
		simulated.simulated = true;
		observation.Begin({}, 0, simulated, 20, false);
		Require(!observation.valid, "Developer pressure simulation trained real native demand");
	}

	void CheckResolution()
	{
		MemoryResolutionPolicy policy;
		policy.projectedCheaperPercent = 30;
		Require(!policy.OnRetired(90, 1) && policy.EffectivePercent(90) == 90,
			"Disabled automatic reduction changed the user's resolution");
		policy.SetEnabled(true);
		Require(!policy.OnRetired(90, 1), "Unprojected slider reduction was accepted");
		policy.projectedCheaperPercent = 90;
		Require(!policy.WhileWaiting(90, Sample(1000, 5001), 5001, true), "Equivalent capacity changed quality");
		policy.projectedCheaperPercent = 50;
		Require(policy.WhileWaiting(90, Sample(1000, 5001), 5001, true) && policy.EffectivePercent(90) == 50,
			"A projected candidate could not skip an equivalent density floor");
		Require(!policy.WhileWaiting(90, Sample(1000, 10001), 10001, true), "A consumed candidate triggered another reduction");
		policy.SetEnabled(false);
		Require(policy.EffectivePercent(90) == 90, "Selected resolution was not restored when adaptation was disabled");
	}
}

int main()
{
	try {
		CheckDemand();
		CheckStartup();
		CheckResolution();
		std::cout << "Native memory policy checks passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
