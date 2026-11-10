#pragma once

#include "Features/ScreenshotCaptureProvider.h"

inline void RunScreenshotCaptureProviderTests()
{
	using namespace CSX::Screenshot;
	using json = nlohmann::json;
	const auto check = [](bool condition) {
		if (!condition)
			throw std::runtime_error("capture provider contract regression");
	};
	const auto rejects = [&](auto&& operation) {
		bool failed = false;
		try {
			operation();
		} catch (const std::exception&) {
			failed = true;
		}
		check(failed);
	};
	const auto root = std::filesystem::current_path() / std::filesystem::path(L"capture-\u773c");
	const json request = { { "action", "burst" }, { "requestId", "host-1" },
		{ "outputDir", Util::PathToUtf8(root) }, { "prefix", "frame" }, { "count", 4 }, { "everyNFrames", 3 } };
	const auto command = ProviderBurstCommand(request, true);
	check(command.at("clientId") == "devbench.capture.burst" && command.at("commandId") == "host-1");
	check(command.at("action") == "sequence_start" && command.at("sequence").at("frameCount") == 4);
	check(command.at("sequence").at("schedule").at("intervalFrames") == 3);
	check(command.at("sequence").at("capture").at("outputs").front().at("view") == "side_by_side");
	check(ProviderBurstCommand(request, false).at("sequence").at("capture").at("source").at("kind") == "desktop_mirror");
	check(command.at("sequence").at("capture").at("clipboard") == "none" && !command.at("sequence").contains("burst"));
	check(ProviderFrameName("frame", 1) == "frame_0000.png" && ProviderFrameName("frame", 600) == "frame_0599.png");
	for (const auto& patch : {
			 json{ { "count", 0 } }, json{ { "count", 601 } }, json{ { "count", -1 } }, json{ { "count", 1.5 } },
			 json{ { "count", 600 }, { "everyNFrames", 600 } }, json{ { "everyNFrames", 0 } }, json{ { "everyNFrames", 601 } }, json{ { "everyNFrames", true } },
			 json{ { "prefix", "../escape" } }, json{ { "prefix", "CON" } }, json{ { "prefix", "name:stream" } },
			 json{ { "prefix", "trailing." } }, json{ { "prefix", std::string(129, 'a') } },
			 json{ { "outputDir", "relative" } }, json{ { "manifestPath", "relative.json" } },
			 json{ { "manifestPath", Util::PathToUtf8(root / "NUL.json") } },
			 json{ { "manifestPath", Util::PathToUtf8(root / "frame_0000.png") } },
			 json{ { "scale", 0.5 } }, json{ { "scale", "1" } }, json{ { "subrect", json::object() } },
			 json{ { "excludeUi", "yes" } }, json{ { "requestId", "" } }, json{ { "requestId", std::string(129, 'a') } } }) {
		auto invalid = request;
		invalid.update(patch);
		rejects([&] { ProviderBurstCommand(invalid, true); });
	}
	auto maximum = request;
	maximum.update({ { "count", 600 }, { "everyNFrames", 360 }, { "scale", 1 }, { "subrect", nullptr } });
	check(ProviderBurstCommand(maximum, true).at("sequence").at("frameCount") == 600);
	ProviderBurst provider{ .runId = 7, .options = command.at("providerBurst") };
	const auto frame = [](uint32_t ordinal, uint64_t time) -> json {
		return { { "ordinal", ordinal }, { "state", "completed" }, { "scheduledEngineFrame", 90 + ordinal },
			{ "actual", { { "acquisition", { { "engineFrame", 100 + ordinal }, { "monotonicTimestampUs", time } } } } },
			{ "artifacts", json::array({ { { "committed", true }, { "path", ProviderFrameName("frame", ordinal) },
							   { "actual", { { "format", "png" }, { "width", 200 }, { "height", 100 } } } } }) } };
	};
	json document = { { "requestId", "api-1" }, { "terminalOutcome", "completed_with_warnings" },
		{ "counts", { { "requested", 4 }, { "scheduled", 4 }, { "written", 2 }, { "dropped", 1 }, { "failed", 1 }, { "cancelled", 0 } } },
		{ "children", json::array({ { { "ordinal", 2 }, { "state", "dropped" }, { "scheduledEngineFrame", 102 }, { "artifacts", json::array() } },
						  frame(1, 1000000), frame(3, 1060000),
						  { { "ordinal", 4 }, { "state", "failed" }, { "scheduledEngineFrame", 104 }, { "artifacts", json::array() } } }) } };
	const auto manifest = ProviderManifest(document, provider);
	check(manifest.at("frames").size() == 2 && manifest.at("frames")[0].at("index") == 0);
	check(manifest.at("frames")[1].at("file") == "frame_0002.png" && manifest.at("frames")[1].at("wallMs") == 60.0);
	check(manifest.at("frames")[0].at("gameFrame") == 101 && manifest.at("dropped").size() == 1 && manifest.at("unwritten").size() == 1);
	check(manifest.at("width") == 200 && manifest.at("uiExcluded") == false);
	for (const auto& patch : { json{ { "width", 201 } }, json{ { "height", 0 } }, json{ { "format", "bmp" } } }) {
		auto invalid = document;
		invalid["children"][2]["artifacts"][0]["actual"].update(patch);
		rejects([&] { ProviderManifest(invalid, provider); });
	}
	auto invalid = document;
	invalid["children"][2]["artifacts"][0]["committed"] = false;
	rejects([&] { ProviderManifest(invalid, provider); });
	invalid = document;
	invalid["children"][2]["artifacts"][0]["path"] = "../escape.png";
	rejects([&] { ProviderManifest(invalid, provider); });
	invalid = document;
	invalid["children"].push_back(frame(3, 1060000));
	rejects([&] { ProviderManifest(invalid, provider); });
	invalid = document;
	invalid["children"][2]["actual"]["acquisition"]["monotonicTimestampUs"] = 999999;
	rejects([&] { ProviderManifest(invalid, provider); });
	invalid = document;
	invalid["counts"]["written"] = 3;
	rejects([&] { ProviderManifest(invalid, provider); });
	auto empty = document;
	empty["children"] = json::array();
	empty["counts"].update({ { "scheduled", 0 }, { "written", 0 } });
	check(ProviderManifest(empty, provider).at("unscheduled") == 4);

	json receipt = { { "requestId", "api-1" }, { "commandId", "host-1" }, { "state", "finalizing" },
		{ "terminalUtc", nullptr }, { "counts", document.at("counts") },
		{ "captureProvider", { { "runId", 7 } } } };
	auto status = ProviderBurstStatus(receipt);
	check(status.at("active") == true && status.at("ok") == false && status.at("manifest").is_null());
	receipt["state"] = "completed";
	receipt["terminalUtc"] = "terminal";
	check(ProviderBurstStatus(receipt).at("ok") == false);
	receipt["counts"]["failed"] = 0;
	receipt["captureProvider"].update({ { "committed", true }, { "manifestPath", "manifest.json" }, { "width", 200 }, { "height", 100 } });
	status = ProviderBurstStatus(receipt);
	check(status.at("ok") == true && status.at("done") == true && status.at("apiRequestId") == "api-1" && status.at("runId") == 7);
	for (const auto* state : { "stopped", "cancelled", "cancelled_partial", "failed", "failed_partial" }) {
		receipt["state"] = state;
		check(ProviderBurstStatus(receipt).at("ok") == false);
	}
	receipt["state"] = "completed_with_warnings";
	receipt["counts"]["written"] = 0;
	check(ProviderBurstStatus(receipt).at("ok") == false);
}
