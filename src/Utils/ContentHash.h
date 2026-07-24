#pragma once

// Fast, non-cryptographic content hashing for shader-cache inputs. The cache
// already trusts files installed under Data/ShaderCache, so collision
// resistance against an attacker is not required here.

#include <xxhash.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

namespace Util::ContentHash
{
	struct Hash128
	{
		uint64_t high = 0;
		uint64_t low = 0;

		bool operator==(const Hash128&) const = default;

		std::string ToHex() const
		{
			return std::format("{:016x}{:016x}", high, low);
		}
	};

	inline Hash128 HashBytes(const void* a_data, size_t a_size)
	{
		const XXH128_hash_t hash = XXH3_128bits(a_data, a_size);
		return Hash128{ hash.high64, hash.low64 };
	}

	inline Hash128 HashString(std::string_view a_string)
	{
		return HashBytes(a_string.data(), a_string.size());
	}

	inline Hash128 CombineHashes(const Hash128& a_left, const Hash128& a_right)
	{
		const std::array<uint64_t, 4> values{
			a_left.high,
			a_left.low,
			a_right.high,
			a_right.low
		};
		return HashBytes(values.data(), values.size() * sizeof(uint64_t));
	}

	// Normalize CRLF to LF so Git checkout settings do not invalidate an
	// otherwise identical prebuilt cache.
	inline std::optional<Hash128> HashFile(const std::filesystem::path& a_path)
	{
		std::ifstream stream(a_path, std::ios::binary);
		if (!stream.is_open())
			return std::nullopt;

		std::string raw(
			(std::istreambuf_iterator<char>(stream)),
			std::istreambuf_iterator<char>());
		if (stream.bad())
			return std::nullopt;

		std::string normalized;
		normalized.reserve(raw.size());
		for (size_t index = 0; index < raw.size(); ++index) {
			if (raw[index] == '\r' && index + 1 < raw.size() && raw[index + 1] == '\n')
				continue;
			normalized.push_back(raw[index]);
		}
		return HashString(normalized);
	}
}
