#include "Profiler.h"
#include "Utils/ProfilerTiming.h"

#include <array>
#include <chrono>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <thread>

namespace
{
	void Check(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	void Near(double actual, double expected, const char* message)
	{
		Check(std::isfinite(actual) && std::abs(actual - expected) < 0.001, message);
	}

	void TestIntervals()
	{
		using namespace Util::ProfilerTiming;
		const std::array intervals{ Interval{ -1, 20 }, Interval{ 0, 10 }, Interval{ 1, 4 }, Interval{ 0, 3 }, Interval{ -1, 2 } };
		const auto self = ResolveSelfTimes(intervals);
		Check(self == std::vector<double>{ 7, 6, 4, 3, 2 }, "nested siblings or roots double counted");
		Near(std::accumulate(self.begin(), self.end(), 0.0), 22, "self sum differs from root sum");
		const std::array invalid{ Interval{ -1, 20 }, Interval{ 0, -1 }, Interval{ 1, 1001 }, Interval{ 2, 4 }, Interval{ 1, 3 } };
		Check(ResolveSelfTimes(invalid) == std::vector<double>{ 13, 0, 0, 4, 3 }, "invalid ancestors lost or duplicated descendant coverage");
		const std::array orphan{ Interval{ -1, -1 }, Interval{ 0, 5 }, Interval{ 1, 2 } };
		Check(ResolveSelfTimes(orphan) == std::vector<double>{ 0, 3, 2 }, "invalid root suppressed valid children");
		const std::array malformed{ Interval{ 0, 2 }, Interval{ 99, 3 }, Interval{ -1, 1 }, Interval{ 2, 2 } };
		Check(ResolveSelfTimes(malformed) == std::vector<double>{ 2, 3, 0, 2 }, "invalid parent index or negative self time");
		Check(ResolveSelfTimes({}).empty(), "empty interval set produced samples");
		Check(IsValidSample(0) && IsValidSample(1000) && !IsValidSample(-1) && !IsValidSample(1001) &&
				  !IsValidSample(std::numeric_limits<double>::infinity()) && !IsValidSample(std::numeric_limits<double>::quiet_NaN()),
			"invalid sample boundary");
		const auto child = CompleteCpuScope(4, 1);
		const auto invalidParent = CompleteCpuScope(-1, child.coveredMs + 2);
		const auto root = CompleteCpuScope(10, invalidParent.coveredMs);
		Near(child.selfMs, 3, "CPU child self time incorrect");
		Near(invalidParent.selfMs, 0, "invalid CPU sample retained");
		Near(root.selfMs, 4, "CPU descendant coverage did not pass invalid parent");
		Near(CompleteCpuScope(2, 3).selfMs, 0, "CPU self time became negative");
	}

	const Profiler::TimerResult& Find(const std::vector<Profiler::TimerResult>& results, std::string_view name)
	{
		const auto it = std::find_if(results.begin(), results.end(), [name](const auto& timer) { return timer.name == name; });
		Check(it != results.end(), "expected timer missing");
		return *it;
	}

	void Work()
	{
		const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(1);
		while (std::chrono::steady_clock::now() < end)
			std::this_thread::yield();
	}

	void TestProfiler()
	{
		winrt::com_ptr<ID3D11Device> device;
		winrt::com_ptr<ID3D11DeviceContext> context;
		const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
		winrt::check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &level, 1,
			D3D11_SDK_VERSION, device.put(), nullptr, context.put()));
		Profiler profiler;
		profiler.Initialize(device.get(), context.get());
		profiler.SetUserEnabled(true);
		uint64_t captureId = 0;
		Check(profiler.StartBoundedCapture(3, true, captureId), "bounded capture refused");
		profiler.EndFrame(0);
		for (uint32_t frame = 1; frame <= 3; ++frame) {
			Check(profiler.BeginCpuPass("Root::CpuOuter"), "CPU outer scope refused");
			Check(profiler.BeginPass("Root::GpuParent", false), "GPU parent refused");
			Check(profiler.BeginCpuPass("Worker::CpuChild"), "CPU child refused");
			Work();
			Check(profiler.BeginPass("Leaf::Repeated", false), "GPU child refused");
			Work();
			Check(profiler.BeginPass("Leaf::Repeated", false), "repeated GPU child refused");
			Work();
			profiler.EndPass(false);
			profiler.EndPass(false);
			profiler.EndCpuPass();
			profiler.EndPass(false);
			profiler.EndCpuPass();
			profiler.EndFrame(frame);
		}
		context->Flush();
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (profiler.GetBoundedCaptureProgress().state == Profiler::CaptureSessionState::Running &&
			   std::chrono::steady_clock::now() < deadline) {
			profiler.EndFrame(4);
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		Check(profiler.GetBoundedCaptureProgress().state == Profiler::CaptureSessionState::Completed, "bounded capture failed to resolve");
		const auto* captured = profiler.GetBoundedCaptureResults(captureId);
		Check(captured && captured->size() == 4, "same-name scopes not aggregated");
		const auto& cpuRoot = Find(*captured, "Root::CpuOuter");
		const auto& gpuRoot = Find(*captured, "Root::GpuParent");
		const auto& leaf = Find(*captured, "Leaf::Repeated");
		Check(cpuRoot.cpuHistoryCount == 3 && gpuRoot.historyCount == 3, "capture history count incorrect");
		for (uint32_t sample = 0; sample < 3; ++sample) {
			double gpuSum = 0, cpuSum = 0;
			for (const auto& timer : *captured) {
				gpuSum += timer.GetHistorySample(sample);
				cpuSum += timer.GetCpuHistorySample(sample);
			}
			Near(gpuSum, gpuRoot.GetOutermostGpuHistorySample(sample), "GPU rows do not partition inclusive root");
			Near(cpuSum, cpuRoot.GetOutermostCpuHistorySample(sample), "mixed CPU/GPU rows do not partition inclusive CPU root");
			Near(leaf.GetHistorySample(sample), leaf.GetOutermostGpuHistorySample(sample), "repeated GPU name counted twice");
			Check(cpuRoot.GetCpuHistorySample(sample) < cpuRoot.GetOutermostCpuHistorySample(sample), "CPU-only root still includes children");
		}
		double gpuSum = 0, cpuSum = 0, topLevelSum = 0;
		for (const auto& timer : profiler.GetResults()) {
			gpuSum += timer.gpuTimeMs;
			cpuSum += timer.cpuTimeMs;
			topLevelSum += timer.topLevelMs;
		}
		Near(gpuSum, profiler.GetResolvedTotalTimeMs(), "resolved GPU total differs from row sum");
		Near(topLevelSum, profiler.GetResolvedTotalTimeMs(), "inclusive top-level GPU contract changed");
		Near(cpuSum, profiler.GetResolvedCpuTotalTimeMs(), "resolved CPU total differs from row sum");
		profiler.ClearTimers();
		Check(profiler.GetResults().empty(), "history reset retained timers");
		profiler.Release();
	}
}

int main()
{
	try {
		TestIntervals();
		TestProfiler();
		std::cout << "Profiler self-time and D3D11 WARP integration tests passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
