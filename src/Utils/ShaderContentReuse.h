#pragma once

#include "Utils/CryptoHash.h"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>

namespace Util::ShaderContentReuse
{
	/** Captured code, runtime/ABI and external-contract identities for one shader. */
	struct Identity
	{
		std::string key;
		std::string abi;
		std::string compatibility;
		bool operator==(const Identity&) const = default;
	};

	/** Binds a captured content identity to the exact persisted bytecode. */
	struct Record
	{
		Identity identity;
		std::string bytecodeHash;
		bool operator==(const Record&) const = default;
	};

	inline bool IsHexDigest(std::string_view a_text, size_t a_length)
	{
		return a_text.size() == a_length && std::ranges::all_of(a_text, [](char a_char) {
			return (a_char >= '0' && a_char <= '9') || (a_char >= 'a' && a_char <= 'f');
		});
	}

	/** Rejects malformed optional metadata without affecting legacy cache entries. */
	inline std::optional<Record> Parse(const nlohmann::json& a_json)
	{
		if (!a_json.is_object())
			return std::nullopt;
		for (const auto* field : { "key", "abi", "compatibility", "bytecodeHash" }) {
			if (!a_json.contains(field) || !a_json[field].is_string())
				return std::nullopt;
		}
		Record result{ { a_json["key"].get<std::string>(), a_json["abi"].get<std::string>(), a_json["compatibility"].get<std::string>() }, a_json["bytecodeHash"].get<std::string>() };
		if (!IsHexDigest(result.identity.key, 32) || !IsHexDigest(result.identity.abi, 32) ||
			!IsHexDigest(result.identity.compatibility, 64) || !IsHexDigest(result.bytecodeHash, 64))
			return std::nullopt;
		return result;
	}

	/** Serializes the record independently of the cache container format. */
	inline nlohmann::json Serialize(const Record& a_record)
	{
		return { { "key", a_record.identity.key }, { "abi", a_record.identity.abi },
			{ "compatibility", a_record.identity.compatibility }, { "bytecodeHash", a_record.bytecodeHash } };
	}

	/** Called with the same immutable bytes passed to the cache writer. */
	inline Record Bind(const Identity& a_identity, std::string_view a_bytecode)
	{
		return { a_identity, CryptoHash::Sha256Hex(a_bytecode) };
	}

	/** Pack callers separately validate overlapping external compatibility ranges. */
	inline bool Matches(const Record& a_record, const Identity& a_requested, bool a_exactCompatibility = true)
	{
		return a_record.identity.key == a_requested.key && a_record.identity.abi == a_requested.abi &&
		       (!a_exactCompatibility || a_record.identity.compatibility == a_requested.compatibility);
	}

	/** Detects interrupted overwrites even when file timestamps are unchanged. */
	inline bool Verify(const Record& a_record, std::string_view a_bytecode)
	{
		return a_record.bytecodeHash == CryptoHash::Sha256Hex(a_bytecode);
	}
}
