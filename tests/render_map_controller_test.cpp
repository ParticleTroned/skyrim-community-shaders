#include "RenderMap/Artifacts.h"
#include "RenderMap/Controller.h"
#include "RenderMap/Serialization.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <format>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
	using namespace CSX::RenderMap;

	void Check(bool a_condition, std::string_view a_message)
	{
		if (!a_condition)
			throw std::runtime_error(std::string(a_message));
	}

	CollectorConfig Config()
	{
		return {
			.maxFrames = 4,
			.maxEvents = 32,
			.maxBytes = Collector::EventRecordSize() * 32,
			.maxDuration = std::chrono::seconds(1),
			.maxScopeDepth = 8,
		};
	}

	CaptureArtifactContext ArtifactContext(const std::filesystem::path& a_root)
	{
		const auto unavailable = nlohmann::json{
			{ "availability", "unavailable" }, { "path", nullptr },
			{ "sha256", nullptr }, { "schemaMajor", nullptr },
		};
		return {
			.outputRoot = a_root,
			.createdAtUtc = "2026-08-23T00:00:00Z",
			.producer = {
				{ "name", "render-map-test" }, { "version", "1" },
				{ "gitCommit", nullptr }, { "dirty", false },
			},
			.capabilities = { "bounded-in-memory-capture", "atomic-events-jsonl" },
			.inputs = {
				{ "shaderManifest", unavailable }, { "engineMap", unavailable },
				{ "csxBuildManifest", unavailable },
			},
			.environment = {
				{ "skyrim", { { "name", "SkyrimVR.exe" }, { "version", "1.4.15" }, { "sha256", nullptr } } },
				{ "csx", { { "name", "CommunityShaders.dll" }, { "version", "test" }, { "sha256", nullptr } } },
				{ "runtimeRoute", "unknown" },
				{ "modEnvironment", {
					{ "manager", "none" }, { "instance", nullptr }, { "profile", nullptr },
					{ "modlistSha256", nullptr }, { "pluginLoadOrderSha256", nullptr },
				} },
				{ "graphics", {
					{ "gpu", nullptr }, { "driver", nullptr }, { "renderWidth", nullptr }, { "renderHeight", nullptr },
					{ "presetSha256", nullptr }, { "settingsSha256", nullptr },
				} },
				{ "shaderCache", { { "identity", "unavailable" }, { "inventorySha256", nullptr }, { "coldAtStart", false } } },
			},
			.scenario = {
				{ "id", "unit-test" }, { "saveFingerprint", nullptr }, { "cell", nullptr },
				{ "worldspace", nullptr }, { "weather", nullptr }, { "gameHour", nullptr },
				{ "cameraMarker", nullptr }, { "notes", "unit test" },
			},
		};
	}

	std::shared_ptr<const CompletedCapture> MakeCapture(CaptureController& a_controller)
	{
		CaptureDescriptor descriptor;
		Check(a_controller.Start(Config(), descriptor) == ControlStatus::kSuccess, "capture did not start");
		CaptureDescriptor second;
		Check(a_controller.Start(Config(), second) == ControlStatus::kBusy, "overlapping capture was accepted");

		auto& runtime = GetRuntime();
		runtime.SetFrameContext({ 123, 4, 5, Eye::kLeft, 1 });
		{
			auto pass = runtime.EnterRenderPass({
				.renderPass = 0x1000,
				.geometry = 0x2000,
				.technique = 3,
				.passEnum = 4,
				.renderFlags = 5,
				.alphaTest = true,
			});
			{
				auto technique = runtime.EnterTechnique({
					.shader = 0x3000,
					.shaderType = 6,
					.vertexDescriptor = 7,
					.pixelDescriptor = 8,
					.callerRva = 0x9000,
					.fxpFilename = "Lighting",
				});
			}
			{
				auto geometry = runtime.EnterGeometry({
					.shader = 0x3000,
					.renderPass = 0x1000,
					.geometry = 0x2000,
					.shaderType = 6,
					.passEnum = 4,
					.renderFlags = 5,
				});
			}
		}

		std::shared_ptr<const CompletedCapture> completed;
		Check(a_controller.Stop("capture-wrong", completed) == ControlStatus::kCaptureNotFound,
			"wrong capture ID stopped the active capture");
		Check(a_controller.Stop(descriptor.captureId, completed) == ControlStatus::kSuccess,
			"capture did not stop");
		Check(completed && completed->snapshot.events.size() == 7, "completed capture is incomplete");
		std::shared_ptr<const CompletedCapture> replay;
		Check(a_controller.Stop(descriptor.captureId, replay) == ControlStatus::kSuccess && replay == completed,
			"completed stop was not idempotent");
		return completed;
	}

	void TestControllerAndSerialization()
	{
		CaptureController controller(1);
		const auto capture = MakeCapture(controller);
		const auto status = controller.GetStatus();
		Check(!status.active, "capture remained active after stop");
		Check(!status.accepting, "stopped capture remained accepting");
		Check(status.completedCaptureIds.size() == 1, "completed capture was not retained");

		const auto summary = SerializeCaptureSummary(*capture);
		Check(summary["captureId"] == capture->descriptor.captureId, "summary capture ID is wrong");
		Check(summary["completion"]["eventCount"] == 7, "summary event count is wrong");
		Check(summary["completion"]["shaderObservationCount"] == 1, "summary shader observation count is wrong");
		Check(summary["completion"]["reason"] == "requested", "summary stop reason is wrong");

		const auto firstPage = SerializeEventPage(*capture, 0, 2, 42);
		Check(firstPage["returnedCount"] == 2, "event page count is wrong");
		Check(firstPage["moreAvailable"] == true, "event page did not report more data");
		const auto& first = firstPage["events"][0];
		Check(first["schema"]["name"] == "csx.render-event", "event schema name is wrong");
		Check(first["type"] == "render-pass-enter", "event kind is wrong");
		Check(first["processId"] == 42, "event process ID is wrong");
		Check(first["threadId"].get<std::uint64_t>() != 0, "event thread ID is missing");
		Check(first["frame"]["cpuFrame"] == 123, "event frame is wrong");
		Check(first["execution"]["observationDomain"] == "cpu-call", "event execution domain is wrong");
		Check(first["scopes"]["renderPass"].get<std::string>().starts_with("obs-render-pass-"),
			"render-pass observation ID is wrong");
		Check(first["payload"]["renderPassPointer"] == "0x1000", "pointer evidence is wrong");
		const auto shaderPage = SerializeEventPage(*capture, 1, 2, 42);
		const auto& observed = shaderPage["events"][0];
		Check(observed["type"] == "shader-observed", "shader-observed event is missing");
		Check(observed["payload"]["fxpFilename"] == "Lighting", "shader identity detail is missing");
		Check(observed["observationRefs"][0]["kind"] == "shader", "typed shader reference is missing");
		const auto& technique = shaderPage["events"][1];
		Check(technique["payload"]["schema"] == "technique-boundary-v2", "technique schema did not advance");
		Check(technique["payload"]["shaderObservationId"] == observed["payload"]["shaderObservationId"],
			"technique did not join to the shader observation");

		const auto finalPage = SerializeEventPage(*capture, 6, 100, 42);
		Check(finalPage["returnedCount"] == 1, "final page count is wrong");
		Check(finalPage["moreAvailable"] == false, "final page incorrectly reports more data");
	}

	void TestCompletedHistoryBound()
	{
		CaptureController controller(1);
		const auto first = MakeCapture(controller);
		const auto second = MakeCapture(controller);
		Check(!controller.GetCompleted(first->descriptor.captureId), "old capture exceeded history bound");
		Check(controller.GetCompleted(second->descriptor.captureId) == second, "latest capture was not retained");
	}

	void TestResolvedStageSerialization()
	{
		CaptureController controller;
		CaptureDescriptor descriptor;
		Check(controller.Start(Config(), descriptor) == ControlStatus::kSuccess,
			"stage serialization capture did not start");
		auto& runtime = GetRuntime();
		{
			auto technique = runtime.EnterTechnique({
				.shader = 0x1000, .shaderType = 6, .vertexDescriptor = 7, .pixelDescriptor = 8,
				.fxpFilename = "Lighting",
			});
			runtime.RecordTechniqueResolution({
				.inputVertexDescriptor = 7,
				.inputPixelDescriptor = 8,
				.resolvedVertexDescriptor = 17,
				.resolvedPixelDescriptor = 18,
				.shaderFound = true,
				.vertex = {
					.route = ShaderSelectionRoute::kCSXCache,
					.shader = {
						.stage = ShaderStage::kVertex, .wrapper = 0x2000, .d3dObject = 0x3000,
						.wrapperDescriptor = 17, .bytecodeSize = 128,
						.bytecodeSha256 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
						.cachePath = "Data/ShaderCache/Lighting/11.vso",
					},
				},
				.pixel = { .route = ShaderSelectionRoute::kSkipped },
			});
		}
		std::shared_ptr<const CompletedCapture> capture;
		Check(controller.Stop(descriptor.captureId, capture) == ControlStatus::kSuccess,
			"stage serialization capture did not stop");
		const auto page = SerializeEventPage(*capture, 0, 20, 42);
		const auto& observed = page["events"][2];
		Check(observed["type"] == "stage-shader-observed", "stage first-seen event type is wrong");
		Check(observed["payload"]["stage"] == "vertex", "stage observation type is wrong");
		Check(observed["payload"]["bytecodeSha256"] ==
			"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
			"stage bytecode identity is missing");
		Check(observed["payload"]["cachePath"] == "Data/ShaderCache/Lighting/11.vso",
			"stage cache path is missing");
		const auto& resolved = page["events"][3];
		Check(resolved["type"] == "technique-resolved", "technique resolution event type is wrong");
		Check(resolved["payload"]["vertexRoute"] == "csx-cache", "vertex selection route is wrong");
		Check(resolved["payload"]["pixelRoute"] == "skipped", "pixel selection route is wrong");
		Check(resolved["observationRefs"].size() == 1 &&
			resolved["observationRefs"][0]["kind"] == "vertex-shader",
			"resolved stage reference is wrong");
	}

	void TestDurableArtifacts()
	{
		const auto root = std::filesystem::temp_directory_path() /
			std::format("csx-render-map-test-{}", std::chrono::steady_clock::now().time_since_epoch().count());
		CaptureController controller;
		const auto capture = MakeCapture(controller);
		const auto bundle = WriteCaptureArtifacts(*capture, ArtifactContext(root), 42);
		Check(bundle.success, "durable artifact write failed");
		Check(std::filesystem::exists(bundle.directory / "events.jsonl"), "events artifact is missing");
		Check(std::filesystem::exists(bundle.directory / "capture-manifest.json"), "capture manifest is missing");

		std::ifstream manifestStream(bundle.directory / "capture-manifest.json");
		nlohmann::json manifest;
		manifestStream >> manifest;
		Check(manifest["status"] == "complete", "complete capture manifest has the wrong status");
		Check(manifest["completion"]["eventCount"] == 7, "manifest event count is wrong");
		Check(manifest["artifacts"][0]["sha256"].get<std::string>().size() == 64, "events hash is missing");
		Check(bundle.manifestArtifact["sha256"].get<std::string>().size() == 64, "manifest hash is missing");

		const auto collision = WriteCaptureArtifacts(*capture, ArtifactContext(root), 42);
		Check(!collision.success && collision.error.find("already exists") != std::string::npos,
			"artifact writer overwrote an existing capture");
		manifestStream.close();
		std::filesystem::remove_all(root);
	}

	void TestGapArtifact()
	{
		const auto root = std::filesystem::temp_directory_path() /
			std::format("csx-render-map-gap-test-{}", std::chrono::steady_clock::now().time_since_epoch().count());
		CaptureController controller;
		auto config = Config();
		config.maxEvents = 1;
		config.maxBytes = Collector::EventRecordSize() * 4;
		CaptureDescriptor descriptor;
		Check(controller.Start(config, descriptor) == ControlStatus::kSuccess, "gap capture did not start");
		{
			auto pass = GetRuntime().EnterRenderPass({ .renderPass = 1 });
			Check(pass.IsActive(), "gap capture event was not recorded");
		}
		Check(!GetRuntime().IsCapturing(), "gap capture did not quiesce at its event limit");
		const auto quiesced = controller.GetStatus();
		Check(quiesced.active && !quiesced.accepting, "quiesced capture was not awaiting finalization");
		std::shared_ptr<const CompletedCapture> capture;
		Check(controller.Stop(descriptor.captureId, capture) == ControlStatus::kSuccess, "gap capture did not stop");
		const auto bundle = WriteCaptureArtifacts(*capture, ArtifactContext(root), 42);
		Check(bundle.success, "gap artifact write failed");
		std::ifstream manifestStream(bundle.directory / "capture-manifest.json");
		nlohmann::json manifest;
		manifestStream >> manifest;
		Check(manifest["status"] == "incomplete", "truncated capture was not marked incomplete");
		Check(manifest["completion"]["eventCount"] == 2, "synthetic gap was not counted");
		Check(manifest["completion"]["droppedEventCount"] == 1, "lost event count is wrong");
		std::ifstream eventsStream(bundle.directory / "events.jsonl");
		std::string eventLine;
		std::getline(eventsStream, eventLine);
		std::getline(eventsStream, eventLine);
		Check(nlohmann::json::parse(eventLine)["type"] == "gap", "gap event was not materialized");
		manifestStream.close();
		eventsStream.close();
		std::filesystem::remove_all(root);
	}
}

int main()
{
	try {
		TestControllerAndSerialization();
		TestCompletedHistoryBound();
		TestResolvedStageSerialization();
		TestDurableArtifacts();
		TestGapArtifact();
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
