#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <algorithm>
#	include <array>
#	include <cstdint>

namespace NeuralRendering
{
	inline constexpr std::uint32_t kMaximumShadowTransferPercent = 100;
	inline constexpr std::uint32_t kMaximumFaceDetailProtectionPercent = 100;
	inline constexpr std::array kReconstructionFootprintSubdivisions{ 1u, 2u, 4u };
	struct ReconstructionPreparation
	{
		std::uint32_t footprintSubdivisions = 2;
		bool depthSampleMotion = true;
		bool faceDetailProtection = false;
		bool operator==(const ReconstructionPreparation&) const = default;
	};

	/** Session-only comparisons; defaults reproduce the production reconstruction. */
	struct ReconstructionTuning
	{
		std::uint32_t shadowTransferPercent = kMaximumShadowTransferPercent;
		std::uint32_t faceDetailProtectionPercent = 0;
		std::uint32_t footprintSubdivisions = 2;
		bool perChannelClamping = true;
		bool depthSampleMotion = true;

		bool operator==(const ReconstructionTuning&) const = default;
		[[nodiscard]] static constexpr bool ValidFootprint(std::uint32_t subdivisions) noexcept
		{
			return std::ranges::find(kReconstructionFootprintSubdivisions, subdivisions) != kReconstructionFootprintSubdivisions.end();
		}
		[[nodiscard]] constexpr bool Valid() const noexcept
		{
			return shadowTransferPercent <= kMaximumShadowTransferPercent && faceDetailProtectionPercent <= kMaximumFaceDetailProtectionPercent && ValidFootprint(footprintSubdivisions);
		}
		/** Native input identity includes preparation; presentation identity covers all controls. */
		[[nodiscard]] constexpr ReconstructionPreparation Preparation(bool faceDetailActive = false) const noexcept { return { footprintSubdivisions, depthSampleMotion, faceDetailActive && faceDetailProtectionPercent != 0 }; }
	};
}
#endif
