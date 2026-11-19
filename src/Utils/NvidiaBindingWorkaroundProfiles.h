#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace Util::NvidiaBindingWorkaround
{
	struct ContextLayout
	{
		std::uint32_t firstCache, secondCache, primaryManager, secondManager;
	};
	struct DriverProfile
	{
		std::string_view version, sha256;
		std::uint32_t firstRva, secondRva, emitterRva;
		ContextLayout context;
	};
	inline constexpr ContextLayout Context616{ 0xc850, 0xc858, 0x21e28, 0x21e30 };
	inline constexpr ContextLayout Context621{ 0xc880, 0xc888, 0x21e48, 0x21e50 };
	inline constexpr std::array DriverProfiles{
		DriverProfile{ "616.92", "670a1c125f8c3851a1ae7d23c0390cd879e5b9d0a423d99eddff37a8e2774bff", 0x3ae7b0, 0x3ae940, 0x3adcd0, Context616 },
		DriverProfile{ "621.01", "bb6c170021de9a722eb16c6368faafda0266831d6ba9e8e343bce131f35949dc", 0x4724b0, 0x472640, 0x4719d0, Context621 },
		DriverProfile{ "621.14", "1151c9d103cb9b3149998a5038a1270274fa43506154eff46ed7b86a057e87c1", 0x472bb0, 0x472d40, 0x4720d0, Context621 },
		DriverProfile{ "621.24", "74124791d329ee4dadd81b2fd2fb5749773a91c921ec888be0b589c146604614", 0x47d140, 0x47d2d0, 0x47c660, Context621 }
	};
	/** Selects only an exact binary identity; version ranges never authorize hooks. */
	constexpr const DriverProfile* FindDriverProfile(std::string_view sha256) noexcept
	{
		for (const auto& profile : DriverProfiles)
			if (profile.sha256 == sha256)
				return &profile;
		return nullptr;
	}
}
