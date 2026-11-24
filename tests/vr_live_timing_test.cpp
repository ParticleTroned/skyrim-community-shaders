#include "Utils/FlatFrameTiming.h"
#include "Utils/VRFrameTiming.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <openvr.h>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "vr_live_timing_fixture.h"

#include "vr_live_timing_under_test.h"

namespace
{
	void Check(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	auto Now()
	{
		return std::chrono::steady_clock::time_point(std::chrono::milliseconds(10000 + TestRuntime::ticks * 10));
	}

	auto Read()
	{
		ProfilingRenderer::PerformanceTimingSummary summary;
		summary.frameCount = TestRuntime::gameFrame;
		CaptureOpenVRGameTiming(summary, Now());
		NormalizeGameFrameTiming(summary);
		return summary;
	}

	auto Tick(bool advanceGame = true)
	{
		++TestRuntime::ticks;
		if (advanceGame)
			++TestRuntime::gameFrame;
		if (TestRuntime::mode != TestRuntime::Mode::Stale)
			++TestRuntime::compositorFrame;
		return Read();
	}

	void CheckLive(const ProfilingRenderer::PerformanceTimingSummary& summary)
	{
		Check(summary.frameMs > 0.0f && summary.fps > 0.0f && summary.hasGameGpu && summary.hasGameCpu,
			"all four overview counters must remain live without a measurement or profiler capture");
		Check(std::abs(summary.fps - 100.0f) < 0.001f && std::abs(summary.gameCpuMs - 2.3f) < 0.001f,
			"live updates must retain corrected cadence and CPU definitions");
	}

	void LiveAndRecovery()
	{
		using namespace TestRuntime;
		const auto captureStart = Tick();
		for (unsigned i = 0; i < 3000; ++i) {
			auto summary = Tick();
			CheckLive(summary);
			Check(summary.vrTimingEpoch == captureStart.vrTimingEpoch && !summary.vrSamples.empty() &&
					  summary.vrSamples.back().systemTime > captureStart.vrSamples.back().systemTime,
				"polling during a capture must retain its clock epoch and advance the end timestamp");
			Check(summary.sampleFrameCount == compositorFrame - Util::VRFrameTiming::kCompletionLagFrames,
				"live capture must advance continuously beyond ten seconds");
			const auto calls = reads;
			auto repeated = Read();
			Check(reads == calls && repeated.sampleFrameCount == summary.sampleFrameCount,
				"same-frame UI callers must share a capture without extra runtime reads");
		}

		for (auto failure : { Mode::ReadFault, Mode::ResolveFault, Mode::Empty, Mode::NoCompositor }) {
			const auto epoch = Read().vrTimingEpoch;
			mode = failure;
			const auto before = resolves;
			for (unsigned i = 0; i < 200; ++i)
				Tick();
			Check(resolves - before <= 8, "failed reads must use a bounded retry rate");
			auto unavailable = Read();
			Check(!unavailable.hasGameGpu && !unavailable.hasGameCpu && unavailable.fps == 0,
				"unavailable timing must not be displayed as a frozen live reading");
			mode = Mode::Live;
			for (unsigned i = 0; i < 30; ++i)
				Tick();
			auto recovered = Read();
			CheckLive(recovered);
			Check(recovered.sampleFrameCount == compositorFrame - Util::VRFrameTiming::kCompletionLagFrames,
				"a failed read must not permanently disable capture");
			if (failure == Mode::ReadFault || failure == Mode::ResolveFault)
				Check(recovered.vrTimingEpoch > epoch, "fault recovery must break comparison sample ownership");
		}

		const auto previous = Read().vrLatestFrameIndex;
		for (unsigned i = 0; i < 2000; ++i)
			CheckLive(Tick(false));
		Check(Read().vrLatestFrameIndex > previous + 1900,
			"live compositor polling must continue when the game-frame counter does not advance");

		mode = Mode::Stale;
		for (unsigned i = 0; i < 150; ++i)
			Tick();
		Check(Read().vrDisplay.frame.count == 0, "duplicate history must expire rather than masquerade as live data");
		mode = Mode::Live;
		CheckLive(Tick());
	}
}

int main()
{
	try {
		LiveAndRecovery();
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
	std::cout << "VR live timing and recovery tests passed\n";
}
