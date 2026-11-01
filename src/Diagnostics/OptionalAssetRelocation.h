#pragma once
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <cstdint>
#	include <optional>

namespace CSX::Diagnostics::Stutters
{
	/** Resolve an optional ID without invoking the Address Library's fatal lookup. */
	template <class Mappings>
	std::optional<std::uint64_t> FindAssetOffset(const Mappings& mappings, std::uint64_t id) noexcept
	{
		if (!id)
			return std::nullopt;
		std::optional<std::uint64_t> offset;
		for (const auto& entry : mappings) {
			if (entry.id != id)
				continue;
			if (!entry.offset || offset)
				return std::nullopt;
			offset = entry.offset;
		}
		return offset;
	}
}
#endif
