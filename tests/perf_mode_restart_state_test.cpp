#include "Features/Upscaling/PerfModeRestartState.h"

namespace
{
	using VRPerfModeRestartState::ActiveBootContractInputs;

	constexpr bool ExpectedRequiresRestart(const ActiveBootContractInputs& a_inputs)
	{
		return a_inputs.bootActive &&
		       (!a_inputs.requestedNow ||
			   a_inputs.displaySizeChanged ||
			   !a_inputs.eligibleNow ||
			   !a_inputs.methodMatches ||
			   !a_inputs.qualityModeMatches);
	}

	constexpr bool CoversEveryInputCombination()
	{
		for (unsigned mask = 0; mask < (1u << 6); ++mask) {
			const ActiveBootContractInputs inputs{
				.bootActive = (mask & (1u << 0)) != 0,
				.requestedNow = (mask & (1u << 1)) != 0,
				.displaySizeChanged = (mask & (1u << 2)) != 0,
				.eligibleNow = (mask & (1u << 3)) != 0,
				.methodMatches = (mask & (1u << 4)) != 0,
				.qualityModeMatches = (mask & (1u << 5)) != 0,
			};
			if (VRPerfModeRestartState::RequiresRestart(inputs) != ExpectedRequiresRestart(inputs))
				return false;
		}
		return true;
	}

	constexpr bool ClearsSupersededRestartRequest()
	{
		bool restartRequired = false;

		// The active physical contract is Balanced. A deferred Quality request
		// marks that immutable target as restart-required.
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
		if (!restartRequired)
			return false;

		// Superseding it with the still-active Balanced contract must clear the
		// stale state so no-op admission can reuse that physical contract.
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
		return !restartRequired;
	}
}

static_assert(CoversEveryInputCombination());
static_assert(ClearsSupersededRestartRequest());

int main() {}
