#include "Api/ShaderServicePolicy.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>

using CSX::Api::EvaluateShaderMutation;
using CSX::Api::ShaderPolicyRequest;
using CSX::Api::ShaderPolicyState;
using CSX::ShaderAPI::MutationAction;
using CSX::ShaderAPI::Status;

static_assert(std::is_standard_layout_v<CSX::ShaderAPI::Snapshot001>);
static_assert(std::is_standard_layout_v<CSX::ShaderAPI::FeatureDescriptor001>);
static_assert(std::is_standard_layout_v<CSX::ShaderAPI::MutationRequest001>);
static_assert(std::is_standard_layout_v<CSX::ShaderAPI::Preflight001>);
static_assert(std::is_standard_layout_v<CSX::ShaderAPI::MutationReceipt001>);
static_assert(std::is_standard_layout_v<CSX::ShaderAPI::Interface001>);

namespace
{
	void Check(bool a_condition, std::string_view a_message)
	{
		if (!a_condition)
			throw std::runtime_error(std::string(a_message));
	}

	ShaderPolicyRequest Request(MutationAction a_action, std::uint64_t a_flags = 0)
	{
		ShaderPolicyRequest request;
		request.action = a_action;
		request.expectedStateRevision = 7;
		request.flags = a_flags;
		return request;
	}
}

int RunTest()
{
	ShaderPolicyState state;
	state.available = true;
	state.stateRevision = 7;

	auto unavailable = state;
	unavailable.available = false;
	Check(EvaluateShaderMutation(unavailable, Request(MutationAction::kSetDiskCache)).status == Status::kUnavailable,
		"unavailable service accepted a mutation");

	Check(EvaluateShaderMutation(state, Request(MutationAction::kSetDiskCache, 1ull << 63)).reasonCode ==
			"unknown_mutation_flags",
		"unknown mutation flag bits were silently accepted");
	Check(EvaluateShaderMutation(state, Request(
		MutationAction::kClearMemoryCache,
		CSX::ShaderAPI::kMutationPersist | CSX::ShaderAPI::kMutationAllowDisruptive)).reasonCode ==
			"persistence_not_applicable",
		"persistence was silently accepted for a non-persistent action");
	auto irrelevantFeatureName = Request(MutationAction::kSetDiskCache);
	irrelevantFeatureName.featureName = "GrassLighting";
	Check(EvaluateShaderMutation(state, irrelevantFeatureName).reasonCode == "feature_name_not_applicable",
		"irrelevant featureName was silently accepted");

	auto stale = Request(MutationAction::kSetDiskCache);
	stale.expectedStateRevision = 6;
	Check(EvaluateShaderMutation(state, stale).status == Status::kRevisionConflict,
		"stale revision was accepted");

	const auto ordinary = EvaluateShaderMutation(state, Request(MutationAction::kSetDiskCache));
	Check(ordinary.allowed && ordinary.requiredFlags == 0, "ordinary runtime setting unexpectedly requires consent");

	auto persistenceBlocked = state;
	persistenceBlocked.persistentMutationBlocked = true;
	Check(EvaluateShaderMutation(
		persistenceBlocked,
		Request(MutationAction::kSetDiskCache, CSX::ShaderAPI::kMutationPersist)).reasonCode ==
			"persistent_mutation_blocked",
		"persistent setting was accepted during the save/load mutation-safety window");
	Check(EvaluateShaderMutation(
		persistenceBlocked,
		Request(MutationAction::kSetDiskCache)).allowed,
		"live-only setting was incorrectly blocked by the persistence safety window");

	const auto noDisruptiveConsent = EvaluateShaderMutation(state, Request(MutationAction::kClearMemoryCache));
	Check(!noDisruptiveConsent.allowed && noDisruptiveConsent.reasonCode == "disruptive_consent_required",
		"memory clear did not require disruptive consent");

	const auto disruptive = EvaluateShaderMutation(
		state, Request(MutationAction::kClearMemoryCache, CSX::ShaderAPI::kMutationAllowDisruptive));
	Check(disruptive.allowed && disruptive.shaderRecompileExpected, "consented memory clear was blocked");

	const auto destructiveMissing = EvaluateShaderMutation(
		state, Request(MutationAction::kClearAllCaches, CSX::ShaderAPI::kMutationAllowDisruptive));
	Check(!destructiveMissing.allowed && destructiveMissing.destructive,
		"full clear did not require destructive consent");

	const auto destructive = EvaluateShaderMutation(
		state,
		Request(MutationAction::kClearAllCaches,
			CSX::ShaderAPI::kMutationAllowDisruptive | CSX::ShaderAPI::kMutationAllowDestructive));
	Check(destructive.allowed && destructive.destructive, "fully consented cache clear was blocked");

	auto featureRequest = Request(MutationAction::kSetFeatureDisabledAtBoot, CSX::ShaderAPI::kMutationPersist);
	featureRequest.featureName = "GrassLighting";
	Check(EvaluateShaderMutation(state, featureRequest).status == Status::kFeatureNotFound,
		"unknown feature was accepted");
	state.featureFound = true;
	const auto feature = EvaluateShaderMutation(state, featureRequest);
	Check(feature.allowed && feature.restartRequired && feature.shaderRecompileExpected,
		"known feature boot mutation has the wrong lifecycle contract");

	auto restoreState = state;
	restoreState.previousCacheAvailable = false;
	const auto noPrevious = EvaluateShaderMutation(
		restoreState,
		Request(MutationAction::kRestorePreviousDiskCache,
			CSX::ShaderAPI::kMutationAllowDisruptive | CSX::ShaderAPI::kMutationAllowDestructive));
	Check(noPrevious.status == Status::kBlocked && noPrevious.reasonCode == "previous_cache_unavailable",
		"restore accepted a missing previous cache");
	restoreState.previousCacheAvailable = true;
	restoreState.persistentMutationBlocked = true;
	Check(EvaluateShaderMutation(
		restoreState,
		Request(MutationAction::kRestorePreviousDiskCache,
			CSX::ShaderAPI::kMutationAllowDisruptive | CSX::ShaderAPI::kMutationAllowDestructive)).reasonCode ==
			"persistent_mutation_blocked",
		"rollback restore was accepted during the save/load mutation-safety window");

	restoreState.persistentMutationBlocked = false;
	restoreState.compiling = true;
	Check(EvaluateShaderMutation(
		restoreState,
		Request(MutationAction::kRestorePreviousDiskCache,
			CSX::ShaderAPI::kMutationAllowDisruptive | CSX::ShaderAPI::kMutationAllowDestructive)).status == Status::kBusy,
		"restore accepted an active compilation");

	auto held = state;
	held.diskCacheHeld = false;
	Check(EvaluateShaderMutation(
		held,
		Request(MutationAction::kAcceptCacheRebuild, CSX::ShaderAPI::kMutationAllowDisruptive)).reasonCode == "cache_not_held",
		"cache rebuild accepted a cache that was not held");

	return 0;
}

int main()
{
	try {
		return RunTest();
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
