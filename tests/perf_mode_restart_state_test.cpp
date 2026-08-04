#include "Features/Upscaling/PerfModeRestartState.h"

#include <cassert>

int main()
{
	using VRPerfModeRestartState::ActiveBootContractInputs;

	bool restartRequired = false;

	// The active physical contract is Balanced. A deferred Quality request must
	// mark that immutable target as restart-required.
	VRPerfModeRestartState::Refresh(
		restartRequired,
		ActiveBootContractInputs{
			.bootActive = true,
			.requestedNow = true,
			.displaySizeChanged = false,
			.eligibleNow = true,
			.methodMatches = true,
			.qualityModeMatches = false,
		});
	assert(restartRequired);

	// Superseding the deferred request back to the still-active Balanced
	// contract must clear that stale state so no-op admission can reuse it.
	VRPerfModeRestartState::Refresh(
		restartRequired,
		ActiveBootContractInputs{
			.bootActive = true,
			.requestedNow = true,
			.displaySizeChanged = false,
			.eligibleNow = true,
			.methodMatches = true,
			.qualityModeMatches = true,
		});
	assert(!restartRequired);

	// A real HMD-size change remains restart-required even when the requested
	// method and quality return to the active contract.
	VRPerfModeRestartState::Refresh(
		restartRequired,
		ActiveBootContractInputs{
			.bootActive = true,
			.requestedNow = true,
			.displaySizeChanged = true,
			.eligibleNow = true,
			.methodMatches = true,
			.qualityModeMatches = true,
		});
	assert(restartRequired);
}
