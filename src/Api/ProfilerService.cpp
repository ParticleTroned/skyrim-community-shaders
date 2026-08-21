#include "Api/ProfilerService.h"

#include "Api/ServiceRegistry.h"
#include "BuildProvenance.h"
#include "Globals.h"
#include "Profiler.h"

#include <algorithm>
#include <mutex>
#include <thread>

namespace
{
	using CSX::ProfilerAPI::CaptureProgress001;
	using CSX::ProfilerAPI::CaptureRequest001;
	using CSX::ProfilerAPI::CaptureState;
	using CSX::ProfilerAPI::Snapshot001;
	using CSX::ProfilerAPI::Status;
	using CSX::ProfilerAPI::TimerDescriptor001;
	using CSX::ProfilerAPI::TimingDomain;

	CaptureState ToApiState(Profiler::CaptureSessionState a_state)
	{
		switch (a_state) {
		case Profiler::CaptureSessionState::Running:
			return CaptureState::kRunning;
		case Profiler::CaptureSessionState::Completed:
			return CaptureState::kCompleted;
		case Profiler::CaptureSessionState::Cancelled:
			return CaptureState::kCancelled;
		default:
			return CaptureState::kNone;
		}
	}

	void FillProgress(const Profiler::CaptureSessionProgress& a_source, CaptureProgress001& a_output)
	{
		a_output = {
			.structSize = sizeof(CaptureProgress001),
			.captureId = a_source.sessionId,
			.state = ToApiState(a_source.state),
			.requestedFrames = a_source.requestedFrames,
			.submittedFrames = a_source.submittedFrames,
			.resolvedFrames = a_source.resolvedFrames,
		};
	}

	class ProfilerService
	{
	public:
		ProfilerService() : ownerThread(std::this_thread::get_id()) {}

		Status GetSnapshot(Snapshot001& a_output) const
		{
			if (!IsOwnerThread())
				return Status::kWrongThread;
			auto* profiler = globals::profiler;
			if (!profiler)
				return Status::kUnavailable;
			a_output = {
				.structSize = sizeof(Snapshot001),
				.available = profiler->IsInitialized() ? 1u : 0u,
				.enabled = profiler->IsUserEnabled() ? 1u : 0u,
				.capturing = profiler->IsEnabled() ? 1u : 0u,
				.timerCount = static_cast<std::uint32_t>(profiler->GetResults().size()),
				.historyCapacity = Profiler::kHistorySize,
				.maximumTimers = Profiler::kMaxTimers,
				.frameLatency = Profiler::kFrameLatency,
				.capturedFrameCount = profiler->GetCapturedFrameCount(),
				.acquiredSlots = profiler->GetAcquiredSlots(),
				.peakAcquiredSlots = profiler->GetPeakAcquiredSlots(),
				.slotRefusals = profiler->GetSlotRefusals(),
				.gpuTotalMs = profiler->GetTotalTimeMs(),
				.cpuTotalMs = profiler->GetCpuTotalTimeMs(),
				.resolvedGpuTotalMs = profiler->GetResolvedTotalTimeMs(),
				.resolvedCpuTotalMs = profiler->GetResolvedCpuTotalTimeMs(),
				.capabilities = ProfilerAPI::ServiceCapabilities,
				.buildId = BuildProvenance::GetBuildId().data(),
			};
			return a_output.available ? Status::kSuccess : Status::kUnavailable;
		}

		std::uint32_t GetTimerCount() const
		{
			return IsOwnerThread() && globals::profiler ?
				static_cast<std::uint32_t>(globals::profiler->GetResults().size()) : 0u;
		}

		Status GetTimer(std::uint32_t a_index, TimerDescriptor001& a_output) const
		{
			if (!IsOwnerThread())
				return Status::kWrongThread;
			if (!globals::profiler)
				return Status::kUnavailable;
			return FillTimer(globals::profiler->GetResults(), a_index, a_output);
		}

		std::uint32_t GetCaptureTimerCount(std::uint64_t a_captureId) const
		{
			if (!IsOwnerThread() || !globals::profiler)
				return 0;
			const auto* timers = globals::profiler->GetBoundedCaptureResults(a_captureId);
			return timers ? static_cast<std::uint32_t>(timers->size()) : 0u;
		}

