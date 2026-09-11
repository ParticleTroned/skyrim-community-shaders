#define NOMINMAX
#include <Windows.h>

#include "Features/Upscaling/FSRTemporalTuningPolicy.h"
#include <FidelityFX/upscalers/include/ffx_upscale.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

namespace logger
{
	template <class... Args>
	void warn(const char*, Args&&...)
	{}
}
namespace globals::features
{
	struct
	{
		unsigned historyResets = 0;
		void RequestHistoryReset() { ++historyResets; }
	} upscaling;
}
ffxFunctions ffxModule{};
#include "temporal_provider_calls_under_test.h"

struct FidelityFX
{
	enum class LifecycleResult
	{
		Ready,
		Pending,
		RuntimeDeviceLost
	};
	enum class RuntimeUpscalerFramePath
	{
		kInactive,
		kHostFsr31,
		kRuntimeFsr31,
		kRuntimeFsr4,
		kHostFsr31Fallback
	};
#include "temporal_snapshot_under_test.h"
	bool runtimeUpscalerSupportCheckKnown = false;
	bool runtimeUpscalerSupportConfirmed = false;
	std::uint64_t runtimeUpscalerProviderMatchedVersionId = 99;
	std::string runtimeUpscalerProviderMatchedVersionName = "previous";
	inline static int contextStorage[2]{};
	ffx::Context runtimeUpscalerContexts[2]{ &contextStorage[0], &contextStorage[1] };
	std::uint32_t runtimeUpscalerContextCount = 2;
	bool runtimeUpscalerContextIndeterminate[2]{};
	mutable std::mutex temporalTuningMutex;
	TemporalTuningSnapshot temporalTuningSnapshot{};
	std::atomic_uint64_t temporalRequestRevision{ 0 };
	std::uint64_t temporalContextRevision = 0;
	std::atomic<RuntimeUpscalerFramePath> temporalLastDispatchPath{ RuntimeUpscalerFramePath::kInactive };
	FSRTemporalTuningPolicy::RejectedRequest temporalRejectedRequest{};
	LifecycleResult runtimeUpscalerQuarantineRetirement = LifecycleResult::Ready;
	LifecycleResult cleanupResult = LifecycleResult::Ready;
	unsigned quarantineCalls = 0;
	unsigned failureCalls = 0;
	unsigned destroyCalls = 0;
	void QuarantineRuntimeUpscalerForSession(const char*) { ++quarantineCalls; }
	LifecycleResult ResolveRuntimeUpscalerLifecycleFailure(const char*)
	{
		++failureCalls;
		return LifecycleResult::RuntimeDeviceLost;
	}
	static LifecycleResult NormalizeRuntimeQuarantineResult(LifecycleResult result) { return result; }
	LifecycleResult DestroyRuntimeUpscalerContexts(bool)
	{
		++destroyCalls;
		return cleanupResult;
	}
	LifecycleResult RecordRuntimeProviderResult(bool);
	bool RequestTemporalTuning(const FSRTemporalTuningPolicy::Settings&);
	TemporalTuningSnapshot GetTemporalTuningSnapshot() const;
	LifecycleResult ConfigureTemporalTuningContexts(const TemporalTuningSnapshot&);
};
#include "temporal_provider_result_under_test.h"

namespace
{
	unsigned queryCalls = 0;
	bool injectFault = false;
	int queryFaultEye = -1;
	ffxReturnCode_t queryResult = FFX_API_RETURN_OK;
	constexpr std::uint64_t fsr411 = (0xF5A5CA1Eull << 32) | FFX_UPSCALER_MAKE_VERSION(4, 1, 1);
	std::array<std::uint64_t, 2> providerIds{ 123, 123 };
	int queryFailureEye = -1;
	unsigned ContextIndex(ffx::Context* context)
	{
		return *context == &FidelityFX::contextStorage[1] ? 1 : 0;
	}
	ffxReturnCode_t Query(ffx::Context* context, ffxQueryDescHeader* header)
	{
		++queryCalls;
		if (injectFault && (queryFaultEye < 0 || static_cast<int>(ContextIndex(context)) == queryFaultEye))
			RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
		if (header->type != FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION)
			return FFX_API_RETURN_ERROR;
		auto& query = *reinterpret_cast<ffxQueryGetProviderVersion*>(header);
		query.versionId = providerIds[ContextIndex(context)];
		query.versionName = "FSR test";
		return static_cast<int>(ContextIndex(context)) == queryFailureEye ? FFX_API_RETURN_ERROR : queryResult;
	}
	int failures = 0;
	unsigned checks = 0;
	void Check(bool condition, const char* message)
	{
		++checks;
		if (!condition) {
			std::cerr << message << '\n';
			++failures;
		}
	}
	struct ConfiguredValue
	{
		unsigned eye;
		std::uint64_t key;
		float value;
	};
	std::vector<ConfiguredValue> configuredValues;
	int configureFailure = -1;
	bool configureFault = false;
	std::function<void()> duringConfigure;
	ffxReturnCode_t Configure(ffx::Context* context, const ffxConfigureDescHeader* header)
	{
		Check(header->type == FFX_API_CONFIGURE_DESC_TYPE_UPSCALE_KEYVALUE, "configure must use the public descriptor type");
		const auto& descriptor = *reinterpret_cast<const ffxConfigureDescUpscaleKeyValue*>(header);
		configuredValues.push_back({ ContextIndex(context), descriptor.key, *static_cast<const float*>(descriptor.ptr) });
		if (duringConfigure)
			duringConfigure();
		if (static_cast<int>(configuredValues.size()) - 1 == configureFailure) {
			if (configureFault)
				RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
			return FFX_API_RETURN_ERROR_PARAMETER;
		}
		return FFX_API_RETURN_OK;
	}
	void ResetProvider(std::uint64_t providerId = fsr411)
	{
		ffxModule.Query = &Query;
		ffxModule.Configure = &Configure;
		providerIds.fill(providerId);
		queryCalls = 0;
		queryResult = FFX_API_RETURN_OK;
		queryFailureEye = -1;
		injectFault = false;
		queryFaultEye = -1;
		configuredValues.clear();
		configureFailure = -1;
		configureFault = false;
		duringConfigure = {};
		globals::features::upscaling.historyResets = 0;
	}
	FSRTemporalTuningPolicy::Settings EnabledProfile()
	{
		return { true, 0.5f, 2.0f, 3.0f, 0.2f, -0.1f };
	}
	FidelityFX::LifecycleResult Apply(FidelityFX& fidelity, const FSRTemporalTuningPolicy::Settings& profile)
	{
		Check(fidelity.RequestTemporalTuning(profile), "valid settings must be accepted");
		return fidelity.ConfigureTemporalTuningContexts(fidelity.GetTemporalTuningSnapshot());
	}
}

