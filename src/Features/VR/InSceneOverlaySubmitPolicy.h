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

	[[nodiscard]] constexpr bool IsQueuedReplacementOnly(
		SuppressionReason a_reasons) noexcept
	{
		constexpr auto queuedReplacementMask =
			static_cast<std::uint8_t>(
				SuppressionReason::RenderScaleTransitionPending) |
			static_cast<std::uint8_t>(
				SuppressionReason::RenderTargetRecreatePending) |
			static_cast<std::uint8_t>(
				SuppressionReason::VendorRuntimeResetPending);
		const auto reasonBits = static_cast<std::uint8_t>(a_reasons);
		return reasonBits != 0 &&
		       (reasonBits & queuedReplacementMask) == reasonBits;
	}

	struct Admission
	{
		SuppressionReason suppressionReasons = SuppressionReason::None;
		bool mainMenuOpen = false;
		bool menuSessionOpen = false;
		bool submitStageUpscalingActive = false;
		bool renderTargetRecreateInProgress = false;
		bool originalSubmitCandidateSafe = false;
		bool stablePresentationProven = false;
		bool deviceLost = false;
		bool nativeRestoreGuardActive = false;
	};

	[[nodiscard]] constexpr bool ShouldAdmit(const Admission& a_admission) noexcept
	{
		if (a_admission.deviceLost ||
			a_admission.renderTargetRecreateInProgress ||
			a_admission.nativeRestoreGuardActive ||
			!a_admission.originalSubmitCandidateSafe) {
			return false;
		}

		if (a_admission.suppressionReasons == SuppressionReason::None)
			return true;

		// The main menu can remain at a controller safe point indefinitely because
		// no world frame advances the transition. A render-target recreate can be
		// queued there for the same reason. Admit the already-classified original
		// texture while the recreate is merely queued, but never after physical
		// mutation has entered.
		if (a_admission.mainMenuOpen &&
			!a_admission.submitStageUpscalingActive &&
			(a_admission.suppressionReasons ==
				SuppressionReason::RenderScaleTransitionPending ||
				a_admission.suppressionReasons ==
					SuppressionReason::RenderTargetRecreatePending)) {
			return true;
		}

		// The in-world CS menu may retain an already-published stereo provider while
		// its replacement is only queued. Runtime reset and unknown reasons fail
		// closed through IsQueuedReplacementOnly.
		return a_admission.menuSessionOpen &&
		       a_admission.stablePresentationProven &&
		       IsQueuedReplacementOnly(a_admission.suppressionReasons);
	}
}
