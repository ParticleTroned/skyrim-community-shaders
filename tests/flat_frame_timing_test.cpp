#include "Profiler.h"
#include "Utils/ResourceName.h"

#include <iostream>
#include <stdexcept>

namespace Util
{
	void SetResourceName(ID3D11DeviceChild* resource, const char* format, ...)
	{
		if (!resource || !std::string_view(format).starts_with("Profiler::"))
			throw std::runtime_error("flat query is unnamed");
	}
}

namespace
{
	void Check(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}

	struct Fixture
	{
		ID3D11Device device;
		ID3D11DeviceContext context;
		Profiler profiler;
		explicit Fixture(bool flat = true, int failedQuery = -1)
		{
			device.failedQueryIndex = failedQuery;
			profiler.Initialize(&device, &context, flat);
			profiler.SetUserEnabled(true);
		}
		void Present(uint32_t frame, HRESULT result = S_OK, UINT flags = 0, bool supported = true)
		{
			profiler.RequestCapture();
			const bool owned = profiler.BeginFlatPresent(frame, flags, supported);
			profilerTestClock += 20;  // Simulated blocking Present must not enter CPU duration.
			if (owned)
				profiler.CompleteFlatPresent(result);
		}
	};

	void RuntimeIsolation()
	{
		Fixture vr(false);
		Check(!vr.profiler.GetFlatTiming(), "VR allocated flat state");
		Check(vr.device.queryCreations == static_cast<int>(Profiler::kFrameLatency * (1 + 2 * Profiler::kMaxTimers)), "VR query allocation changed");
		const auto clock = profilerTestClock;
		vr.profiler.BeginFlatPresent(1, 0);
		vr.profiler.CompleteFlatPresent(S_OK);
		Check(vr.context.writes == 0 && vr.context.reads == 0 && profilerTestClock == clock, "flat calls touched VR");
		for (uint32_t frame = 0; frame < 12; ++frame) {
			vr.profiler.RequestCapture();
			vr.profiler.EndFrame(frame);
			Check(vr.profiler.BeginPass("VR::Pass", false), "VR pass capture refused");
			vr.profiler.EndPass(false);
			const auto beforeReads = vr.context.reads;
			const auto beforeWrites = vr.context.writes;
			const auto beforeClock = profilerTestClock;
			vr.profiler.BeginFlatPresent(frame, 0, false);
			vr.profiler.CompleteFlatPresent(E_FAIL);
			Check(vr.context.reads == beforeReads && vr.context.writes == beforeWrites && profilerTestClock == beforeClock,
				"flat path touched an active VR capture");
		}
		vr.profiler.EndFrame(12);
		Check(vr.profiler.GetResults().size() == 1 && vr.profiler.GetResults().front().gpuTimeMs == 1.0f,
			"VR pass timing changed");
	}

	void WholeFrameBoundaries()
	{
		Fixture f;
		f.Present(0);
		for (uint32_t frame = 1; frame <= 20; ++frame) {
			profilerTestClock += 4;
			f.context.clock += 6;
			f.Present(frame);
		}
		bool found = false;
		for (const auto& sample : f.profiler.GetFlatTiming()->samples) {
			if (!sample.hasGpu)
				continue;
			found = true;
			Check(sample.presentId == sample.frame + 1, "GPU lost original frame identity");
			Check(sample.hasCpu && sample.cpuMs == 5.0f, "CPU includes Present wait or uses a different frame");
			Check(sample.gpuMs == 7.0f, "GPU timestamps do not enclose full frame");
		}
		Check(found, "no whole-frame GPU results without named pass scopes");
		const auto id = f.profiler.GetFlatTiming()->presentId;
		const auto writes = f.context.writes;
		f.Present(21, S_OK, DXGI_PRESENT_TEST);
		Check(f.profiler.GetFlatTiming()->presentId == id && f.context.writes == writes, "test Present rotated flat frame");
		const auto epoch = f.profiler.GetFlatTiming()->epoch;
		f.Present(21, DXGI_STATUS_OCCLUDED);
		Check(f.profiler.GetFlatTiming()->samples.empty() && f.profiler.GetFlatTiming()->epoch > epoch, "occluded Present published timing");
		f.Present(22, E_FAIL);
		Check(f.profiler.GetFlatTiming()->samples.empty(), "failed Present published timing");
		f.Present(23, S_FALSE);
		Check(f.profiler.GetFlatTiming()->samples.empty(), "non-presenting status published timing");
		Check(f.profiler.BeginFlatPresent(24, 0), "outer Present did not acquire timing");
		Check(!f.profiler.BeginFlatPresent(25, 0), "nested Present stole timing completion");
		f.profiler.CompleteFlatPresent(S_OK);
		Check(f.profiler.GetFlatTiming()->samples.back().frame == 24, "nested Present changed the source frame");
	}