		Status GetCaptureTimer(std::uint64_t a_captureId, std::uint32_t a_index, TimerDescriptor001& a_output) const
		{
			if (!IsOwnerThread())
				return Status::kWrongThread;
			if (!globals::profiler)
				return Status::kUnavailable;
			const auto* timers = globals::profiler->GetBoundedCaptureResults(a_captureId);
			return timers ? FillTimer(*timers, a_index, a_output) : Status::kCaptureNotFound;
		}

		Status GetCaptureHistory(std::uint64_t a_captureId, std::uint32_t a_timerIndex, TimingDomain a_domain, std::uint32_t a_sampleIndex, float& a_output) const
		{
			if (!IsOwnerThread())
				return Status::kWrongThread;
			if (!globals::profiler)
				return Status::kUnavailable;
			const auto* timers = globals::profiler->GetBoundedCaptureResults(a_captureId);
			return timers ? ReadHistory(*timers, a_timerIndex, a_domain, a_sampleIndex, a_output) : Status::kCaptureNotFound;
		}

	private:
		static Status FillTimer(const std::vector<Profiler::TimerResult>& a_timers, std::uint32_t a_index, TimerDescriptor001& a_output)
		{
			if (a_index >= a_timers.size())
				return Status::kTimerNotFound;
			const auto& timer = a_timers[a_index];
			a_output = {
				.structSize = sizeof(TimerDescriptor001),
				.name = timer.name.c_str(),
				.hasGpu = timer.hasGpu ? 1u : 0u,
				.hasCpu = timer.hasCpu ? 1u : 0u,
				.activeGpu = timer.activeGpu ? 1u : 0u,
				.activeCpu = timer.activeCpu ? 1u : 0u,
				.gpuHistoryCount = timer.historyCount,
				.cpuHistoryCount = timer.cpuHistoryCount,
				.gpuMs = timer.gpuTimeMs,
				.gpuTopLevelMs = timer.topLevelMs,
				.gpuAverageMs = timer.avgMs,
				.gpuP95Ms = timer.p95Ms,
				.gpuP99Ms = timer.p99Ms,
				.cpuMs = timer.cpuTimeMs,
				.cpuAverageMs = timer.cpuAvgMs,
				.cpuP95Ms = timer.cpuP95Ms,
				.cpuP99Ms = timer.cpuP99Ms,
			};
			return Status::kSuccess;
		}

		static Status ReadHistory(const std::vector<Profiler::TimerResult>& a_timers, std::uint32_t a_timerIndex, TimingDomain a_domain, std::uint32_t a_sampleIndex, float& a_output)
		{
			if (a_timerIndex >= a_timers.size())
				return Status::kTimerNotFound;
			const auto& timer = a_timers[a_timerIndex];
			if (a_domain == TimingDomain::kGpu) {
				if (a_sampleIndex >= timer.historyCount)
					return Status::kInvalidArgument;
				a_output = timer.GetHistorySample(a_sampleIndex);
				return Status::kSuccess;
			}
			if (a_domain == TimingDomain::kCpu) {
				if (a_sampleIndex >= timer.cpuHistoryCount)
					return Status::kInvalidArgument;
				a_output = timer.GetCpuHistorySample(a_sampleIndex);
				return Status::kSuccess;
			}
			return Status::kInvalidArgument;
		}

	public:

		Status GetHistory(std::uint32_t a_timerIndex, TimingDomain a_domain, std::uint32_t a_sampleIndex, float& a_output) const
		{
			if (!IsOwnerThread())
				return Status::kWrongThread;
			if (!globals::profiler)
				return Status::kUnavailable;
			return ReadHistory(globals::profiler->GetResults(), a_timerIndex, a_domain, a_sampleIndex, a_output);
		}

		Status SetEnabled(bool a_enabled) const
		{
			if (!IsOwnerThread())
				return Status::kWrongThread;
			if (!globals::profiler)
				return Status::kUnavailable;
			globals::profiler->SetUserEnabled(a_enabled);
			return Status::kSuccess;
		}

		Status ClearHistory() const
		{
			if (!IsOwnerThread())
				return Status::kWrongThread;
			if (!globals::profiler)
				return Status::kUnavailable;
			if (globals::profiler->GetBoundedCaptureProgress().state == Profiler::CaptureSessionState::Running)
				return Status::kBusy;
			globals::profiler->ClearTimers();
			return Status::kSuccess;
		}

