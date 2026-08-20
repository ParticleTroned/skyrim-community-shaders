#pragma once

#include <cstdint>

namespace VRInSceneOverlaySubmitPolicy
{
	enum class SuppressionReason : std::uint8_t
	{
		None = 0,
		RenderScaleTransitionPending = 1u << 0,
		RenderTargetRecreatePending = 1u << 1,
		RuntimeResetPending = 1u << 2,
		VendorRuntimeResetPending = 1u << 3,
	};

	[[nodiscard]] constexpr SuppressionReason operator|(
		SuppressionReason a_left,
		SuppressionReason a_right) noexcept
	{
		return static_cast<SuppressionReason>(
			static_cast<std::uint8_t>(a_left) |
			static_cast<std::uint8_t>(a_right));
	}

	constexpr SuppressionReason& operator|=(
		SuppressionReason& a_left,
		SuppressionReason a_right) noexcept
	{
		a_left = a_left | a_right;
		return a_left;
	}

	struct Admission
	{
		SuppressionReason suppressionReasons = SuppressionReason::None;
		bool mainMenuOpen = false;
		bool submitStageUpscalingActive = false;
		bool originalSubmitCandidateSafe = false;
	};

	[[nodiscard]] constexpr bool ShouldAdmit(const Admission& a_admission) noexcept
	{
		if (a_admission.suppressionReasons == SuppressionReason::None)
			return true;

		// The main menu can remain at a controller safe point indefinitely because
		// no world frame advances the transition. Admit only that single benign
		// wait, and only after the exact original texture passed the compositor
		// fallback classifier. Target recreation and runtime/vendor reset reasons
		// remain hard gates even when the menu is open.
		return a_admission.suppressionReasons ==
		           SuppressionReason::RenderScaleTransitionPending &&
		       a_admission.mainMenuOpen &&
		       !a_admission.submitStageUpscalingActive &&
		       a_admission.originalSubmitCandidateSafe;
	}
}