	void PendingAndFailure()
	{
		for (const bool disjoint : { false, true }) {
			Fixture f;
			f.context.pending = true;
			for (uint32_t frame = 0; frame < 20; ++frame)
				f.Present(frame);
			Check(f.context.reads < 60, "pending GPU caused polling loop");
			Check(std::ranges::any_of(f.profiler.GetFlatTiming()->samples, [](const auto& s) { return s.hasCpu; }), "pending GPU lost CPU");
			f.context.pending = false;
			f.context.disjoint = disjoint;
			for (uint32_t frame = 20; frame < 40; ++frame)
				f.Present(frame);
			Check(std::ranges::any_of(f.profiler.GetFlatTiming()->samples, [](const auto& s) { return s.hasGpu; }) != disjoint, "disjoint GPU published a valid sample");
			const auto epoch = f.profiler.GetFlatTiming()->epoch;
			f.profiler.Initialize(&f.device, &f.context, true);
			Check(f.profiler.GetFlatTiming()->epoch > epoch && f.profiler.GetFlatTiming()->samples.empty(), "device reset reused timing identity");
			f.Present(40);
		}
		const int flatQueryStart = Profiler::kFrameLatency * (1 + 2 * Profiler::kMaxTimers);
		for (const int failedQuery : { 0, flatQueryStart, flatQueryStart + 1 }) {
			Fixture f(true, failedQuery);
			for (uint32_t frame = 0; frame < 20; ++frame)
				f.Present(frame);
			Check(std::ranges::any_of(f.profiler.GetFlatTiming()->samples, [](const auto& s) { return s.hasCpu && !s.hasGpu; }), "missing GPU query prevented CPU fallback");
		}
	}

	void SharedPassRingAndUnsupportedPath()
	{
		Fixture f;
		f.Present(0);
		for (uint32_t frame = 1; frame < 12; ++frame) {
			Check(f.profiler.BeginPass("Flat::Pass", false), "flat pass capture refused");
			f.context.clock += 2;
			f.profiler.EndPass(false);
			f.Present(frame);
		}
		Check(f.profiler.GetResults().size() == 1 && f.profiler.GetResolvedTotalTimeMs() == 3.0f,
			"whole-frame timing contaminated named pass totals");
		Check(std::ranges::any_of(f.profiler.GetFlatTiming()->samples, [](const auto& s) { return s.hasGpu && s.gpuMs == 5; }),
			"whole-frame queries did not share the named pass interval");
		const auto oldEpoch = f.profiler.GetFlatTiming()->epoch;
		for (uint32_t frame = 12; frame < 20; ++frame) {
			f.Present(frame, S_OK, 0, false);
			Check(f.profiler.GetFlatTiming()->samples.empty(), "unsupported presentation path retained timing");
		}
		Check(f.profiler.GetFlatTiming()->epoch > oldEpoch, "unsupported source did not invalidate pending measurements");
		for (uint32_t frame = 20; frame < 32; ++frame)
			f.Present(frame);
		for (const auto& sample : f.profiler.GetFlatTiming()->samples)
			Check(sample.frame >= 20, "unsupported source was republished after returning to D3D11");
		Check(std::ranges::any_of(f.profiler.GetFlatTiming()->samples, [](const auto& s) { return s.hasCpu && s.hasGpu; }),
			"timing did not recover after an unsupported presentation path");
		f.profiler.ClearTimers();
		f.context.failed = true;
		for (uint32_t frame = 32; frame < 44; ++frame)
			f.Present(frame);
		Check(std::ranges::none_of(f.profiler.GetFlatTiming()->samples, [](const auto& s) { return s.hasGpu; }),
			"failed query reads published GPU timing");
		Check(std::ranges::any_of(f.profiler.GetFlatTiming()->samples, [](const auto& s) { return s.hasCpu; }),
			"failed query reads discarded independent CPU timing");
	}

