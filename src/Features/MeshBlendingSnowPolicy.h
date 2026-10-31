#pragma once

#include <cstdint>

namespace CSX::MeshBlendingSnowPolicy
{
	enum class Eligibility
	{
		kAccepted,
		kMissingFlags,
		kExcludedFlags,
		kMaterialFeature
	};

	template <class ShaderFlag>
	[[nodiscard]] constexpr std::uint64_t RequiredFlags() noexcept
	{
		return static_cast<std::uint64_t>(ShaderFlag::kProjectedUV) |
		       static_cast<std::uint64_t>(ShaderFlag::kMultiIndexSnow);
	}

	/** Admit the multi-index snow family while retaining other overlay exclusions. */
	template <class ShaderFlag, class MaterialFeature>
	[[nodiscard]] constexpr Eligibility Classify(
		std::uint64_t a_flags, MaterialFeature a_feature, std::uint64_t a_excludedOverlayFlags) noexcept
	{
		constexpr auto required = RequiredFlags<ShaderFlag>();
		constexpr auto snowFamily = required | static_cast<std::uint64_t>(ShaderFlag::kSnow);
		if ((a_flags & required) != required)
			return Eligibility::kMissingFlags;
		if ((a_flags & (a_excludedOverlayFlags & ~snowFamily)) != 0u)
			return Eligibility::kExcludedFlags;
		if (a_feature != MaterialFeature::kMultiIndexTriShapeSnow)
			return Eligibility::kMaterialFeature;
		return Eligibility::kAccepted;
	}
}
