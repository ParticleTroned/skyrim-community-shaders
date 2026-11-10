#pragma once

#include "Utils/StringUtils.h"
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace CSX::Screenshot
{
	/** Translate the host capture contract into one explicit native PNG request. */
	inline nlohmann::json ReferenceCommand(const nlohmann::json& request, bool vr)
	{
		using json = nlohmann::json;
		const auto text = request.at("outputPath").get<std::string>();
		const auto id = request.at("requestId").get<std::string>();
		auto path = std::filesystem::path(std::u8string(text.begin(), text.end())).lexically_normal();
		if (text.find('\0') != std::string::npos || !path.is_absolute() ||
			Util::ToLowerAscii(path.extension().string()) != ".png" || id.empty() || id.size() > 128)
			throw std::invalid_argument("reference capture requires an absolute PNG path and bounded requestId");
		path = std::filesystem::weakly_canonical(path.parent_path()) / path.filename();
		json output = { { "view", vr ? "side_by_side" : "source_native" },
			{ "encoding", { { "format", "png" }, { "colourContract", "sdr_srgb" } } } };
		if (const auto subrect = request.find("subrect"); subrect != request.end()) {
			if (!subrect->is_object())
				throw std::invalid_argument("reference subrect must be an object");
			for (const auto* field : { "x", "y", "w", "h" }) {
				const auto value = subrect->find(field);
				if (value != subrect->end() && !value->is_number())
					throw std::invalid_argument("reference subrect coordinates must be numbers");
			}
			// The shared capture validator checks finite bounds before queue admission.
			output["crop"] = {
				{ "x", subrect->value("x", 0.0f) }, { "y", subrect->value("y", 0.0f) },
				{ "width", subrect->value("w", 1.0f) }, { "height", subrect->value("h", 1.0f) }
			};
		}
		return {
			{ "contractMajor", 1 }, { "clientId", "devbench.capture" }, { "commandId", id }, { "action", "capture" },
			{ "useSettings", false }, { "referenceOutputPath", Util::PathToUtf8(path) },
			{ "capture", { { "source", { { "kind", vr ? "hmd_submission" : "desktop_mirror" }, { "fallback", "reject" } } },
							 { "outputs", json::array({ output }) },
							 { "destination", { { "policy", "absolute" }, { "directory", Util::PathToUtf8(path.parent_path()) }, { "overwrite", "never" } } },
							 { "clipboard", "none" } } }
		};
	}

	/** Produce capture.ready only from the retained terminal publication receipt. */
	inline nlohmann::json ReferenceCompletion(const nlohmann::json& receipt)
	{
		using json = nlohmann::json;
		json event = { { "requestId", receipt.at("commandId") }, { "ok", false }, { "uiExcluded", false } };
		const auto& artifacts = receipt.at("artifacts");
		const auto state = receipt.at("state").get<std::string>();
		if ((state == "completed" || state == "completed_with_warnings") && artifacts.size() == 1 &&
			artifacts.front().value("committed", false)) {
			const auto& artifact = artifacts.front();
			event.update(artifact);
			event["width"] = artifact.at("actual").at("width");
			event["height"] = artifact.at("actual").at("height");
			event["ok"] = true;
		} else {
			const auto error = receipt.value("error", json(nullptr));
			event["error"] = error.is_object() ? error.value("message", state) : state;
		}
		return event;
	}
}
