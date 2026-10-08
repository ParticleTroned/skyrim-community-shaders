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
		return {
			{ "contractMajor", 1 }, { "clientId", "devbench.capture" }, { "commandId", id }, { "action", "capture" },
			{ "useSettings", false }, { "referenceOutputPath", Util::PathToUtf8(path) },
			{ "capture", { { "source", { { "kind", vr ? "hmd_submission" : "desktop_mirror" }, { "fallback", "reject" } } },
							 { "outputs", json::array({ { { "view", vr ? "side_by_side" : "source_native" },
											  { "encoding", { { "format", "png" }, { "colourContract", "sdr_srgb" } } } } }) },
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