	void ResetAndDisable()
	{
		Fixture f;
		f.Present(0);
		f.profiler.ClearTimers();
		f.Present(1);
		Check(f.profiler.GetFlatTiming()->samples.size() == 1 &&
				  !f.profiler.GetFlatTiming()->samples.back().hasCpu && f.profiler.GetFlatTiming()->samples.back().resolved,
			"history reset republished a partial frame");
		f.profiler.SetUserEnabled(false);
		const auto disabledClock = profilerTestClock;
		f.profiler.BeginFlatPresent(2, 0);
		f.profiler.CompleteFlatPresent(S_OK);
		Check(profilerTestClock == disabledClock, "disabled flat profiler read the clock");
		Check(f.profiler.GetFlatTiming()->samples.empty(), "disabled profiler republished timing");
		f.profiler.SetUserEnabled(true);
		for (uint32_t frame = 3; frame < 12; ++frame)
			f.Present(frame);
		Check(std::ranges::any_of(f.profiler.GetFlatTiming()->samples, [](const auto& s) { return s.hasCpu && s.hasGpu; }),
			"profiler did not resume after disable");
		f.profiler.BeginFlatPresent(12, 0);
		f.profiler.ClearTimers();
		f.profiler.CompleteFlatPresent(S_OK);
		Check(f.profiler.GetFlatTiming()->samples.empty(), "Present republished a sample invalidated while pending");
		f.profiler.Release();
		Check(f.context.activeDisjointQueries == 0, "release left a whole-frame disjoint interval open");
	}

	void MeasurementAssociation()
	{
		using namespace Util::FlatFrameTiming;
		std::deque<PendingSample<5>> pending;
		pending.push_back({ 10, 1, { 0.25, 0.75, 0, 0, 0 } });
		pending.push_back({ 11, 1, { 0, 1, 0, 0, 0 } });
		std::array<Sample, 2> samples{};
		samples[0].presentId = 10;
		samples[0].cpuMs = 2;
		samples[0].hasCpu = true;
		samples[0].resolved = false;
		samples[1].presentId = 11;
		samples[1].gpuMs = 7;
		samples[1].hasGpu = true;
		std::array<double, 5> gpu{}, cpu{};
		unsigned missingGpu = 0, missingCpu = 0, consumed = 0;
		const auto consume = [&](const auto& original, const Sample* result) {
			++consumed;
			const bool hasGpu = result && result->resolved && result->hasGpu;
			const bool hasCpu = result && result->hasCpu;
			missingGpu += !hasGpu;
			missingCpu += !hasCpu;
			for (std::size_t i = 0; i < gpu.size(); ++i) {
				if (hasGpu)
					gpu[i] += result->gpuMs * original.weights[i];
				if (hasCpu)
					cpu[i] += result->cpuMs * original.weights[i];
			}
		};
		ResolvePending(pending, samples, 15, 1, false, consume);
		Check(consumed == 0, "late result overtook pending frame");
		samples[0].resolved = true;
		samples[0].hasGpu = true;
		samples[0].gpuMs = 4;
		ResolvePending(pending, samples, 16, 1, false, consume);
		ResolvePending(pending, samples, 16, 1, false, consume);
		Check(consumed == 2 && gpu[0] == 1 && gpu[1] == 10 && cpu[0] == 0.5 && cpu[1] == 1.5, "delayed timings changed original block weights or duplicated samples");
		Check(missingGpu == 0 && missingCpu == 1, "missing CPU was fabricated");
		pending.push_back({ 12, 1, {} });
		ResolvePending(pending, samples, 16, 1, true, consume);
		Check(pending.empty() && missingGpu == 1 && missingCpu == 2, "finalization waited for absent metrics");
		pending.push_back({ 10, 1, {} });
		ResolvePending(pending, samples, 16, 2, false, consume);
		Check(missingGpu == 2 && missingCpu == 3, "reset source reused old samples");
		pending.push_back({ 12, 2, {} });
		ResolvePending(pending, samples, 12 + kRetainedFrames, 2, false, consume);
		Check(pending.empty(), "expired query grew pending measurements without bound");
	}
}

int main()
{
	try {
		RuntimeIsolation();
		WholeFrameBoundaries();
		PendingAndFailure();
		ResetAndDisable();
		SharedPassRingAndUnsupportedPath();
		MeasurementAssociation();
		std::cout << "Flat frame timing and VR isolation cases passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
