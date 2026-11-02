#include "Features/Upscaling/NeuralRendering/NgxCapabilityInfo.h"
#include "neural_sdk_parameters_under_test.h"
#include <atomic>
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
	struct NVSDK_NGX_Parameter
	{
		NVSDK_NGX_Result lookupResult = kSuccess, callbackResult = kSuccess, sizeResult = kSuccess;
		unsigned long long allocated = 0;
		unsigned calls = 0;
		bool hasCallback = true;
		NVSDK_NGX_Result Get(const char* key, void** value)
		{
			if (std::string_view(key) != NVSDK_NGX_Parameter_DLSSNRGetStatsCallback)
				throw std::runtime_error("Unexpected capability query");
			++calls;
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
		return parameters->callbackResult;
	}
	struct Runtime
	{
		struct Backend
		{
			NVSDK_NGX_Parameter* capabilities = nullptr;
		};
		Backend* backend_ = nullptr;
		void* device_ = nullptr;
		bool abandoned_ = false;
		std::atomic_bool abandonRequested_ = false;
		mutable std::recursive_mutex mutex_;
		NgxAllocationStatistics QueryAllocationStatistics() const;
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
	runtime.abandonRequested_ = true;
	const auto callsBefore = parameters.calls;
	require(!runtime.QueryAllocationStatistics().callbackQuery && parameters.calls == callsBefore);
}
