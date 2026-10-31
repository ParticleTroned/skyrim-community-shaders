#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <istream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Util
{
	/** Accepts one nonempty filename component without drive syntax or navigation. */
	inline bool IsSinglePathComponent(std::string_view name)
	{
		return !name.empty() && name.find_first_of("/\\:<>\"|?*") == std::string_view::npos && name.find('\0') == std::string_view::npos &&
		       name != "." && name != ".." && name.back() != '.' && name.back() != ' ';
	}

	/** Joins a Data-relative resource path, rejecting roots and parent traversal. */
	inline std::filesystem::path JoinRelativePath(const std::filesystem::path& base, const std::filesystem::path& path)
	{
		if (path.has_root_path())
			throw std::invalid_argument("Resource path must be relative");
		for (const auto& part : path) {
			if (part == ".")
				continue;
			if (!IsSinglePathComponent(part.string()))
				throw std::invalid_argument("Invalid resource path component");
		}
		return base / path;
	}

	/** Appends trimmed config lines after removing hash comments. */
	inline void ReadConfigLines(std::istream& stream, std::vector<std::string>& lines)
	{
		std::string line;
		while (std::getline(stream, line)) {
			line.resize(std::min(line.size(), line.find('#')));
			const auto begin = line.find_first_not_of(" \t\r\n");
			if (begin != std::string::npos)
				lines.push_back(line.substr(begin, line.find_last_not_of(" \t\r\n") - begin + 1));
		}
		if (stream.bad() || (stream.fail() && !stream.eof()))
			throw std::runtime_error("Could not read geometry configuration");
	}

	/** Preserve Unicode filenames when passing paths through JSON and DevBench. */
	inline std::string PathToUtf8(const std::filesystem::path& path)
	{
		const auto text = path.u8string();
		return { reinterpret_cast<const char*>(text.data()), text.size() };
	}

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
