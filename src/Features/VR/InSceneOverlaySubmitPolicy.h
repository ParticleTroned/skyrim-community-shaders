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
		bool renderTargetRecreateInProgress = false;
		bool originalSubmitCandidateSafe = false;
	};

	[[nodiscard]] constexpr bool ShouldAdmit(const Admission& a_admission) noexcept
	{
		if (a_admission.suppressionReasons == SuppressionReason::None)
			return true;

		if (!a_admission.mainMenuOpen ||
			 a_admission.submitStageUpscalingActive ||
			 !a_admission.originalSubmitCandidateSafe) {
			return false;
		}

		// The main menu can remain at a controller safe point indefinitely because
		// no world frame advances the transition. A render-target recreate can be
		// queued there for the same reason. Admit the already-classified original
		// texture while the recreate is merely queued, but never after physical
		// mutation has entered. Combined reasons and runtime/vendor resets remain
		// hard gates.
		if (a_admission.suppressionReasons ==
			SuppressionReason::RenderScaleTransitionPending) {
			return true;
		}

		return a_admission.suppressionReasons ==
		           SuppressionReason::RenderTargetRecreatePending &&
		       !a_admission.renderTargetRecreateInProgress;
	}
}
