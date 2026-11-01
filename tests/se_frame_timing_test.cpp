#include "Profiler.h"
#include "Utils/ResourceName.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace Util
{
	void SetResourceName(ID3D11DeviceChild* resource, const char* format, ...)
	{
		if (!resource || !std::string_view(format).starts_with("Profiler::"))
			throw std::runtime_error("unnamed profiler resource");
	}
}

namespace
{
	using Mode = Profiler::CaptureMode;
	static_assert(static_cast<unsigned>(Mode::None) == 0);
	static_assert(static_cast<unsigned>(Mode::WholeFrameOnly) == 1);
	static_assert(static_cast<unsigned>(Mode::DetailedPasses) == 2);
	static_assert(Profiler::kFrameLatency == 8);

	void Check(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}
	void Near(float actual, float expected, const char* message)
	{
		Check(std::isfinite(actual) && std::abs(actual - expected) < 0.001f, message);
	}

	struct Fixture
	{
		ID3D11Device device;
		ID3D11DeviceContext context;
		Profiler profiler;
		uint32_t frame = 0;
		explicit Fixture(int failedQueryIndex = -1)
		{
			profilerTestClock = 100;
			device.failedQueryIndex = failedQueryIndex;
			profiler.Initialize(&device, &context);
		}
		~Fixture() { profiler.Release(); }
		void Present(UINT sync = 0, HRESULT result = S_OK, UINT flags = 0)
		{
			profiler.SetEngineFrameCount(++frame);
			profiler.BeginPresent(sync, flags);
			profiler.CompletePresent(result);
			profiler.BeginFrame();
		}
		void At(int64_t time, UINT sync = 0, HRESULT result = S_OK, UINT flags = 0)
		{
			profilerTestClock = time;
			Present(sync, result, flags);
		}
		void Arm(Mode mode)
		{
			profiler.SetUserEnabled(true);
			profiler.RequestCapture(mode);
			Present();
		}
		void Work()
		{
			profilerTestClock += 10;
			context.clock += 10;
		}
		void Drain()
		{
			for (unsigned i = 0; i < Profiler::kFrameLatency + 1; ++i) {
				Work();
				Present();
			}
		}
	};

	void AlwaysOnAcceptedPresent()
	{
		Fixture f;
		Check(!f.profiler.IsUserEnabled(), "fixture enabled profiling");
		f.At(100, 1);
		Check(!f.profiler.HasPresentInterval(), "first Present fabricated an interval");
		f.At(116, 0);
		Near(f.profiler.GetPresentIntervalMs(), 16, "disabled profiler lost direct intervals");
		Check(f.profiler.WasLatestPresentIntervalSynced(), "sync mode must belong to the preceding Present");
		const auto id = f.profiler.GetPresentIntervalSampleId();
		f.At(125, 1, S_OK, DXGI_PRESENT_TEST);
		Check(f.profiler.GetPresentIntervalSampleId() == id, "test Present advanced the accepted source");
		f.At(132, 0);
		Near(f.profiler.GetPresentIntervalMs(), 16, "test Present split a display interval");
		Check(!f.profiler.WasLatestPresentIntervalSynced(), "stale sync mode survived a new interval");
		Check(f.context.writes == 0, "always-on direct intervals acquired GPU queries");

		for (const auto failure : { E_FAIL, DXGI_STATUS_OCCLUDED }) {
			const auto beforeId = f.profiler.GetPresentIntervalSampleId();
			const auto epoch = f.profiler.GetPresentDiscontinuityEpoch();
			f.At(200, 0, failure);
			Check(f.profiler.GetPresentDiscontinuityEpoch() == epoch + 1, "rejected Present did not invalidate its boundary");
			f.At(216);
			Check(f.profiler.GetPresentIntervalSampleId() == beforeId, "first accepted Present bridged a rejected boundary");
			f.At(232);
			Near(f.profiler.GetPresentIntervalMs(), 16, "rejected source did not recover");
		}
		const auto epoch = f.profiler.GetPresentDiscontinuityEpoch();
		const auto beforeId = f.profiler.GetPresentIntervalSampleId();
		f.At(3000);
		Check(f.profiler.GetPresentDiscontinuityEpoch() == epoch + 1 && f.profiler.GetPresentIntervalSampleId() == beforeId,
			"loading gap became an ordinary hitch");
		f.At(3016);
		Near(f.profiler.GetPresentIntervalMs(), 16, "gap did not establish a fresh anchor");
		const auto rollbackEpoch = f.profiler.GetPresentDiscontinuityEpoch();
		f.At(3008);
		Check(f.profiler.GetPresentDiscontinuityEpoch() == rollbackEpoch + 1, "counter rollback did not rebaseline");
	}

