#include "Features/Upscaling/FSRRuntimeLifecyclePolicy.h"

namespace
{
	using namespace FSRRuntimeLifecyclePolicy;

	static_assert(!HasRetirementRelevantState({}));
	static_assert(!HasRetirementRelevantState({
		.interopFencePresent = true,
		.commandInfrastructurePresent = true,
	}));
	static_assert(HasRetirementRelevantState({ .providerContext = true }));
	static_assert(HasRetirementRelevantState({ .sharedResource = true }));
	static_assert(HasRetirementRelevantState({ .commandWorkInFlight = true }));
	static_assert(HasRetirementRelevantState({ .teardownFencePending = true }));
	static_assert(
		ResolveDispatchFenceAction(false) == DispatchFenceAction::Proceed);
	static_assert(
		ResolveDispatchFenceAction(true) ==
		DispatchFenceAction::PollPendingFence);
	static_assert(
		ResolveIdleProofAction(true, false) == IdleProofAction::ReuseProof);
	static_assert(
		ResolveIdleProofAction(false, false) == IdleProofAction::PollForIdle);
	static_assert(
		ResolveIdleProofAction(true, true) == IdleProofAction::PollForIdle);

	constexpr bool CoversRetirementAdmission()
	{
		for (unsigned bits = 0; bits < 64; ++bits) {
			const RetirementState state{
				.providerContext = (bits & 1u) != 0,
				.sharedResource = (bits & 2u) != 0,
				.commandWorkInFlight = (bits & 4u) != 0,
				.teardownFencePending = (bits & 8u) != 0,
				.interopFencePresent = (bits & 16u) != 0,
				.commandInfrastructurePresent = (bits & 32u) != 0,
			};
			const bool expected = (bits & 15u) != 0;
			if (HasRetirementRelevantState(state) != expected)
				return false;
		}
		return true;
	}

	static_assert(CoversRetirementAdmission());

	constexpr ResourceCompatibilityState CompatibleRuntimeResources()
	{
		return {
			.devicesReady = true,
			.interopFencesReady = true,
			.commandContextsReady = true,
			.providerVersionMatches = true,
			.contextsCompatible = true,
			.sharedResourcesComplete = true,
		};
	}

	static_assert(HasStructurallyCompatibleRuntimeResources(
		CompatibleRuntimeResources()));

	constexpr bool TransientFallbackRetainsRuntimeRecoveryPath()
	{
		auto state = CompatibleRuntimeResources();
		state.transientHostFallback = true;
		return HasStructurallyCompatibleRuntimeResources(state);
	}

	static_assert(TransientFallbackRetainsRuntimeRecoveryPath());

	constexpr bool StructuralBlockersFailClosed()
	{
		for (unsigned blocker = 0; blocker < 10; ++blocker) {
			auto state = CompatibleRuntimeResources();
			switch (blocker) {
			case 0:
				state.terminalFailure = true;
				break;
			case 1:
				state.sessionQuarantined = true;
				break;
			case 2:
				state.ownershipDetached = true;
				break;
			case 3:
				state.teardownFencePending = true;
				break;
			case 4:
				state.devicesReady = false;
				break;
			case 5:
				state.interopFencesReady = false;
				break;
			case 6:
				state.commandContextsReady = false;
				break;
			case 7:
				state.providerVersionMatches = false;
				break;
			case 8:
				state.contextsCompatible = false;
				break;
			case 9:
				state.sharedResourcesComplete = false;
				break;
			}
			if (HasStructurallyCompatibleRuntimeResources(state))
				return false;
		}
		return true;
	}

	static_assert(StructuralBlockersFailClosed());
}

int main() {}
