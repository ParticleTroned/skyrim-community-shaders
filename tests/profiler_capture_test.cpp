#include "Profiler.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
	using Mode = Profiler::CaptureMode;
	void Check(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
	void Near(float actual, float expected, const char* message)
	{
		Check(std::isfinite(actual) && std::abs(actual - expected) < 0.001f, message);
	}
	const Profiler::TimerResult& Find(const std::vector<Profiler::TimerResult>& results, std::string_view name)
	{
		const auto found = std::find_if(results.begin(), results.end(), [name](const auto& timer) { return timer.name == name; });
		Check(found != results.end(), "missing timer");
		return *found;
	}
	void Pass(Profiler& profiler, std::string_view name)
	{
		Check(profiler.BeginPass(name, false), "pass refused");
		profiler.EndPass(false);
	}
	struct Fixture
	{
		ID3D11Device device;
		ID3D11DeviceContext context;
		Profiler profiler;
		Fixture()
		{
			profiler.Initialize(&device, &context);
			profiler.SetUserEnabled(true);
		}
		void Arm(Mode mode)
		{
			profiler.RequestCapture(mode);
			profiler.EndFrame(0);
		}
		void Drain(uint32_t firstFrame)
		{
			for (uint32_t frame = firstFrame; frame < firstFrame + 8; ++frame)
				profiler.EndFrame(frame);
		}
	};

	void CpuOnlyAndModeSwitch()
	{
		Fixture f;
		f.Arm(Mode::CPU);
		Check(f.profiler.BeginPass("Root::Outer", false), "CPU-only GPU scope refused");
		Check(f.profiler.BeginCpuPass("Root::Child"), "CPU child refused");
		profilerTestClock += 5;
		f.profiler.EndCpuPass();
		f.profiler.EndPass(false);
		f.profiler.RequestCapture(Mode::CPU);
		f.profiler.EndFrame(1);
		const auto& root = Find(f.profiler.GetImmediateCpuResults(), "Root::Outer");
		const auto& child = Find(f.profiler.GetImmediateCpuResults(), "Root::Child");
		Near(root.cpuTimeMs + child.cpuTimeMs, root.GetOutermostCpuHistorySample(0), "CPU-only nesting double counted");
		Near(child.GetOutermostCpuHistorySample(0), 0, "same feature child counted in feature total");
		Check(f.context.writes == 0 && f.profiler.GetCapturedCpuFrameCount() == 1, "CPU-only publication acquired GPU queries or lagged");
		Check(f.profiler.GetCapturedFrameCount() == 0, "paired publication did not retain its frame");
		f.profiler.RequestCapture(Mode::GPU);
		f.profiler.EndFrame(2);
		Check(!Find(f.profiler.GetImmediateCpuResults(), "Root::Outer").activeCpu, "empty CPU cycle retained activity");
		Near(f.profiler.GetImmediateCpuTotalTimeMs(), 0, "empty CPU cycle retained a total");
		const auto cpuClock = profilerTestClock;
		Pass(f.profiler, "Gpu::Only");
		Check(!f.profiler.BeginCpuPass("Cpu::Excluded"), "GPU-only mode acquired CPU timing");
		f.profiler.EndFrame(3);
		Check(profilerTestClock == cpuClock && f.context.writes != 0, "GPU-only mode read QPC or omitted GPU queries");
		Check(f.profiler.GetCapturedCpuFrameCount() == 2 && f.profiler.GetCpuPublicationCount() == 2, "GPU-only mode rewrote CPU provenance");
		f.Drain(4);
		Check(f.profiler.GetCapturedFrameCount() == 3, "GPU-only result did not drain");
		f.profiler.ClearTimers();
		f.Drain(12);
		Check(f.profiler.GetImmediateCpuResults().empty() && f.profiler.GetResults().empty(), "cleared results resurrected");
		Check(f.profiler.GetCpuPublicationCount() == 0, "clear retained CPU publication identity");
	}

	void PendingGpuDoesNotBlockCpu()
	{
		Fixture f;
		f.context.pending = true;
		f.Arm(Mode::Both);
		for (uint32_t frame = 1; frame <= 9; ++frame) {
			Pass(f.profiler, frame <= 3 ? "Gpu::Queued" : "Cpu::Fallback");
			f.profiler.RequestCapture(Mode::Both);
			f.profiler.EndFrame(frame);
			Check(f.profiler.GetCapturedCpuFrameCount() == frame && f.profiler.GetCpuPublicationCount() == frame, "pending GPU blocked CPU publication");
			Near(f.profiler.GetImmediateCpuTotalTimeMs(), 1, "CPU measurements merged across engine frames");
		}
		Check(f.profiler.GetCapturedFrameCount() == 0, "pending GPU data was published");
		Check(Find(f.profiler.GetImmediateCpuResults(), "Cpu::Fallback").cpuHistoryCount == 6, "CPU fallback history lost frames");
		const auto writes = f.context.writes;
		f.context.pending = false;
		f.Drain(10);
		Check(f.profiler.GetCapturedFrameCount() == 3 && f.context.writes == writes, "draining fabricated a GPU frame");
		Check(std::none_of(f.profiler.GetResults().begin(), f.profiler.GetResults().end(), [](const auto& timer) { return timer.name == "Cpu::Fallback"; }), "unpaired CPU frames contaminated the paired view");
	}

	void CapacityAndMixedNesting()
	{
		Fixture f;
		f.Arm(Mode::Both);
		Check(f.profiler.BeginPass("Root::Outer", false), "GPU root refused");
		for (uint32_t index = 1; index < Profiler::kMaxTimers; ++index)
			Pass(f.profiler, "Root::Repeated");
		Pass(f.profiler, "Child::Overflow");
		f.profiler.EndPass(false);
		f.profiler.EndFrame(1);
		Check(f.profiler.GetSlotRefusals() == 1, "GPU capacity refusal not counted");
		const auto& results = f.profiler.GetImmediateCpuResults();
		float sum = 0;
		for (const auto& result : results)
			sum += result.cpuTimeMs;
		Near(sum, Find(results, "Root::Outer").GetOutermostCpuHistorySample(0), "CPU fallback did not subtract from GPU-backed parent");
		Check(Find(results, "Child::Overflow").activeCpu, "GPU capacity discarded CPU sample");
		Check(Find(results, "Root::Repeated").cpuHistoryCount == 1, "repeated name pushed multiple history samples");
		f.Drain(2);
		Near(f.profiler.GetResolvedCpuTotalTimeMs(), sum, "paired CPU accounting changed");
		Fixture cpu;
		cpu.Arm(Mode::CPU);
		for (uint32_t index = 0; index < Profiler::kMaxTimers; ++index)
			Pass(cpu.profiler, "Cpu::Pass");
		Check(!cpu.profiler.BeginPass("Cpu::Overflow", false) && cpu.profiler.GetCpuSlotRefusals() == 1, "CPU capacity was not bounded or diagnosed");
	}

	void PartialRingDrain()
	{
		for (uint32_t count : { 1u, 2u }) {
			for (int mode = 0; mode < 3; ++mode) {
				Fixture f;
				f.Arm(Mode::Both);
				for (uint32_t frame = 1; frame <= count; ++frame) {
					Pass(f.profiler, "Gpu::Partial");
					f.profiler.RequestCapture(frame < count ? Mode::Both : mode == 1 ? Mode::CPU :
																					   Mode::None);
					f.profiler.EndFrame(frame);
				}
				if (mode == 2)
					f.profiler.SetUserEnabled(false);
				const auto writes = f.context.writes;
				f.Drain(count + 1);
				Check(f.profiler.GetCapturedFrameCount() == count && f.context.writes == writes, "partial GPU ring failed to drain without new acquisition");
			}
		}
	}

	void BoundedCaptureIsolation()
	{
		Fixture f;
		f.context.pending = true;
		uint64_t id = 0;
		Check(f.profiler.StartBoundedCapture(3, true, id), "bounded capture refused");
		f.profiler.EndFrame(0);
		for (uint32_t frame = 1; frame <= 6; ++frame) {
			Pass(f.profiler, frame <= 3 ? "Capture::Owned" : "Cpu::Independent");
			f.profiler.RequestCapture(Mode::CPU);
			f.profiler.EndFrame(frame);
		}
		Check(f.profiler.GetBoundedCaptureProgress().submittedFrames == 3 && f.profiler.GetBoundedCaptureProgress().resolvedFrames == 0, "independent CPU publication completed a pending bounded capture");
		f.context.pending = false;
		f.Drain(7);
		const auto* captured = f.profiler.GetBoundedCaptureResults(id);
		Check(f.profiler.GetBoundedCaptureProgress().state == Profiler::CaptureSessionState::Completed && captured && captured->size() == 1, "bounded capture lost ownership or failed to complete");
		Check(captured->front().cpuHistoryCount == 3 && captured->front().historyCount == 3, "bounded CPU/GPU frame histories diverged");

		Fixture mid;
		mid.Arm(Mode::Both);
		Check(mid.profiler.BeginPass("Before::Session", false), "pre-session pass refused");
		Check(mid.profiler.StartBoundedCapture(1, false, id), "mid-frame bounded capture refused");
		mid.profiler.EndPass(false);
		mid.profiler.EndFrame(1);
		Check(mid.profiler.GetBoundedCaptureProgress().submittedFrames == 0, "session claimed a pre-existing frame");
		Pass(mid.profiler, "Session::Owned");
		mid.profiler.EndFrame(2);
		mid.Drain(3);
		captured = mid.profiler.GetBoundedCaptureResults(id);
		Check(captured && captured->size() == 1 && captured->front().name == "Session::Owned", "mid-frame start contaminated bounded results");
	}

	void RequestsRemovalAndReinitialization()
	{
		Fixture f;
		f.profiler.RequestCapture(Mode::CPU);
		f.profiler.RequestCapture(Mode::GPU);
		f.profiler.EndFrame(0);
		Pass(f.profiler, "Feature::Both");
		f.profiler.EndFrame(1);
		Check(f.context.writes > 0 && f.profiler.GetCpuPublicationCount() == 1, "capture requests did not combine");
		f.profiler.ClearTimersForFeature("Feature");
		f.Drain(2);
		Check(f.profiler.GetImmediateCpuResults().empty() && f.profiler.GetResults().empty(), "feature removal resurrected queued data");
		f.profiler.Initialize(&f.device, &f.context);
		Check(f.profiler.GetCpuPublicationCount() == 0 && f.profiler.GetImmediateCpuResults().empty(), "reinitialization retained CPU data");
	}

	void LateGpuReadinessDoesNotClaimCpuFallback()
	{
		Fixture f;
		f.context.pending = true;
		f.Arm(Mode::Both);
		uint64_t id = 0;
		for (uint32_t frame = 1; frame <= 3; ++frame) {
			Pass(f.profiler, "Before::Queued");
			if (frame == 3)
				Check(f.profiler.StartBoundedCapture(1, false, id), "queued-ring session refused");
			f.profiler.RequestCapture(Mode::Both);
			f.profiler.EndFrame(frame);
		}
		Pass(f.profiler, "Cpu::Unpaired");
		f.context.pending = false;
		f.profiler.EndFrame(4);
		Check(f.profiler.GetBoundedCaptureProgress().submittedFrames == 0, "late GPU readiness claimed a CPU fallback as a paired frame");
		Check(Find(f.profiler.GetImmediateCpuResults(), "Cpu::Unpaired").activeCpu, "unpaired CPU result was lost");
		Pass(f.profiler, "Capture::Paired");
		f.profiler.EndFrame(5);
		f.Drain(6);
		const auto* results = f.profiler.GetBoundedCaptureResults(id);
		Check(results && results->size() == 1 && results->front().name == "Capture::Paired", "bounded results contain an unpaired fallback");
	}

	void InvalidGpuAndOpenScopeReset()
	{
		for (bool failed : { false, true }) {
			Fixture f;
			f.context.failed = failed;
			f.context.disjoint = !failed;
			f.Arm(Mode::Both);
			Pass(f.profiler, "Cpu::Survives");
			f.profiler.EndFrame(1);
			Near(f.profiler.GetImmediateCpuTotalTimeMs(), 1, "invalid GPU clock discarded immediate CPU measurement");
			f.Drain(2);
			Near(f.profiler.GetResolvedCpuTotalTimeMs(), 1, "invalid GPU clock discarded paired CPU measurement");
		}

		Fixture f;
		f.Arm(Mode::Both);
		Check(f.profiler.BeginPass("Cleared::Gpu", false), "open GPU scope refused");
		Check(f.profiler.BeginCpuPass("Cleared::Cpu"), "open CPU scope refused");
		f.profiler.ClearTimers();
		f.profiler.EndCpuPass();
		f.profiler.EndPass(false);
		f.profiler.EndFrame(1);
		f.Drain(2);
		Check(f.profiler.GetImmediateCpuResults().empty() && f.profiler.GetResults().empty(), "open scopes republished after clear");

		Fixture disabled;
		disabled.Arm(Mode::CPU);
		Check(disabled.profiler.BeginPass("Cpu::Closing", false), "CPU fallback refused");
		disabled.profiler.SetUserEnabled(false);
		disabled.profiler.EndPass(false);
		disabled.profiler.EndFrame(1);
		Near(disabled.profiler.GetImmediateCpuTotalTimeMs(), 1, "disable lost an already-open CPU fallback");
		Check(!disabled.profiler.IsEnabled(), "disable re-enabled acquisition");
	}
}

int main()
{
	try {
		CpuOnlyAndModeSwitch();
		PendingGpuDoesNotBlockCpu();
		CapacityAndMixedNesting();
		PartialRingDrain();
		BoundedCaptureIsolation();
		RequestsRemovalAndReinitialization();
		LateGpuReadinessDoesNotClaimCpuFallback();
		InvalidGpuAndOpenScopeReset();
		std::cout << "Profiler capture regression cases passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
