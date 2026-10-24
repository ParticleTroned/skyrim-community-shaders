#include "RenderMap/Artifacts.h"
#include "RenderMap/CapturePublication.h"
#include "RenderMap/CaptureStart.h"
#include "RenderMap/Controller.h"
#include "RenderMap/PayloadSchemaNames.h"
#include "RenderMap/Serialization.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <new>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
	using namespace CSX::RenderMap;

	void Check(bool a_condition, std::string_view a_message)
	{
		if (!a_condition)
			throw std::runtime_error(std::string(a_message));
	}

	void TestPayloadSchemaCatalogue()
	{
		const std::set<std::string> advertised(
			PayloadSchemaNames::kAll.begin(), PayloadSchemaNames::kAll.end());
		Check(advertised.size() == PayloadSchemaNames::kAll.size(), "registry repeats payload schemas");
		std::set<std::string> emitted;
		for (std::uint16_t schema = 1; schema <= static_cast<std::uint16_t>(PayloadSchema::kNativePipelineSnapshot); ++schema) {
			EventRecord event{};
			event.payload.schema = schema;
			const auto serialized = SerializeEvent(event, "catalogue-test", 42);
			const auto name = serialized["payload"]["schema"].get<std::string>();
			Check(advertised.contains(name), "serializer emits an unadvertised payload schema");
			emitted.insert(name);
		}
		Check(emitted == advertised, "registry advertises an unreachable payload schema");
	}

	void TestResourceCpuAccessBoundaries()
	{
		struct Case
		{
			ResourceCpuAccessPhase phase;
			std::uint32_t mapType;
			std::uint32_t result;
			std::uint64_t mapObservationId;
			bool visible;
			bool published;
		};
		const Case cases[] = {
			{ ResourceCpuAccessPhase::kMap, 1, 0, 9, true, false },
			{ ResourceCpuAccessPhase::kMap, 3, 0, 9, true, false },
			{ ResourceCpuAccessPhase::kMap, 2, 0, 9, false, false },
			{ ResourceCpuAccessPhase::kMap, 4, 0, 9, false, false },
			{ ResourceCpuAccessPhase::kMap, 5, 0, 9, false, false },
			{ ResourceCpuAccessPhase::kMap, 1, 0x80004005u, 0, false, false },
			{ ResourceCpuAccessPhase::kUnmap, 1, 0, 9, false, false },
			{ ResourceCpuAccessPhase::kUnmap, 2, 0, 9, false, true },
			{ ResourceCpuAccessPhase::kUnmap, 3, 0, 9, false, true },
			{ ResourceCpuAccessPhase::kUnmap, 4, 0, 9, false, true },
			{ ResourceCpuAccessPhase::kUnmap, 5, 0, 9, false, true },
			{ ResourceCpuAccessPhase::kUnmap, 2, 0, 0, false, false },
		};
		for (const auto& test : cases) {
			EventRecord event{};
			event.kind = EventKind::kResourceCpuAccess;
			event.payload = {
				.schema = static_cast<std::uint16_t>(PayloadSchema::kResourceCpuAccess),
				.words = { static_cast<std::uint64_t>(test.phase), test.mapObservationId, 7, 0, test.mapType, 0, test.result, 0 },
			};
			const auto payload = SerializeEvent(event, "cpu-boundary-test", 42).at("payload");
			Check(test.visible ? payload.at("visibilityBoundary") == "cpu-readable-after-map-return" :
								 payload.at("visibilityBoundary").is_null(),
				"CPU map visibility boundary has the wrong JSON value");
			Check(test.published ? payload.at("publicationBoundary") == "gpu-visible-after-unmap-return" :
								   payload.at("publicationBoundary").is_null(),
				"CPU unmap publication boundary has the wrong JSON value");
		}
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
				{ "skyrim", BuildSkyrimModuleIdentity(true, "1.4.15.0", true) },
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

	void TestRuntimeArtifactIdentity()
	{
		const auto vr = BuildSkyrimModuleIdentity(true, "1.4.15.0", true);
		Check(vr["name"] == "SkyrimVR.exe" && vr["version"] == "1.4.15.0", "VR identity was not retained");
		for (const auto version : { "1.5.97.0", "1.6.629.0" }) {
			const auto flat = BuildSkyrimModuleIdentity(false, version, false);
			Check(flat["name"] == "SkyrimSE.exe" && flat["version"] == version, "SE/AE identity was mislabeled");
			Check(flat["sha256"].is_null(), "unobserved executable hash was invented");
		}
		Check(BuildSkyrimModuleIdentity(false, "")["version"].is_null(), "missing runtime version was substituted");
		for (const auto vrRuntime : { false, true }) {
			bool rejected = false;
			try {
				BuildSkyrimModuleIdentity(vrRuntime, "observed-version", !vrRuntime);
			} catch (const std::invalid_argument&) {
				rejected = true;
			}
			Check(rejected, "contradictory observed shader/runtime identity was accepted");
		}
	}

	void TestPreparedStartFailures()
	{
		for (const auto failure : { CaptureStartPhase::kContext, CaptureStartPhase::kResponse }) {
			CaptureController controller;
			CaptureDescriptor descriptor;
			CaptureStartPhase phase = CaptureStartPhase::kContext;
			std::unordered_map<std::string, CaptureArtifactContext> contexts;
			const auto root = std::filesystem::temp_directory_path() /
			                  std::format("csx-render-map-start-failure-{}", std::chrono::steady_clock::now().time_since_epoch().count());
			nlohmann::json response;
			const auto status = StartPreparedCapture(controller, Config(), descriptor, phase, [&](const auto& a_capture) {
					Check(!GetRuntime().IsCapturing(), "hooks activated before context retention");
					if (failure == CaptureStartPhase::kContext)
						throw std::bad_alloc();
					contexts.emplace(a_capture.captureId, ArtifactContext(root)); }, [&](const auto& a_capture) {
					Check(!GetRuntime().IsCapturing(), "hooks activated before response construction");
					Check(contexts.contains(a_capture.captureId), "response preceded context retention");
					response["captureId"] = a_capture.captureId;
					if (failure == CaptureStartPhase::kResponse)
						throw std::bad_alloc(); }, [&](const std::string& a_captureId) noexcept { contexts.erase(a_captureId); });
			Check(status == ControlStatus::kAllocationFailed && phase == failure, "injected start failure was misclassified");
			Check(!descriptor.captureId.empty(), "failure did not exercise a reserved capture identity");
			Check(contexts.empty(), "failed start retained artifact context");
			const auto state = controller.GetStatus();
			Check(!state.active && !state.accepting && state.completedCaptureIds.empty(), "failed start published a capture");
			Check(!controller.GetCompleted(descriptor.captureId), "failed start permits event paging");
			std::shared_ptr<const CompletedCapture> stopped;
			Check(controller.Stop(descriptor.captureId, stopped) == ControlStatus::kNotCapturing && !stopped,
				"failed start permits completed stop or artifact serialization");
			Check(!GetRuntime().IsCapturing() && !GetRuntime().IsCaptureDraining(), "failed start left hooks active or draining");
			Check(!std::filesystem::exists(root), "failed start created an artifact directory");
			CaptureDescriptor next;
			Check(controller.Start(Config(), next) == ControlStatus::kSuccess, "failed preparation prevented a later capture");
			Check(controller.Stop(next.captureId, stopped) == ControlStatus::kSuccess, "later capture could not complete");
		}
	}

	void TestPreparedStartSuccess()
	{
		CaptureController controller;
		CaptureDescriptor descriptor;
		CaptureStartPhase phase = CaptureStartPhase::kContext;
		std::unordered_map<std::string, std::string> contexts;
		nlohmann::json response;
		const auto status = StartPreparedCapture(controller, Config(), descriptor, phase, [&](const auto& a_capture) {
				Check(!GetRuntime().IsCapturing(), "successful start activated before provenance");
				contexts.emplace(a_capture.captureId, "capture-start"); }, [&](const auto& a_capture) {
				Check(!GetRuntime().IsCapturing(), "successful start activated before response");
				response["captureId"] = a_capture.captureId; }, [&](const std::string& a_captureId) noexcept { contexts.erase(a_captureId); });
		Check(status == ControlStatus::kSuccess && phase == CaptureStartPhase::kRuntime, "prepared start failed");
		Check(GetRuntime().IsCapturing(), "prepared start did not activate capture");
		Check(response["captureId"] == descriptor.captureId && contexts.at(descriptor.captureId) == "capture-start",
			"prepared response/context identity changed at activation");
		std::shared_ptr<const CompletedCapture> capture;
		Check(controller.Stop(descriptor.captureId, capture) == ControlStatus::kSuccess, "prepared capture could not stop");
		Check(controller.GetCompleted(descriptor.captureId) == capture, "successful prepared capture was not retained");
	}

	void TestPreparedRuntimeFailure()
	{
		CaptureController controller;
		CaptureDescriptor descriptor;
		CaptureStartPhase phase = CaptureStartPhase::kContext;
		std::unordered_map<std::string, std::string> contexts;
		auto invalid = Config();
		invalid.maxEvents = 0;
		const auto status = StartPreparedCapture(controller, invalid, descriptor, phase, [&](const auto& a_capture) { contexts.emplace(a_capture.captureId, "capture-start"); }, [](const auto&) {}, [&](const std::string& a_captureId) noexcept { contexts.erase(a_captureId); });
		Check(status == ControlStatus::kInvalidBounds && phase == CaptureStartPhase::kRuntime,
			"runtime admission failure was not preserved");
		Check(contexts.empty() && !controller.GetStatus().active && controller.GetStatus().completedCaptureIds.empty(),
			"runtime admission failure retained prepared capture state");
		Check(!GetRuntime().IsCapturing(), "invalid runtime admission activated hooks");
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
		Check(object["schema"]["minor"] == 19 && object["payload"]["schema"] == "scene-object-observation-v1",
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
		const ResourceViewInput srv{
			.resource = { .d3dObject = 0xA300, .dimension = ResourceDimension::kTexture2D },
			.view = { .kind = TargetViewKind::kShaderResource, .d3dObject = 0xA400 },
		};
		const ResourceViewInput changedSrv{
			.resource = { .d3dObject = 0xA300, .dimension = ResourceDimension::kTexture2D },
			.view = { .kind = TargetViewKind::kShaderResource, .d3dObject = 0xA401 },
		};
		runtime.BindResourceViews(
			0xA000, ResourceBindingKind::kShaderResource, ResourceStage::kPixel, 2, 1, &srv,
			false, ResourceBindingSource::kPostCallQuery);
		runtime.RecordExecuteCommandList(0xA000, 0xA200, false);
		runtime.BindResourceViews(
			0xA000, ResourceBindingKind::kShaderResource, ResourceStage::kPixel, 2, 1, &srv,
			false, ResourceBindingSource::kPostCallQuery);
		runtime.BindResourceViews(
			0xA000, ResourceBindingKind::kShaderResource, ResourceStage::kPixel, 2, 1, &changedSrv,
			false, ResourceBindingSource::kPostCallQuery);
		runtime.BindResourceViews(
			0xA000, ResourceBindingKind::kShaderResource, ResourceStage::kPixel, 2, 1, nullptr,
			false, ResourceBindingSource::kPostCallQuery);
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
		std::vector<nlohmann::json> effectiveBindings;
		for (const auto& event : page["events"]) {
			if (event["type"] == "resource-view-bind" &&
				event["payload"]["source"] == "post-call-query") {
				effectiveBindings.push_back(event);
			}
		}
		Check(effectiveBindings.size() == 4,
			std::format("serialized {} effective SRV bindings instead of four", effectiveBindings.size()));
		const auto& initialViewId = effectiveBindings[0]["payload"]["viewObservationId"];
		const auto& reboundViewId = effectiveBindings[1]["payload"]["viewObservationId"];
		const auto& changedViewId = effectiveBindings[2]["payload"]["viewObservationId"];
		const auto& nullViewId = effectiveBindings[3]["payload"]["viewObservationId"];
		Check(initialViewId.is_string() && reboundViewId.is_string() &&
				  initialViewId == reboundViewId,
			"serialized restore-false execution lost the same-view effective SRV identity");
		Check(changedViewId.is_string() && changedViewId != reboundViewId,
			"serialized effective SRV identity did not change with the view");
		Check(nullViewId.is_null(), "serialized null effective SRV retained a view identity");
		Check(std::all_of(effectiveBindings.begin(), effectiveBindings.end(),
				  [](const nlohmann::json& a_event) { return a_event["payload"]["slot"] == 2; }),
			"serialized effective SRV identity controls changed the tested slot");

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

	bool g_failPublicationAllocation{ false };

	template <class T>
	struct PublicationFaultAllocator
	{
		using value_type = T;
		PublicationFaultAllocator() = default;
		template <class U>
		PublicationFaultAllocator(const PublicationFaultAllocator<U>&) noexcept
		{}
		T* allocate(std::size_t a_count)
		{
			if (std::exchange(g_failPublicationAllocation, false))
				throw std::bad_alloc();
			return std::allocator<T>{}.allocate(a_count);
		}
		void deallocate(T* a_pointer, std::size_t a_count) noexcept
		{
			std::allocator<T>{}.deallocate(a_pointer, a_count);
		}
		template <class U>
		bool operator==(const PublicationFaultAllocator<U>&) const noexcept
		{
			return true;
		}
	};

	void TestPreparedArtifactPublication()
	{
		const auto root = std::filesystem::temp_directory_path() /
		                  std::format("csx-render-map-publication-test-{}", std::chrono::steady_clock::now().time_since_epoch().count());
		CaptureController controller;
		const auto capture = MakeCapture(controller);
		using Entry = std::pair<const std::string, PreparedCapturePublication>;
		std::unordered_map<std::string, PreparedCapturePublication, std::hash<std::string>,
			std::equal_to<std::string>, PublicationFaultAllocator<Entry>>
			cache;
		std::size_t writes = 0;
		auto write = [&] {
			++writes;
			return WriteCaptureArtifacts(*capture, ArtifactContext(root), 42);
		};
		auto respond = [](const CaptureArtifactBundle& a_bundle) { return SerializeArtifactBundle(a_bundle); };

		g_failPublicationAllocation = true;
		bool admissionFailed = false;
		try {
			WithPreparedCaptureArtifacts(cache, capture->descriptor.captureId, write, respond);
		} catch (const std::bad_alloc&) {
			admissionFailed = true;
		}
		Check(admissionFailed && !g_failPublicationAllocation && cache.empty() && writes == 0 &&
				  !std::filesystem::exists(root),
			"cache admission failure published artifacts or retained a placeholder");

		bool writerFailed = false;
		try {
			WithPreparedCaptureArtifacts(cache, capture->descriptor.captureId, []() -> CaptureArtifactBundle { throw std::bad_alloc(); }, respond);
		} catch (const std::bad_alloc&) {
			writerFailed = true;
		}
		Check(writerFailed && cache.empty() && writes == 0 && !std::filesystem::exists(root),
			"writer exception retained an unfinished publication");

		bool responseFailed = false;
		try {
			WithPreparedCaptureArtifacts(cache, capture->descriptor.captureId, write,
				[](const CaptureArtifactBundle&) -> nlohmann::json { throw std::bad_alloc(); });
		} catch (const std::bad_alloc&) {
			responseFailed = true;
		}
		Check(responseFailed && writes == 1 && cache.size() == 1,
			"response failure lost the completed publication");
		const auto& retained = cache.at(capture->descriptor.captureId);
		Check(retained.finished && retained.artifacts.success, "verified publication was not committed before response construction");
		const auto expected = SerializeArtifactBundle(retained.artifacts);
		const auto eventsPath = retained.artifacts.directory / "events.jsonl";
		const auto manifestPath = retained.artifacts.directory / "capture-manifest.json";
		auto readBytes = [](const std::filesystem::path& a_path) {
			std::ifstream stream(a_path, std::ios::binary);
			Check(stream.is_open(), "published artifact cannot be read");
			return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
		};
		const auto events = readBytes(eventsPath);
		const auto manifest = readBytes(manifestPath);
		const auto eventsTime = std::filesystem::last_write_time(eventsPath);
		const auto manifestTime = std::filesystem::last_write_time(manifestPath);
		std::shared_ptr<const CompletedCapture> stoppedAgain;
		Check(controller.Stop(capture->descriptor.captureId, stoppedAgain) == ControlStatus::kSuccess && stoppedAgain == capture,
			"fresh stop did not recover the completed capture");
		const auto recovered = WithPreparedCaptureArtifacts(cache, stoppedAgain->descriptor.captureId, write, respond);
		Check(recovered == expected && writes == 1 && events == readBytes(eventsPath) && manifest == readBytes(manifestPath) &&
				  eventsTime == std::filesystem::last_write_time(eventsPath) && manifestTime == std::filesystem::last_write_time(manifestPath),
			"fresh stop rewrote artifacts or changed verified paths, hashes or byte counts");

		const auto foreign = MakeCapture(controller);
		const auto foreignDirectory = root / foreign->descriptor.captureId;
		std::filesystem::create_directories(foreignDirectory);
		{
			std::ofstream stream(foreignDirectory / "events.jsonl", std::ios::binary);
			stream << "unrelated-existing-file";
		}
		WithPreparedCaptureArtifacts(cache, foreign->descriptor.captureId, [&] { return WriteCaptureArtifacts(*foreign, ArtifactContext(root), 42); }, respond);
		const auto& collision = cache.at(foreign->descriptor.captureId).artifacts;
		Check(!collision.success && collision.error.find("already exists") != std::string::npos &&
				  readBytes(foreignDirectory / "events.jsonl") == "unrelated-existing-file" &&
				  !std::filesystem::exists(foreignDirectory / "capture-manifest.json"),
			"prepared publication accepted or replaced an unrelated existing file");
		std::filesystem::remove_all(root);
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
		Check(manifest["schema"]["minor"] == 9, "capture manifest schema revision is wrong");
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

	void TestSummaryAndManifestCompleteness()
	{
		struct Case
		{
			std::uint64_t CaptureStatistics::* counter;
			const char* reason;
			bool eventLoss;
		};
		const Case cases[] = {
			{ &CaptureStatistics::droppedStopped, "event-loss", true },
			{ &CaptureStatistics::droppedEventLimit, "event-loss", true },
			{ &CaptureStatistics::droppedByteLimit, "event-loss", true },
			{ &CaptureStatistics::scopeOverflow, "scope-overflow", false },
			{ &CaptureStatistics::scopeMismatch, "scope-mismatch", false },
			{ &CaptureStatistics::droppedShaderObservations, "shader-observation-loss", false },
			{ &CaptureStatistics::droppedStageShaderObservations, "stage-shader-observation-loss", false },
			{ &CaptureStatistics::droppedResourceObservations, "resource-observation-loss", false },
			{ &CaptureStatistics::droppedTargetViewObservations, "target-view-observation-loss", false },
			{ &CaptureStatistics::droppedTargetBindingObservations, "target-binding-observation-loss", false },
			{ &CaptureStatistics::droppedSceneObjectObservations, "scene-object-observation-loss", false },
			{ &CaptureStatistics::droppedGeometryObservations, "geometry-observation-loss", false },
			{ &CaptureStatistics::droppedMaterialStateObservations, "material-state-observation-loss", false },
		};
		const auto root = std::filesystem::temp_directory_path() /
		                  std::format("csx-render-map-completeness-test-{}", std::chrono::steady_clock::now().time_since_epoch().count());
		std::uint64_t index = 0;
		const auto verify = [&](CompletedCapture a_capture, const char* a_reason, bool a_evidenceLoss, bool a_eventLoss) {
			a_capture.descriptor.captureId = std::format("capture-completeness-{}", ++index);
			a_capture.snapshot.config = Config();
			const auto summary = SerializeCaptureSummary(a_capture);
			const auto bundle = WriteCaptureArtifacts(a_capture, ArtifactContext(root), 42);
			Check(bundle.success, "completeness fixture could not publish artifacts");
			std::ifstream stream(bundle.directory / "capture-manifest.json");
			nlohmann::json manifest;
			stream >> manifest;
			const auto expected = a_reason ? nlohmann::json::array({ a_reason }) : nlohmann::json::array();
			Check(summary["completion"]["incompleteReasons"] == expected &&
					  manifest["extensions"]["csx.captureIncompleteReasons"] == expected,
				"summary and manifest disagree on the exact incompleteness reasons");
			Check(summary["completion"]["incomplete"] == (a_reason != nullptr) &&
					  manifest["status"] == (a_reason ? "incomplete" : "complete") &&
					  manifest["artifacts"][0]["complete"] == (a_reason == nullptr),
				"summary and durable artifact disagree on evidence completeness");
			Check(summary["completion"]["truncated"] == a_evidenceLoss &&
					  manifest["completion"]["truncated"] == a_eventLoss &&
					  manifest["completion"]["droppedEventCount"] == (a_eventLoss ? 1 : 0),
				"evidence truncation was conflated with event loss or lifecycle failure");
		};
		for (const auto& test : cases) {
			CompletedCapture capture;
			capture.snapshot.statistics.*test.counter = 1;
			verify(std::move(capture), test.reason, true, test.eventLoss);
		}
		for (const auto reason : { StopReason::kShutdown, StopReason::kFailure }) {
			CompletedCapture capture;
			capture.snapshot.stopReason = reason;
			verify(std::move(capture), "lifecycle-failure", false, false);
		}
		CompletedCapture bounded;
		bounded.snapshot.stopReason = StopReason::kFrameLimit;
		bounded.snapshot.statistics.droppedFrameLimit = 1;
		bounded.snapshot.statistics.droppedTimeLimit = 1;
		verify(std::move(bounded), nullptr, false, false);
		std::filesystem::remove_all(root);
	}

	void TestTransferSerializationAndDurableAdmissionGap()
	{
		EventRecord bootstrap;
		bootstrap.kind = EventKind::kRasterStateObserved;
		bootstrap.payload = { .schema = static_cast<std::uint16_t>(PayloadSchema::kNativePipelineSnapshot),
			.words = { 1, 2, 3, 4, 5, 6 } };
		const auto nativeState = SerializeEvent(bootstrap, "capture-bootstrap-test", 42).at("payload");
		Check(nativeState["schema"] == "native-pipeline-snapshot-v1" && nativeState["source"] == "activation-getters" &&
				  !nativeState.contains("operationObservationId") && nativeState["nativeLifetimeJoinVerified"] == false,
			"bootstrap invented execution or object lifetime evidence");
		CaptureSnapshot windowSnapshot;
		windowSnapshot.config.latePostProcessingWindow = true;
		windowSnapshot.window = { .phase = CaptureWindowPhase::kActive, .armedTick = 10, .activationTick = 20, .cpuFrame = 12, .publicationGeneration = 77, .compositorCycle = 90, .bootstrapThreadId = 3, .bootstrapEventCount = 4, .acceptedEyeMask = 1, .bootstrapComplete = true };
		Check(!EvaluateCaptureCompleteness(windowSnapshot).reasons.empty(), "missing eye pair claimed completeness");
		windowSnapshot.window.phase = CaptureWindowPhase::kMatchedEyes;
		windowSnapshot.window.acceptedEyeMask = 3;
		Check(EvaluateCaptureCompleteness(windowSnapshot).reasons.empty(), "complete bounded window was rejected");
		windowSnapshot.statistics.droppedTargetViewObservations = 1;
		Check(!EvaluateCaptureCompleteness(windowSnapshot).reasons.empty(), "matched pair hid catalogue loss");
		const auto window = SerializeCaptureWindow(windowSnapshot.window);
		Check(window["activationBoundary"]["attemptCount"] == 0 &&
				  window["activationBoundary"]["lastTarget"].is_null() &&
				  window["activationBoundary"]["lastCpuFrame"].is_null() &&
				  window["activationBoundary"]["lastPublicationGeneration"].is_null(),
			"unobserved activation inputs invented default values");
		windowSnapshot.window.activationBoundary = { .attemptCount = 4, .targetRejections = 1, .frameRejections = 2, .publicationRejections = 2, .lastCpuFrame = 0, .lastPublicationGeneration = 0, .lastTarget = 9 };
		const auto refusedBoundary = SerializeCaptureWindow(windowSnapshot.window)["activationBoundary"];
		Check(refusedBoundary["scope"] == "armed-window-input-validation" && refusedBoundary["attemptCount"] == 4 &&
				  refusedBoundary["targetRejections"] == 1 && refusedBoundary["frameRejections"] == 2 &&
				  refusedBoundary["publicationRejections"] == 2 && refusedBoundary["lastTarget"] == 9 &&
				  refusedBoundary["lastCpuFrame"] == 0 && refusedBoundary["lastPublicationGeneration"] == 0,
			"refused activation input serialization hid observed zero values");
		Check(window["prefixEventCount"].is_null() && window["preWindowProducerHistory"] == "unobserved" &&
				  window["fullPipelineEstablished"] == false && window["pixelTransferEstablished"] == false &&
				  window["omittedPrefix"]["fromTickInclusive"] == 10 && window["omittedPrefix"]["toTickExclusive"] == 20,
			"late window promoted unknown prefix history or changed half-open bounds");
		const auto payload = [](PayloadSchema schema, std::array<std::uint64_t, 8> words) {
			EventRecord event;
			event.sessionGeneration = 77;
			event.kind = EventKind::kTransferResourceAccess;
			event.payload = { .schema = static_cast<std::uint16_t>(schema), .words = words };
			return SerializeEvent(event, "capture-transfer-test", 42);
		};
		const auto boundary = payload(PayloadSchema::kPostProcessingBoundary, { 1, 2, 3, 4, 5 });
		Check(!boundary["payload"]["actualSourceEstablishedByBoundary"].get<bool>() &&
				  boundary["observationRefs"][1]["role"] == "destination-before",
			"destination-before was relabeled as source");
		const auto access = payload(PayloadSchema::kTransferResourceAccess, { 2, 0, 9, 12, 5, 0, 3, 7 });
		Check(access["payload"]["commandVersionObservationId"] == 9 && access["payload"]["commandEpoch"] == 12 &&
				  access["payload"]["operationObservationId"] == 7 && access["payload"]["pixelReadOrWriteEstablished"] == false,
			"observed command epoch was omitted or promoted into pixel proof");
		const auto missing = payload(PayloadSchema::kTransferResourceAccess, { 2, 0, 0, 0, 5, 0, 1, 7 });
		Check(missing["payload"]["commandVersionObservationId"].is_null() &&
				  missing["payload"]["versionCatalogueAdmissionAvailable"] == false,
			"unavailable admission serialized a version");
		const auto eye = payload(PayloadSchema::kEyePublication, { 2, 9, 12, 5, 90, 1, 1 });
		Check(eye["payload"]["renderTargetPublicationGeneration"] == 5 &&
				  eye["payload"]["acceptedWithCurrentPublicationLease"] == true &&
				  eye["payload"]["causalPixelTransferEstablished"] == false,
			"accepted publication overstated pixel lineage");
		const auto copy = payload(PayloadSchema::kTransferCopyRegion, { 7, 2ull | (3ull << 32), 4ull | (5ull << 32), 6, 0, 0, 0, 0 });
		Check(copy["payload"]["sourceSubresource"] == 2 && copy["payload"]["destinationSubresource"] == 3 &&
				  copy["payload"]["sourceBox"].is_null() && copy["payload"]["nullBoxMeansEntireSourceSubresource"] == true,
			"copy-subresource or null source-box semantics were lost");

		CaptureController controller;
		auto config = Config();
		config.maxEvents = 2048;
		config.maxBytes = Collector::RequiredStorageBytes(config);
		CaptureDescriptor descriptor;
		Check(controller.Start(config, descriptor) == ControlStatus::kSuccess, "capacity-gap capture did not start");
		auto& runtime = GetRuntime();
		runtime.SetImmediateContext(0xB000);
		runtime.SetCpuFrame(12);
		{
			auto scope = runtime.EnterPostProcessing({}, {}, 9, 77);
			Check(runtime.IsInsidePostProcessing(), "capacity-gap boundary did not enter");
			runtime.BeginTransferOperation(0xB000, true, 0, {});
			for (std::uint64_t index = 1; index <= TransferVersions::kCapacity + 1; ++index)
				runtime.RecordTransferResourceAccess(0xB000,
					{ .resource = { .d3dObject = 0xB100 + index, .dimension = ResourceDimension::kTexture2D, .widthOrBytes = 64, .height = 32, .depthOrArraySize = 1, .mipLevels = 1 } },
					ResourceStage::kCompute, 0, true);
		}
		std::shared_ptr<const CompletedCapture> capture;
		Check(controller.Stop(descriptor.captureId, capture) == ControlStatus::kSuccess && capture,
			"capacity-gap capture did not stop");
		const auto summary = SerializeCaptureSummary(*capture);
		Check(summary["completion"]["observedTransferVersionAdmissionFailures"] == 1 &&
				  summary["completion"]["maximumTransferVersionResources"] == 256 &&
				  summary["completion"]["truncated"] == true && summary["completion"]["droppedEventCount"] == 0,
			"version gap was conflated with event drops or hidden from summary");
		const auto root = std::filesystem::temp_directory_path() /
		                  std::format("csx-render-map-transfer-test-{}", std::chrono::steady_clock::now().time_since_epoch().count());
		const auto bundle = WriteCaptureArtifacts(*capture, ArtifactContext(root), 42);
		Check(bundle.success, "capacity-gap artifact write failed");
		std::ifstream stream(bundle.directory / "capture-manifest.json");
		nlohmann::json manifest;
		stream >> manifest;
		Check(manifest["status"] == "incomplete" && manifest["artifacts"][0]["complete"] == false &&
				  manifest["extensions"]["csx.observedTransferVersionAdmissionFailures"] == 1 &&
				  !manifest["completion"]["errors"].empty(),
			"durable manifest concealed the version admission gap");
		Check(manifest["extensions"]["csx.captureIncompleteReasons"] == summary["completion"]["incompleteReasons"] &&
				  summary["completion"]["incompleteReasons"] == nlohmann::json::array({ "transfer-version-admission-failure" }) &&
				  summary["completion"]["incomplete"] == true && manifest["completion"]["truncated"] == false,
			"transfer admission failure diverged between summary and manifest projections");
		stream.close();
		std::filesystem::remove_all(root);
	}

}

int main()
{
	try {
		TestTransferSerializationAndDurableAdmissionGap();
		TestSummaryAndManifestCompleteness();
		TestPayloadSchemaCatalogue();
		TestResourceCpuAccessBoundaries();
		TestRuntimeArtifactIdentity();
		TestPreparedStartFailures();
		TestPreparedStartSuccess();
		TestPreparedRuntimeFailure();
		TestControllerAndSerialization();
		TestStopActiveWithoutCaptureId();
		TestCompletedHistoryBound();
		TestResolvedStageSerialization();
		TestSemanticIdentitySerialization();
		TestDeferredCommandSerialization();
		TestPreparedArtifactPublication();
		TestDurableArtifacts();
		TestGapArtifact();
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
