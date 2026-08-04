#pragma once

namespace VRPerfModeRestartState
{
	struct ActiveBootContractInputs
	{
		bool bootActive = false;
		bool requestedNow = false;
		bool displaySizeChanged = false;
		bool eligibleNow = false;
		bool methodMatches = false;
		bool qualityModeMatches = false;
	};

	[[nodiscard]] constexpr bool RequiresRestart(const ActiveBootContractInputs& a_inputs) noexcept
	{
		return a_inputs.bootActive &&
		       (!a_inputs.requestedNow ||
				   a_inputs.displaySizeChanged ||
				   !a_inputs.eligibleNow ||
				   !a_inputs.methodMatches ||
				   !a_inputs.qualityModeMatches);
	}

	constexpr void Refresh(bool& a_restartRequired, const ActiveBootContractInputs& a_inputs) noexcept
	{
		a_restartRequired = RequiresRestart(a_inputs);
	}
}
