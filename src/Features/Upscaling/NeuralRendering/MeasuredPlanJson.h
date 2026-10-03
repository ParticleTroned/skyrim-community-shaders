#pragma once

#include "MeasuredPlanPolicy.h"
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace NeuralRendering::MeasuredPlan
{
	/** Match geometry across warm-up; the recorded cost key retains actual reset/rebuild flags. */
	inline std::string CalibrationKey(nlohmann::json key)
	{
		if (!key.is_array() || key.empty() || key.size() > kMaximumRegionEvaluations)
			throw std::invalid_argument("expected bounded final-plan key");
		for (auto& region : key) {
			if (!region.is_object() || !region.contains("mode") || !region["mode"].is_string())
				throw std::invalid_argument("calibration requires an attributed rendering mode");
			region.erase("resourceRebuild");
			region.erase("effectiveReset");
		}
		return key.dump();
	}

	/** Bounded explicit experiment; never supplies costs or relaxes profile qualification. */
	struct Calibration
	{
		static constexpr std::uint32_t kMaximumFrames = 600;
		static constexpr std::uint64_t kMaximumMilliseconds = 60000;
		std::string identity, key;
		std::uint32_t remaining = 0;
		std::optional<std::uint32_t> lastFrame;
		std::optional<std::uint64_t> generation;
		std::uint64_t startedMilliseconds = 0;
		const char* reason = "inactive";

		[[nodiscard]] Decision Select(std::span<const Candidate> candidates, std::string_view currentIdentity,
			std::uint32_t frame, std::uint64_t epoch, std::uint64_t milliseconds)
		{
			const auto stop = [&](const char* why) {
				remaining = 0;
				reason = why;
				return Decision{ 0, why };
			};
			if (!remaining)
				return { 0, reason };
			if (milliseconds < startedMilliseconds || milliseconds - startedMilliseconds >= kMaximumMilliseconds)
				return stop("calibration_expired");
			if (currentIdentity != identity || (generation && *generation != epoch))
				return stop("calibration_identity_changed");
			if (lastFrame && frame <= *lastFrame)
				return stop("calibration_source_not_unique");
			lastFrame = frame;
			generation = epoch;
			for (std::size_t i = 0; i < candidates.size(); ++i) {
				const auto& candidate = candidates[i];
				if (!candidate.coverageValid || candidate.capacityRejected)
					continue;
				std::string currentKey;
				try {
					currentKey = CalibrationKey(nlohmann::json::parse(candidate.key));
				} catch (const std::exception&) {
					return stop("calibration_key_unavailable");
				}
				if (currentKey == key) {
					--remaining;
					reason = remaining ? "calibration_running" : "calibration_complete";
					return { i, "calibration_candidate" };
				}
			}
			return stop("calibration_candidate_unavailable");
		}
	};

	/** Validate a manually supplied experiment; evidence declarations are not independent proof. */
	inline Profile ReadProfile(const nlohmann::json& value)
	{
		if (!value.at("schemaVersion").is_number_integer() || value.at("schemaVersion") != 1 || value.at("scope") != "experimental" || !value.at("identity").is_object())
			throw std::invalid_argument("unsupported measured-plan profile");
		const auto& gates = value.at("qualification");
		for (const auto* gate : { "matchedBaselineBrackets", "uniqueFinalizedSources", "stableBaselines", "controlledSceneContent",
				 "heldOutValidation", "transitionCosts", "cpuCriticalPath", "nativeResidency", "unchangedQualityAndCadence" })
			if (!gates.at(gate).is_boolean() || !gates.at(gate).get<bool>())
				throw std::invalid_argument(std::string("unqualified measured-plan profile: ") + gate);
		std::vector<std::string> hashes;
		for (const auto* group : { "baselineEvidence", "heldOutEvidence" }) {
			const auto& evidence = value.at(group);
			if (!evidence.is_array() || evidence.empty() || evidence.size() > 64)
				throw std::invalid_argument("bounded evidence hashes are required");
			for (const auto& item : evidence) {
				const auto hash = item.get<std::string>();
				if (hash.size() != 64 || !std::ranges::all_of(hash, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }) ||
					std::ranges::find(hashes, hash) != hashes.end())
					throw std::invalid_argument("evidence hashes must be unique SHA-256 values");
				hashes.push_back(hash);
			}
		}
		const auto positiveInteger = [](const nlohmann::json& field) {
			if (!field.is_number_unsigned() || field.get<std::uint64_t>() == 0)
				throw std::invalid_argument("expected positive unsigned integer");
			return field.get<std::uint64_t>();
		};
		const auto dwell = positiveInteger(value.at("dwellFrames"));
		if (dwell > 600)
			throw std::invalid_argument("dwellFrames exceeds 600");
		Profile result{ value.at("identity").dump(), {}, static_cast<std::uint32_t>(dwell), positiveInteger(value.at("residentBudgetBytes")) };
		const auto& observations = value.at("observations");
		if (!observations.is_array() || observations.empty() || observations.size() > 256)
			throw std::invalid_argument("one to 256 measured final-plan observations required");
		for (const auto& row : observations) {
			for (const auto* field : { "gpuLowerMs", "gpuUpperMs", "gpuTailMs", "cpuCriticalUpperMs", "transitionUpperMs" })
				if (!row.at(field).is_number())
					throw std::invalid_argument("measured durations must be numeric");
			if (!row.at("key").is_array() || row.at("key").empty() || row.at("key").size() > kMaximumRegionEvaluations)
				throw std::invalid_argument("expected exact final-plan key");
			const auto samples = positiveInteger(row.at("samples"));
			if (samples > UINT32_MAX)
				throw std::invalid_argument("sample count exceeds uint32");
			result.observations.push_back({ row.at("key").dump(), { row.at("gpuLowerMs").get<double>(), row.at("gpuUpperMs").get<double>(),
																	  row.at("gpuTailMs").get<double>(), row.at("cpuCriticalUpperMs").get<double>(), row.at("transitionUpperMs").get<double>(),
																	  positiveInteger(row.at("residentBytes")), static_cast<std::uint32_t>(samples) } });
		}
		if (!result.Valid())
			throw std::invalid_argument("invalid, duplicate or incomplete measured costs");
		return result;
	}
}
