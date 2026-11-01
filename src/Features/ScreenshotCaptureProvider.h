#pragma once

#include "Features/ScreenshotApiPolicy.h"
#include "Features/ScreenshotBurstPolicy.h"
#include "Features/ScreenshotReferencePolicy.h"
#include "Features/ScreenshotStorageSecurity.h"

#include <algorithm>
#include <format>
#include <vector>

namespace CSX::Screenshot
{
	/** Publication options shared with the existing manifest worker, without a second capture queue. */
	struct ProviderBurst
	{
		uint64_t runId = 0;
		nlohmann::json options;
		std::filesystem::path manifestPath;
		std::shared_ptr<ScreenshotStorage::DirectoryLease> manifestDirectory;
	};

	inline std::string ProviderFrameName(std::string_view prefix, uint32_t ordinal)
	{
		return std::format("{}_{:04}.png", prefix, ordinal - 1);
	}

	/** Validate host parameters before admission and preserve the host request ID as the command ID. */
	inline nlohmann::json ProviderBurstCommand(const nlohmann::json& request, bool vr)
	{
		using json = nlohmann::json;
		const auto count = ScreenshotBurst::Read(request, "count", 1, 600);
		const auto interval = request.contains("everyNFrames") ? ScreenshotBurst::Read(request, "everyNFrames", 1, 600) : 1;
		if (!ScreenshotPolicy::IsGameFrameScheduleWithinLimit(0, static_cast<uint32_t>(interval), static_cast<uint32_t>(count)))
			throw std::invalid_argument("burst cadence exceeds the screenshot API's 216000-frame span limit");
		const auto prefix = request.at("prefix").get<std::string>();
		if (prefix.size() > 128 || !ScreenshotPolicy::IsSafeWindowsFilenameSegment(prefix))
			throw std::invalid_argument("burst prefix must be a safe filename stem of at most 128 characters");
		const auto directoryText = request.at("outputDir").get<std::string>();
		const auto directory = std::filesystem::u8path(directoryText).lexically_normal();
		if (directoryText.find('\0') != std::string::npos || !directory.is_absolute())
			throw std::invalid_argument("burst outputDir must be absolute");
		if ((request.contains("scale") && (!request.at("scale").is_number() || request.at("scale") != 1)) ||
			(request.contains("subrect") && !request.at("subrect").is_null()))
			throw std::invalid_argument("burst supports only whole native frames with scale=1");
		const bool excludeUi = request.value("excludeUi", true);
		const auto manifestText = request.value("manifestPath", Util::PathToUtf8(directory / (prefix + "_manifest.json")));
		const auto manifest = std::filesystem::u8path(manifestText).lexically_normal();
		const auto filename = Util::PathToUtf8(manifest.filename());
		if (manifestText.find('\0') != std::string::npos || !manifest.is_absolute() || filename.size() > 200 ||
			!ScreenshotPolicy::IsSafeWindowsFilenameSegment(filename) || Util::ToLowerAscii(manifest.extension().string()) != ".json")
			throw std::invalid_argument("burst manifestPath must be an absolute JSON path with a safe filename");
		auto command = ReferenceCommand({ { "requestId", request.at("requestId") },
											{ "outputPath", Util::PathToUtf8(directory / ProviderFrameName(prefix, 1)) } },
			vr);
		command["clientId"] = "devbench.capture.burst";
		command["action"] = "sequence_start";
		command["sequence"] = {
			{ "useSettings", false }, { "frameCount", count },
			{ "capture", command.at("capture") },
			{ "schedule", { { "basis", "game_frames" }, { "intervalFrames", interval }, { "pausePolicy", "hold" } } },
			{ "backpressure", { { "policy", "skip" }, { "maximumConsecutiveSkips", 10 } } },
			{ "failurePolicy", "continue" }, { "packaging", { { "frameManifest", true } } }
		};
		command.erase("capture");
		command.erase("useSettings");
		command.erase("referenceOutputPath");
		command["providerBurst"] = { { "requestId", request.at("requestId") }, { "prefix", prefix },
			{ "outputDir", Util::PathToUtf8(directory) }, { "manifestPath", Util::PathToUtf8(manifest) },
			{ "excludeUi", excludeUi }, { "everyNFrames", interval } };
		return command;
	}

