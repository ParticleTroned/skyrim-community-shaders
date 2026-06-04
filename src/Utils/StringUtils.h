#pragma once

#include <cctype>
#include <optional>
#include <string>
#include <string_view>

namespace Util
{
	inline std::string ToLowerAscii(std::string_view a_value)
	{
		std::string result;
		result.reserve(a_value.size());
		for (char c : a_value) {
			result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
		}
		return result;
	}

	inline bool IEndsWithAsciiInsensitive(std::string_view a_value, std::string_view a_suffix)
	{
		if (a_value.size() < a_suffix.size()) {
			return false;
		}

		a_value.remove_prefix(a_value.size() - a_suffix.size());
		for (std::size_t i = 0; i < a_suffix.size(); ++i) {
			const auto lhs = static_cast<unsigned char>(a_value[i]);
			const auto rhs = static_cast<unsigned char>(a_suffix[i]);
			if (std::tolower(lhs) != std::tolower(rhs)) {
				return false;
			}
		}
		return true;
	}

	inline std::optional<std::string> GetLowercaseStem(std::string_view a_path, std::string_view a_extension)
	{
		if (a_path.empty() || a_extension.empty()) {
			return std::nullopt;
		}

		const auto lastSeparatorPos = a_path.find_last_of("\\/");
		std::string_view filename = lastSeparatorPos == std::string_view::npos ? a_path : a_path.substr(lastSeparatorPos + 1);
		if (filename.empty() || !IEndsWithAsciiInsensitive(filename, a_extension)) {
			return std::nullopt;
		}

		filename.remove_suffix(a_extension.size());
		if (filename.empty()) {
			return std::nullopt;
		}

		return ToLowerAscii(filename);
	}
}
