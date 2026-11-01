#include "Api/UpscalingContract.h"
#include "Api/UpscalingServicePolicy.h"
#include <cassert>
#include <cstdint>

int main()
{
	using namespace CSX::UpscalingAPI;
	using namespace CSX::Api;
	for (unsigned method = 0; method <= 3; ++method) {
		for (unsigned quality = 0; quality <= 6; ++quality) {
			Profile001 profile;
			profile.method = static_cast<Method>(method); profile.qualityMode = static_cast<QualityMode>(quality);
			assert(ValidateUpscalingProfile(profile) == Status::kSuccess);
			profile.renderScaleMode = 1;
			assert(ValidateUpscalingProfile(profile) == Status::kUnsupportedRuntime);
		}
	}
	Profile001 malformed; malformed.qualityMode = static_cast<QualityMode>(UINT32_MAX);
	assert(ValidateUpscalingProfile(malformed) == Status::kUnsupportedProfile);
	malformed = {}; malformed.structSize = sizeof(malformed) - 1;
	assert(ValidateUpscalingProfile(malformed) == Status::kStructureTooSmall);
	ApplyRequest001 request; request.clientId = "client"; request.commandId = "command";
	assert(ValidateUpscalingApplyRequest(request) == Status::kSuccess);
	request.commandId = "";
	assert(ValidateUpscalingApplyRequest(request) == Status::kInvalidArgument);
	const auto loading = ResolveUpscalingAdmission(kConditionLoadingTransition, RequestPurpose::kEnvironmentProfileTransition, PersistencePolicy::kRuntimeOnly, false);
	assert(loading.blockingConditions == kConditionLoadingTransition);
	const auto persist = ResolveUpscalingAdmission(0, RequestPurpose::kDirect, PersistencePolicy::kPersistWhenStable, false);
	assert(persist.blockingConditions == kConditionPersistenceUnavailable);
	const auto fallback = ResolveUpscalingAdmission(0, RequestPurpose::kDirect, PersistencePolicy::kRuntimeOnly, false, kConditionProviderUnavailable);
	assert(fallback.blockingConditions == 0 && fallback.observedConditions == kConditionProviderUnavailable);
	assert(HasUpscalingServiceCapacity(1, 1, 1, 3));
	assert(!HasUpscalingServiceCapacity(3, 1, 1, 3));
	assert(!HasUpscalingServiceCapacity(1, 3, 1, 3));
	assert(IsTerminalUpscalingOperation(OperationState::kCompleted) && !IsTerminalUpscalingOperation(OperationState::kPreparing));
}
