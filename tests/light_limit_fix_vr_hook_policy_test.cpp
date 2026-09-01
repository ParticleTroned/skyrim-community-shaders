#include "Features/LightLimitFix/VRHookPolicy.h"

#include <array>
#include <cstdint>

namespace
{
	using namespace LightLimitFixVRHookPolicy;

	constexpr bool CoversExternalBranchRecognition()
	{
		constexpr std::array<std::uint8_t, 2> relativeJump{ 0xE9, 0x00 };
		constexpr std::array<std::uint8_t, 2> indirectJump{ 0xFF, 0x25 };
		constexpr std::array<std::uint8_t, 2> relativeCall{ 0xE8, 0x00 };
		constexpr std::array<std::uint8_t, 1> truncatedIndirectJump{ 0xFF };

		return HasExternalBranchPrefix(relativeJump.data(), relativeJump.size()) &&
		       HasExternalBranchPrefix(indirectJump.data(), indirectJump.size()) &&
		       !HasExternalBranchPrefix(relativeCall.data(), relativeCall.size()) &&
		       !HasExternalBranchPrefix(truncatedIndirectJump.data(), truncatedIndirectJump.size()) &&
		       !HasExternalBranchPrefix(nullptr, 0);
	}

	constexpr bool CoversIntegratedGuardDecision()
	{
		using Decision = SceneGraphGuardDecision;

		return DecideSceneGraphGuard(true, true, false, false, false) == Decision::kInstall &&
		       DecideSceneGraphGuard(true, true, true, false, false) == Decision::kInstall &&
		       DecideSceneGraphGuard(true, false, true, true, true) == Decision::kSkipCompatibleExternalGuard &&
		       DecideSceneGraphGuard(true, false, false, true, true) == Decision::kRejectUnknownSite &&
		       DecideSceneGraphGuard(true, false, true, false, true) == Decision::kRejectUnknownSite &&
		       DecideSceneGraphGuard(true, false, true, true, false) == Decision::kRejectUnknownSite &&
		       DecideSceneGraphGuard(false, true, false, false, false) == Decision::kRejectUnknownSite &&
		       DecideSceneGraphGuard(false, false, true, true, true) == Decision::kRejectUnknownSite;
	}

	constexpr bool CoversShadowMapXmmRestorePrefixes()
	{
		constexpr std::array<std::uint8_t, 45> expected{
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			0x41, 0, 0, 0, 0,
			0x41, 0, 0, 0, 0,
			0x45, 0, 0, 0, 0,
			0x45, 0, 0, 0, 0,
			0x45, 0, 0, 0, 0
		};
		constexpr std::array<std::uint8_t, 45> formerXmm14Xmm15{
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			0x45, 0, 0, 0, 0,
			0x45, 0, 0, 0, 0,
			0x45, 0, 0, 0, 0,
			0x45, 0, 0, 0, 0,
			0x45, 0, 0, 0, 0
		};

		return HasExpectedShadowMapXmmRestorePrefixes(expected.data(), expected.size()) &&
		       !HasExpectedShadowMapXmmRestorePrefixes(formerXmm14Xmm15.data(), formerXmm14Xmm15.size()) &&
		       !HasExpectedShadowMapXmmRestorePrefixes(expected.data(), 44) &&
		       !HasExpectedShadowMapXmmRestorePrefixes(nullptr, 0);
	}

	static_assert(CoversExternalBranchRecognition());
	static_assert(CoversIntegratedGuardDecision());
	static_assert(CoversShadowMapXmmRestorePrefixes());
}

int main()
{
	return CoversExternalBranchRecognition() &&
	               CoversIntegratedGuardDecision() &&
	               CoversShadowMapXmmRestorePrefixes() ?
	           0 :
	           1;
}
