#pragma once

#include "Utils/TexturePath.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace DetailMapsPolicy
{
	inline constexpr uint32_t ShaderFlag = 1u << 14;
	inline constexpr uint32_t TextureSlot = 23;
	inline constexpr uint32_t ConstantSlot = 9;
	inline constexpr uint32_t SamplerSlot = 15;
	inline constexpr size_t MaxPatternLength = 512;
	inline constexpr size_t MaxPathLength = 4096;

	/** @brief Reuses bounded scratch storage while matching all rules against one texture path. */
	class PathMatcher
	{
	public:
		explicit PathMatcher(std::string_view path) : text(path), next(path.size() <= MaxPathLength ? path.size() + 1 : 0), current(next.size()) {}
		bool Matches(std::string_view pattern)
		{
			if (text.size() > MaxPathLength || pattern.empty() || pattern.size() > MaxPatternLength)
				return false;
			std::fill(next.begin(), next.end(), uint8_t{ 0 });
			next[text.size()] = 1;
			for (size_t p = pattern.size(); p > 0;) {
				--p;
				const char token = pattern[p];
				const bool directories = token == '\\' && p >= 2 && pattern[p - 1] == '*' && pattern[p - 2] == '*';
				const bool recursive = token == '*' && p > 0 && pattern[p - 1] == '*';
				if (directories)
					p -= 2;
				else if (recursive)
					--p;
				std::fill(current.begin(), current.end(), uint8_t{ 0 });
				bool directorySuffix = false;
				for (size_t i = text.size() + 1; i-- > 0;) {
					if (directories) {
						directorySuffix = directorySuffix || (i < text.size() && text[i] == '\\' && next[i + 1]);
						current[i] = next[i] || directorySuffix;
					} else if (token == '*')
						current[i] = next[i] || (i < text.size() && (recursive || text[i] != '\\') && current[i + 1]);
					else if (i < text.size())
						current[i] = next[i + 1] && (token == '?' ? text[i] != '\\' : token == text[i]);
				}
				next.swap(current);
			}
			return next[0] != 0;
		}

	private:
		std::string_view text;
		std::vector<uint8_t> next, current;
	};

	/** @brief Removes the optional PBR path prefix before applying shared pack rules. */
	inline std::string NormalizePath(std::string path)
	{
		path = Util::NormalizeTexturePath(std::move(path));
		if (path.starts_with("textures\\pbr\\"))
			path.erase(9, 4);
		return path;
	}

	/** @brief Filters lighting variants without changing the native technique field. */
	inline bool IsEligible(uint32_t descriptor, bool allowForward)
	{
		constexpr uint32_t excluded = (1u << 2) | (1u << 6) | ShaderFlag | (1u << 18) | (1u << 30) | (1u << 31);
		if ((descriptor & excluded) || (!allowForward && !(descriptor & (1u << 4))))
			return false;
		switch ((descriptor >> 24) & 0x3F) {
		case 0:
		case 1:
		case 2:
		case 3:
		case 7:
		case 11:
		case 12:
		case 14:
			return true;
		default:
			return false;
		}
	}
}
