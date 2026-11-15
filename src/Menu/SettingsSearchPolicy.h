#pragma once

#include <span>
#include <string_view>
#include <vector>

namespace MenuUI::SettingsSearch::Policy
{
	inline constexpr size_t MaxQueryBytes = 255;
	inline constexpr size_t MaxResults = 64;

	/** Split a normalized query; returned views borrow the query's storage. */
	inline std::vector<std::string_view> Words(std::string_view query)
	{
		std::vector<std::string_view> words;
		for (size_t start = query.find_first_not_of(" \t\r\n"); start != std::string_view::npos;) {
			const auto end = query.find_first_of(" \t\r\n", start);
			words.push_back(query.substr(start, end == std::string_view::npos ? end : end - start));
			start = end == std::string_view::npos ? end : query.find_first_not_of(" \t\r\n", end);
		}
		return words;
	}

	/** Require every word, preferring title matches to location matches. */
	inline int Score(std::string_view title, std::string_view location, std::span<const std::string_view> words)
	{
		if (words.empty())
			return -1;
		int score = 0;
		for (const auto word : words) {
			const auto at = title.find(word);
			if (at != std::string_view::npos)
				score += 100 + (at == 0 ? 50 : 0);
			else if (location.find(word) != std::string_view::npos)
				score += 20;
			else
				return -1;
		}
		return score;
	}
}
