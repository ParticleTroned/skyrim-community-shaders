#pragma once

#include <cstdint>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace Diagnostics::SunReceiverTrace
{
	inline constexpr std::uint32_t MaximumCaptureFrames = 3600;
	inline constexpr std::uint64_t MaximumCaptureMilliseconds = 30000;

	struct CaptureRequest
	{
		bool allGeometry{}, repair{};
		std::uint32_t maximumFrames{};
		std::string traceId;
		std::uint32_t geometryIndex{};
		std::uintptr_t expectedGeometry{}, expectedProperty{};
	};

	/** Reject negative, floating or oversized values before any narrowing conversion. */
	inline std::uint64_t UnsignedArgument(const nlohmann::json& args, const char* key,
		std::uint64_t minimum, std::uint64_t maximum)
	{
		const auto& value = args.at(key);
		if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<std::int64_t>() < 0))
			throw std::invalid_argument(std::string(key) + " must be a nonnegative integer");
		const auto number = value.get<std::uint64_t>();
		if (number < minimum || number > maximum)
			throw std::invalid_argument(std::string(key) + " is outside the supported range");
		return number;
	}

	/** Validate capture mode and bounds before binding native object identities. */
	inline CaptureRequest ParseStart(const nlohmann::json& args)
	{
		const auto mode = args.value("mode", std::string("observe"));
		const auto scope = args.value("scope", std::string("selected"));
		const auto id = args.at("traceId").get<std::string>();
		const auto frames = UnsignedArgument(args, "maximumFrames", 1, MaximumCaptureFrames);
		if (id.empty() || id.size() > 64 ||
			(mode != "observe" && mode != "repair") || (scope != "selected" && scope != "all"))
			throw std::invalid_argument("Require 1..3600 frames, a 1..64 byte traceId, mode observe/repair and scope selected/all");
		for (const auto* key : { "formId", "geometryIndex", "expectedGeometry", "expectedProperty" })
			if (args.contains(key) == (scope == "all"))
				throw std::invalid_argument("Selected scope requires mesh identity; all scope must omit it");
		CaptureRequest request{ scope == "all", mode == "repair", static_cast<std::uint32_t>(frames), id };
		if (!request.allGeometry) {
			request.geometryIndex = static_cast<std::uint32_t>(UnsignedArgument(args, "geometryIndex", 0, 255));
			request.expectedGeometry = UnsignedArgument(args, "expectedGeometry", 1, std::numeric_limits<std::uintptr_t>::max());
			request.expectedProperty = UnsignedArgument(args, "expectedProperty", 1, std::numeric_limits<std::uintptr_t>::max());
		}
		return request;
	}
}