void TestInitialProviderQuery()
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
}

void TestCompleteProfiles()
{
	using namespace FSRTemporalTuningPolicy;
	const auto profile = EnabledProfile();
	for (const auto provider : { fsr411, (0xF5A5CA1Eull << 32) | FFX_UPSCALER_MAKE_VERSION(3, 1, 5), 0x123456789ABCDEFull }) {
		for (unsigned contexts : { 1u, 2u }) {
			ResetProvider(provider);
			FidelityFX fidelity;
			fidelity.runtimeUpscalerContextCount = contexts;
			Check(Apply(fidelity, profile) == FidelityFX::LifecycleResult::Ready, "provider version must not gate complete profile application");
			const auto snapshot = fidelity.GetTemporalTuningSnapshot();
			Check(snapshot.status == Status::Applied && snapshot.contextSettings == profile &&
					  snapshot.providerId == provider && snapshot.configuredContexts == contexts,
				"applied status must describe the actual provider and complete context set");
			Check(configuredValues.size() == contexts * 5 && queryCalls == contexts && globals::features::upscaling.historyResets == 1,
				"every eye must be identified and configured before requesting history reset");
			for (size_t i = 0; i < configuredValues.size(); ++i) {
				const auto& value = configuredValues[i];
				Check(value.eye == i / 5 && value.key == i % 5 && value.value == Values(profile)[i % 5],
					"each public configure key must receive its matching value on both eyes");
			}
			fidelity.temporalLastDispatchPath = FidelityFX::RuntimeUpscalerFramePath::kRuntimeFsr4;
			Check(fidelity.GetTemporalTuningSnapshot().contextSettings.enabled, "FSR4 dispatch must retain applied tuning status");
			fidelity.temporalLastDispatchPath = FidelityFX::RuntimeUpscalerFramePath::kHostFsr31Fallback;
			const auto host = fidelity.GetTemporalTuningSnapshot();
			Check(!host.contextSettings.enabled && host.configuredContexts == 0 && host.providerId == 0,
				"host fallback must not claim dormant runtime overrides are applied");
		}
	}
	ResetProvider();
	FidelityFX disabled;
	Check(Apply(disabled, {}) == FidelityFX::LifecycleResult::Ready && configuredValues.empty() && queryCalls == 0 &&
			  disabled.GetTemporalTuningSnapshot().status == Status::VendorDefaults,
		"disabled profiles must leave fresh provider defaults untouched");
	Check(Apply(disabled, profile) == FidelityFX::LifecycleResult::Ready, "enabling FSR4 tuning must apply the profile");
	configuredValues.clear();
	Check(Apply(disabled, {}) == FidelityFX::LifecycleResult::Ready && configuredValues.empty() &&
			  !disabled.GetTemporalTuningSnapshot().contextSettings.enabled && disabled.GetTemporalTuningSnapshot().configuredContexts == 0,
		"disabling must configure no overrides on the replacement contexts");
}

