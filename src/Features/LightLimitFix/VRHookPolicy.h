#pragma once

#include <cstddef>
#include <cstdint>

namespace LightLimitFixVRHookPolicy
{
	enum class SceneGraphGuardDecision
	{
		kInstall,
		kSkipCompatibleExternalGuard,
		kRejectUnknownSite
	};

	[[nodiscard]] constexpr bool HasExternalBranchPrefix(
		const std::uint8_t* a_instructions,
		std::size_t a_size) noexcept
	{
		if (a_instructions == nullptr || a_size == 0)
			return false;

		return a_instructions[0] == 0xE9 ||
		       (a_size >= 2 && a_instructions[0] == 0xFF && a_instructions[1] == 0x25);
	}

	[[nodiscard]] constexpr SceneGraphGuardDecision DecideSceneGraphGuard(
		bool a_helperSignaturesMatch,
		bool a_vanillaCallContextMatches,
		bool a_engineFixesLoaded,
		bool a_externalCallPrefixMatches,
		bool a_externalBranchPrefixMatches) noexcept
	{
		if (!a_helperSignaturesMatch)
			return SceneGraphGuardDecision::kRejectUnknownSite;

		if (a_vanillaCallContextMatches)
			return SceneGraphGuardDecision::kInstall;

		if (a_engineFixesLoaded && a_externalCallPrefixMatches && a_externalBranchPrefixMatches)
			return SceneGraphGuardDecision::kSkipCompatibleExternalGuard;

		return SceneGraphGuardDecision::kRejectUnknownSite;
	}

	[[nodiscard]] constexpr bool HasExpectedShadowMapXmmRestorePrefixes(
		const std::uint8_t* a_epilogue,
		std::size_t a_size) noexcept
	{
		// Skyrim VR restores XMM6/XMM7 with 0x41, then XMM8-XMM12 with 0x45.
		return a_epilogue != nullptr && a_size >= 45 &&
		       a_epilogue[20] == 0x41 && a_epilogue[25] == 0x41 &&
		       a_epilogue[30] == 0x45 && a_epilogue[35] == 0x45 &&
		       a_epilogue[40] == 0x45;
	}
}
