#pragma once

namespace FSRRuntimeLifecyclePolicy
{
	enum class ProviderRoute
	{
		Host,
		Runtime,
		Unavailable
	};

	struct ProviderRouteInputs
	{
		bool hostSupported = false;
		bool runtimePresent = false;
		bool amdAdapter = false;
	};

	/** Selects FSR3 by capability while keeping the AMD runtime preference. */
	[[nodiscard]] constexpr ProviderRoute SelectProviderRoute(
		const ProviderRouteInputs& a_inputs) noexcept
	{
		if (a_inputs.runtimePresent &&
			(a_inputs.amdAdapter || !a_inputs.hostSupported)) {
			return ProviderRoute::Runtime;
		}

		if (a_inputs.hostSupported)
			return ProviderRoute::Host;

		return ProviderRoute::Unavailable;
	}

	enum class DispatchFenceAction
	{
		Proceed,
		PollPendingFence
	};

	enum class DispatchFencePollResult
	{
		Ready,
		Pending,
		Failed
	};

	enum class DispatchAdmission
	{
		Proceed,
		Defer,
		Fail
	};

	enum class PendingDispatchResolution
	{
		UseHostFallback,
		Defer
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

	[[nodiscard]] constexpr DispatchAdmission ResolveDispatchAdmission(
		DispatchFencePollResult a_result) noexcept
	{
		switch (a_result) {
		case DispatchFencePollResult::Ready:
			return DispatchAdmission::Proceed;
		case DispatchFencePollResult::Pending:
			return DispatchAdmission::Defer;
		default:
			return DispatchAdmission::Fail;
		}
	}

	[[nodiscard]] constexpr PendingDispatchResolution ResolvePendingDispatch(
		bool a_safeHostFallbackReady) noexcept
	{
		// Provider setup can be pending without being a failure. Fall through only
		// when a complete host provider can service the entire stereo pair.
		return a_safeHostFallbackReady ?
		           PendingDispatchResolution::UseHostFallback :
		           PendingDispatchResolution::Defer;
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
