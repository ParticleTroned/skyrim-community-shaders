#define NOMINMAX
#include <Windows.h>

#include "Features/Upscaling/FSRTemporalTuningPolicy.h"

#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>

using ffxReturnCode_t = std::int32_t;
constexpr ffxReturnCode_t FFX_API_RETURN_OK = 0;
constexpr ffxReturnCode_t FFX_API_RETURN_ERROR = -1;
constexpr std::uint64_t FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION = 7;
namespace ffx
{
	using Context = void*;
}
struct ffxQueryDescHeader
{
	std::uint64_t type = 0;
};
struct ffxQueryGetProviderVersion
{
	ffxQueryDescHeader header{};
	std::uint64_t versionId = 0;
	const char* versionName = nullptr;
};
struct
{
	ffxReturnCode_t (*Query)(ffx::Context*, ffxQueryDescHeader*) = nullptr;
} ffxModule;
#include "temporal_provider_query_under_test.h"

struct FidelityFX
{
	enum class LifecycleResult
	{
		Ready,
		RuntimeDeviceLost
	};
	struct Snapshot
	{
		FSRTemporalTuningPolicy::Status status = FSRTemporalTuningPolicy::Status::Inactive;
		std::int32_t lastConfigureResult = 0;
	};
	bool runtimeUpscalerSupportCheckKnown = false;
	bool runtimeUpscalerSupportConfirmed = false;
	std::uint64_t runtimeUpscalerProviderMatchedVersionId = 99;
	std::string runtimeUpscalerProviderMatchedVersionName = "previous";
	inline static int contextStorage = 0;
	ffx::Context runtimeUpscalerContexts[2]{ &contextStorage, &contextStorage };
	bool runtimeUpscalerContextIndeterminate[2]{};
	std::mutex temporalTuningMutex;
	Snapshot temporalTuningSnapshot{};
	LifecycleResult runtimeUpscalerQuarantineRetirement = LifecycleResult::Ready;
	unsigned quarantineCalls = 0;
	unsigned failureCalls = 0;
	void QuarantineRuntimeUpscalerForSession(const char*) { ++quarantineCalls; }
	LifecycleResult ResolveRuntimeUpscalerLifecycleFailure(const char*)
	{
		++failureCalls;
		return LifecycleResult::RuntimeDeviceLost;
	}
	static LifecycleResult NormalizeRuntimeQuarantineResult(LifecycleResult result) { return result; }
	LifecycleResult RecordRuntimeProviderResult(bool);
};
#include "temporal_provider_result_under_test.h"

namespace
{
	unsigned queryCalls = 0;
	bool injectFault = false;
	ffxReturnCode_t queryResult = FFX_API_RETURN_OK;
	ffxReturnCode_t Query(ffx::Context*, ffxQueryDescHeader* header)
	{
		++queryCalls;
		if (injectFault)
			RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
		if (header->type != FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION)
			return FFX_API_RETURN_ERROR;
		auto& query = *reinterpret_cast<ffxQueryGetProviderVersion*>(header);
		query.versionId = 123;
		query.versionName = "FSR test";
		return queryResult;
	}
	int failures = 0;
	void Check(bool condition, const char* message)
	{
		if (!condition) {
			std::cerr << message << '\n';
			++failures;
		}
	}
}

int main()
{
	ffxModule.Query = &Query;
	FidelityFX valid;
	Check(valid.RecordRuntimeProviderResult(true) == FidelityFX::LifecycleResult::Ready && queryCalls == 1,
		"valid fresh provider query must admit context setup");
	Check(valid.runtimeUpscalerSupportConfirmed && valid.runtimeUpscalerProviderMatchedVersionId == 123 &&
			  valid.runtimeUpscalerProviderMatchedVersionName == "FSR test",
		"successful query must publish the actual provider");
	queryResult = FFX_API_RETURN_ERROR;
	FidelityFX unavailable;
	Check(unavailable.RecordRuntimeProviderResult(true) == FidelityFX::LifecycleResult::Ready && unavailable.quarantineCalls == 0 &&
			  unavailable.runtimeUpscalerProviderMatchedVersionId == 0 && unavailable.runtimeUpscalerProviderMatchedVersionName.empty(),
		"ordinary query rejection must clear stale identity without quarantining a valid context");
	injectFault = true;
	FidelityFX fault;
	Check(fault.RecordRuntimeProviderResult(true) == FidelityFX::LifecycleResult::RuntimeDeviceLost,
		"query SEH must return the lifecycle failure rather than escaping to dispatch");
	Check(fault.runtimeUpscalerContextIndeterminate[0] && !fault.runtimeUpscalerContextIndeterminate[1] &&
			  fault.quarantineCalls == 1 && fault.failureCalls == 1 && !fault.runtimeUpscalerSupportConfirmed,
		"query fault must quarantine the exact indeterminate context before use");
	Check(fault.temporalTuningSnapshot.status == FSRTemporalTuningPolicy::Status::Faulted &&
			  fault.temporalTuningSnapshot.lastConfigureResult == FFX_API_RETURN_ERROR &&
			  fault.runtimeUpscalerQuarantineRetirement == FidelityFX::LifecycleResult::RuntimeDeviceLost,
		"query fault must preserve diagnostic and retirement evidence");
	const auto before = queryCalls;
	FidelityFX absent;
	Check(absent.RecordRuntimeProviderResult(false) == FidelityFX::LifecycleResult::Ready && queryCalls == before,
		"unsupported creation must not query a provider");
	absent.runtimeUpscalerContexts[0] = nullptr;
	Check(absent.RecordRuntimeProviderResult(true) == FidelityFX::LifecycleResult::Ready && queryCalls == before,
		"absent context must not query a provider");
	return failures ? 1 : 0;
}
