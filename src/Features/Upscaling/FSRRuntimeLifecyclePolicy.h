#pragma once

namespace FSRRuntimeLifecyclePolicy
{
	enum class DispatchFenceAction
	{
		Proceed,
		PollPendingFence
	};

	enum class IdleProofAction
	{
		ReuseProof,
		PollForIdle
	};

	struct RetirementState
	{
		bool providerContext = false;
		bool sharedResource = false;
		bool commandWorkInFlight = false;
		bool teardownFencePending = false;
		bool interopFencePresent = false;
		bool commandInfrastructurePresent = false;
	};

	struct ResourceCompatibilityState
	{
		bool terminalFailure = false;
		bool sessionQuarantined = false;
		bool ownershipDetached = false;
		bool teardownFencePending = false;
		bool devicesReady = false;
		bool interopFencesReady = false;
		bool commandContextsReady = false;
		bool providerVersionMatches = false;
		bool contextsCompatible = false;
		bool sharedResourcesComplete = false;
		bool transientHostFallback = false;
	};

	[[nodiscard]] constexpr bool HasRetirementRelevantState(
		const RetirementState& a_state) noexcept
	{
		// Idle interop infrastructure is reusable and carries no provider
		// ownership. Only submitted work or provider-owned state needs a drain.
		return a_state.providerContext ||
		       a_state.sharedResource ||
		       a_state.commandWorkInFlight ||
		       a_state.teardownFencePending;
	}

	[[nodiscard]] constexpr DispatchFenceAction ResolveDispatchFenceAction(
		bool a_teardownFencePending) noexcept
	{
		// A pending fence must keep dispatch fail-closed, but admission also owns
		// the non-blocking poll that lets the fence eventually retire.
		return a_teardownFencePending ?
		           DispatchFenceAction::PollPendingFence :
		           DispatchFenceAction::Proceed;
	}

	[[nodiscard]] constexpr IdleProofAction ResolveIdleProofAction(
		bool a_idleProofValid,
		bool a_teardownFencePending) noexcept
	{
		// A completed proof remains valid until runtime-provider GPU work is
		// submitted. Never reuse it while either side of the interop fence is pending.
		return a_idleProofValid && !a_teardownFencePending ?
		           IdleProofAction::ReuseProof :
		           IdleProofAction::PollForIdle;
	}

	[[nodiscard]] constexpr bool HasStructurallyCompatibleRuntimeResources(
		const ResourceCompatibilityState& a_state) noexcept
	{
		// A transient host fallback is a recovery phase, not a loss of provider
		// ownership. The compatible runtime must remain eligible to dispatch so a
		// successful frame can retire the fallback without a circular wait.
		return !a_state.terminalFailure &&
		       !a_state.sessionQuarantined &&
		       !a_state.ownershipDetached &&
		       !a_state.teardownFencePending &&
		       a_state.devicesReady &&
		       a_state.interopFencesReady &&
		       a_state.commandContextsReady &&
		       a_state.providerVersionMatches &&
		       a_state.contextsCompatible &&
		       a_state.sharedResourcesComplete;
	}
}