	void SharedClockAndIndependentTotals()
	{
		Fixture f;
		f.Arm(Mode::DetailedPasses);
		Check(f.profiler.IsWholeFrameCaptureActive() && f.profiler.IsDetailedCaptureActive(), "legacy detailed mode did not capture both services");
		for (unsigned i = 0; i < Profiler::kFrameLatency; ++i) {
			Check(f.profiler.BeginPass("Work::Named", false), "detailed scope refused");
			f.profiler.EndPass(false);
			f.Work();
			f.profiler.RequestCapture(Mode::DetailedPasses);
			f.Present();
		}
		Check(f.profiler.HasLatestWholeFrameGpuSample() && f.profiler.HasLatestWholeFrameCpuSample(), "accepted whole-frame sample missing");
		Check(f.profiler.GetWholeFrameGpuTimeMs() > f.profiler.GetProfiledPassGpuTotalMs(), "whole GPU frame was replaced by pass sum");
		Check(f.profiler.GetWholeFrameCpuTimeMs() > f.profiler.GetProfiledPassCpuTotalMs(), "whole CPU frame was replaced by pass sum");
		Check(f.profiler.GetWholeFramePresentIntervalSampleId() == 1, "delayed whole-frame sample lost its matching Present interval");
		Check(f.profiler.GetCapturedFrameCount() == 2, "named capture lost engine frame identity");
		Check(f.context.activeDisjointQueries == 1, "whole-frame and named scopes did not share one open clock");
	}

	void TestPresentKeepsCaptureOpen()
	{
		Fixture f;
		f.Arm(Mode::DetailedPasses);
		const auto writes = f.context.writes;
		const auto epoch = f.profiler.GetPresentDiscontinuityEpoch();
		f.Present(0, S_OK, DXGI_PRESENT_TEST);
		Check(f.context.writes == writes && f.context.activeDisjointQueries == 1, "test Present closed or reopened capture");
		Check(f.profiler.GetPresentDiscontinuityEpoch() == epoch && f.profiler.IsDetailedCaptureActive(), "test Present promoted or discarded requests");
		Check(f.profiler.BeginPass("Test::Retained", false), "test Present lost the original frame");
		f.profiler.EndPass(false);
		f.Work();
		f.Present();
		f.Drain();
		Check(f.profiler.GetWholeFrameSampleId() == 1 && f.profiler.GetResults().size() == 1, "test Present split the captured frame");
	}