	/** Convert immutable child receipts; file paths refer directly to the original encoder publications. */
	inline nlohmann::json ProviderManifest(const nlohmann::json& document, const ProviderBurst& provider)
	{
		using json = nlohmann::json;
		const auto requested = document.at("counts").at("requested").get<uint32_t>();
		const auto scheduled = document.at("counts").at("scheduled").get<uint32_t>();
		if (scheduled > requested || document.at("children").size() != scheduled)
			throw std::runtime_error("burst manifest is missing scheduled frame receipts");
		json result = { { "requestId", provider.options.at("requestId") }, { "apiRequestId", document.at("requestId") },
			{ "width", 0 }, { "height", 0 }, { "format", "png" }, { "uiExcluded", false },
			{ "everyNFrames", provider.options.at("everyNFrames") }, { "frames", json::array() }, { "dropped", json::array() },
			{ "state", document.at("terminalOutcome") }, { "counts", document.at("counts") },
			{ "outputDir", provider.options.at("outputDir") }, { "unwritten", json::array() },
			{ "unscheduled", requested - scheduled },
			{ "clock", "monotonic; gameTimeMs aliases wallMs, not simulation time" } };
		uint64_t firstTime = 0;
		uint64_t previousTime = 0;
		uint32_t previousOrdinal = 0;
		std::vector<const json*> ordered;
		for (const auto& child : document.at("children"))
			ordered.push_back(&child);
		std::ranges::sort(ordered, [](const json* left, const json* right) { return left->at("ordinal") < right->at("ordinal"); });
		for (const auto* entry : ordered) {
			const auto& child = *entry;
			const auto ordinal = child.at("ordinal").get<uint32_t>();
			if (ordinal <= previousOrdinal || ordinal > requested)
				throw std::runtime_error("burst manifest contains invalid frame ordinals");
			previousOrdinal = ordinal;
			const auto& artifacts = child.at("artifacts");
			if (artifacts.empty()) {
				result[child.at("state") == "dropped" ? "dropped" : "unwritten"].push_back({ { "index", ordinal - 1 }, { "gameFrame", child.at("scheduledEngineFrame") },
					{ "reason", child.at("state") } });
				continue;
			}
			const auto& artifact = artifacts.front();
			const auto& actual = artifact.at("actual");
			const auto expectedName = ProviderFrameName(provider.options.at("prefix").get<std::string>(), ordinal);
			if (artifacts.size() != 1 || !artifact.value("committed", false) || artifact.at("path") != expectedName ||
				actual.at("format") != "png" || actual.at("width").get<uint32_t>() == 0 || actual.at("height").get<uint32_t>() == 0)
				throw std::runtime_error("burst frame lacks a committed native PNG receipt");
			const auto& acquisition = child.at("actual").at("acquisition");
			const auto timestamp = acquisition.at("monotonicTimestampUs").get<uint64_t>();
			if (result["frames"].empty()) {
				result["width"] = actual.at("width");
				result["height"] = actual.at("height");
				firstTime = timestamp;
			} else if (result["width"] != actual.at("width") || result["height"] != actual.at("height") || timestamp < previousTime) {
				throw std::runtime_error("burst frame dimensions or acquisition clock changed");
			}
			previousTime = timestamp;
			const auto milliseconds = (timestamp - firstTime) / 1000.0;
			result["frames"].push_back({ { "index", ordinal - 1 },
				{ "file", expectedName },
				{ "gameFrame", acquisition.at("engineFrame") }, { "wallMs", milliseconds }, { "gameTimeMs", milliseconds } });
		}
		if (result["frames"].size() != document.at("counts").at("written").get<uint32_t>())
			throw std::runtime_error("burst manifest frame count differs from the API receipt");
		return result;
	}

	/** Both provider status and events derive from the same retained API receipt. */
	inline nlohmann::json ProviderBurstStatus(const nlohmann::json& receipt)
	{
		using json = nlohmann::json;
		const auto& provider = receipt.at("captureProvider");
		const auto& counts = receipt.at("counts");
		const bool done = !receipt.at("terminalUtc").is_null();
		const auto state = receipt.at("state").get<std::string>();
		const bool committed = provider.value("committed", false);
		const bool ok = done && committed && counts.at("written").get<uint32_t>() != 0 &&
		                counts.at("failed").get<uint32_t>() == 0 && counts.at("cancelled").get<uint32_t>() == 0 &&
		                (state == "completed" || state == "completed_with_warnings");
		json result = { { "provider", "communityshaders" }, { "requestId", receipt.at("commandId") },
			{ "apiRequestId", receipt.at("requestId") }, { "runId", provider.at("runId") },
			{ "active", !done }, { "done", done }, { "ok", ok }, { "state", state },
			{ "requested", counts.at("requested") }, { "saved", counts.at("written") }, { "captured", counts.at("written") },
			{ "failed", counts.at("failed") }, { "dropped", counts.at("dropped") }, { "cancelled", counts.at("cancelled") },
			{ "uiExcluded", false }, { "width", provider.value("width", 0u) }, { "height", provider.value("height", 0u) },
			{ "manifest", committed ? provider.at("manifestPath") : json(nullptr) },
			{ "manifestPath", committed ? provider.at("manifestPath") : json(nullptr) } };
		if (done && !ok) {
			const auto error = receipt.value("error", json(nullptr));
			result["error"] = error.is_object() ? error.value("message", state) : state;
		}
		return result;
	}
}
