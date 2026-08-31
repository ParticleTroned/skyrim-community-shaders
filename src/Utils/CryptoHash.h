#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace Util::CryptoHash
{
	using Sha256 = std::array<std::byte, 32>;

	Sha256 Sha256Bytes(std::span<const std::byte> a_bytes);
	inline Sha256 Sha256Bytes(std::string_view a_value)
	{
		return Sha256Bytes(std::as_bytes(std::span(a_value.data(), a_value.size())));
	}
	std::string ToHex(const Sha256& a_digest);
	inline std::string Sha256Hex(std::string_view a_value)
	{
		return ToHex(Sha256Bytes(a_value));
	}
}
