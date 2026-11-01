#pragma once

#include <cstdint>

namespace CSX::ProfilerAPI
{
	inline constexpr char ServiceName[] = "csx.profiler";
	inline constexpr std::uint32_t ServiceMajor = 1;
	inline constexpr std::uint32_t ServiceMinor = 1;
	inline constexpr std::uint32_t SchemaRevision = 2;
	inline constexpr std::uint32_t SourceServiceMinor = 2;
	inline constexpr std::uint32_t SourceSchemaRevision = 3;

	enum class Status : std::uint32_t
	{
		kSuccess = 0,
		kInvalidArgument = 1,
		kStructureTooSmall = 2,
		kUnavailable = 3,
		kWrongThread = 4,
		kDisabled = 5,
		kBusy = 6,
		kCaptureNotFound = 7,
		kTimerNotFound = 8,
		kInternalError = 9
	};

	enum ServiceCapability : std::uint64_t
	{
		kCapabilitySnapshot = 1ull << 0,
		kCapabilityTimerCatalog = 1ull << 1,
		kCapabilityHistory = 1ull << 2,
		kCapabilityBoundedCapture = 1ull << 3,
		kCapabilityRuntimeControl = 1ull << 4,
		kCapabilityHistoryReset = 1ull << 5,
		/** Timer values, statistics, and histories exclude profiled descendants. */
		kCapabilitySelfTime = 1ull << 6,
		/** Independent CPU publication and source-selective capture requests. */
		kCapabilityIndependentCpu = 1ull << 7
	};

	inline constexpr std::uint64_t ServiceCapabilities =
		kCapabilitySnapshot |
		kCapabilityTimerCatalog |
		kCapabilityHistory |
		kCapabilityBoundedCapture |
		kCapabilityRuntimeControl |
		kCapabilityHistoryReset |
		kCapabilitySelfTime;

	enum class TimingDomain : std::uint32_t
	{
		kGpu = 1,
		kCpu = 2
	};

	enum class CaptureState : std::uint32_t
	{
		kNone = 0,
		kRunning = 1,
		kCompleted = 2,
		kCancelled = 3
	};

	struct Snapshot001
	{
		std::uint32_t structSize = sizeof(Snapshot001);
		std::uint32_t available = 0;
		std::uint32_t enabled = 0;
		std::uint32_t capturing = 0;
		std::uint32_t timerCount = 0;
		std::uint32_t historyCapacity = 0;
		std::uint32_t maximumTimers = 0;
		std::uint32_t frameLatency = 0;
		std::uint32_t capturedFrameCount = 0;
		std::uint32_t acquiredSlots = 0;
		std::uint32_t peakAcquiredSlots = 0;
		std::uint32_t slotRefusals = 0;
		float gpuTotalMs = 0.0f;
		float cpuTotalMs = 0.0f;
		float resolvedGpuTotalMs = 0.0f;
		float resolvedCpuTotalMs = 0.0f;
		std::uint64_t capabilities = 0;
		const char* buildId = nullptr;
	};

	struct TimerDescriptor001
	{
		std::uint32_t structSize = sizeof(TimerDescriptor001);
		const char* name = nullptr;
		std::uint32_t hasGpu = 0;
		std::uint32_t hasCpu = 0;
		std::uint32_t activeGpu = 0;
		std::uint32_t activeCpu = 0;
		std::uint32_t gpuHistoryCount = 0;
		std::uint32_t cpuHistoryCount = 0;
		/** GPU self time; averages, percentiles, and histories use the same basis. */
		float gpuMs = 0.0f;
		/** Inclusive depth-zero contribution to the resolved GPU total. */
		float gpuTopLevelMs = 0.0f;
		float gpuAverageMs = 0.0f;
		float gpuP95Ms = 0.0f;
		float gpuP99Ms = 0.0f;
		/** CPU self time across both CPU-only and GPU-backed scopes. */
		float cpuMs = 0.0f;
		float cpuAverageMs = 0.0f;
		float cpuP95Ms = 0.0f;
		float cpuP99Ms = 0.0f;
	};

