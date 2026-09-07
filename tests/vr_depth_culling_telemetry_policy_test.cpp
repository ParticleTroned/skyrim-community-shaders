#include "Features/VRDepthCullingTelemetryPolicy.h"

#include <latch>
#include <stdexcept>
#include <thread>

int main()
{
	using namespace VRDepthCullingTelemetryPolicy;
	if (DurationBin(0) != 0 || DurationBin(1'000) != 0 ||
		DurationBin(1'001) != 1 || DurationBin(64'001) != 7) {
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
	gate.UnlockAfterReset();

	gate.SetEnabled(false);
	if (gate.IsEnabled() || gate.TryEnter())
		throw std::runtime_error("disabled telemetry admitted a writer");
	return 0;
}
