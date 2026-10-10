#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "LifetimeDiagnostics.h"

#	include <nlohmann/json.hpp>
#	include <string>

namespace NeuralRendering
{
	/** Preserve absent completion and opaque identities without lossy numeric coercion. */
	inline nlohmann::json LifetimeFenceJson(const LifetimeFenceSnapshot& value)
	{
		return {
			{ "device", std::to_string(value.device) }, { "queue", std::to_string(value.queue) },
			{ "device11", std::to_string(value.device11) }, { "context11", std::to_string(value.context11) },
			{ "fence", std::to_string(value.fence) }, { "issued", std::to_string(value.issued) },
			{ "completed", value.completedKnown ? nlohmann::json(std::to_string(value.completed)) : nlohmann::json(nullptr) },
			{ "contextFences", { std::to_string(value.contexts[0]), std::to_string(value.contexts[1]), std::to_string(value.contexts[2]) } },
			{ "initialized", value.initialized }, { "recording", value.recording },
			{ "completedKnown", value.completedKnown }, { "deviceRemoved", value.deviceRemoved }
		};
	}

	/** Serialize one completed CPU record with explicit native call outcomes. */
	inline nlohmann::json LifetimeRecordJson(const LifetimeRecord& value)
	{
		using json = nlohmann::json;
		const auto rect = [](const ComputeSubrect& r) { return json{ { "x", r.baseX }, { "y", r.baseY }, { "width", r.width }, { "height", r.height } }; };
		const auto identities = [](const auto& values) {
			auto result = json::array();
			for (auto identity : values)
				result.push_back(std::to_string(identity));
			return result;
		};
		auto regions = json::array();
		for (std::size_t index = 0; index < value.regionCount && index < value.regions.size(); ++index) {
			const auto& region = value.regions[index];
			regions.push_back({ { "slot", region.slot }, { "resourceSerial", std::to_string(region.resourceSerial) },
				{ "previousResourceSerial", std::to_string(region.previousResourceSerial) },
				{ "regionIdentity", std::to_string(region.regionIdentity) }, { "previousRegionIdentity", std::to_string(region.previousRegionIdentity) },
				{ "previousFrame", region.previousHistoryValid ? json(region.previousFrame) : json(nullptr) },
				{ "previousSourceFrame", region.previousHistoryValid ? json(region.previousSourceFrame) : json(nullptr) },
				{ "previousHistoryValid", region.previousHistoryValid }, { "context", rect(region.context) },
				{ "previousContext", region.previousHistoryValid ? rect(region.previousContext) : json(nullptr) },
				{ "resources11", identities(region.resources11) }, { "resources12", identities(region.resources12) },
				{ "colorSize", region.colorSize }, { "guideSize", region.guideSize }, { "outputSize", region.outputSize },
				{ "resourcesRebuilt", region.resourcesRebuilt }, { "effectiveReset", region.effectiveReset },
				{ "createAttempted", region.createAttempted }, { "createSucceeded", region.createSucceeded },
				{ "evaluateAttempted", region.evaluateAttempted }, { "evaluateSucceeded", region.evaluateSucceeded } });
		}
		return {
			{ "sequence", value.sequence }, { "backendSerial", std::to_string(value.backendSerial) },
			{ "sourceTransactionId", std::to_string(value.sourceTransactionId) }, { "generation", std::to_string(value.generation) },
			{ "operation", value.operation == LifetimeOperation::Batch ? "batch" : value.operation == LifetimeOperation::BackendRetirement ? "backend_retirement" :
																																			 "slot_retirement" },
			{ "requestFrame", value.requestFrame }, { "sourceFrame", value.sourceFrame },
			{ "modeValue", value.mode ? json(*value.mode) : json(nullptr) }, { "insertionPointValue", value.insertion },
			{ "stageValue", value.stage }, { "result", value.result }, { "succeeded", value.succeeded },
			{ "failureObserved", value.failureObserved }, { "slotMask", value.slotMask },
			{ "before", LifetimeFenceJson(value.before) }, { "after", LifetimeFenceJson(value.after) },
			{ "regions", std::move(regions) }
		};
	}

	/** Diagnostic identity values are decimal strings to survive JSON clients using binary64. */
	inline nlohmann::json LifetimeDiagnosticsJson(const LifetimeSnapshot& value, bool enabled)
	{
		using json = nlohmann::json;
		auto records = json::array();
		for (const auto& record : value.records)
			records.push_back(LifetimeRecordJson(record));
		return {
			{ "schemaVersion", 1 }, { "enabled", enabled }, { "frozen", value.firstFailure.has_value() },
			{ "capacity", LifetimeDiagnostics::kCapacity }, { "totalRecords", value.totalRecords },
			{ "overwrittenRecords", value.overwrittenRecords }, { "diagnosticFailures", value.diagnosticFailures },
			{ "resourceOrder", { "color", "depth", "motion", "output", "controlMask" } },
			{ "scope", "CPU observations; queue acceptance does not prove GPU completion or fault causation" },
			{ "records", std::move(records) },
			{ "firstFailure", value.firstFailure ? LifetimeRecordJson(*value.firstFailure) : json(nullptr) }
		};
	}
}

#endif
