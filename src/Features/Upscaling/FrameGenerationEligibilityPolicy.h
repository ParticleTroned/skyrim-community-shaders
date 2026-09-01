#pragma once

namespace FrameGenerationEligibilityPolicy
{
	/** @brief Stable inputs used to decide whether frame generation may run. */
	struct Inputs
	{
		bool runtimeStateAvailable = false;
		bool communityShadersMenuOpen = false;
		bool gamePaused = false;
		bool mainOrLoadingMenuOpen = false;
	};

	/**
	 * @brief Returns whether persistent game-loop state permits generation.
	 *
	 * Eligibility is queried outside the render-world scope, so it must not
	 * depend on render-pass-local state such as State::inWorld.
	 */
	[[nodiscard]] constexpr bool IsInteractiveGameplay(const Inputs& a_inputs) noexcept
	{
		return a_inputs.runtimeStateAvailable &&
		       !a_inputs.communityShadersMenuOpen &&
		       !a_inputs.gamePaused &&
		       !a_inputs.mainOrLoadingMenuOpen;
	}
}
