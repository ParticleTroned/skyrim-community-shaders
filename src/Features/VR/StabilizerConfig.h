#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace VRFpsStabilizer
{
	inline constexpr size_t kMaxIniBytes = 4u * 1024u * 1024u;
	inline constexpr unsigned char LowerAscii(unsigned char value)
	{
		return value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value;
	}

	inline std::string_view Trim(std::string_view text)
	{
		const auto first = text.find_first_not_of(" \t\r\n");
		return first == text.npos ? std::string_view{} : text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
	}

	inline bool Equal(std::string_view left, std::string_view right)
	{
		return std::ranges::equal(left, right, [](unsigned char a, unsigned char b) {
			return LowerAscii(a) == LowerAscii(b);
		});
	}

	/** A byte-preserving INI editor; only explicitly edited values or section bodies change. */
	class IniDocument
	{
	public:
		std::string original;
		std::string text;

		static bool ValidateText(std::string_view contents, std::string& error)
		{
			if (contents.size() > kMaxIniBytes || contents.find('\0') != contents.npos ||
				contents.starts_with("\xFF\xFE") || contents.starts_with("\xFE\xFF")) {
				error = "INI must be text without NUL bytes or UTF-16 encoding and at most 4 MiB.";
				return false;
			}
			return true;
		}

		std::optional<std::string> Get(std::string_view section, std::string_view key) const
		{
			std::optional<std::string> value;
			ForEachLine([&](const Line& line) {
				if (Equal(line.section, section) && Equal(line.key, key))
					value = text.substr(line.valueBegin, line.valueEnd - line.valueBegin);
			});
			return value;
		}

		/** Replace all duplicate occurrences consistently, retaining comments and whitespace. */
		bool Set(std::string_view section, std::string_view key, std::string_view value, std::string& error)
		{
			error.clear();
			if (section.empty() || key.empty() || section.find_first_of("\r\n[]#;=") != section.npos ||
				key.find_first_of("\r\n[]#;=") != key.npos || value.find_first_of("\r\n#;") != value.npos ||
				!ValidateText(value, error)) {
				error = "INI fields cannot contain line breaks, section headers or comment delimiters.";
				return false;
			}
			std::vector<Line> matches;
			ForEachLine([&](const Line& line) {
				if (Equal(line.section, section) && Equal(line.key, key))
					matches.push_back(line);
			});
			auto updated = text;
			if (matches.empty()) {
				const auto end = SectionBounds(section);
				const auto insertion = end ? end->second : text.size();
				std::string addition;
				if (insertion && text[insertion - 1] != '\n')
					addition += Newline();
				if (!end)
					addition += "[" + std::string(section) + "]" + Newline();
				addition += std::string(key) + " = " + std::string(value) + Newline();
				updated.insert(insertion, addition);
			} else {
				for (auto it = matches.rbegin(); it != matches.rend(); ++it)
					updated.replace(it->valueBegin, it->valueEnd - it->valueBegin, value);
			}
			if (!ValidateText(updated, error))
				return false;
			text = std::move(updated);
			return true;
		}

		std::string Body(std::string_view section) const
		{
			const auto bounds = SectionBounds(section);
			return bounds ? text.substr(bounds->first, bounds->second - bounds->first) : std::string{};
		}

		/** Return active key/value rows for the structured quality and location editors. */
		std::vector<std::pair<std::string, std::string>> Entries(std::string_view section) const
		{
			std::vector<std::pair<std::string, std::string>> entries;
			std::unordered_map<std::string, size_t> indices;
			ForEachLine([&](const Line& line) {
				if (line.key.empty() || !Equal(line.section, section))
					return;
				std::string normalized(line.key);
				std::ranges::transform(normalized, normalized.begin(), LowerAscii);
				const auto [entry, inserted] = indices.emplace(std::move(normalized), entries.size());
				if (inserted)
					entries.emplace_back(line.key, std::string{});
				entries[entry->second].second = text.substr(line.valueBegin, line.valueEnd - line.valueBegin);
			});
			return entries;
		}

		/** Advanced command editing stays inside one existing or explicitly created section. */
		bool SetBody(std::string_view section, std::string_view body, std::string& error)
		{
			error.clear();
			if (section.empty() || section.find_first_of("\r\n[]#;=") != section.npos) {
				error = "Invalid INI section name.";
				return false;
			}
			size_t sections = 0;
			ForEachLine([&](const Line& line) { sections += line.header && Equal(line.section, section) ? 1 : 0; });
			if (sections > 1) {
				error = "This INI has duplicate section headers. Consolidate that section before editing its commands.";
				return false;
			}
			if (!ValidateText(body, error))
				return false;
			for (size_t start = 0; start < body.size();) {
				const auto end = body.find('\n', start);
				if (Trim(body.substr(start, end == body.npos ? body.size() - start : end - start)).starts_with('[')) {
					error = "Enter only this section's contents; section headers are not allowed.";
					return false;
				}
				start = end == body.npos ? body.size() : end + 1;
			}
			const auto bounds = SectionBounds(section);
			auto updated = text;
			std::string replacement(body);
			if (!replacement.empty() && replacement.back() != '\n')
				replacement += Newline();
			if (bounds) {
				if (!replacement.empty() && bounds->first && text[bounds->first - 1] != '\n')
					replacement.insert(0, Newline());
				updated.replace(bounds->first, bounds->second - bounds->first, replacement);
			} else if (!replacement.empty()) {
				if (!updated.empty() && updated.back() != '\n')
					updated += Newline();
				updated += "[" + std::string(section) + "]" + Newline() + replacement;
			}
			if (!ValidateText(updated, error))
				return false;
			text = std::move(updated);
			return true;
		}

	private:
		struct Line
		{
			std::string_view section;
			std::string_view key;
			size_t begin = 0, end = 0, valueBegin = 0, valueEnd = 0;
			bool header = false;
		};

		std::string Newline() const { return text.find("\r\n") != text.npos ? "\r\n" : "\n"; }

		template <class Visitor>
		void ForEachLine(Visitor visit) const
		{
			std::string_view section;
			for (size_t start = text.starts_with("\xEF\xBB\xBF") ? 3 : 0; start < text.size();) {
				const auto newline = text.find('\n', start);
				const auto end = newline == text.npos ? text.size() : newline + 1;
				auto payload = std::string_view(text).substr(start, end - start);
				payload = Trim(payload.substr(0, payload.find_first_of("#;")));
				Line line{ .section = section, .begin = start, .end = end };
				if (payload.starts_with('[') && payload.ends_with(']')) {
					section = Trim(payload.substr(1, payload.size() - 2));
					line.section = section;
					line.header = true;
				} else if (!payload.empty()) {
					const auto equals = payload.find('=');
					if (equals != payload.npos) {
						line.key = Trim(payload.substr(0, equals));
						auto value = payload.substr(equals + 1);
						const auto first = value.find_first_not_of(" \t");
						line.valueBegin = static_cast<size_t>(value.data() - text.data()) + (first == value.npos ? value.size() : first);
						line.valueEnd = static_cast<size_t>(payload.data() - text.data()) + payload.size();
					}
				}
				visit(line);
				start = end;
			}
		}

		std::optional<std::pair<size_t, size_t>> SectionBounds(std::string_view section) const
		{
			std::optional<std::pair<size_t, size_t>> bounds;
			ForEachLine([&](const Line& line) {
				if (line.header) {
					if (bounds && bounds->second == text.size())
						bounds->second = line.begin;
					if (!bounds && Equal(line.section, section))
						bounds = { line.end, text.size() };
				}
			});
			return bounds;
		}
	};
}
