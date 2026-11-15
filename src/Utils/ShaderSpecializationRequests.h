#pragma once

#include <compare>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Util
{
	/** Stable discovery identities; persisted requests never supply paths or compiler macros. */
	enum class SpecializationFamily : uint8_t
	{
		Lighting,
		Effect,
		Composite
	};

	struct ShaderSpecializationRequest
	{
		SpecializationFamily family;
		uint32_t descriptor;
		auto operator<=>(const ShaderSpecializationRequest&) const = default;
	};

	/** Bounded discovery history. Pixel requests must also match a current startup base before replay. */
	class ShaderSpecializationRequests
	{
	public:
		static constexpr size_t kMaximumRequests = 8192;
		static constexpr size_t kMaximumFileBytes = 512 * 1024;

		bool Add(ShaderSpecializationRequest request)
		{
			if (!Valid(request) || requests.size() >= kMaximumRequests)
				return false;
			return requests.insert(request).second;
		}

		std::vector<ShaderSpecializationRequest> Snapshot() const { return { requests.begin(), requests.end() }; }
		size_t Size() const { return requests.size(); }

		/** Rejects incompatible or malformed input atomically, preserving the previous history. */
		void Load(std::string_view text, std::string_view runtime)
		{
			if (text.size() > kMaximumFileBytes)
				throw std::runtime_error("specialization history exceeds its size limit");
			const auto document = nlohmann::json::parse(text, [](int depth, nlohmann::json::parse_event_t, nlohmann::json&) {
				if (depth > 8)
					throw std::runtime_error("specialization history exceeds its nesting limit");
				return true;
			});
			if (!document.is_object() || !document.contains("version") || !document["version"].is_number_unsigned() || document["version"] != 1u ||
				document.value("runtime", std::string{}) != runtime || !document.contains("requests") ||
				!document["requests"].is_array() || document["requests"].size() > kMaximumRequests)
				throw std::runtime_error("incompatible specialization history");
			ShaderSpecializationRequests replacement;
			for (const auto& entry : document["requests"]) {
				if (!entry.is_array() || entry.size() != 2 || !entry[0].is_number_unsigned() ||
					!entry[1].is_number_unsigned() || entry[0].get<uint64_t>() > 2 || entry[1].get<uint64_t>() > UINT32_MAX)
					throw std::runtime_error("invalid specialization request");
				const ShaderSpecializationRequest request{ static_cast<SpecializationFamily>(entry[0].get<uint8_t>()), entry[1].get<uint32_t>() };
				if (!Valid(request))
					throw std::runtime_error("invalid specialization descriptor");
				replacement.Add(request);
			}
			requests.swap(replacement.requests);
		}

		std::string Serialize(std::string_view runtime) const
		{
			auto entries = nlohmann::json::array();
			for (const auto& request : requests)
				entries.push_back({ static_cast<uint8_t>(request.family), request.descriptor });
			return nlohmann::json{ { "version", 1 }, { "runtime", runtime }, { "requests", std::move(entries) } }.dump();
		}

	private:
		static bool Valid(ShaderSpecializationRequest request)
		{
			if (request.family == SpecializationFamily::Composite)
				return request.descriptor < 8 && (request.descriptor & 3) != 0;
			return request.family == SpecializationFamily::Lighting || request.family == SpecializationFamily::Effect;
		}
		std::set<ShaderSpecializationRequest> requests;
	};
}
