#pragma once

#include <cstdint>

namespace FSRHostLifecyclePolicy
{
	inline constexpr std::uint32_t MinimumFeatureLevel = 0xB100;

	[[nodiscard]] constexpr bool SupportsHostFsr3FeatureLevel(
		std::uint32_t a_featureLevel) noexcept
	{
		// Current host shaders bind more than eight compute UAV slots.
		return a_featureLevel >= MinimumFeatureLevel;
	}

	[[nodiscard]] constexpr bool CanAttemptHostFallback(
		bool a_hostSupported,
		bool a_runtimeProviderUsedForFrame) noexcept
	{
		return a_hostSupported && !a_runtimeProviderUsedForFrame;
	}

	enum class CallDisposition
	{
		Succeeded,
		ReturnedError,
		Faulted
	};

	[[nodiscard]] constexpr CallDisposition ClassifyCallDisposition(
		bool a_faulted,
		bool a_succeeded) noexcept
	{
		if (a_faulted)
			return CallDisposition::Faulted;
		return a_succeeded ?
		           CallDisposition::Succeeded :
		           CallDisposition::ReturnedError;
	}

	[[nodiscard]] constexpr bool RequiresOwnershipQuarantine(
		CallDisposition a_disposition) noexcept
	{
		// FidelityFX create/destroy can return after partial backend mutation.
		// Only an explicit success proves that the caller may reuse or release
		// the context storage and its shared scratch allocation.
		return a_disposition != CallDisposition::Succeeded;
	}

	[[nodiscard]] constexpr bool CanReleaseFailedInterfaceScratch(
		CallDisposition a_disposition) noexcept
	{
		// ffxGetInterfaceDX11 has not transferred backend ownership when it
		// returns an error. A fault is different: execution may have escaped at
		// an unknown point, so only the ordinary returned-error path is releasable.
		return a_disposition == CallDisposition::ReturnedError;
	}

	[[nodiscard]] constexpr bool CanReleaseHostOwnership(
		bool a_quarantined,
		bool a_hasValidContext,
		bool a_hasIndeterminateContext) noexcept
	{
		return !a_quarantined &&
		       !a_hasValidContext &&
		       !a_hasIndeterminateContext;
	}
}