		Status StartCapture(const CaptureRequest001& a_request, CaptureProgress001& a_output) const
		{
			if (!IsOwnerThread())
				return Status::kWrongThread;
			if (!globals::profiler || !globals::profiler->IsInitialized())
				return Status::kUnavailable;
			if (!globals::profiler->IsUserEnabled())
				return Status::kDisabled;
			if (a_request.frameCount == 0 || a_request.frameCount > Profiler::kHistorySize)
				return Status::kInvalidArgument;
			std::uint64_t captureId = 0;
			if (!globals::profiler->StartBoundedCapture(a_request.frameCount, a_request.clearHistory != 0, captureId))
				return Status::kBusy;
			FillProgress(globals::profiler->GetBoundedCaptureProgress(), a_output);
			return Status::kSuccess;
		}

		Status GetCapture(std::uint64_t a_captureId, CaptureProgress001& a_output) const
		{
			if (!IsOwnerThread())
				return Status::kWrongThread;
			if (!globals::profiler)
				return Status::kUnavailable;
			const auto progress = globals::profiler->GetBoundedCaptureProgress();
			if (progress.sessionId == 0 || progress.sessionId != a_captureId)
				return Status::kCaptureNotFound;
			FillProgress(progress, a_output);
			return Status::kSuccess;
		}

		Status CancelCapture(std::uint64_t a_captureId, CaptureProgress001& a_output) const
		{
			if (!IsOwnerThread())
				return Status::kWrongThread;
			if (!globals::profiler)
				return Status::kUnavailable;
			const auto before = globals::profiler->GetBoundedCaptureProgress();
			if (before.sessionId != a_captureId)
				return Status::kCaptureNotFound;
			if (before.state == Profiler::CaptureSessionState::Running)
				globals::profiler->CancelBoundedCapture(a_captureId);
			FillProgress(globals::profiler->GetBoundedCaptureProgress(), a_output);
			return Status::kSuccess;
		}

	private:
		bool IsOwnerThread() const { return std::this_thread::get_id() == ownerThread; }
		std::thread::id ownerThread;
	};

	ProfilerService* ServiceFrom(const void* a_context)
	{
		return const_cast<ProfilerService*>(static_cast<const ProfilerService*>(a_context));
	}

	Status GetSnapshot(const void* a_context, Snapshot001* a_output)
	{
		if (!a_context || !a_output)
			return Status::kInvalidArgument;
		if (a_output->structSize < sizeof(Snapshot001))
			return Status::kStructureTooSmall;
		return ServiceFrom(a_context)->GetSnapshot(*a_output);
	}

	std::uint32_t GetTimerCount(const void* a_context)
	{
		return a_context ? ServiceFrom(a_context)->GetTimerCount() : 0u;
	}

	Status GetTimerDescriptor(const void* a_context, std::uint32_t a_index, TimerDescriptor001* a_output)
	{
		if (!a_context || !a_output)
			return Status::kInvalidArgument;
		if (a_output->structSize < sizeof(TimerDescriptor001))
			return Status::kStructureTooSmall;
		return ServiceFrom(a_context)->GetTimer(a_index, *a_output);
	}

	Status GetHistorySample(const void* a_context, std::uint32_t a_timerIndex, TimingDomain a_domain, std::uint32_t a_sampleIndex, float* a_output)
	{
		if (!a_context || !a_output)
			return Status::kInvalidArgument;
		return ServiceFrom(a_context)->GetHistory(a_timerIndex, a_domain, a_sampleIndex, *a_output);
	}

	Status SetEnabled(const void* a_context, std::uint32_t a_enabled)
	{
		return a_context ? ServiceFrom(a_context)->SetEnabled(a_enabled != 0) : Status::kInvalidArgument;
	}

	Status ClearHistory(const void* a_context)
	{
		return a_context ? ServiceFrom(a_context)->ClearHistory() : Status::kInvalidArgument;
	}

	Status StartCapture(const void* a_context, const CaptureRequest001* a_request, CaptureProgress001* a_output)
	{
		if (!a_context || !a_request || !a_output)
			return Status::kInvalidArgument;
		if (a_request->structSize < sizeof(CaptureRequest001) || a_output->structSize < sizeof(CaptureProgress001))
			return Status::kStructureTooSmall;
		return ServiceFrom(a_context)->StartCapture(*a_request, *a_output);
	}

