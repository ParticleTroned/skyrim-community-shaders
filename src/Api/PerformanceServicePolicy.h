#pragma once

#include <algorithm>
#include <array>
#include <nlohmann/json.hpp>
#include <string_view>

namespace CSX::Api::PerformancePolicy
{
	inline constexpr std::array actions{
		"status", "start_quick_scan", "start_quick_scan_batch", "start_feature_cost",
		"start_feature_costs", "start_upscaling_sweep", "set_feature_enabled", "cancel",
		"results_status", "refresh_results", "load_result", "export_result", "export_session", "retry_results"
	};

	inline bool ReadOnly(std::string_view action) { return action == "status" || action == "results_status"; }

	/** Fail before dispatch for unknown actions, mistyped values or out-of-scope fields. */
	inline const char* Validate(const nlohmann::json& request)
	{
		if (!request.is_object())
			return "Request must be an object.";
		if (!request.contains("contractMajor") || !request["contractMajor"].is_number_integer() || request["contractMajor"] != 1)
			return "contractMajor must be 1.";
		for (const auto* key : { "clientId", "commandId", "action" }) {
			if (!request.contains(key) || !request[key].is_string() || request[key].get_ref<const std::string&>().empty() ||
				request[key].get_ref<const std::string&>().size() > 128)
				return "Bounded action, clientId and commandId strings are required.";
		}
		const auto action = request["action"].get<std::string_view>();
		if (std::find(actions.begin(), actions.end(), action) == actions.end())
			return "Unsupported performance action.";
		for (const auto& [key, value] : request.items()) {
			if (value.is_string() && value.get_ref<const std::string&>().find('\0') != std::string::npos)
				return "Strings cannot contain NUL bytes.";
			if (key == "contractMajor" || key == "action" || key == "clientId" || key == "commandId")
				continue;
			if (key == "expectedBuildId") {
				if (!value.is_string() || value.get_ref<const std::string&>().empty() || value.get_ref<const std::string&>().size() > 128)
					return "Invalid expectedBuildId.";
				continue;
			}
			const bool boolean = (key == "recordFpsVR" && (action == "start_quick_scan" || action == "start_quick_scan_batch")) ||
			                     (key == "enabled" && action == "set_feature_enabled") || (key == "comparison" && action == "load_result");
			const bool text = (key == "featureShortName" && (action == "start_feature_cost" || action == "set_feature_enabled")) ||
			                  ((key == "matrix" || key == "dlssPreset") && action == "start_upscaling_sweep") ||
			                  (key == "resultId" && (action == "load_result" || action == "export_result")) ||
			                  ((key == "comparisonId" || key == "format") && action == "export_result") ||
			                  (key == "directory" && (action == "export_result" || action == "export_session"));
			if (boolean && value.is_boolean())
				continue;
			if (text && value.is_string() && value.get_ref<const std::string&>().size() <= (key == "directory" ? 4096u : 128u))
				continue;
			return "Unexpected or mistyped action parameter.";
		}
		if ((action == "start_feature_cost" || action == "set_feature_enabled") &&
			(!request.contains("featureShortName") || request["featureShortName"] == ""))
			return "featureShortName is required.";
		if (action == "set_feature_enabled" && !request.contains("enabled"))
			return "enabled is required.";
		if ((action == "load_result" || action == "export_result") &&
			(!request.contains("resultId") || request["resultId"] == ""))
			return "resultId is required.";
		if ((action == "export_result" || action == "export_session") &&
			(!request.contains("directory") || request["directory"] == ""))
			return "An explicit export directory is required.";
		if (request.contains("matrix") && request["matrix"] != "auto" && request["matrix"] != "nvidia" && request["matrix"] != "amd")
			return "Invalid matrix.";
		if (request.contains("format") && request["format"] != "csv" && request["format"] != "html" && request["format"] != "both")
			return "Invalid format.";
		return nullptr;
	}
}
