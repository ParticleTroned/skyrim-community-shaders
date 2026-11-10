#include "Features/Upscaling/NeuralRendering/NgxCapabilityInfo.h"
#include "neural_sdk_parameters_under_test.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <string_view>

namespace NeuralRendering
{
	using NVSDK_NGX_Result = std::uint32_t;
	constexpr NVSDK_NGX_Result kSuccess = 1, kUnsupported = 0xBAD00010;
	constexpr bool NVSDK_NGX_FAILED(NVSDK_NGX_Result result) { return (result & 0xFFF00000) == 0xBAD00000; }
	struct NVSDK_NGX_Parameter;
	using PFN_NVSDK_NGX_DLSSNR_GetStatsCallback = NVSDK_NGX_Result (*)(NVSDK_NGX_Parameter*);
	NVSDK_NGX_Result ReadStats(NVSDK_NGX_Parameter*);
	std::uint64_t GetTickCount64() { return 100; }
	enum class RuntimeStatus
	{
		Initialized,
		FeatureCreateFailed
	};
	struct NVSDK_NGX_Parameter
	{
		NVSDK_NGX_Result lookupResult = kSuccess, callbackResult = kSuccess, sizeResult = kSuccess;
		unsigned long long allocated = 0;
		unsigned calls = 0;
		bool hasCallback = true;
		bool throwsLookup = false;
		bool throws = false;
		bool throwsUnknown = false;
		NVSDK_NGX_Result Get(const char* key, void** value)
		{
			if (std::string_view(key) != NVSDK_NGX_Parameter_DLSSNRGetStatsCallback)
				throw std::runtime_error("Unexpected capability query");
			++calls;
			if (throwsLookup)
				throw std::runtime_error("Optional capability lookup failed");
			*value = hasCallback ? reinterpret_cast<void*>(&ReadStats) : nullptr;
			return lookupResult;
		}
		NVSDK_NGX_Result Get(const char* key, unsigned long long* value)
		{
			if (std::string_view(key) != NVSDK_NGX_Parameter_SizeInBytes)
				throw std::runtime_error("Unexpected allocation query");
			++calls;
			*value = allocated;
			return sizeResult;
		}
	};
	NVSDK_NGX_Result ReadStats(NVSDK_NGX_Parameter* parameters)
	{
		++parameters->calls;
		if (parameters->throws)
			throw std::runtime_error("Optional provider statistics failed");
		if (parameters->throwsUnknown)
			throw 1;
		return parameters->callbackResult;
	}
	struct Runtime
	{
		static constexpr auto kFeatureSlotCount = kLogicalFeatureSlotCount;
		struct Backend
		{
			NVSDK_NGX_Parameter* capabilities = nullptr;
			std::array<std::size_t, kFeatureSlotCount> scratchBytes{};
		};
		Backend* backend_ = nullptr;
		void* device_ = nullptr;
		bool abandoned_ = false;
		RuntimeStatus status_ = RuntimeStatus::Initialized;
		std::array<void*, kFeatureSlotCount> featureHandles_{};
		std::atomic_bool abandonRequested_ = false;
		mutable std::recursive_mutex mutex_;
		NgxAllocationStatistics QueryAllocationStatistics() const
		{
			std::scoped_lock lock(mutex_);
			return QueryAllocationStatisticsLocked();
		}
		NgxAllocationStatistics QueryAllocationStatisticsLocked() const;
		NativeMemorySnapshot QueryMemorySnapshot() const;
	};
#include "neural_ngx_statistics_under_test.h"
}

int main()
{
	using namespace NeuralRendering;
	const auto require = [](bool condition) { if (!condition) throw std::runtime_error("NGX aggregate statistics contract failed"); };
	Runtime runtime;
	require(!runtime.QueryAllocationStatistics().callbackQuery.has_value());
	NVSDK_NGX_Parameter parameters;
	Runtime::Backend backend{ &parameters };
	runtime.backend_ = &backend;
	runtime.device_ = &backend;
	const auto zero = runtime.QueryAllocationStatistics();
	require(zero.allocatedBytes == 0 && zero.callbackResult == kSuccess && parameters.calls == 3);
	parameters.allocated = 123456789;
	require(runtime.QueryAllocationStatistics().allocatedBytes == parameters.allocated);
	parameters.lookupResult = kUnsupported;
	const auto unsupported = runtime.QueryAllocationStatistics();
	require(unsupported.callbackQuery == kUnsupported && !unsupported.allocatedBytes && !unsupported.callbackResult);
	parameters.lookupResult = kSuccess;
	parameters.hasCallback = false;
	require(!runtime.QueryAllocationStatistics().callbackResult);
	parameters.hasCallback = true;
	parameters.callbackResult = kUnsupported;
	require(!runtime.QueryAllocationStatistics().allocatedBytes);
	parameters.callbackResult = kSuccess;
	parameters.sizeResult = kUnsupported;
	const auto failedSize = runtime.QueryAllocationStatistics();
	require(failedSize.sizeQuery == kUnsupported && !failedSize.allocatedBytes);
	parameters.sizeResult = kSuccess;
	parameters.throwsLookup = true;
	const auto failedLookup = runtime.QueryAllocationStatistics();
	require(failedLookup.queryThrew && !failedLookup.callbackQuery && !failedLookup.allocatedBytes);
	parameters.throwsLookup = false;
	parameters.throws = true;
	const auto exception = runtime.QueryMemorySnapshot();
	require(exception.valid && exception.provider.queryThrew && !exception.provider.allocatedBytes);
	parameters.throws = false;
	parameters.throwsUnknown = true;
	require(runtime.QueryMemorySnapshot().provider.queryThrew);
	parameters.throwsUnknown = false;
	backend.scratchBytes[2] = 123;
	runtime.featureHandles_[2] = &backend;
	const auto snapshot = runtime.QueryMemorySnapshot();
	require(snapshot.valid && snapshot.provider.allocatedBytes == parameters.allocated && snapshot.residentMask == 4 &&
			snapshot.scratchBytes[2] == 123 && snapshot.sampledAtMs == 100);
	runtime.status_ = RuntimeStatus::FeatureCreateFailed;
	const auto failedCalls = parameters.calls;
	require(!runtime.QueryMemorySnapshot().valid && parameters.calls == failedCalls);
	runtime.status_ = RuntimeStatus::Initialized;
	runtime.abandonRequested_ = true;
	const auto callsBefore = parameters.calls;
	require(!runtime.QueryAllocationStatistics().callbackQuery && parameters.calls == callsBefore);
}
