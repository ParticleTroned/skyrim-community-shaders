#include "ProviderDescriptorGuardContract.h"

#include <iostream>
#include <set>

namespace
{
	int checks = 0, failures = 0;
	void Check(bool value, const char* label)
	{
		++checks;
		if (!value) {
			++failures;
			std::cerr << label << '\n';
		}
	}
}

int main()
{
	using namespace NrReplay::DescriptorGuard;
	std::set<std::size_t> addresses;
	std::set<unsigned> interfaces;
	for (const auto& slot : kSlots) {
		Check(!slot.name.empty() && slot.cacheRva % sizeof(void*) == 0, "aligned named provider cache slot");
		Check(addresses.insert(slot.cacheRva).second, "unique cache slot");
		Check(interfaces.insert(slot.interfaceId).second, "unique interface identifier");
	}

	Counters counts;
	Check(counts.Healthy() && !counts.WarmupObserved(), "no silent warmup proof before observation");
	for (std::size_t i = 0; i < kSlots.size(); ++i)
		Check(counts.Observe(static_cast<Api>(i), true), "warmup forwards every admitted interface");
	Check(counts.WarmupObserved() && counts.SteadyCalls() == 0, "warmup evidence is distinct from steady misses");
	counts.BeginSample();
	for (std::size_t i = 0; i < kSlots.size(); ++i)
		Check(counts.SampleCounts()[i] == 0 && counts.TotalCounts()[i] == 1, "sample reset preserves lifetime evidence");
	Check(counts.Healthy(), "cache-hit-only steady sample remains admissible");

	for (std::size_t i = 0; i < kSlots.size(); ++i) {
		Counters guard;
		unsigned forwarded = 0, submissions = 0;
		(void)guard.Observe(Api::CaptureUav, true);
		guard.BeginSample();
		const auto nativeCall = [&](Api api) {
			const bool accepted = guard.Observe(api, false);
			++forwarded;
			return accepted;
		};
		Check(!nativeCall(static_cast<Api>(i)), "each miss path rejects deferred submission");
		if (guard.Healthy())
			++submissions;
		Check(forwarded == 1 && submissions == 0, "native forwarding may finish but failed list cannot submit");
		Check(guard.SteadyCalls() == 1 && guard.SampleCounts()[i] == 1, "steady miss is retained");
		guard.BeginSample();
		Check(!guard.Healthy() && !guard.Observe(Api::Merged, true), "later warmup cannot erase failed admission");
		Check(guard.SteadyCalls() == 1, "sample reset cannot erase steady miss history");
	}
	Counters invalid;
	Check(!invalid.Observe(Api::Count, true), "out-of-range interface fails closed");
	Check(!invalid.WarmupObserved() && !invalid.Healthy(), "invalid event cannot establish warmup evidence");
	Check(!invalid.Observe(static_cast<Api>(-1), false), "invalid negative enum fails closed");
	std::cout << checks << " descriptor guard checks; " << failures << " failed\n";
	return failures ? 1 : 0;
}