	Status GetCaptureProgress(const void* a_context, std::uint64_t a_captureId, CaptureProgress001* a_output)
	{
		if (!a_context || !a_output)
			return Status::kInvalidArgument;
		if (a_output->structSize < sizeof(CaptureProgress001))
			return Status::kStructureTooSmall;
		return ServiceFrom(a_context)->GetCapture(a_captureId, *a_output);
	}

	Status CancelCapture(const void* a_context, std::uint64_t a_captureId, CaptureProgress001* a_output)
	{
		if (!a_context || !a_output)
			return Status::kInvalidArgument;
		if (a_output->structSize < sizeof(CaptureProgress001))
			return Status::kStructureTooSmall;
		return ServiceFrom(a_context)->CancelCapture(a_captureId, *a_output);
	}

	std::uint32_t GetCaptureTimerCount(const void* a_context, std::uint64_t a_captureId)
	{
		return a_context ? ServiceFrom(a_context)->GetCaptureTimerCount(a_captureId) : 0u;
	}

	Status GetCaptureTimerDescriptor(const void* a_context, std::uint64_t a_captureId, std::uint32_t a_index, TimerDescriptor001* a_output)
	{
		if (!a_context || !a_output)
			return Status::kInvalidArgument;
		if (a_output->structSize < sizeof(TimerDescriptor001))
			return Status::kStructureTooSmall;
		return ServiceFrom(a_context)->GetCaptureTimer(a_captureId, a_index, *a_output);
	}

	Status GetCaptureHistorySample(const void* a_context, std::uint64_t a_captureId, std::uint32_t a_timerIndex, TimingDomain a_domain, std::uint32_t a_sampleIndex, float* a_output)
	{
		if (!a_context || !a_output)
			return Status::kInvalidArgument;
		return ServiceFrom(a_context)->GetCaptureHistory(a_captureId, a_timerIndex, a_domain, a_sampleIndex, *a_output);
	}
}

namespace CSX::Api
{
	void InitializeProfilerService()
	{
		static std::once_flag initialized;
		std::call_once(initialized, [] {
			static ProfilerService service;
			static const ProfilerAPI::Interface001 serviceInterface{
				.structSize = sizeof(ProfilerAPI::Interface001),
				.major = ProfilerAPI::ServiceMajor,
				.minor = ProfilerAPI::ServiceMinor,
				.schemaRevision = ProfilerAPI::SchemaRevision,
				.capabilities = kCapabilities,
				.context = &service,
				.GetSnapshot = ::GetSnapshot,
				.GetTimerCount = ::GetTimerCount,
				.GetTimerDescriptor = ::GetTimerDescriptor,
				.GetHistorySample = ::GetHistorySample,
				.SetEnabled = ::SetEnabled,
				.ClearHistory = ::ClearHistory,
				.StartCapture = ::StartCapture,
				.GetCaptureProgress = ::GetCaptureProgress,
				.CancelCapture = ::CancelCapture,
				.GetCaptureTimerCount = ::GetCaptureTimerCount,
				.GetCaptureTimerDescriptor = ::GetCaptureTimerDescriptor,
				.GetCaptureHistorySample = ::GetCaptureHistorySample,
			};
			const auto status = GetProcessServiceRegistry().Register({
				ProfilerAPI::ServiceName,
				ProfilerAPI::ServiceMajor,
				ProfilerAPI::ServiceMinor,
				ProfilerAPI::SchemaRevision,
				ServiceAPI::kCapabilityInspection | ServiceAPI::kCapabilityRuntimeMutation | ServiceAPI::kCapabilityAsynchronousOperations,
				&serviceInterface,
			});
			if (status != ServiceAPI::Status::kSuccess && status != ServiceAPI::Status::kAlreadyRegistered)
				logger::error("Failed to register profiler API service ({})", static_cast<std::uint32_t>(status));
		});
	}

	const ProfilerAPI::Interface001* GetProfilerService001()
	{
		InitializeProfilerService();
		ServiceAPI::ServiceQuery001 query;
		query.name = ProfilerAPI::ServiceName;
		query.major = ProfilerAPI::ServiceMajor;
		query.minimumMinor = 0;
		query.maximumMinor = ProfilerAPI::ServiceMinor;
		const void* result = nullptr;
		if (GetProcessServiceRegistry().Query(query, result, nullptr) != ServiceAPI::Status::kSuccess)
			return nullptr;
		return static_cast<const ProfilerAPI::Interface001*>(result);
	}
}
