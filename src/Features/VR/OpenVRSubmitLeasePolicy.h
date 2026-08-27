#pragma once

#include <cstdint>

namespace OpenVRSubmitLeasePolicy
{
	enum class PayloadKind : std::uint8_t
	{
		Texture,
		TextureWithPose,
		TextureWithDepth,
		TextureWithPoseAndDepth
	};

	[[nodiscard]] constexpr PayloadKind SelectPayloadKind(
		bool a_hasPose,
		bool a_hasDepth) noexcept
	{
		if (a_hasPose && a_hasDepth)
			return PayloadKind::TextureWithPoseAndDepth;
		if (a_hasPose)
			return PayloadKind::TextureWithPose;
		if (a_hasDepth)
			return PayloadKind::TextureWithDepth;
		return PayloadKind::Texture;
	}

	struct PublicationLease
	{
		std::uint64_t generation = 0;
		std::uintptr_t deviceIdentity = 0;
		bool colorTextureRetained = false;
		bool depthTextureRequired = false;
		bool depthTextureRetained = false;

		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			return generation != 0 &&
			       deviceIdentity != 0 &&
			       colorTextureRetained &&
			       (!depthTextureRequired || depthTextureRetained);
		}
	};

	[[nodiscard]] constexpr bool CanPublish(
		const PublicationLease& a_lease,
		std::uint64_t a_currentGeneration,
		std::uintptr_t a_currentDeviceIdentity) noexcept
	{
		return a_lease.IsValid() &&
		       a_lease.generation == a_currentGeneration &&
		       a_lease.deviceIdentity == a_currentDeviceIdentity;
	}
}
