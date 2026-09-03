#include "RenderMap/Artifacts.h"
#include "RenderMap/Controller.h"
#include "RenderMap/Serialization.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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
		CollectorConfig config{
			.maxFrames = 4,
			.maxEvents = 32,
			.maxBytes = 1,
			.maxDuration = std::chrono::seconds(1),
			.maxScopeDepth = 8,
		};
		config.maxBytes = Collector::RequiredStorageBytes(config);
		return config;
	}

	CaptureArtifactContext ArtifactContext(const std::filesystem::path& a_root)
	{
		const auto unavailable = nlohmann::json{
			{ "availability", "unavailable" },
			{ "path", nullptr },
			{ "sha256", nullptr },
			{ "schemaMajor", nullptr },
		};
		return {
			.outputRoot = a_root,
			.createdAtUtc = "2026-08-23T00:00:00Z",
			.producer = {
				{ "name", "render-map-test" },
				{ "version", "1" },
				{ "gitCommit", nullptr },
				{ "dirty", false },
			},
			.capabilities = { "bounded-in-memory-capture", "atomic-events-jsonl" },
			.inputs = {
				{ "shaderManifest", unavailable },
				{ "engineMap", unavailable },
				{ "csxBuildManifest", unavailable },
			},
			.environment = {
				{ "skyrim", { { "name", "SkyrimVR.exe" }, { "version", "1.4.15" }, { "sha256", nullptr } } },
				{ "csx", { { "name", "CommunityShaders.dll" }, { "version", "test" }, { "sha256", nullptr } } },
				{ "runtimeRoute", "unknown" },
				{ "modEnvironment", {
										{ "manager", "none" },
										{ "instance", nullptr },
										{ "profile", nullptr },
										{ "modlistSha256", nullptr },
										{ "pluginLoadOrderSha256", nullptr },
									} },
				{ "graphics", {
								  { "gpu", nullptr },
								  { "driver", nullptr },
								  { "renderWidth", nullptr },
								  { "renderHeight", nullptr },
								  { "presetSha256", nullptr },
								  { "settingsSha256", nullptr },
							  } },
				{ "shaderCache", { { "identity", "unavailable" }, { "inventorySha256", nullptr }, { "coldAtStart", false } } },
			},
			.scenario = {
				{ "id", "unit-test" },
				{ "saveFingerprint", nullptr },
				{ "cell", nullptr },
				{ "worldspace", nullptr },
				{ "weather", nullptr },
				{ "gameHour", nullptr },
				{ "cameraMarker", nullptr },
				{ "notes", "unit test" },
			},
			.extensions = {
				{ "csx.shaderCompilation", {
											   { "availability", "observed" },
											   { "evidenceClass", "runtime-observed" },
											   { "capturedAt", "capture-start" },
										   } },
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
		Check(completed && completed->snapshot.events.size() == 8,
			std::format("completed capture event count is {}, expected 8",
				completed ? completed->snapshot.events.size() : 0));
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
		Check(summary["completion"]["eventCount"] == 8, "summary event count is wrong");
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

		const auto finalPage = SerializeEventPage(*capture, 7, 100, 42);
		Check(finalPage["returnedCount"] == 1, "final page count is wrong");
		Check(finalPage["moreAvailable"] == false, "final page incorrectly reports more data");
	}

	void TestStopActiveWithoutCaptureId()
	{
		CaptureController controller;
		CaptureDescriptor descriptor;
		Check(controller.Start(Config(), descriptor) == ControlStatus::kSuccess,
			"capture for active-stop test did not start");
		std::shared_ptr<const CompletedCapture> completed;
		Check(controller.Stop({}, completed) == ControlStatus::kSuccess,
			"omitted capture ID did not stop the sole active capture");
		Check(completed && completed->descriptor.captureId == descriptor.captureId,
			"active-stop returned the wrong capture");
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
		const StageShaderObservationInput::EngineAlias vertexEngineAliases[] = {
			{ .loaderType = "ISHDRDownSample4", .compileSourceName = "ISHDR", .descriptor = 17 },
		};
		{
			auto technique = runtime.EnterTechnique({
				.shader = 0x1000,
				.shaderType = 6,
				.vertexDescriptor = 7,
				.pixelDescriptor = 8,
				.fxpFilename = "ISHDRDownSample4",
				.imageSpaceName = "BSImagespaceShaderISHDRDownSample4",
				.compileSourceName = "ISHDR",
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
						.stage = ShaderStage::kVertex,
						.wrapper = 0x2000,
						.d3dObject = 0x3000,
						.wrapperDescriptor = 17,
						.bytecodeSize = 128,
						.bytecodeSha256 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
						.cachePath = "Data/ShaderCache/Lighting/11.vso",
						.engineAliases = vertexEngineAliases,
						.engineAliasCount = 1,
						.engineAliasTotalCount = 1,
					},
				},
				.pixel = { .route = ShaderSelectionRoute::kSkipped },
			});
		}
		runtime.SetImmediateContext(0x9000);
		runtime.BindStage(0x9000, ShaderStage::kVertex, 0x3000);
		const ResourceViewInput shaderResource{
			.resource = {
				.d3dObject = 0x9400,
				.dimension = ResourceDimension::kTexture2D,
				.widthOrBytes = 64,
				.height = 64,
				.depthOrArraySize = 1,
				.mipLevels = 1,
				.format = 28,
				.sampleCount = 1,
				.bindFlags = 0x28,
			},
			.view = {
				.kind = TargetViewKind::kShaderResource,
				.d3dObject = 0x9500,
				.format = 28,
				.dimension = 4,
			},
		};
		runtime.BindResourceViews(
			0x9000, ResourceBindingKind::kShaderResource, ResourceStage::kPixel,
			0, 1, &shaderResource);
		runtime.BindResourceViews(
			0x9000, ResourceBindingKind::kShaderResource, ResourceStage::kPixel,
			0, 1, &shaderResource, false, ResourceBindingSource::kPostCallQuery);
		runtime.BindResourceViews(
			0x9000, ResourceBindingKind::kShaderResource, ResourceStage::kPixel,
			0, 1, nullptr, false, ResourceBindingSource::kPostCallQuery);
		const std::uintptr_t renderTargets[] = { 0x9100, 0x9200 };
		runtime.BindRenderTargets(0x9000, 2, renderTargets, 0x9300);
		runtime.RecordDraw(0x9000, DrawOperation::kDrawIndexed, 24, 3, 2);
		std::shared_ptr<const CompletedCapture> capture;
		Check(controller.Stop(descriptor.captureId, capture) == ControlStatus::kSuccess,
			"stage serialization capture did not stop");
		const auto summary = SerializeCaptureSummary(*capture);
		Check(summary["completion"]["targetViewObservationCount"] == 4 &&
				  summary["completion"]["targetBindingObservationCount"] == 1,
			"target catalogue counts are missing from the capture summary");
		const auto page = SerializeEventPage(*capture, 0, 20, 42);
		const auto& shaderObserved = page["events"][0];
		Check(shaderObserved["type"] == "shader-observed" &&
				  shaderObserved["payload"]["schema"] == "shader-observation-v2" &&
				  shaderObserved["payload"]["compileSourceName"] == "ISHDR",
			"engine shader observation did not retain its effective compile source");
		const auto& observed = page["events"][2];
		Check(observed["type"] == "stage-shader-observed", "stage first-seen event type is wrong");
		Check(observed["payload"]["stage"] == "vertex", "stage observation type is wrong");
		Check(observed["payload"]["schema"] == "stage-shader-observation-v3",
			"stage observation schema did not advance");
		Check(observed["payload"]["bytecodeSha256"] ==
				  "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
			"stage bytecode identity is missing");
		Check(observed["payload"]["cachePath"] == "Data/ShaderCache/Lighting/11.vso",
			"stage cache path is missing");
		Check(observed["payload"]["engineAliasTotalCount"] == 1 &&
				  observed["payload"]["engineAliases"].size() == 1 &&
				  observed["payload"]["engineAliases"][0]["loaderType"] == "ISHDRDownSample4" &&
				  observed["payload"]["engineAliases"][0]["compileSourceName"] == "ISHDR" &&
				  observed["payload"]["engineAliases"][0]["descriptor"] == 17,
			"stage engine alias is missing");
		const auto& resolved = page["events"][3];
		Check(resolved["type"] == "technique-resolved", "technique resolution event type is wrong");
		Check(resolved["payload"]["vertexRoute"] == "csx-cache", "vertex selection route is wrong");
		Check(resolved["payload"]["pixelRoute"] == "skipped", "pixel selection route is wrong");
		Check(resolved["observationRefs"].size() == 1 &&
				  resolved["observationRefs"][0]["kind"] == "vertex-shader",
			"resolved stage reference is wrong");
		const auto drawIterator = std::find_if(
			page["events"].begin(), page["events"].end(),
			[](const nlohmann::json& a_event) { return a_event["type"] == "draw"; });
		Check(drawIterator != page["events"].end(), "serialized draw event is missing");
		const auto targetBindingIterator = std::find_if(
			page["events"].begin(), page["events"].end(),
			[](const nlohmann::json& a_event) { return a_event["type"] == "render-target-bind"; });
		Check(targetBindingIterator != page["events"].end() &&
				  (*targetBindingIterator)["payload"]["schema"] == "render-target-binding-v2" &&
				  (*targetBindingIterator)["payload"]["source"] == "observed-call",
			"output-merger binding provenance was not serialized");
		const auto effectiveBindingIterator = std::find_if(
			page["events"].begin(), page["events"].end(),
			[](const nlohmann::json& a_event) {
				return a_event["type"] == "resource-view-bind" &&
			           a_event["payload"].value("source", "") == "post-call-query";
			});
		Check(effectiveBindingIterator != page["events"].end() &&
				  (*effectiveBindingIterator)["payload"]["schema"] == "resource-view-binding-v2",
			"post-call resource-view provenance was not serialized");
		const auto effectiveSummaryIterator = std::find_if(
			page["events"].begin(), page["events"].end(),
			[](const nlohmann::json& a_event) {
				return a_event["type"] == "resource-view-state-observed";
			});
		Check(effectiveSummaryIterator != page["events"].end() &&
				  (*effectiveSummaryIterator)["payload"]["schema"] == "resource-view-state-observed-v1" &&
				  (*effectiveSummaryIterator)["payload"]["changedSlotCount"] == 1,
			"effective resource-view query summary was not serialized");
		const auto& draw = *drawIterator;
		Check(draw["type"] == "draw" && draw["payload"]["schema"] == "draw-call-v4" &&
				  draw["payload"]["preparedGeometrySetupObservationId"].is_null(),
			"draw event schema is wrong");
		Check(draw["payload"]["targetBindingObservationId"].is_string(),
			"draw did not serialize its output-merger binding");
		Check(draw["payload"]["operation"] == "draw-indexed" &&
				  draw["payload"]["arguments"]["indexCount"] == 24,
			"draw operation arguments are missing");
		Check(draw["observationRefs"].size() == 6 &&
				  draw["observationRefs"][0]["role"] == "immediate-context" &&
				  draw["observationRefs"][1]["role"] == "output-merger-binding" &&
				  draw["observationRefs"][2]["role"] == "bound-render-target-0" &&
				  draw["observationRefs"][3]["role"] == "bound-render-target-1" &&
				  draw["observationRefs"][4]["role"] == "bound-depth-target" &&
				  draw["observationRefs"][5]["role"] == "bound-at-draw",
			"draw did not join to its selected shader and output-merger state");
		Check(draw["deviceContextObservationId"].is_string(),
			"draw did not serialize its typed immediate-context identity");
		Check(draw["execution"]["commandStreamSequence"] == 6,
			"draw did not serialize its context-local command sequence");
		const auto contextIterator = std::find_if(
			page["events"].begin(), page["events"].end(),
			[](const nlohmann::json& a_event) { return a_event["type"] == "device-context-observed"; });
		Check(contextIterator != page["events"].end(), "serialized context declaration is missing");
		Check((*contextIterator)["payload"]["schema"] == "device-context-observation-v2" &&
				  (*contextIterator)["payload"]["kind"] == "immediate" &&
				  (*contextIterator)["payload"]["creationEvidence"] == "initial-immediate-context" &&
				  (*contextIterator)["payload"]["contextFlags"] == 0,
			"serialized context declaration is malformed");
	}

	void TestSemanticIdentitySerialization()
	{
		CaptureController controller;
		CaptureDescriptor descriptor;
		Check(controller.Start(Config(), descriptor) == ControlStatus::kSuccess,
			"semantic serialization capture did not start");
		GetRuntime().SetImmediateContext(0x7000);
		{
			auto geometry = GetRuntime().EnterGeometry({
				.shader = 0x1000,
				.renderPass = 0x2000,
				.geometry = 0x3000,
				.shaderType = 6,
				.passEnum = 7,
				.renderFlags = 8,
				.sceneObject = {
					.reference = 0x4000,
					.referenceFormId = 0x01000001,
					.baseFormId = 0x00000002,
					.referenceName = "Observed reference",
					.baseFormName = "Observed base",
				},
				.geometryObservation = {
					.runtimeTypeName = "BSTriShape",
					.name = "Observed geometry",
					.geometryType = 1,
					.vertexDescriptor = 0x1234,
					.worldTransformAvailable = true,
					.worldBoundAvailable = true,
				},
				.materialState = {
					.shaderProperty = 0x5000,
					.shaderPropertyRuntimeTypeName = "BSLightingShaderProperty",
					.shaderPropertyFlags = 0x80,
					.alpha = 1.0f,
					.material = 0x6000,
					.materialType = 2,
					.feature = 3,
					.hashKey = 4,
					.shaderPropertyAvailable = true,
					.materialAvailable = true,
				},
			});
			Check(geometry.IsActive(), "semantic serialization boundary did not enter");
		}
		GetRuntime().RecordDraw(0x7000, DrawOperation::kDrawIndexed, 6);
		std::shared_ptr<const CompletedCapture> capture;
		Check(controller.Stop(descriptor.captureId, capture) == ControlStatus::kSuccess && capture,
			"semantic serialization capture did not stop");
		const auto page = SerializeEventPage(*capture, 0, 10, 42);
		const auto bounds = SerializeBounds(capture->snapshot.config);
		Check(bounds["geometryShaderTypes"].size() == 64 &&
				  bounds["executionWithinSelectedGeometry"] == false,
			"geometry selector defaults were not serialized");
		Check(page["returnedCount"] == 7, "semantic serialization emitted the wrong event count");
		const auto& object = page["events"][0];
		const auto& observedGeometry = page["events"][1];
		const auto& material = page["events"][2];
		const auto& setup = page["events"][3];
		Check(object["schema"]["minor"] == 17 && object["payload"]["schema"] == "scene-object-observation-v1",
			"scene-object declaration schema is wrong");
		Check(observedGeometry["payload"]["schema"] == "geometry-observation-v1" &&
				  observedGeometry["payload"]["sceneObjectObservationId"] == object["payload"]["sceneObjectObservationId"],
			"geometry declaration did not serialize its exact scene-object join");
		Check(material["payload"]["schema"] == "material-state-observation-v1" &&
				  material["payload"]["stateRevision"] == 1,
			"material-state declaration schema is wrong");
		Check(setup["payload"]["schema"] == "geometry-boundary-v2" &&
				  setup["payload"]["geometryObservationId"] == observedGeometry["payload"]["geometryObservationId"] &&
				  setup["payload"]["materialStateObservationId"] == material["payload"]["materialStateObservationId"],
			"geometry setup did not serialize its exact semantic joins");
		Check(setup["observationRefs"].size() == 2,
			"geometry setup did not publish both typed semantic references");
		const auto& draw = page["events"][6];
		Check(draw["payload"]["schema"] == "draw-call-v4" &&
				  draw["scopes"]["geometry"].is_null() &&
				  draw["payload"]["preparedGeometrySetupObservationId"] == setup["scopes"]["geometry"] &&
				  draw["observationRefs"][1]["kind"] == "geometry-setup" &&
				  draw["observationRefs"][1]["role"] == "prepared-at-draw",
			"draw did not serialize the post-setup prepared geometry handoff");
	}

	void TestDeferredCommandSerialization()
	{
		CaptureController controller;
		auto config = Config();
		config.maxEvents = 64;
		config.maxBytes = Collector::RequiredStorageBytes(config);
		CaptureDescriptor descriptor;
		Check(controller.Start(config, descriptor) == ControlStatus::kSuccess,
			"deferred serialization capture did not start");
		auto& runtime = GetRuntime();
		runtime.SetImmediateContext(0xA000);
		runtime.RegisterDeferredContext(0xA100, 0);
		runtime.RecordDraw(0xA100, DrawOperation::kDraw, 3);
		runtime.RecordFinishCommandList(0xA100, 0xA200, false, 0);
		runtime.RecordExecuteCommandList(0xA000, 0xA200, true);
		std::shared_ptr<const CompletedCapture> capture;
		Check(controller.Stop(descriptor.captureId, capture) == ControlStatus::kSuccess && capture,
			"deferred serialization capture did not stop");
		const auto page = SerializeEventPage(*capture, 0, 64, 42);
		const auto findEvent = [&](std::string_view a_type) {
			return std::find_if(page["events"].begin(), page["events"].end(),
				[&](const nlohmann::json& a_event) {
					return a_event["type"].get<std::string>() == a_type;
				});
		};
		const auto draw = findEvent("draw");
		const auto list = findEvent("command-list-observed");
		const auto finish = findEvent("finish-command-list");
		const auto execute = findEvent("execute-command-list");
		Check(draw != page["events"].end() &&
				  (*draw)["payload"]["schema"] == "draw-call-v4" &&
				  (*draw)["payload"]["deviceContextPointer"] == "0xA100" &&
				  (*draw)["execution"]["observationDomain"] == "command-recording" &&
				  std::any_of((*draw)["observationRefs"].begin(), (*draw)["observationRefs"].end(),
					  [](const nlohmann::json& a_ref) { return a_ref["kind"] == "command-recording"; }),
			"deferred draw did not serialize its versioned context and recording provenance");
		Check(list != page["events"].end() && finish != page["events"].end() &&
				  (*list)["payload"]["schema"] == "command-list-observation-v2" &&
				  (*finish)["payload"]["schema"] == "finish-command-list-v2" &&
				  (*list)["payload"]["sourceRecordingComplete"] == false &&
				  (*finish)["payload"]["sourceRecordingComplete"] == false &&
				  (*list)["payload"]["sourceRecordingIncompleteReasons"].get<std::vector<std::string>>() ==
					  std::vector<std::string>{ "hook-coverage-unqualified" },
			"deferred command-list serialization overstated recording completeness");
		Check(execute != page["events"].end() &&
				  (*execute)["execution"]["observationDomain"] == "cpu-call" &&
				  (*execute)["commandRecordingObservationId"].is_null() &&
				  (*execute)["payload"]["sourceCommandRecordingObservationId"].is_string(),
			"ExecuteCommandList was mislabelled as a recorded deferred command");

		Check(controller.Start(config, descriptor) == ControlStatus::kSuccess,
			"failed-finish serialization capture did not start");
		runtime.RecordFinishCommandList(
			0xA100, 0xDEAD, true, static_cast<std::int32_t>(0x80004005u));
		Check(controller.Stop(descriptor.captureId, capture) == ControlStatus::kSuccess && capture,
			"failed-finish serialization capture did not stop");
		const auto failedPage = SerializeEventPage(*capture, 0, 64, 42);
		const auto failedFinish = std::find_if(
			failedPage["events"].begin(), failedPage["events"].end(),
			[](const nlohmann::json& a_event) {
				return a_event["type"].get<std::string>() == "finish-command-list";
			});
		Check(failedFinish != failedPage["events"].end() &&
				  (*failedFinish)["payload"]["succeeded"] == false &&
				  (*failedFinish)["payload"]["commandListObservationId"].is_null() &&
				  (*failedFinish)["payload"]["commandListPointer"].is_null(),
			"failed FinishCommandList serialized a materialized list identity or pointer");
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
		Check(manifest["completion"]["eventCount"] == 8, "manifest event count is wrong");
		Check(manifest["schema"]["minor"] == 7, "capture manifest schema revision is wrong");
		Check(manifest["bounds"]["requestedEventKinds"].is_array() &&
				  manifest["bounds"]["resolvedEventKinds"].is_array() &&
				  manifest["bounds"]["observedEventKinds"].is_array(),
			"capture selector and observed event-kind inventories are missing");
		Check(manifest["artifacts"][0]["sha256"].get<std::string>().size() == 64, "events hash is missing");
		Check(bundle.manifestArtifact["sha256"].get<std::string>().size() == 64, "manifest hash is missing");
		Check(manifest["extensions"]["csx.shaderCompilation"]["evidenceClass"] == "runtime-observed",
			"capture-start shader compilation provenance was not preserved");
		Check(manifest["extensions"]["csx.processId"] == 42,
			"artifact metrics were not merged with capture-start provenance");

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
		config.maxBytes = Collector::RequiredStorageBytes(config);
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
		TestStopActiveWithoutCaptureId();
		TestCompletedHistoryBound();
		TestResolvedStageSerialization();
		TestSemanticIdentitySerialization();
		TestDeferredCommandSerialization();
		TestDurableArtifacts();
		TestGapArtifact();
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
