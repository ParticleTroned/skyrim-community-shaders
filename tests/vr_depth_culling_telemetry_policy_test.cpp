#include "Features/VRDepthCullingTelemetryPolicy.h"
#include "Features/VRDepthCullingTemporal.h"

#include <barrier>
#include <latch>
#include <limits>
#include <stdexcept>
#include <thread>

int main()
{
	using namespace VRDepthCullingTelemetryPolicy;
	static_assert(VRDepthCullingTemporal::Status::DurationBinCount == DurationBinCount);
	if (DurationBin(0) != 0 || DurationBin(std::numeric_limits<std::uint64_t>::max()) != DurationBinCount - 1) {
		throw std::runtime_error("duration histogram boundary is incorrect");
	}
	for (std::size_t index = 0; index < DurationUpperBoundsNanoseconds.size(); ++index) {
		const auto upper = DurationUpperBoundsNanoseconds[index];
		if (DurationBin(upper - 1) != index || DurationBin(upper) != index || DurationBin(upper + 1) != index + 1)
			throw std::runtime_error("duration histogram boundary is incorrect");
	}

	WriterGate gate;
	std::latch entered{ 1 };
	std::latch release{ 1 };
	std::thread writer([&] {
		if (!gate.TryEnter())
			throw std::runtime_error("writer was unexpectedly rejected");
		entered.count_down();
		release.wait();
		gate.Leave();
	});
	entered.wait();
	if (gate.TryLockForReset())
		throw std::runtime_error("reset entered while a writer was active");
	release.count_down();
	writer.join();
	if (!gate.TryLockForReset())
		throw std::runtime_error("idle reset was rejected");
	if (gate.TryEnter() || gate.TryLockForReset())
		throw std::runtime_error("reset did not exclude writers and other resets");
	gate.SetEnabled(false);
	if (gate.IsEnabled() || gate.TryEnter())
		throw std::runtime_error("disabling telemetry lost reset ownership");
	gate.UnlockAfterReset();

	if (gate.IsEnabled() || gate.TryEnter())
		throw std::runtime_error("disabled telemetry admitted a writer");
	if (!gate.TryLockForReset())
		throw std::runtime_error("disabled telemetry could not be reset");
	gate.SetEnabled(true);
	if (!gate.IsEnabled() || gate.TryEnter())
		throw std::runtime_error("enabling telemetry lost reset ownership");
	gate.UnlockAfterReset();
	if (!gate.TryEnter() || !gate.TryEnter())
		throw std::runtime_error("enabled telemetry did not admit multiple writers");
	gate.SetEnabled(false);
	if (gate.TryLockForReset() || gate.TryEnter())
		throw std::runtime_error("disable lost active writer ownership");
	gate.Leave();
	if (gate.TryLockForReset())
		throw std::runtime_error("reset ignored a remaining writer");
	gate.Leave();
	if (!gate.TryLockForReset())
		throw std::runtime_error("finished disabled writers kept reset busy");
	gate.UnlockAfterReset();
	gate.SetEnabled(true);

	// Race two writers against reset admission. The separate markers detect
	// overlapping admitted scopes without relying on non-atomic test data.
	std::barrier start{ 3 };
	std::atomic_uint32_t activeWriters{ 0 };
	std::atomic_bool activeReset{ false };
	std::atomic_bool overlap{ false };
	auto raceWriter = [&] {
		for (std::size_t iteration = 0; iteration < 10'000; ++iteration) {
			start.arrive_and_wait();
			if (gate.TryEnter()) {
				activeWriters.fetch_add(1);
				if (activeReset.load())
					overlap.store(true);
				std::this_thread::yield();
				activeWriters.fetch_sub(1);
				gate.Leave();
			}
		}
	};
	std::thread firstWriter(raceWriter);
	std::thread secondWriter(raceWriter);
	for (std::size_t iteration = 0; iteration < 10'000; ++iteration) {
		start.arrive_and_wait();
		if (gate.TryLockForReset()) {
			activeReset.store(true);
			if (activeWriters.load() != 0)
				overlap.store(true);
			std::this_thread::yield();
			activeReset.store(false);
			gate.UnlockAfterReset();
		}
	}
	firstWriter.join();
	secondWriter.join();
	if (overlap.load())
		throw std::runtime_error("reset and recovery writers were admitted together");
	if (!gate.TryLockForReset())
		throw std::runtime_error("admission race leaked a writer or reset");
	gate.UnlockAfterReset();
	return 0;
}
