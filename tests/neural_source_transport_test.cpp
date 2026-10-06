#include "Features/Upscaling/NeuralRendering/CapacityFallback.h"
#include "Features/Upscaling/NeuralRendering/ColorPolicy.h"
#include "Features/Upscaling/NeuralRendering/Renderer.h"
#include "Features/Upscaling/NeuralRendering/SourceTransport.h"

#include <iostream>
#include <stdexcept>

using namespace NeuralRendering;

void Require(bool value, const char* reason)
{
	if (!value)
		throw std::runtime_error(reason);
}

int main()
{
	static_assert(kPhysicalFeatureSlotCount == 4 && kMaximumRegionEvaluations == 2);
	struct Transition
	{
		unsigned resource = 0, featureState = 0;
	};
	std::array<Transition, 3> transitions{};
	std::size_t count = 0;
	Require(AddSourceTransition(transitions, count, Transition{ 1, 2 }), "first input");
	Require(AddSourceTransition(transitions, count, Transition{ 1, 2 }) && count == 1, "one barrier for shared input");
	Require(!AddSourceTransition(transitions, count, Transition{ 1, 3 }) && count == 1, "conflicting states fail closed");
	Require(AddSourceTransition(transitions, count, Transition{ 2, 3 }), "private first output");
	Require(AddSourceTransition(transitions, count, Transition{ 3, 3 }), "private second output");
	Require(!AddSourceTransition(transitions, count, Transition{ 4, 3 }) && count == 3, "bounded transition storage");
	Require(!AddSourceTransition(transitions, count, Transition{}), "null resource rejected");
	Color::Experiments experiment;
	Require(!experiment.CompactInputsEnabled(), "compact inputs default off in both builds");
#ifdef DEVBENCH_BRIDGE_ENABLED
	for (auto reason : { CapacityFailure::Pressure, CapacityFailure::Unsupported, CapacityFailure::Unsafe }) {
		CapacityFallback fallback;
		std::string order;
		const auto apply = [&] { order += 'a'; return false; };
		const auto classify = [&] { order += 'c'; return reason; };
		const auto retire = [&] { order += 'r'; return true; };
		const auto merged = [&] { order += 'm'; return true; };
		const bool safe = reason != CapacityFailure::Unsafe;
		Require(fallback.ApplyBatch(true, apply, classify, retire, merged) == safe, "only recoverable failures retry");
		Require(order == (safe ? "acrm" : "ac"), "unwind, classify and fence before fallback");
		if (safe) {
			order.clear();
			Require(fallback.ApplyBatch(true, apply, classify, retire, merged) && order == "m", "rejected count cannot resurrect");
		}
	}
	CapacityFallback failedRetirement;
	unsigned retries = 0;
	Require(!failedRetirement.ApplyBatch(true, [] { return false; }, [] { return CapacityFailure::Pressure; }, [] { return false; }, [&] { ++retries; return true; }) && !retries, "failed retirement never retries");
	Require(failedRetirement.rejected && failedRetirement.recoveries == 0, "retain failed recovery evidence");
	CapacityFallback ordinary;
	Require(!ordinary.ApplyBatch(false, [] { return false; }, [] { return CapacityFailure::Pressure; }, [] { return true; }, [&] { ++retries; return true; }) && !retries && !ordinary.rejected, "default count keeps existing failure policy");
	experiment.compactInputs = true;
	Require(experiment.CompactInputsEnabled(), "bridge may enable stateless compact inputs");
#endif
	std::cout << "Source transitions and compact fallback checks passed\n";
}
