#pragma once

namespace VRVendorRelatchPolicy
{
	struct GameEntryConvergence
	{
		bool hasGateOwner = false;
		bool mainMenuActive = false;
		bool loadingPresentationActive = false;
		bool raceSexPresentationActive = false;
		bool saveLoadProtectionActive = false;
		bool completedWorldFrame = false;
		bool recoveryPending = false;
		bool relatchPending = false;
		bool profileTransitionPending = false;
	};

	constexpr bool CanReleaseGameEntryVendorGate(const GameEntryConvergence& a_state)
	{
		return a_state.hasGateOwner &&
		       !a_state.mainMenuActive &&
		       !a_state.loadingPresentationActive &&
		       !a_state.raceSexPresentationActive &&
		       !a_state.saveLoadProtectionActive &&
		       a_state.completedWorldFrame &&
		       !a_state.recoveryPending &&
		       !a_state.relatchPending &&
		       !a_state.profileTransitionPending;
	}

	constexpr bool RequiresVendorRuntime(bool a_targetActive, bool a_vendorMethod)
	{
		return a_targetActive && a_vendorMethod;
	}

	constexpr bool RequiresFSRCompatibility(bool a_targetActive, bool a_fsrMethod)
	{
		return a_targetActive && a_fsrMethod;
	}

	constexpr bool NeedsDeferredFSRReset(
		bool a_targetActive,
		bool a_fsrMethod,
		bool a_preservedResources,
		bool a_recreatedResources)
	{
		return a_targetActive &&
		       a_fsrMethod &&
		       !a_preservedResources &&
		       !a_recreatedResources;
	}

	constexpr bool ShouldDeferOrdinaryVendorWork(
		bool a_isVR,
		bool a_postLoadResetPending,
		bool a_relatchPending,
		bool a_relatchInProgress)
	{
		return a_isVR &&
		       (a_postLoadResetPending || a_relatchPending || a_relatchInProgress);
	}
}