void TestRejectedProfiles()
{
	using namespace FSRTemporalTuningPolicy;
	const auto profile = EnabledProfile();
	for (int failure = 0; failure < 10; ++failure) {
		for (bool fault : { false, true }) {
			ResetProvider();
			configureFailure = failure;
			configureFault = fault;
			FidelityFX fidelity;
			const auto result = Apply(fidelity, profile);
			const auto snapshot = fidelity.GetTemporalTuningSnapshot();
			Check(configuredValues.size() == static_cast<size_t>(failure + 1) && !snapshot.contextSettings.enabled && snapshot.configuredContexts == 0,
				"any rejected eye/key must stop configuration without publishing a partial profile");
			Check(globals::features::upscaling.historyResets == 0 && snapshot.providerId == fsr411,
				"failed FSR4 configuration must retain its provider identity without applying history state");
			if (fault) {
				Check(result == FidelityFX::LifecycleResult::RuntimeDeviceLost && snapshot.status == Status::Faulted &&
						  snapshot.lastConfigureResult == FFX_API_RETURN_ERROR && fidelity.quarantineCalls == 1 && fidelity.destroyCalls == 0 &&
						  fidelity.runtimeUpscalerContextIndeterminate[failure / 5] && !fidelity.runtimeUpscalerContextIndeterminate[1 - failure / 5],
					"configure faults must quarantine the exact eye and retain indeterminate ownership");
			} else {
				Check(result == FidelityFX::LifecycleResult::Pending && snapshot.status == Status::Rejected &&
						  snapshot.lastConfigureResult == FFX_API_RETURN_ERROR_PARAMETER && fidelity.destroyCalls == 1,
					"ordinary rejection must discard the fresh context set before retrying with defaults");
				configuredValues.clear();
				Check(Apply(fidelity, profile) == FidelityFX::LifecycleResult::Ready && configuredValues.empty() && fidelity.destroyCalls == 1 &&
						  fidelity.GetTemporalTuningSnapshot().lastConfigureResult == FFX_API_RETURN_ERROR_PARAMETER,
					"replacement contexts must retain untouched defaults for the same rejected request");
				auto changed = profile;
				changed.velocityFactor = 0.25f;
				Check(fidelity.RequestTemporalTuning(changed) && fidelity.RequestTemporalTuning(profile), "edits away and back must be accepted");
				configureFailure = -1;
				Check(Apply(fidelity, profile) == FidelityFX::LifecycleResult::Ready && configuredValues.size() == 10,
					"a new request revision must permit FSR4 configuration again");
			}
		}
	}
	ResetProvider();
	configureFailure = 0;
	FidelityFX cleanup;
	cleanup.cleanupResult = FidelityFX::LifecycleResult::RuntimeDeviceLost;
	Check(Apply(cleanup, profile) == FidelityFX::LifecycleResult::RuntimeDeviceLost,
		"failed cleanup must propagate instead of admitting partially configured contexts");
}

void TestIdentityAndRequestChanges()
{
	using namespace FSRTemporalTuningPolicy;
	for (bool mismatch : { false, true }) {
		ResetProvider();
		providerIds[1] = mismatch ? FFX_UPSCALER_MAKE_VERSION(3, 1, 5) : 0;
		FidelityFX fidelity;
		Check(Apply(fidelity, EnabledProfile()) == FidelityFX::LifecycleResult::Ready && configuredValues.empty() &&
				  fidelity.GetTemporalTuningSnapshot().status == Status::UnsupportedProvider,
			"unidentified or mixed-eye providers must not receive any overrides");
	}
	for (bool fault : { false, true }) {
		ResetProvider();
		queryFailureEye = 1;
		injectFault = fault;
		queryFaultEye = 1;
		FidelityFX fidelity;
		const auto result = Apply(fidelity, EnabledProfile());
		Check(configuredValues.empty() && queryCalls == 2 && (fault ? result == FidelityFX::LifecycleResult::RuntimeDeviceLost : result == FidelityFX::LifecycleResult::Ready),
			"identity query failures must prevent configuration and contain provider faults");
		if (fault)
			Check(fidelity.runtimeUpscalerContextIndeterminate[1] && !fidelity.runtimeUpscalerContextIndeterminate[0],
				"a second-eye query fault must quarantine that exact context");
	}
	ResetProvider();
	FidelityFX changed;
	const auto original = EnabledProfile();
	auto next = original;
	next.shadingChangeScale = 1.5f;
	duringConfigure = [&] {
		if (configuredValues.size() == 1)
			Check(changed.RequestTemporalTuning(next), "mid-configuration request must be accepted");
	};
	Check(Apply(changed, original) == FidelityFX::LifecycleResult::Ready, "current profile must finish both eyes");
	const auto snapshot = changed.GetTemporalTuningSnapshot();
	Check(snapshot.status == Status::Pending && snapshot.requested == next && snapshot.contextSettings == original && snapshot.configuredContexts == 2 &&
			  changed.temporalContextRevision != changed.temporalRequestRevision.load(),
		"pending edits must preserve accurate applied and requested profile evidence");
	Check(configuredValues.size() == 10 && configuredValues[2].value == original.shadingChangeScale && configuredValues[7].value == original.shadingChangeScale,
		"both eyes must use the same captured profile despite a concurrent edit");
	ResetProvider();
}

int main()
{
	TestInitialProviderQuery();
	TestCompleteProfiles();
	TestRejectedProfiles();
	TestIdentityAndRequestChanges();
	std::cout << checks << " checks; " << failures << " failures\n";
	return failures ? 1 : 0;
}
