#include "Features/Upscaling/NeuralRendering/CapacityFallback.h"
#include "Features/Upscaling/NeuralRendering/ColorPolicy.h"
#include "Features/Upscaling/NeuralRendering/Renderer.h"

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
	std::cout << "Compact fallback checks passed\n";
}