	struct CaptureRequest001
	{
		std::uint32_t structSize = sizeof(CaptureRequest001);
		std::uint32_t frameCount = 1;
		std::uint32_t clearHistory = 0;
	};

	struct CaptureProgress001
	{
		std::uint32_t structSize = sizeof(CaptureProgress001);
		std::uint64_t captureId = 0;
		CaptureState state = CaptureState::kNone;
		std::uint32_t requestedFrames = 0;
		std::uint32_t submittedFrames = 0;
		std::uint32_t resolvedFrames = 0;
	};

	struct Interface001
	{
		std::uint32_t structSize = sizeof(Interface001);
		std::uint32_t major = ServiceMajor;
		std::uint32_t minor = ServiceMinor;
		std::uint32_t schemaRevision = SchemaRevision;
		std::uint64_t capabilities = 0;
		const void* context = nullptr;

		Status (*GetSnapshot)(const void* context, Snapshot001* output) = nullptr;
		std::uint32_t (*GetTimerCount)(const void* context) = nullptr;
		Status (*GetTimerDescriptor)(const void* context, std::uint32_t index, TimerDescriptor001* output) = nullptr;
		Status (*GetHistorySample)(const void* context, std::uint32_t timerIndex, TimingDomain domain, std::uint32_t sampleIndex, float* outputMs) = nullptr;
		Status (*SetEnabled)(const void* context, std::uint32_t enabled) = nullptr;
		Status (*ClearHistory)(const void* context) = nullptr;
		Status (*StartCapture)(const void* context, const CaptureRequest001* request, CaptureProgress001* output) = nullptr;
		Status (*GetCaptureProgress)(const void* context, std::uint64_t captureId, CaptureProgress001* output) = nullptr;
		Status (*CancelCapture)(const void* context, std::uint64_t captureId, CaptureProgress001* output) = nullptr;
		std::uint32_t (*GetCaptureTimerCount)(const void* context, std::uint64_t captureId) = nullptr;
		Status (*GetCaptureTimerDescriptor)(const void* context, std::uint64_t captureId, std::uint32_t index, TimerDescriptor001* output) = nullptr;
		Status (*GetCaptureHistorySample)(const void* context, std::uint64_t captureId, std::uint32_t timerIndex, TimingDomain domain, std::uint32_t sampleIndex, float* outputMs) = nullptr;
	};

	enum class CaptureMode : std::uint32_t
	{
		kGpu = 1,
		kCpu = 2,
		kBoth = 3
	};

	/** CPU-only view whose frame and totals are independent of GPU resolution. */
	struct CpuSnapshot001
	{
		std::uint32_t structSize = sizeof(CpuSnapshot001);
		std::uint32_t available = 0;
		std::uint32_t enabled = 0;
		std::uint32_t capturing = 0;
		std::uint32_t timerCount = 0;
		std::uint32_t capturedFrameCount = 0;
		std::uint64_t publicationCount = 0;
		std::uint32_t historyCapacity = 0;
		std::uint32_t maximumTimersPerFrame = 0;
		std::uint32_t slotRefusals = 0;
		float resolvedTotalMs = 0.0f;
		const char* buildId = nullptr;
	};

	/**
	 * Additive 1.2 interface. The first member preserves the complete 1.1 ABI.
	 * Query minor 2 and check the returned size before using the extension.
	 */
	struct Interface002
	{
		Interface001 paired;
		/** Requests one future frame; requests combine and bounded sessions require both sources. */
		Status (*RequestCapture)(const void* context, CaptureMode mode) = nullptr;
		Status (*GetCpuSnapshot)(const void* context, CpuSnapshot001* output) = nullptr;
		std::uint32_t (*GetCpuTimerCount)(const void* context) = nullptr;
		/** CPU catalog indices are independent of the paired and bounded catalogs. */
		Status (*GetCpuTimerDescriptor)(const void* context, std::uint32_t index, TimerDescriptor001* output) = nullptr;
		Status (*GetCpuHistorySample)(const void* context, std::uint32_t timerIndex, std::uint32_t sampleIndex, float* outputMs) = nullptr;
	};
}
