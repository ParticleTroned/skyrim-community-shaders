#pragma once

#include "RenderMap/Collector.h"

#include <array>
#include <nlohmann/json.hpp>
#include <optional>
#include <string_view>

namespace CSX::RenderMap::DevBenchBounds
{
	inline constexpr std::uint64_t kMaximumFrames = 600;
	inline constexpr std::uint64_t kMaximumDurationMs = 10000;
	inline constexpr std::uint64_t kMaximumEvents = 65536;
	inline constexpr std::uint64_t kMaximumBytes = 64ull * 1024ull * 1024ull;
	inline constexpr std::uint32_t kMaximumShaderObservations = 8192;
	inline constexpr std::uint32_t kMaximumStageShaderObservations = 32768;
	inline constexpr std::uint32_t kMaximumResourceObservations = 32768;
	inline constexpr std::uint32_t kMaximumTargetViewObservations = 32768;
	inline constexpr std::uint32_t kMaximumTargetBindingObservations = 32768;
	inline constexpr std::uint32_t kMaximumSceneObjectObservations = 32768;
	inline constexpr std::uint32_t kMaximumGeometryObservations = 65536;
	inline constexpr std::uint32_t kMaximumMaterialStateObservations = 65536;
	inline constexpr std::uint64_t kDefaultFrames = 4;
	inline constexpr std::uint64_t kDefaultDurationMs = 2000;
	inline constexpr std::uint64_t kDefaultEvents = 8192;
	inline constexpr std::uint64_t kDefaultBytes = kMaximumBytes;
	inline constexpr std::uint32_t kDefaultScopeDepth = 8;
	inline constexpr std::uint32_t kDefaultShaderObservations = 64;
	inline constexpr std::uint32_t kDefaultStageShaderObservations = 128;
	inline constexpr std::uint32_t kDefaultResourceObservations = 1024;
	inline constexpr std::uint32_t kDefaultTargetViewObservations = 64;
	inline constexpr std::uint32_t kDefaultTargetBindingObservations = 64;
	inline constexpr std::uint32_t kDefaultSceneObjectObservations = 512;
	inline constexpr std::uint32_t kDefaultGeometryObservations = 1024;
	inline constexpr std::uint32_t kDefaultMaterialStateObservations = 1024;

	struct BoundSpec
	{
		std::string_view field;
		std::uint64_t maximum;
	};

	inline constexpr std::array<BoundSpec, 14> kBounds{ {
		{ "maxFrames", kMaximumFrames },
		{ "maxDurationMs", kMaximumDurationMs },
		{ "maxActivationWaitMs", kMaximumDurationMs },
		{ "maxEvents", kMaximumEvents },
		{ "maxBytes", kMaximumBytes },
		{ "maxScopeDepth", kMaximumScopeDepth },
		{ "maxShaderObservations", kMaximumShaderObservations },
		{ "maxStageShaderObservations", kMaximumStageShaderObservations },
		{ "maxResourceObservations", kMaximumResourceObservations },
		{ "maxTargetViewObservations", kMaximumTargetViewObservations },
		{ "maxTargetBindingObservations", kMaximumTargetBindingObservations },
		{ "maxSceneObjectObservations", kMaximumSceneObjectObservations },
		{ "maxGeometryObservations", kMaximumGeometryObservations },
		{ "maxMaterialStateObservations", kMaximumMaterialStateObservations },
	} };

	struct BoundViolation
	{
		BoundSpec bound;
		bool invalidType;
	};

	/** Validate unconverted JSON values before narrowing or duration conversion. */
	inline std::optional<BoundViolation> Validate(const nlohmann::json& a_args)
	{
		for (const auto& bound : kBounds) {
			const auto found = a_args.find(bound.field);
			if (found == a_args.end())
				continue;
			if (!found->is_number_unsigned())
				return BoundViolation{ bound, true };
			const auto value = found->get<std::uint64_t>();
			if (value == 0 || value > bound.maximum)
				return BoundViolation{ bound, false };
		}
		return std::nullopt;
	}

	/** The exact profile advertised by the optional DevBench adapter. */
	inline CollectorConfig DefaultConfig()
	{
		return {
			.maxFrames = kDefaultFrames,
			.maxEvents = kDefaultEvents,
			.maxBytes = kDefaultBytes,
			.maxDuration = std::chrono::milliseconds(kDefaultDurationMs),
			.maxScopeDepth = static_cast<std::uint8_t>(kDefaultScopeDepth),
			.maxShaderObservations = kDefaultShaderObservations,
			.maxStageShaderObservations = kDefaultStageShaderObservations,
			.maxResourceObservations = kDefaultResourceObservations,
			.maxTargetViewObservations = kDefaultTargetViewObservations,
			.maxTargetBindingObservations = kDefaultTargetBindingObservations,
			.maxSceneObjectObservations = kDefaultSceneObjectObservations,
			.maxGeometryObservations = kDefaultGeometryObservations,
			.maxMaterialStateObservations = kDefaultMaterialStateObservations,
		};
	}
}
