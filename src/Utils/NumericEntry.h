#pragma once

#include <charconv>
#include <cmath>
#include <optional>
#include <string_view>
#include <type_traits>

namespace MenuUI
{
	/** Parses the complete draft without changing a setting on invalid input. */
	template <class T>
	std::optional<T> ParseNumber(std::string_view a_text, T a_minimum, T a_maximum)
	{
		if (a_text.empty() || !std::isfinite(static_cast<double>(a_minimum)) ||
			!std::isfinite(static_cast<double>(a_maximum)) || a_minimum > a_maximum)
			return std::nullopt;
		T value{};
		const auto result = std::from_chars(a_text.data(), a_text.data() + a_text.size(), value);
		if (result.ec != std::errc{} || result.ptr != a_text.data() + a_text.size() ||
			!std::isfinite(static_cast<double>(value)) || value < a_minimum || value > a_maximum)
			return std::nullopt;
		return value;
	}
}
