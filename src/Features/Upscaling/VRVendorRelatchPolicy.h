#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace VRVendorRelatchPolicy
{
	using WorkGateMask = std::uint32_t;
	using WorkGateState = std::uint64_t;

	enum class WorkGateSource : WorkGateMask
	{
		None = 0,
		ProcessStartup = 1u << 0,
		MainMenu = 1u << 1,
		LoadingMenu = 1u << 2,
		PreLoadGame = 1u << 3,
		GameLoadNotification = 1u << 4
	};

	inline constexpr WorkGateMask kNoWorkGateSources = 0;
	inline constexpr WorkGateMask kGameEntryWorkGateSources =
		static_cast<WorkGateMask>(WorkGateSource::ProcessStartup) |
		static_cast<WorkGateMask>(WorkGateSource::MainMenu) |
		static_cast<WorkGateMask>(WorkGateSource::PreLoadGame) |
		static_cast<WorkGateMask>(WorkGateSource::GameLoadNotification);
	inline constexpr WorkGateMask kAllWorkGateSources =
		kGameEntryWorkGateSources |
		static_cast<WorkGateMask>(WorkGateSource::LoadingMenu);
	inline constexpr std::array kWorkGateSources{
		WorkGateSource::ProcessStartup,
		WorkGateSource::MainMenu,
		WorkGateSource::LoadingMenu,
		WorkGateSource::PreLoadGame,
		WorkGateSource::GameLoadNotification,
	};
	inline constexpr std::uint32_t kWorkGateStateMaskBits = 32u;

	[[nodiscard]] constexpr WorkGateMask ToMask(WorkGateSource a_source) noexcept
	{
		return static_cast<WorkGateMask>(a_source);
	}

	[[nodiscard]] constexpr std::string_view GetWorkGateSourceName(
		WorkGateSource a_source) noexcept
	{
		switch (a_source) {
		case WorkGateSource::ProcessStartup:
			return "process_startup";
		case WorkGateSource::MainMenu:
			return "main_menu";
		case WorkGateSource::LoadingMenu:
			return "loading_menu";
		case WorkGateSource::PreLoadGame:
			return "pre_load_game";
		case WorkGateSource::GameLoadNotification:
			return "game_load_notification";
		default:
			return "none";
		}
	}

	[[nodiscard]] constexpr WorkGateMask GetStateMask(WorkGateState a_state) noexcept
	{
		return static_cast<WorkGateMask>(a_state);
	}

	[[nodiscard]] constexpr std::uint32_t GetStateEpoch(WorkGateState a_state) noexcept
	{
		return static_cast<std::uint32_t>(a_state >> kWorkGateStateMaskBits);
	}

	[[nodiscard]] constexpr WorkGateState AdvanceState(
		WorkGateState a_previous,
		WorkGateMask a_nextMask) noexcept
	{
		const auto nextEpoch = static_cast<std::uint32_t>(GetStateEpoch(a_previous) + 1u);
		return (static_cast<WorkGateState>(nextEpoch) << kWorkGateStateMaskBits) |
		       static_cast<WorkGateState>(a_nextMask);
	}

	[[nodiscard]] constexpr bool HasAny(WorkGateMask a_sources) noexcept
	{
		return a_sources != kNoWorkGateSources;
	}

	[[nodiscard]] constexpr bool HasAny(
		WorkGateMask a_sources,
		WorkGateMask a_candidates) noexcept
	{
		return (a_sources & a_candidates) != kNoWorkGateSources;
	}

	[[nodiscard]] constexpr bool HasSource(
		WorkGateMask a_sources,
		WorkGateSource a_source) noexcept
	{
		return HasAny(a_sources, ToMask(a_source));
	}

	[[nodiscard]] constexpr WorkGateMask AcquireSource(
		WorkGateMask a_sources,
		WorkGateSource a_source) noexcept
	{
		return a_sources | ToMask(a_source);
	}

	[[nodiscard]] constexpr WorkGateMask ReleaseSource(
		WorkGateMask a_sources,
		WorkGateSource a_source) noexcept
	{
		return a_sources & ~ToMask(a_source);
	}

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

	[[nodiscard]] constexpr bool CanReleaseGameEntryVendorGate(
		const GameEntryConvergence& a_state) noexcept
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

	struct LifecycleMutationAdmission
	{
		bool isVR = false;
		WorkGateMask gateSources = kNoWorkGateSources;
		bool postLoadResetPending = false;
		bool relatchPending = false;
		bool relatchInProgress = false;
	};

	[[nodiscard]] constexpr bool CanMutateVendorLifecycle(
		const LifecycleMutationAdmission& a_state) noexcept
	{
		return !a_state.isVR ||
		       (!HasAny(a_state.gateSources) &&
			   !a_state.postLoadResetPending &&
			   !a_state.relatchPending &&
			   !a_state.relatchInProgress);
	}

	struct DispatchAdmission
	{
		bool isVR = false;
		bool vendorEvaluationSelected = false;
		bool resourcesReady = false;
		bool relatchInProgress = false;
	};

	[[nodiscard]] constexpr bool CanDispatchVendorEvaluation(
		const DispatchAdmission& a_state) noexcept
	{
		return a_state.vendorEvaluationSelected &&
		       a_state.resourcesReady &&
		       (!a_state.isVR || !a_state.relatchInProgress);
	}

	[[nodiscard]] constexpr bool ShouldDeferPhysicalRelatchForStereo(
		std::uint32_t a_currentFrame,
		std::uint32_t a_admissionFrame,
		std::uint32_t a_submittedEyeMask,
		std::uint64_t a_relatchEpoch,
		std::uint64_t a_deferredRelatchEpoch) noexcept
	{
		const std::uint32_t stereoMask = a_submittedEyeMask & 0x3u;
		return a_relatchEpoch != 0 &&
		       a_relatchEpoch != a_deferredRelatchEpoch &&
		       a_admissionFrame == a_currentFrame &&
		       (stereoMask == 0x1u || stereoMask == 0x2u);
	}

	[[nodiscard]] constexpr bool UsesVendorEvaluation(bool a_vendorMethod) noexcept
	{
		return a_vendorMethod;
	}

	[[nodiscard]] constexpr bool RequiresFSRCompatibility(bool a_fsrEvaluation) noexcept
	{
		return a_fsrEvaluation;
	}

	[[nodiscard]] constexpr bool NeedsDeferredFSRReset(
		bool a_fsrEvaluation,
		bool a_preservedResources,
		bool a_recreatedResources) noexcept
	{
		return a_fsrEvaluation &&
		       !a_preservedResources &&
		       !a_recreatedResources;
	}
}