	void LimitsAndRealWholeFrameOnly()
	{
		Fixture f;
		f.profiler.SetCaptureModeLimit(Mode::WholeFrameOnly);
		f.Arm(Mode::DetailedPasses);
		Check(f.profiler.IsWholeFrameCaptureActive() && !f.profiler.IsDetailedCaptureActive(), "legacy capture limit changed ordinal semantics");
		Check(!f.profiler.BeginPass("Excluded::Pass", false) && f.profiler.GetGpuCaptureEpoch() == 0, "whole-frame-only limit admitted named or external capture");
		uint64_t capture = 0;
		Check(!f.profiler.StartBoundedCapture(10, false, capture), "bounded capture bypassed whole-frame limit");
		f.Work();
		f.Present();
		f.Drain();
		Check(f.profiler.GetWholeFrameSampleId() == 1 && f.profiler.HasLatestWholeFrameGpuSample(), "whole-frame-only mode did not retain a real GPU interval");
		Check(f.profiler.GetResults().empty() && f.profiler.GetProfiledPassGpuTotalMs() == 0, "whole-frame-only capture fabricated pass costs");
		f.profiler.SetCaptureModeLimit(Mode::None);
		const auto id = f.profiler.GetPresentIntervalSampleId();
		f.profiler.RequestCapture(Mode::DetailedPasses);
		f.Work();
		f.Present();
		Check(!f.profiler.IsWholeFrameCaptureActive() && !f.profiler.IsDetailedCaptureActive(), "None limit allowed capture");
		Check(f.profiler.GetPresentIntervalSampleId() == id + 1, "None limit stopped always-on Present intervals");
	}

	void NonblockingRingAndRejectedSamples()
	{
		Fixture f;
		f.Arm(Mode::WholeFrameOnly);
		f.context.pending = true;
		for (unsigned i = 0; i < Profiler::kFrameLatency + 3; ++i) {
			f.Work();
			f.profiler.RequestCapture(Mode::WholeFrameOnly);
			f.Present();
		}
		Check(f.profiler.GetWholeFrameSampleId() == 0 && f.profiler.GetSkippedWholeFrameCaptureCount() == 4, "pending query ring blocked or silently fabricated samples");
		const auto writes = f.context.writes;
		f.Work();
		f.profiler.RequestCapture(Mode::WholeFrameOnly);
		f.Present();
		Check(f.context.writes == writes, "pending whole-frame slots were overwritten");
		f.context.pending = false;
		f.Work();
		f.Present();
		f.Drain();
		Check(f.profiler.GetWholeFrameSampleId() == Profiler::kFrameLatency, "accepted delayed samples were lost or duplicated");

		Fixture rejected;
		rejected.Arm(Mode::WholeFrameOnly);
		rejected.Work();
		rejected.Present(0, DXGI_STATUS_OCCLUDED);
		rejected.Drain();
		Check(rejected.profiler.GetWholeFrameSampleId() == 0 && !rejected.profiler.HasWholeFrameGpuTime(), "occluded frame entered whole-frame history");
	}

	void InvalidQueriesPreserveCpuAndIdentity()
	{
		for (unsigned failure = 0; failure < 4; ++failure) {
			Fixture f(failure == 3 ? static_cast<int>(Profiler::kFrameLatency * (1 + 2 * Profiler::kMaxTimers)) : -1);
			f.Arm(Mode::WholeFrameOnly);
			f.Work();
			f.Present();
			f.context.disjoint = failure == 0;
			f.context.timestampsFailed = failure == 1;
			f.context.failed = failure == 2;
			f.Drain();
			Check(f.profiler.GetWholeFrameSampleId() == 1 && f.profiler.HasLatestWholeFrameCpuSample(), "GPU failure destroyed accepted CPU provenance");
			Check(!f.profiler.HasWholeFrameGpuTime() && !f.profiler.HasLatestWholeFrameGpuSample(), "invalid GPU timing was published as zero");
		}
		Fixture missingClock(0);
		missingClock.Arm(Mode::WholeFrameOnly);
		missingClock.Work();
		missingClock.Present();
		missingClock.Drain();
		Check(missingClock.profiler.HasLatestWholeFrameCpuSample() && !missingClock.profiler.HasWholeFrameGpuTime(), "missing disjoint query lost CPU timing or invented GPU timing");
	}
}

int main()
{
	try {
		AlwaysOnAcceptedPresent();
		SharedClockAndIndependentTotals();
		TestPresentKeepsCaptureOpen();
		LimitsAndRealWholeFrameOnly();
		NonblockingRingAndRejectedSamples();
		InvalidQueriesPreserveCpuAndIdentity();
		std::cout << "SE Present/whole-frame integration passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
