#include "Profiler.h"
#include "Utils/ProfilerTiming.h"
#include "Utils/ResourceName.h"

#include <array>
#include <chrono>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <thread>

namespace Util
{
	bool failNextQueryName = false;
	// Naming is observed here without linking the game's resource implementation.
	void SetResourceName(ID3D11DeviceChild* resource, const char* format, ...)
	{
		if (!resource || (!std::string_view(format).starts_with("Profiler::Detail") &&
							 !std::string_view(format).starts_with("Profiler::WholeFrame")))
			throw std::runtime_error("profiler query missing shared resource naming");
		if (std::exchange(failNextQueryName, false))
			throw std::bad_alloc();
	}
}

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
		using Util::PassTimingState;
		const auto newCapture = [] { return std::make_shared<Util::PassTimingCapture>(); };
		const auto unavailable = newCapture();
		const auto sharedCapture = unavailable;
		const auto independentCapture = newCapture();
		Check(Util::ReadPassTiming(unavailable).captureId != 0 &&
				  Util::ReadPassTiming(unavailable).captureId == Util::ReadPassTiming(sharedCapture).captureId &&
				  Util::ReadPassTiming(unavailable).captureId != Util::ReadPassTiming(independentCapture).captureId,
			"shared timing identity missing or reused by another invocation");
		Check(!profiler.BeginDetailPass("Detail::Uninitialized", unavailable), "uninitialized detail entered");
		Check(std::string_view(Util::ReadPassTiming(unavailable).gpuReason) == "profiler_uninitialized", "uninitialized reason missing");
		Check(std::string_view(Util::ReadPassTiming({}).gpuReason) == "capture_disabled", "null capture not explicit");
		Check(std::string_view(Util::ReadPassTiming(newCapture()).gpuReason) == "scope_not_entered", "unentered capture not explicit");
		profiler.Initialize(device.get(), context.get());
		const auto disabled = newCapture();
		Check(!profiler.BeginPass("Root::Disabled", false, disabled), "disabled pass entered");
		Check(std::string_view(Util::ReadPassTiming(disabled).cpuReason) == "profiler_disabled", "disabled profiler reason missing");
		profiler.SetUserEnabled(true);
		const auto inactive = newCapture();
		Check(!profiler.BeginDetailPass("Detail::Inactive", inactive), "inactive capture entered");
		Check(std::string_view(Util::ReadPassTiming(inactive).gpuReason) == "capture_inactive", "inactive capture reason missing");
		uint64_t captureId = 0;
		Check(profiler.StartBoundedCapture(3, true, captureId), "bounded capture refused");
		profiler.EndFrame(0);
		std::array<Util::PassTimingHandle, 3> roots, leaves, detailParents, detailChildren;
		for (uint32_t frame = 1; frame <= 3; ++frame) {
			const auto index = frame - 1;
			roots[index] = newCapture();
			leaves[index] = newCapture();
			detailParents[index] = newCapture();
			detailChildren[index] = newCapture();
			Check(profiler.BeginCpuPass("Root::CpuOuter"), "CPU outer scope refused");
			Check(profiler.BeginPass("Root::GpuParent", false, roots[index]), "GPU parent refused");
			Check(Util::ReadPassTiming(roots[index]).cpuState == PassTimingState::Pending, "open CPU scope not pending");
			Check(!profiler.BeginDetailPass("Detail::Disabled", {}), "null detail allocated a scope");
			Check(profiler.BeginDetailPass("Detail::Outer", detailParents[index]), "detail parent refused");
			Check(profiler.BeginCpuPass("Worker::CpuChild"), "CPU child refused");
			Work();
			Check(profiler.BeginPass("Leaf::Repeated", false, leaves[index]), "GPU child refused");
			Check(profiler.BeginDetailPass("Detail::Inner", detailChildren[index]), "detail child refused");
			Work();
			profiler.EndDetailPass();
			Check(profiler.BeginPass("Leaf::Repeated", false), "repeated GPU child refused");
			Work();
			profiler.EndPass(false);
			profiler.EndPass(false);
			profiler.EndCpuPass();
			profiler.EndDetailPass();
			profiler.EndPass(false);
			profiler.EndCpuPass();
			const auto pending = Util::ReadPassTiming(roots[index]);
			Check(pending.cpuState == PassTimingState::Ready && pending.gpuState == PassTimingState::Pending, "CPU not ready independently of GPU");
			profiler.EndFrame(frame);
			Check(profiler.GetAcquiredSlots() == 3, "detail scopes consumed legacy slots");
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
			const auto rootTiming = Util::ReadPassTiming(roots[sample]);
			const auto leafTiming = Util::ReadPassTiming(leaves[sample]);
			const auto detailTiming = Util::ReadPassTiming(detailParents[sample]);
			const auto childTiming = Util::ReadPassTiming(detailChildren[sample]);
			Check(rootTiming.gpuState == PassTimingState::Ready && detailTiming.gpuState == PassTimingState::Ready && childTiming.gpuState == PassTimingState::Ready, "GPU capture failed to resolve");
			Check(rootTiming.capturedFrame == sample + 1 && detailTiming.detailOnly && !rootTiming.detailOnly, "captured invocation identity changed");
			Near(rootTiming.gpuSelfMs, gpuRoot.GetHistorySample(sample), "captured existing GPU self differs from legacy row");
			Near(rootTiming.cpuSelfMs, gpuRoot.GetCpuHistorySample(sample), "captured existing CPU self differs from legacy row");
			Near(rootTiming.gpuInclusiveMs, rootTiming.gpuSelfMs + leafTiming.gpuInclusiveMs, "detail work subtracted from legacy GPU parent");
			Near(detailTiming.cpuInclusiveMs, detailTiming.cpuSelfMs + childTiming.cpuInclusiveMs, "detail CPU nesting incorrect");
			Near(detailTiming.gpuInclusiveMs, detailTiming.gpuSelfMs + childTiming.gpuInclusiveMs, "detail GPU nesting incorrect");
			Check(detailTiming.cpuInclusiveMs > 2.0f && detailTiming.cpuSelfMs > 1.0f, "ordinary scopes subtracted from detail CPU accounting");
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
		const float retainedRootGpuMs = gpuRoot.GetOutermostGpuHistorySample(0);
		profiler.ClearTimers();
		Check(profiler.GetResults().empty(), "history reset retained timers");
		Check(Util::ReadPassTiming(roots[0]).gpuState == PassTimingState::Ready, "history reset erased resolved caller evidence");
		profiler.RequestCapture();
		profiler.EndFrame(5);
		Check(!profiler.BeginDetailPass("Detail::Reused", detailParents[0]), "retained handle reused for a new invocation");
		Check(profiler.BeginPass("Root::Capacity", false), "capacity parent refused");
		for (uint32_t i = 0; i < Profiler::kMaxDetailTimers; ++i) {
			Check(profiler.BeginDetailPass("Detail::Capacity", newCapture()), "detail pool refused before capacity");
			profiler.EndDetailPass();
		}
		const auto exhausted = newCapture();
		Check(!profiler.BeginDetailPass("Detail::Exhausted", exhausted), "detail capacity unbounded");
		Check(std::string_view(Util::ReadPassTiming(exhausted).gpuReason) == "query_capacity_exhausted", "capacity refusal missing reason");
		Check(profiler.BeginPass("Root::Unaffected", false), "detail exhaustion consumed ordinary capacity");
		profiler.EndPass(false);
		profiler.EndPass(false);
		profiler.EndFrame(6);
		profiler.ClearTimers();
		profiler.RequestCapture();
		profiler.EndFrame(7);
		const auto cancelled = newCapture();
		Check(profiler.BeginPass("Root::Cancelled", false, cancelled), "cancel scope refused");
		profiler.EndPass(false);
		profiler.EndFrame(8);
		profiler.SetUserEnabled(false);
		const auto cancelledState = Util::ReadPassTiming(cancelled);
		Check(cancelledState.cpuState == PassTimingState::Ready && cancelledState.gpuState == PassTimingState::Unavailable &&
				  std::string_view(cancelledState.gpuReason) == "profiler_disabled",
			"disable retained a permanently pending GPU handle");
		profiler.SetUserEnabled(true);
		profiler.ClearTimers();
		profiler.RequestCapture();
		profiler.EndFrame(9);
		const auto disabledDuringScope = newCapture();
		const auto disabledDuringDetail = newCapture();
		Check(profiler.BeginPass("Root::DisableDuringScope", false, disabledDuringScope), "midframe disable parent refused");
		Check(profiler.BeginDetailPass("Detail::DisableDuringScope", disabledDuringDetail), "midframe disable detail refused");
		profiler.SetUserEnabled(false);
		profiler.EndDetailPass();
		profiler.EndPass(false);
		profiler.EndFrame(10);
		Check(Util::ReadPassTiming(disabledDuringScope).cpuState == PassTimingState::Unavailable &&
				  Util::ReadPassTiming(disabledDuringDetail).gpuState == PassTimingState::Unavailable,
			"ending disabled scopes revived cancelled evidence");
		profiler.SetUserEnabled(true);
		profiler.ClearTimers();
		profiler.RequestCapture();
		profiler.EndFrame(11);
		const auto unfinished = newCapture();
		Check(profiler.BeginDetailPass("Detail::Unfinished", unfinished), "unfinished scope refused");
		profiler.EndFrame(12);
		Check(Util::ReadPassTiming(unfinished).gpuState == PassTimingState::Failed &&
				  std::string_view(Util::ReadPassTiming(unfinished).gpuReason) == "scope_not_ended",
			"unterminated query remained pending");
		profiler.Release();
		Near(Util::ReadPassTiming(roots[0]).gpuInclusiveMs, retainedRootGpuMs, "retained capture changed after ring reuse");
		profiler.Initialize(device.get(), context.get());
		Check(profiler.StartBoundedCapture(1, true, captureId), "fresh short capture refused");
		profiler.EndFrame(0);
		const auto shortCapture = newCapture();
		Check(profiler.BeginPass("Root::ShortCapture", false, shortCapture), "short capture pass refused");
		const auto failedDetail = newCapture();
		Util::failNextQueryName = true;
		Check(!profiler.BeginDetailPass("Detail::AllocationFailure", failedDetail), "optional detail allocation failure escaped");
		Check(std::string_view(Util::ReadPassTiming(failedDetail).gpuReason) == "timing_allocation_failed", "detail allocation failure reason missing");
		Check(Util::ReadPassTiming(failedDetail).captureId != 0, "failed timing lost its scope identity");
		Check(profiler.BeginDetailPass("Detail::RetryAfterFailure", newCapture()), "failed detail poisoned the next query slot");
		profiler.EndDetailPass();
		profiler.EndPass(false);
		profiler.EndFrame(1);
		context->Flush();
		const auto shortDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		while (profiler.GetBoundedCaptureProgress().state == Profiler::CaptureSessionState::Running &&
			   std::chrono::steady_clock::now() < shortDeadline) {
			profiler.EndFrame(2);
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		Check(profiler.GetBoundedCaptureProgress().state == Profiler::CaptureSessionState::Completed &&
				  Util::ReadPassTiming(shortCapture).gpuState == PassTimingState::Ready,
			"fresh one-frame capture cannot drain before ring warmup");
		profiler.Release();

		profiler.Initialize(device.get(), context.get(), true);
		const auto flatDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		bool flatResolved = false;
		for (uint32_t frame = 0; !flatResolved && std::chrono::steady_clock::now() < flatDeadline; ++frame) {
			profiler.RequestCapture();
			Work();
			profiler.BeginFlatPresent(frame, 0);
			context->Flush();
			profiler.CompleteFlatPresent(S_OK);
			for (const auto& sample : profiler.GetFlatTiming()->samples) {
				if (!sample.hasGpu || !sample.hasCpu)
					continue;
				Check(sample.presentId == sample.frame + 1 && sample.cpuMs > 0 && sample.gpuMs > 0,
					"WARP whole-frame timing lost CPU/GPU source identity");
				flatResolved = true;
			}
		}
		Check(flatResolved, "whole-frame D3D11 queries did not resolve on WARP");
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
