#pragma once

#include <cstdint>
#include <string_view>

namespace CSX::MeshBlendingPolicy
{
	enum class CachedClassification : std::uint8_t
	{
		kRejected,
		kAllowedByRule,
		kAutomatic,
	};

	[[nodiscard]] constexpr bool CanReuseCacheHit(
		CachedClassification a_classification,
		bool a_rootHasAnimation,
		bool a_automaticReceiverIsCurrentAndSafe) noexcept
	{
		if (a_classification == CachedClassification::kRejected)
			return true;
		if (a_rootHasAnimation)
			return false;
		return a_classification != CachedClassification::kAutomatic ||
		       a_automaticReceiverIsCurrentAndSafe;
	}

	[[nodiscard]] constexpr std::string_view TrimAsciiSpaces(std::string_view a_value) noexcept
	{
		const auto first = a_value.find_first_not_of(' ');
		if (first == std::string_view::npos)
			return {};
		const auto lastNonSpace = a_value.find_last_not_of(' ');
		return a_value.substr(first, lastNonSpace - first + 1);
	}

	[[nodiscard]] constexpr bool HasLandscapeSelector(
		std::string_view a_form,
		std::string_view a_editorID,
		std::string_view a_diffuse) noexcept
	{
		return !TrimAsciiSpaces(a_form).empty() ||
		       !TrimAsciiSpaces(a_editorID).empty() ||
		       !TrimAsciiSpaces(a_diffuse).empty();
	}
}
