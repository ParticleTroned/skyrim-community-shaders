#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace LightLimitFixDetail
{
	/** Exact shader inputs, stored as bits without comparing structure padding. */
	struct ClusterBoundsKey
	{
		std::uint32_t nearPlane = 0;
		std::uint32_t farPlane = 0;
		std::uint32_t farDepth = 0;
		std::uint32_t eyeCount = 0;
		std::array<std::uint32_t, 3> dimensions{};
		std::array<std::array<std::uint32_t, 16>, 2> inverseProjection{};
		bool operator==(const ClusterBoundsKey&) const = default;
	};

	/** A key becomes reusable only after its owning build has been submitted. */
	class ClusterBoundsCache
	{
	public:
		bool Matches(const ClusterBoundsKey& inputs) const { return built && *built == inputs; }
		void Publish(const ClusterBoundsKey& inputs) { built = inputs; }
		void Invalidate() { built.reset(); }

	private:
		std::optional<ClusterBoundsKey> built;
	};
}
