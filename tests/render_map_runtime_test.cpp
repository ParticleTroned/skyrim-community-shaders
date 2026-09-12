#include "RenderMap/Runtime.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
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
			.captureNumericId = 77,
			.maxFrames = 2,
			.maxEvents = 32,
			.maxBytes = 1,
			.maxDuration = std::chrono::minutes(1),
			.maxScopeDepth = 8,
		};
		config.maxBytes = Collector::RequiredStorageBytes(config);
		return config;
	}

	void TestInactiveRuntime()
	{
		Runtime runtime;
		runtime.SetCpuFrame(12);
		Check(!runtime.EnterRenderPass({ .renderPass = 1 }).IsActive(), "inactive render pass entered");
		Check(!runtime.EnterTechnique({ .shader = 2 }).IsActive(), "inactive technique entered");
		Check(!runtime.EnterGeometry({ .geometry = 3 }).IsActive(), "inactive geometry entered");
	}

	void TestNestedBoundaries()
	{
		Runtime runtime;
		Check(runtime.StartCapture(Config()) == StartResult::kStarted, "runtime capture did not start");
		runtime.SetFrameContext({ 50, 60, 70, Eye::kLeft, 1 });
		{
			auto pass = runtime.EnterRenderPass({
				.renderPass = 0x1000,
				.geometry = 0x2000,
				.technique = 4,
				.passEnum = 5,
				.renderFlags = 6,
				.alphaTest = true,
			});
			Check(pass.IsActive(), "render-pass boundary did not enter");
			{
				auto technique = runtime.EnterTechnique({
					.shader = 0x3000,
					.shaderType = 7,
					.vertexDescriptor = 8,
					.pixelDescriptor = 9,
					.callerRva = 10,
					.skipPixelShader = true,
					.fxpFilename = "Lighting",
				});
				Check(technique.IsActive(), "technique boundary did not enter");
			}
			{
				auto geometry = runtime.EnterGeometry({
					.shader = 0x3000,
					.renderPass = 0x1000,
					.geometry = 0x2000,
					.shaderType = 7,
					.passEnum = 5,
					.renderFlags = 6,
				});
				Check(geometry.IsActive(), "geometry boundary did not enter");
			}
		}

		auto snapshot = runtime.StopCapture();
		Check(snapshot.has_value(), "runtime capture did not stop");
		Check(snapshot->events.size() == 8, "runtime boundary event count is wrong");
		Check(snapshot->events[0].kind == EventKind::kRenderPassEnter, "render-pass begin is missing");
		Check(snapshot->events[1].kind == EventKind::kShaderObserved, "first-seen shader event is missing");
		Check(snapshot->events[2].kind == EventKind::kTechniqueBegin, "technique begin is missing");
		Check(snapshot->events[3].kind == EventKind::kTechniqueEnd, "technique end is missing");
		Check(snapshot->events[4].kind == EventKind::kGeometryObserved, "geometry declaration is missing");
		Check(snapshot->events[5].kind == EventKind::kGeometrySetupBegin, "geometry begin is missing");
		Check(snapshot->events[6].kind == EventKind::kGeometrySetupEnd, "geometry end is missing");
		Check(snapshot->events[7].kind == EventKind::kRenderPassExit, "render-pass end is missing");

		const auto passObservation = snapshot->events[0].scopes.renderPass.observationId;
		Check(passObservation != 0, "render-pass observation is missing");
		for (const auto& event : snapshot->events) {
			Check(event.frame.cpuFrame == 50, "runtime frame context was not propagated");
			Check(event.scopes.renderPass.observationId == passObservation, "nested event lost its render-pass scope");
		}
		Check(snapshot->events[0].payload.schema == static_cast<std::uint16_t>(PayloadSchema::kRenderPassBoundary),
			"render-pass payload schema is wrong");
		Check(snapshot->events[0].payload.words[0] == 0x1000, "render-pass pointer evidence is wrong");
		Check(snapshot->events[0].payload.words[5] == 1, "render-pass alpha-test evidence is wrong");
		Check(snapshot->events[2].payload.schema == static_cast<std::uint16_t>(PayloadSchema::kTechniqueBoundary),
			"technique payload schema is wrong");
		Check(snapshot->events[2].payload.words[4] == 9, "technique descriptor evidence is wrong");
		Check(snapshot->events[2].payload.words[0] != 0, "technique did not reference its shader observation");
		Check(snapshot->events[5].payload.schema == static_cast<std::uint16_t>(PayloadSchema::kGeometryBoundaryV2),
			"geometry payload schema is wrong");
		Check(snapshot->events[5].payload.words[2] == 0x2000, "geometry pointer evidence is wrong");
		Check(snapshot->events[5].payload.words[6] == snapshot->geometryObservations[0].observationId,
			"geometry boundary did not reference its semantic declaration");
		Check(snapshot->shaderObservations.size() == 1, "shader observation catalog is wrong");
		Check(std::string_view(snapshot->shaderObservations[0].fxpFilename.data()) == "Lighting",
			"shader filename was not retained");
	}

	void TestShaderIdentityGenerations()
	{
		Runtime runtime;
		auto config = Config();
		config.maxShaderObservations = 8;
		Check(runtime.StartCapture(config) == StartResult::kStarted, "identity capture did not start");
		{
			auto first = runtime.EnterTechnique({
				.shader = 0x4444,
				.shaderType = 5,
				.fxpFilename = "ImageSpace",
				.imageSpaceName = "ISCopy",
			});
		}
		{
			auto repeated = runtime.EnterTechnique({
				.shader = 0x4444,
				.shaderType = 5,
				.fxpFilename = "ImageSpace",
				.imageSpaceName = "ISCopy",
			});
		}
		{
			auto changed = runtime.EnterTechnique({
				.shader = 0x4444,
				.shaderType = 5,
				.fxpFilename = "ImageSpace",
				.imageSpaceName = "ISBlur3",
			});
		}
		runtime.RetireShaderObservation(0x4444);
		{
			auto reused = runtime.EnterTechnique({
				.shader = 0x4444,
				.shaderType = 5,
				.fxpFilename = "ImageSpace",
				.imageSpaceName = "ISBlur3",
			});
		}

		auto snapshot = runtime.StopCapture();
		Check(snapshot && snapshot->shaderObservations.size() == 3,
			"shader identity reuse or retirement produced the wrong catalog size");
		Check(snapshot->shaderObservations[0].pointerGeneration == 1,
			"first pointer generation is wrong");
		Check(snapshot->shaderObservations[1].pointerGeneration == 2,
			"changed semantic identity did not advance pointer generation");
		Check(snapshot->shaderObservations[2].pointerGeneration == 3,
			"retired pointer identity was merged on reuse");
		const auto observedEvents = std::count_if(
			snapshot->events.begin(), snapshot->events.end(),
			[](const EventRecord& a_event) { return a_event.kind == EventKind::kShaderObserved; });
		Check(observedEvents == 3, "shader first-seen events were not deduplicated");
	}

	void TestCpuFrameUpdatePreservesEyeContext()
	{
		Runtime runtime;
		Check(runtime.StartCapture(Config()) == StartResult::kStarted, "frame-update capture did not start");
		runtime.SetFrameContext({ 1, 2, 3, Eye::kRight, 2 });
		runtime.SetCpuFrame(99);
		{
			auto pass = runtime.EnterRenderPass({ .renderPass = 1 });
			Check(pass.IsActive(), "frame-update boundary did not enter");
		}
		auto snapshot = runtime.StopCapture();
		Check(snapshot->events[0].frame.cpuFrame == 99, "CPU frame was not updated");
		Check(snapshot->events[0].frame.sceneEpoch == 2, "scene epoch was not preserved");
		Check(snapshot->events[0].frame.eye == Eye::kRight, "eye context was not preserved");
	}

	void TestShaderObservationBoundIsExplicit()
	{
		Runtime runtime;
		auto config = Config();
		config.maxShaderObservations = 1;
		Check(runtime.StartCapture(config) == StartResult::kStarted, "bounded identity capture did not start");
		{
			auto first = runtime.EnterTechnique({ .shader = 1, .shaderType = 1, .fxpFilename = "Grass" });
		}
		{
			auto overflow = runtime.EnterTechnique({ .shader = 2, .shaderType = 2, .fxpFilename = "Sky" });
		}
		auto snapshot = runtime.StopCapture();
		Check(snapshot && snapshot->shaderObservations.size() == 1,
			"shader observation catalog exceeded its bound");
		Check(snapshot->statistics.droppedShaderObservations == 1,
			"shader observation overflow was not reported");
		const auto secondTechnique = std::find_if(
			snapshot->events.rbegin(), snapshot->events.rend(),
			[](const EventRecord& a_event) { return a_event.kind == EventKind::kTechniqueBegin; });
		Check(secondTechnique != snapshot->events.rend() && secondTechnique->payload.words[0] == 0,
			"overflowed shader was silently joined to an existing observation");
	}

	void TestResolvedStageShaderIdentity()
	{
		Runtime runtime;
		auto config = Config();
		config.maxStageShaderObservations = 4;
		Check(runtime.StartCapture(config) == StartResult::kStarted, "stage identity capture did not start");
		{
			auto technique = runtime.EnterTechnique({
				.shader = 0x1000,
				.shaderType = 6,
				.vertexDescriptor = 7,
				.pixelDescriptor = 8,
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
						.stage = ShaderStage::kVertex,
						.wrapper = 0x2000,
						.d3dObject = 0x3000,
						.wrapperDescriptor = 17,
						.bytecodeSize = 128,
						.bytecodeSha256 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
						.cachePath = "Data/ShaderCache/Lighting/11.vso",
					},
				},
				.pixel = {
					.route = ShaderSelectionRoute::kEngine,
					.shader = {
						.stage = ShaderStage::kPixel,
						.wrapper = 0x4000,
						.d3dObject = 0x5000,
						.wrapperDescriptor = 18,
						.bytecodeSize = 256,
						.bytecodeSha256 = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
					},
				},
			});
		}
		auto snapshot = runtime.StopCapture();
		Check(snapshot && snapshot->stageShaderObservations.size() == 2,
			"resolved stage shaders were not catalogued");
		Check(snapshot->events.size() == 6, "resolved technique event sequence is wrong");
		Check(snapshot->events[2].kind == EventKind::kStageShaderObserved,
			"vertex stage first-seen event is missing");
		Check(snapshot->events[3].kind == EventKind::kStageShaderObserved,
			"pixel stage first-seen event is missing");
		Check(snapshot->events[4].kind == EventKind::kTechniqueResolved,
			"technique resolution event is missing");
		Check(snapshot->events[4].payload.words[2] == 17 && snapshot->events[4].payload.words[3] == 18,
			"resolved descriptors are wrong");
		Check(snapshot->events[4].payload.words[4] != 0 && snapshot->events[4].payload.words[5] != 0,
			"resolution did not reference both selected stage shaders");
		Check(std::string_view(snapshot->stageShaderObservations[0].cachePath.data()) ==
				  "Data/ShaderCache/Lighting/11.vso",
			"stage cache path was not retained");
	}

	void TestStageShaderObservationBoundIsExplicit()
	{
		Runtime runtime;
		auto config = Config();
		config.maxStageShaderObservations = 1;
		Check(runtime.StartCapture(config) == StartResult::kStarted, "bounded stage capture did not start");
		{
			auto technique = runtime.EnterTechnique({ .shader = 1, .shaderType = 1 });
			runtime.RecordTechniqueResolution({
				.shaderFound = true,
				.vertex = {
					.route = ShaderSelectionRoute::kEngine,
					.shader = { .stage = ShaderStage::kVertex, .wrapper = 2, .d3dObject = 3 },
				},
				.pixel = {
					.route = ShaderSelectionRoute::kEngine,
					.shader = { .stage = ShaderStage::kPixel, .wrapper = 4, .d3dObject = 5 },
				},
			});
		}
		auto snapshot = runtime.StopCapture();
		Check(snapshot && snapshot->stageShaderObservations.size() == 1,
			"stage shader catalogue exceeded its bound");
		Check(snapshot->statistics.droppedStageShaderObservations == 1,
			"stage shader overflow was not reported");
		const auto resolved = std::find_if(
			snapshot->events.begin(), snapshot->events.end(),
			[](const EventRecord& a_event) { return a_event.kind == EventKind::kTechniqueResolved; });
		Check(resolved != snapshot->events.end() && resolved->payload.words[4] != 0 && resolved->payload.words[5] == 0,
			"overflowed stage shader was silently joined to an existing observation");
	}

	void TestImmediateContextDrawAndDispatchState()
	{
		Runtime runtime;
		auto config = Config();
		config.maxEvents = 64;
		config.maxStageShaderObservations = 8;
		config.maxBytes = Collector::RequiredStorageBytes(config);
		runtime.SetImmediateContext(0x9000);
		Check(runtime.StartCapture(config) == StartResult::kStarted, "draw capture did not start");
		runtime.BindStage(0x9000, ShaderStage::kVertex, 0x3000);
		runtime.BindStage(0x9000, ShaderStage::kPixel, 0x5000);
		runtime.BindStage(0x9000, ShaderStage::kCompute, 0x7000);
		runtime.RecordTechniqueResolution({
			.shaderFound = true,
			.vertex = {
				.route = ShaderSelectionRoute::kCSXCache,
				.shader = { .stage = ShaderStage::kVertex, .wrapper = 0x2000, .d3dObject = 0x3000, .wrapperDescriptor = 17, .bytecodeSize = 128 },
			},
			.pixel = {
				.route = ShaderSelectionRoute::kEngine,
				.shader = { .stage = ShaderStage::kPixel, .wrapper = 0x4000, .d3dObject = 0x5000, .wrapperDescriptor = 18, .bytecodeSize = 256 },
			},
		});
		runtime.RecordDraw(0x9000, DrawOperation::kDrawIndexed, 24, 3, 2);
		runtime.RecordDraw(0x9001, DrawOperation::kDraw, 99);
		runtime.RecordDispatch(0x9000, DispatchOperation::kDispatch, 8, 4, 2);

		auto snapshot = runtime.StopCapture();
		Check(snapshot && snapshot->stageShaderObservations.size() == 3,
			std::format("draw and dispatch retained {} stage identities, expected 3",
				snapshot ? snapshot->stageShaderObservations.size() : 0));
		const auto context = std::find_if(snapshot->events.begin(), snapshot->events.end(),
			[](const EventRecord& a_event) { return a_event.kind == EventKind::kDeviceContextObserved; });
		Check(context != snapshot->events.end(), "immediate context was not declared");
		Check(context->deviceContextObservationId != 0 && context->payload.words[0] == context->deviceContextObservationId,
			"immediate-context declaration identity is inconsistent");
		Check(context->payload.words[1] == 0x9000 && context->payload.words[2] == 1,
			"immediate-context pointer evidence is wrong");
		Check(std::count_if(snapshot->events.begin(), snapshot->events.end(),
				  [](const EventRecord& a_event) { return a_event.kind == EventKind::kDeviceContextObserved; }) == 1,
			"immediate context was declared more than once");
		const auto draw = std::find_if(snapshot->events.begin(), snapshot->events.end(),
			[](const EventRecord& a_event) { return a_event.kind == EventKind::kDraw; });
		Check(draw != snapshot->events.end(), "immediate-context draw was not recorded");
		Check(draw->payload.schema == static_cast<std::uint16_t>(PayloadSchema::kDrawCall),
			"draw payload schema is wrong");
		Check(draw->payload.words[0] == 0x9000 && draw->payload.words[2] != 0 && draw->payload.words[3] != 0,
			"draw did not retain context and selected stage references");
		Check(draw->payload.words[4] == 24 && draw->payload.words[5] == 3 && draw->payload.words[6] == 2,
			"draw arguments are wrong");
		Check(draw->deviceContextObservationId == context->deviceContextObservationId,
			"draw did not join to the declared immediate context");
		Check(draw->commandStreamSequence == 4,
			"draw command-stream sequence did not include the three observed stage binds");
		Check(std::count_if(snapshot->events.begin(), snapshot->events.end(),
				  [](const EventRecord& a_event) { return a_event.kind == EventKind::kDraw; }) == 1,
			"non-immediate context draw was recorded");
		const auto dispatch = std::find_if(snapshot->events.begin(), snapshot->events.end(),
			[](const EventRecord& a_event) { return a_event.kind == EventKind::kDispatch; });
		Check(dispatch != snapshot->events.end() && dispatch->payload.words[2] != 0,
			"dispatch did not reference its compute shader");
		Check(dispatch->payload.words[3] == 8 && dispatch->payload.words[4] == 4 &&
				  dispatch->payload.words[5] == 2,
			"dispatch arguments are wrong");
		Check(dispatch->deviceContextObservationId == context->deviceContextObservationId,
			"dispatch did not join to the declared immediate context");
		Check(dispatch->commandStreamSequence == 5,
			"dispatch command-stream sequence is not monotonic after draw");
	}

	void TestCaptureStartSeedsInheritedStageIdentity()
	{
		Runtime runtime;
		auto config = Config();
		config.maxEvents = 16;
		config.maxStageShaderObservations = 4;
		config.maxBytes = Collector::RequiredStorageBytes(config);
		runtime.SetImmediateContext(0xA000);
		runtime.RegisterCreatedStageShader(
			ShaderStage::kVertex, 0xA100, 128,
			"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
		runtime.RegisterCreatedStageShader(
			ShaderStage::kPixel, 0xA200, 256,
			"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
		runtime.RegisterEngineStageShader(ShaderStage::kVertex, 0xA100, "Lighting", 17, "Lighting");
		runtime.RegisterEngineStageShader(ShaderStage::kVertex, 0xA100, "Lighting", 17, "Lighting");
		runtime.RegisterEngineStageShader(ShaderStage::kVertex, 0xA100, "Utility", 9, "Utility");
		runtime.RegisterEngineStageShader(ShaderStage::kPixel, 0xA200, "ISHDRDownSample4", 18, "ISHDR");
		// These binds predate the capture. StartCapture clears only capture-local
		// observation IDs, while retaining the actual immediate-context state.
		runtime.BindStage(0xA000, ShaderStage::kVertex, 0xA100);
		runtime.BindStage(0xA000, ShaderStage::kPixel, 0xA200);

		Check(runtime.StartCapture(config) == StartResult::kStarted,
			"inherited-stage capture did not start");
		runtime.RecordDraw(0xA000, DrawOperation::kDrawIndexed, 6, 0, 0);
		auto snapshot = runtime.StopCapture();

		Check(snapshot && snapshot->stageShaderObservations.size() == 2,
			"first captured draw did not seed inherited stage identities");
		Check(snapshot->stageShaderObservations[0].bytecodeSize == 128 &&
				  std::string_view(snapshot->stageShaderObservations[0].bytecodeSha256.data()) ==
					  "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
			"inherited vertex bytecode provenance is missing");
		Check(snapshot->stageShaderObservations[1].bytecodeSize == 256 &&
				  std::string_view(snapshot->stageShaderObservations[1].bytecodeSha256.data()) ==
					  "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
			"inherited pixel bytecode provenance is missing");
		Check(snapshot->stageShaderObservations[0].engineAliasCount == 2 &&
				  snapshot->stageShaderObservations[0].engineAliasTotalCount == 2 &&
				  std::string_view(snapshot->stageShaderObservations[0].engineAliases[0].loaderType.data()) == "Lighting" &&
				  std::string_view(snapshot->stageShaderObservations[0].engineAliases[0].compileSourceName.data()) == "Lighting" &&
				  snapshot->stageShaderObservations[0].engineAliases[0].descriptor == 17 &&
				  std::string_view(snapshot->stageShaderObservations[0].engineAliases[1].loaderType.data()) == "Utility" &&
				  std::string_view(snapshot->stageShaderObservations[0].engineAliases[1].compileSourceName.data()) == "Utility" &&
				  snapshot->stageShaderObservations[0].engineAliases[1].descriptor == 9 &&
				  !snapshot->stageShaderObservations[0].engineAliasesTruncated,
			"inherited engine shader aliases were not retained or deduplicated");
		Check(snapshot->stageShaderObservations[1].engineAliasCount == 1 &&
				  std::string_view(snapshot->stageShaderObservations[1].engineAliases[0].loaderType.data()) ==
					  "ISHDRDownSample4" &&
				  std::string_view(snapshot->stageShaderObservations[1].engineAliases[0].compileSourceName.data()) ==
					  "ISHDR",
			"ImageSpace loader alias did not retain its distinct shared compile source");
		const auto draw = std::find_if(snapshot->events.begin(), snapshot->events.end(),
			[](const EventRecord& a_event) { return a_event.kind == EventKind::kDraw; });
		Check(draw != snapshot->events.end() && draw->payload.words[2] != 0 && draw->payload.words[3] != 0,
			"first captured draw did not join inherited VS/PS identities");
		Check(draw->commandStreamSequence == 1,
			"synthetic inherited-state declarations changed command ordering");
	}

	void TestCreatedStagePointerReuseAdvancesIdentity()
	{
		Runtime runtime;
		auto config = Config();
		config.maxEvents = 16;
		config.maxStageShaderObservations = 4;
		config.maxBytes = Collector::RequiredStorageBytes(config);
		runtime.SetImmediateContext(0xB000);
		Check(runtime.StartCapture(config) == StartResult::kStarted,
			"stage-reuse capture did not start");

		runtime.RegisterCreatedStageShader(
			ShaderStage::kVertex, 0xB100, 64,
			"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
		runtime.BindStage(0xB000, ShaderStage::kVertex, 0xB100);
		runtime.RecordDraw(0xB000, DrawOperation::kDraw, 3);

		runtime.RegisterCreatedStageShader(
			ShaderStage::kVertex, 0xB100, 96,
			"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc");
		runtime.BindStage(0xB000, ShaderStage::kVertex, 0xB100);
		runtime.RecordDraw(0xB000, DrawOperation::kDraw, 4);
		auto snapshot = runtime.StopCapture();

		Check(snapshot && snapshot->stageShaderObservations.size() == 2,
			"reused D3D pointer was merged across creation identities");
		Check(snapshot->stageShaderObservations[0].pointerGeneration == 1 &&
				  snapshot->stageShaderObservations[1].pointerGeneration == 2,
			"reused D3D pointer did not advance its capture-local generation");
		Check(snapshot->stageShaderObservations[1].bytecodeSize == 96 &&
				  std::string_view(snapshot->stageShaderObservations[1].bytecodeSha256.data()) ==
					  "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
			"reused D3D pointer retained stale creation provenance");
		std::vector<std::uint64_t> vertexIds;
		for (const auto& event : snapshot->events) {
			if (event.kind == EventKind::kDraw)
				vertexIds.push_back(event.payload.words[2]);
		}
		Check(vertexIds.size() == 2 && vertexIds[0] != 0 && vertexIds[1] != 0 &&
				  vertexIds[0] != vertexIds[1],
			"draws did not distinguish reused stage objects");
	}

	void TestStageShaderEvidenceEnrichesWithoutPointerReuse()
	{
		Runtime runtime;
		auto config = Config();
		config.maxEvents = 32;
		config.maxStageShaderObservations = 4;
		config.maxBytes = Collector::RequiredStorageBytes(config);
		runtime.SetImmediateContext(0xC000);
		Check(runtime.StartCapture(config) == StartResult::kStarted,
			"stage-enrichment capture did not start");

		constexpr auto bytecodeSha =
			"dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";
		runtime.RegisterCreatedStageShader(ShaderStage::kVertex, 0xC100, 128, bytecodeSha);
		runtime.BindStage(0xC000, ShaderStage::kVertex, 0xC100);
		runtime.RegisterEngineStageShader(ShaderStage::kVertex, 0xC100, "Effect", 66, "Effect");
		runtime.RecordTechniqueResolution({
			.inputVertexDescriptor = 66,
			.resolvedVertexDescriptor = 66,
			.shaderFound = true,
			.vertex = {
				.route = ShaderSelectionRoute::kCSXCache,
				.shader = {
					.stage = ShaderStage::kVertex,
					.wrapper = 0xC200,
					.d3dObject = 0xC100,
					.wrapperDescriptor = 66,
					.bytecodeSize = 128,
					.bytecodeSha256 = bytecodeSha,
					.cachePath = "Data/ShaderCache/Effect/42.vso",
				},
			},
		});
		runtime.RecordDraw(0xC000, DrawOperation::kDrawIndexedInstanced, 6, 2);
		auto snapshot = runtime.StopCapture();

		Check(snapshot && snapshot->stageShaderObservations.size() == 1,
			"compatible selected-stage evidence was mistaken for pointer reuse");
		const auto& shader = snapshot->stageShaderObservations[0];
		Check(shader.pointerGeneration == 1 && shader.wrapperEvidence == 0xC200 &&
				  shader.wrapperDescriptor == 66,
			"selected-stage wrapper evidence did not enrich the original observation");
		Check(shader.bytecodeSize == 128 && std::string_view(shader.bytecodeSha256.data()) == bytecodeSha &&
				  std::string_view(shader.cachePath.data()) == "Data/ShaderCache/Effect/42.vso",
			"selected-stage bytecode or cache provenance was not retained");
		Check(shader.engineAliasCount == 1 && shader.engineAliasTotalCount == 1 &&
				  std::string_view(shader.engineAliases[0].loaderType.data()) == "Effect" &&
				  std::string_view(shader.engineAliases[0].compileSourceName.data()) == "Effect" &&
				  shader.engineAliases[0].descriptor == 66,
			"selected-stage engine alias did not enrich the original observation");

		const auto resolved = std::find_if(snapshot->events.begin(), snapshot->events.end(),
			[](const EventRecord& a_event) { return a_event.kind == EventKind::kTechniqueResolved; });
		const auto draw = std::find_if(snapshot->events.begin(), snapshot->events.end(),
			[](const EventRecord& a_event) { return a_event.kind == EventKind::kDraw; });
		Check(resolved != snapshot->events.end() && draw != snapshot->events.end() &&
				  resolved->payload.words[4] == shader.observationId &&
				  draw->payload.words[2] == shader.observationId,
			"technique resolution and draw did not join the enriched stage identity");
	}

	void TestImmediateContextOutputMergerState()
	{
		Runtime runtime;
		auto config = Config();
		config.maxEvents = 64;
		config.maxBytes = Collector::RequiredStorageBytes(config);
		runtime.SetImmediateContext(0xB000);
		Check(runtime.StartCapture(config) == StartResult::kStarted, "output-merger capture did not start");

		const std::uintptr_t renderTargets[] = { 0xB100, 0xB200, 0 };
		runtime.BindRenderTargets(0xB000, 3, renderTargets, 0xB300);
		runtime.BindRenderTargets(0xB000, 3, renderTargets, 0xB300);
		runtime.RecordDraw(0xB000, DrawOperation::kDraw, 3);
		runtime.BindRenderTargets(0xB000, 0, nullptr, 0, true);
		runtime.RecordDraw(0xB000, DrawOperation::kDraw, 4);
		runtime.BindRenderTargets(0xB000, 0, nullptr, 0);
		runtime.RecordDraw(0xB000, DrawOperation::kDraw, 5);

		auto snapshot = runtime.StopCapture();
		Check(snapshot && snapshot->targetViewObservations.size() == 3,
			"render and depth target views were not deduplicated");
		Check(snapshot->targetBindingObservations.size() == 2,
			"bound and explicitly unbound target sets were not catalogued exactly once");
		Check(std::count_if(snapshot->events.begin(), snapshot->events.end(),
				  [](const EventRecord& a_event) { return a_event.kind == EventKind::kTargetViewObserved; }) == 3,
			"target-view first-seen declarations are wrong");
		Check(std::count_if(snapshot->events.begin(), snapshot->events.end(),
				  [](const EventRecord& a_event) { return a_event.kind == EventKind::kRenderTargetBind; }) == 3,
			"output-merger bind calls were not preserved");

		std::vector<const EventRecord*> draws;
		for (const auto& event : snapshot->events) {
			if (event.kind == EventKind::kDraw)
				draws.push_back(std::addressof(event));
		}
		Check(draws.size() == 3, "output-merger test draw count is wrong");
		const auto boundId = snapshot->targetBindingObservations[0].observationId;
		const auto unboundId = snapshot->targetBindingObservations[1].observationId;
		Check(draws[0]->targetBindingObservationId == boundId,
			"draw did not join to the active output-merger binding");
		Check(draws[1]->targetBindingObservationId == boundId,
			"KEEP_RENDER_TARGETS changed the active output-merger binding");
		Check(draws[2]->targetBindingObservationId == unboundId,
			"explicit target unbind was not joined to the next draw");
		Check(draws[0]->commandStreamSequence == 3 && draws[1]->commandStreamSequence == 5 &&
				  draws[2]->commandStreamSequence == 7,
			"output-merger calls were not included in monotonic command order");
		Check(snapshot->targetBindingObservations[0].renderTargetCount == 3 &&
				  snapshot->targetBindingObservations[0].renderTargetObservationIds[0] != 0 &&
				  snapshot->targetBindingObservations[0].renderTargetObservationIds[1] != 0 &&
				  snapshot->targetBindingObservations[0].renderTargetObservationIds[2] == 0 &&
				  snapshot->targetBindingObservations[0].depthTargetObservationId != 0,
			"output-merger binding did not preserve slots, nulls, and depth target");
	}

	void TestCaptureStartClaimsOneEffectiveOutputMergerSnapshot()
	{
		Runtime runtime;
		auto config = Config();
		config.maxEvents = 32;
		config.maxBytes = Collector::RequiredStorageBytes(config);
		runtime.SetImmediateContext(0xB800);
		Check(runtime.StartCapture(config) == StartResult::kStarted,
			"effective output-merger snapshot capture did not start");

		const auto generation = runtime.ClaimRenderTargetStateSeed(0xB800);
		Check(generation != 0, "first draw could not claim effective output-merger state");
		Check(runtime.ClaimRenderTargetStateSeed(0xB800) == 0,
			"effective output-merger state could be claimed twice in one capture");
		ResourceViewInput target{};
		target.view.kind = TargetViewKind::kRenderTarget;
		target.view.d3dObject = 0xB810;
		runtime.BindRenderTargetViews(
			0xB800, 1, &target, nullptr, false,
			TargetBindingSource::kCaptureStateSnapshot, generation);
		runtime.RecordDraw(0xB800, DrawOperation::kDraw, 3);
		auto snapshot = runtime.StopCapture();

		const auto binding = std::find_if(snapshot->events.begin(), snapshot->events.end(),
			[](const EventRecord& a_event) { return a_event.kind == EventKind::kRenderTargetBind; });
		const auto draw = std::find_if(snapshot->events.begin(), snapshot->events.end(),
			[](const EventRecord& a_event) { return a_event.kind == EventKind::kDraw; });
		Check(binding != snapshot->events.end() &&
				  binding->payload.words[2] == static_cast<std::uint64_t>(TargetBindingSource::kCaptureStateSnapshot),
			"effective output-merger snapshot provenance was not retained");
		Check(draw != snapshot->events.end() && draw->targetBindingObservationId != 0,
			"first draw did not consume the effective output-merger snapshot");

		Check(runtime.StartCapture(config) == StartResult::kStarted,
			"second effective output-merger capture did not start");
		Check(runtime.ClaimRenderTargetStateSeed(0xB800) != 0,
			"effective output-merger seed leaked across capture generations");
		runtime.StopCapture();
	}

	void TestOutputMergerBoundsAreExplicit()
	{
		auto viewConfig = Config();
		viewConfig.maxEvents = 32;
		viewConfig.maxTargetViewObservations = 1;
		viewConfig.maxBytes = Collector::RequiredStorageBytes(viewConfig);
		Runtime viewRuntime;
		viewRuntime.SetImmediateContext(0xC000);
		Check(viewRuntime.StartCapture(viewConfig) == StartResult::kStarted,
			"bounded target-view capture did not start");
		const std::uintptr_t renderTargets[] = { 0xC100, 0xC200 };
		viewRuntime.BindRenderTargets(0xC000, 2, renderTargets, 0xC300);
		viewRuntime.RecordDraw(0xC000, DrawOperation::kDraw, 3);
		auto viewSnapshot = viewRuntime.StopCapture();
		Check(viewSnapshot && viewSnapshot->targetViewObservations.size() == 1,
			"target-view catalogue exceeded its bound");
		Check(viewSnapshot->targetBindingObservations.empty(),
			"incomplete target views were collapsed into a binding identity");
		Check(viewSnapshot->statistics.droppedTargetViewObservations == 2,
			"target-view overflow was not reported");
		const auto viewDraw = std::find_if(viewSnapshot->events.begin(), viewSnapshot->events.end(),
			[](const EventRecord& a_event) { return a_event.kind == EventKind::kDraw; });
		Check(viewDraw != viewSnapshot->events.end() && viewDraw->targetBindingObservationId == 0,
			"incomplete target views produced a draw binding join");

		auto bindingConfig = Config();
		bindingConfig.maxEvents = 32;
		bindingConfig.maxTargetBindingObservations = 1;
		bindingConfig.maxBytes = Collector::RequiredStorageBytes(bindingConfig);
		Runtime bindingRuntime;
		bindingRuntime.SetImmediateContext(0xD000);
		Check(bindingRuntime.StartCapture(bindingConfig) == StartResult::kStarted,
			"bounded target-binding capture did not start");
		const std::uintptr_t oneTarget[] = { 0xD100 };
		bindingRuntime.BindRenderTargets(0xD000, 1, oneTarget, 0);
		bindingRuntime.BindRenderTargets(0xD000, 0, nullptr, 0);
		bindingRuntime.RecordDraw(0xD000, DrawOperation::kDraw, 3);
		auto bindingSnapshot = bindingRuntime.StopCapture();
		Check(bindingSnapshot && bindingSnapshot->targetBindingObservations.size() == 1,
			"target-binding catalogue exceeded its bound");
		Check(bindingSnapshot->statistics.droppedTargetBindingObservations == 1,
			"target-binding overflow was not reported");
		const auto bindingDraw = std::find_if(bindingSnapshot->events.begin(), bindingSnapshot->events.end(),
			[](const EventRecord& a_event) { return a_event.kind == EventKind::kDraw; });
		Check(bindingDraw != bindingSnapshot->events.end() && bindingDraw->targetBindingObservationId == 0,
			"overflowed target binding was silently joined to an existing binding");
	}

	void TestResourceFlowStateIsTypedAndOrdered()
	{
		Runtime runtime;
		auto config = Config();
		config.maxEvents = 64;
		config.maxBytes = Collector::RequiredStorageBytes(config);
		runtime.SetImmediateContext(0xE000);
		Check(runtime.StartCapture(config) == StartResult::kStarted, "resource-flow capture did not start");

		const ResourceObservationInput source{
			.d3dObject = 0xE100,
			.dimension = ResourceDimension::kTexture2D,
			.widthOrBytes = 1024,
			.height = 512,
			.depthOrArraySize = 2,
			.mipLevels = 4,
			.format = 28,
			.sampleCount = 1,
			.bindFlags = 0x28,
		};
		const ResourceObservationInput destination{
			.d3dObject = 0xE200,
			.dimension = ResourceDimension::kTexture2D,
			.widthOrBytes = 1024,
			.height = 512,
			.depthOrArraySize = 2,
			.mipLevels = 4,
			.format = 28,
			.sampleCount = 1,
			.bindFlags = 0x28,
		};
		const ResourceViewInput srv{
			.resource = source,
			.view = { .kind = TargetViewKind::kShaderResource, .d3dObject = 0xE300, .format = 28, .dimension = 5, .mipSlice = 1, .arraySize = 3 },
		};
		const ResourceViewInput rtv{
			.resource = destination,
			.view = { .kind = TargetViewKind::kRenderTarget, .d3dObject = 0xE400, .format = 28, .dimension = 5, .mipSlice = 0, .firstArraySlice = 1, .arraySize = 1 },
		};
		runtime.BindResourceViews(
			0xE000, ResourceBindingKind::kShaderResource, ResourceStage::kPixel, 7, 1, &srv);
		runtime.BindRenderTargetViews(0xE000, 1, &rtv, nullptr);
		runtime.RecordDraw(0xE000, DrawOperation::kDraw, 3);
		runtime.RecordResourceFlow(
			0xE000, ResourceFlowOperation::kCopySubresourceRegion, destination, source, 2, 1);
		runtime.RecordResourceFlow(
			0xE000, ResourceFlowOperation::kClearRenderTarget, {}, destination);

		auto snapshot = runtime.StopCapture();
		Check(snapshot && snapshot->resourceObservations.size() == 2,
			"resource identities were not deduplicated across views and flow operations");
		Check(snapshot->targetViewObservations.size() == 2,
			"typed SRV and RTV observations were not retained");
		Check(std::count_if(snapshot->events.begin(), snapshot->events.end(),
				  [](const EventRecord& a_event) { return a_event.kind == EventKind::kResourceObserved; }) == 2,
			"resource first-seen declarations are wrong");
		const auto bind = std::find_if(snapshot->events.begin(), snapshot->events.end(),
			[](const EventRecord& a_event) { return a_event.kind == EventKind::kResourceViewBind; });
		Check(bind != snapshot->events.end() && bind->payload.words[0] != 0 &&
				  bind->payload.words[2] == static_cast<std::uint64_t>(ResourceStage::kPixel) &&
				  bind->payload.words[3] == 7 && bind->commandStreamSequence == 1,
			"ordered pixel SRV binding was not recorded");
		const auto flow = std::find_if(snapshot->events.begin(), snapshot->events.end(),
			[](const EventRecord& a_event) { return a_event.kind == EventKind::kResourceFlow; });
		Check(flow != snapshot->events.end() && flow->payload.words[1] != 0 && flow->payload.words[2] != 0 &&
				  flow->payload.words[3] == 2 && flow->payload.words[4] == 1 && flow->commandStreamSequence == 4,
			"copy-subresource flow did not retain ordered source and destination identities");
		const auto clear = std::find_if(snapshot->events.begin(), snapshot->events.end(),
			[](const EventRecord& a_event) {
				return a_event.kind == EventKind::kResourceFlow &&
			           a_event.payload.words[0] == static_cast<std::uint64_t>(ResourceFlowOperation::kClearRenderTarget);
			});
		Check(clear != snapshot->events.end() && clear->payload.words[1] == 0 && clear->payload.words[2] != 0 &&
				  clear->commandStreamSequence == 5,
			"destination-only resource mutation did not retain an ordered typed destination");
	}

	void TestEffectiveResourceViewQueriesAreRevisionedAndDeltaEncoded()
	{
		Runtime runtime;
		auto config = Config();
		config.maxEvents = 64;
		config.maxBytes = Collector::RequiredStorageBytes(config);
		runtime.SetImmediateContext(0xF000);
		Check(runtime.StartCapture(config) == StartResult::kStarted,
			"effective resource-state capture did not start");
		const ResourceViewInput srv{
			.resource = {
				.d3dObject = 0xF100,
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
				.d3dObject = 0xF200,
				.format = 28,
				.dimension = 4,
			},
		};
		runtime.BindResourceViews(
			0xF000, ResourceBindingKind::kShaderResource, ResourceStage::kPixel, 2, 1, &srv);
		runtime.BindResourceViews(
			0xF000, ResourceBindingKind::kShaderResource, ResourceStage::kPixel, 2, 1, &srv,
			false, ResourceBindingSource::kPostCallQuery);
		runtime.BindResourceViews(
			0xF000, ResourceBindingKind::kShaderResource, ResourceStage::kPixel, 2, 1, &srv,
			false, ResourceBindingSource::kPostCallQuery);
		runtime.BindResourceViews(
			0xF000, ResourceBindingKind::kShaderResource, ResourceStage::kPixel, 2, 1, nullptr,
			false, ResourceBindingSource::kPostCallQuery);

		auto snapshot = runtime.StopCapture();
		Check(snapshot.has_value(), "effective resource-state capture did not stop");
		std::vector<const EventRecord*> bindings;
		std::vector<const EventRecord*> summaries;
		for (const auto& event : snapshot->events) {
			if (event.kind == EventKind::kResourceViewBind)
				bindings.push_back(&event);
			if (event.kind == EventKind::kResourceViewStateObserved)
				summaries.push_back(&event);
		}
		Check(bindings.size() == 3 && summaries.size() == 3,
			"effective resource state was not delta encoded with one summary per query");
		Check(bindings[0]->payload.words[4] ==
					  static_cast<std::uint64_t>(ResourceBindingSource::kRequestedCall) &&
				  bindings[1]->payload.words[4] ==
					  static_cast<std::uint64_t>(ResourceBindingSource::kPostCallQuery) &&
				  bindings[2]->payload.words[0] == 0,
			"requested, effective, and auto-null evidence were not distinguished");
		Check(summaries[0]->payload.words[5] == 1 &&
				  summaries[1]->payload.words[5] == 0 && summaries[2]->payload.words[5] == 1,
			"effective query summaries did not retain changed-slot counts");
		const auto generation = runtime.StartCapture(config) == StartResult::kStarted ?
		                            runtime.ActiveCaptureGeneration() :
		                            0;
		Check(generation != 0 && runtime.ClaimResourceViewStateSeed(0xF000) == generation &&
				  runtime.ClaimResourceViewStateSeed(0xF000) == 0,
			"capture-generation resource-state snapshot claim was not one-shot");
		runtime.BindResourceViews(
			0xF000, ResourceBindingKind::kShaderResource, ResourceStage::kPixel, 2, 1, &srv,
			false, ResourceBindingSource::kCaptureStateSnapshot, generation);
		auto second = runtime.StopCapture();
		Check(second && std::count_if(second->events.begin(), second->events.end(),
							[](const EventRecord& event) {
								return event.kind == EventKind::kResourceViewBind && event.payload.words[0] != 0;
							}) == 1,
			"effective resource-state tracking leaked across capture generations");
	}

	void TestRestoreFalseReobservesEffectiveResourceViews()
	{
		Runtime runtime;
		auto config = Config();
		config.maxEvents = 128;
		config.maxBytes = Collector::RequiredStorageBytes(config);
		runtime.SetImmediateContext(0xF500);
		Check(runtime.StartCapture(config) == StartResult::kStarted,
			"post-execute resource-state capture did not start");
		const ResourceViewInput srvA{
			.resource = { .d3dObject = 0xF510, .dimension = ResourceDimension::kTexture2D },
			.view = { .kind = TargetViewKind::kShaderResource, .d3dObject = 0xF520 },
		};
		const ResourceViewInput srvB{
			.resource = { .d3dObject = 0xF530, .dimension = ResourceDimension::kTexture2D },
			.view = { .kind = TargetViewKind::kShaderResource, .d3dObject = 0xF540 },
		};
		const ResourceViewInput uav{
			.resource = { .d3dObject = 0xF550, .dimension = ResourceDimension::kTexture2D },
			.view = { .kind = TargetViewKind::kUnorderedAccess, .d3dObject = 0xF560 },
		};
		const auto query = [&](ResourceBindingKind a_kind, ResourceStage a_stage,
							   std::uint32_t a_slot, const ResourceViewInput* a_view) {
			runtime.BindResourceViews(
				0xF500, a_kind, a_stage, a_slot, 1, a_view, false,
				ResourceBindingSource::kPostCallQuery);
		};

		query(ResourceBindingKind::kShaderResource, ResourceStage::kPixel, 2, &srvA);
		query(ResourceBindingKind::kUnorderedAccess, ResourceStage::kCompute, 1, &uav);
		runtime.RecordExecuteCommandList(0xF500, 0xF570, false);
		query(ResourceBindingKind::kShaderResource, ResourceStage::kPixel, 2, &srvA);
		query(ResourceBindingKind::kUnorderedAccess, ResourceStage::kCompute, 1, nullptr);
		query(ResourceBindingKind::kShaderResource, ResourceStage::kPixel, 2, &srvB);
		runtime.RecordExecuteCommandList(0xF500, 0xF570, true);
		query(ResourceBindingKind::kShaderResource, ResourceStage::kPixel, 2, &srvB);

		auto snapshot = runtime.StopCapture();
		Check(snapshot.has_value(), "post-execute resource-state capture did not stop");
		std::vector<const EventRecord*> bindings;
		std::vector<const EventRecord*> summaries;
		for (const auto& event : snapshot->events) {
			if (event.kind == EventKind::kResourceViewBind &&
				event.payload.words[4] == static_cast<std::uint64_t>(ResourceBindingSource::kPostCallQuery)) {
				bindings.push_back(&event);
			}
			if (event.kind == EventKind::kResourceViewStateObserved)
				summaries.push_back(&event);
		}
		Check(bindings.size() == 5 && summaries.size() == 6,
			"restore-state resource queries emitted the wrong delta sequence");
		Check(bindings[0]->payload.words[0] != 0 &&
				  bindings[2]->payload.words[0] == bindings[0]->payload.words[0],
			"restore-false execution did not republish a same-view SRV rebind");
		Check(bindings[3]->payload.words[0] == 0 &&
				  bindings[3]->payload.words[1] == static_cast<std::uint64_t>(ResourceBindingKind::kUnorderedAccess),
			"restore-false execution did not publish an explicit UAV unbind");
		Check(bindings[4]->payload.words[0] != 0 &&
				  bindings[4]->payload.words[0] != bindings[0]->payload.words[0],
			"changed-view control did not publish the replacement SRV");
		Check(summaries[2]->payload.words[5] == 1 && summaries[3]->payload.words[5] == 1 &&
				  summaries[4]->payload.words[5] == 1 && summaries[5]->payload.words[5] == 0,
			"restore-false reseeding or restore-true preservation was not delta coherent");
	}

	void TestCpuMapUnmapBoundariesArePairedAndTimed()
	{
		Runtime runtime;
		auto config = Config();
		config.maxEvents = 32;
		config.maxBytes = Collector::RequiredStorageBytes(config);
		runtime.SetImmediateContext(0xFA00);
		Check(runtime.StartCapture(config) == StartResult::kStarted,
			"CPU resource-access capture did not start");
		const auto generation = runtime.ActiveCaptureGeneration();
		const ResourceObservationInput staging{
			.d3dObject = 0xFA10,
			.dimension = ResourceDimension::kTexture2D,
			.widthOrBytes = 64,
			.height = 64,
			.depthOrArraySize = 1,
			.mipLevels = 1,
			.format = 28,
			.sampleCount = 1,
			.usage = 3,
			.cpuAccessFlags = 0x30000,
		};
		runtime.RecordCpuMap(0xFA00, staging, 0, 1, 0, 0, 25, 200, 256, 16384, generation);
		runtime.RecordCpuUnmap(0xFA00, staging, 0, 250, generation);
		runtime.RecordCpuMap(0xFA00, staging, 0, 4, 0, 0, 10, 300, 256, 16384, generation);
		runtime.RecordCpuUnmap(0xFA00, staging, 0, 345, generation);
		runtime.RecordCpuMap(0xFA00, staging, 0, 1, 0x100000, -1, 5, 400, 0, 0, generation);
		runtime.RecordCpuUnmap(0xFA00, staging, 0, 410, generation);

		auto snapshot = runtime.StopCapture();
		Check(snapshot.has_value(), "CPU resource-access capture did not stop");
		std::vector<const EventRecord*> access;
		for (const auto& event : snapshot->events) {
			if (event.kind == EventKind::kResourceCpuAccess)
				access.push_back(&event);
		}
		Check(access.size() == 6, "CPU resource-access events were not retained");
		Check(access[0]->payload.words[0] == static_cast<std::uint64_t>(ResourceCpuAccessPhase::kMap) &&
				  access[0]->payload.words[5] == 25 && access[0]->payload.words[7] == (16384ull << 32u | 256u),
			"successful read Map did not retain timing and pitch evidence");
		Check(access[1]->payload.words[0] == static_cast<std::uint64_t>(ResourceCpuAccessPhase::kUnmap) &&
				  access[1]->payload.words[1] == access[0]->payload.words[1] && access[1]->payload.words[5] == 50,
			"read Map/Unmap lifetime was not paired");
		Check(access[3]->payload.words[1] == access[2]->payload.words[1] && access[3]->payload.words[5] == 45,
			"write Map/Unmap lifetime was not paired");
		Check(static_cast<std::uint32_t>(access[4]->payload.words[6]) == 0xFFFFFFFFu,
			"failed Map HRESULT was not retained losslessly");
		Check(access[5]->payload.words[1] == 0 && access[5]->payload.words[4] == 0,
			"Unmap after failed Map was falsely paired");
		Check(access[0]->commandStreamSequence < access[1]->commandStreamSequence &&
				  access[1]->commandStreamSequence < access[2]->commandStreamSequence,
			"CPU access calls were not ordered in the immediate-context stream");
	}

	void TestExecutionJoinsDeclaredScopes()
	{
		Runtime runtime;
		auto config = Config();
		config.maxEvents = 64;
		config.maxBytes = Collector::RequiredStorageBytes(config);
		runtime.SetImmediateContext(0xA000);
		Check(runtime.StartCapture(config) == StartResult::kStarted, "scope-join capture did not start");
		runtime.BindStage(0xA000, ShaderStage::kVertex, 0xA100);
		{
			auto pass = runtime.EnterRenderPass({ .renderPass = 0xA200, .geometry = 0xA300 });
			Check(pass.IsActive(), "scope-join render pass did not enter");
			{
				auto technique = runtime.EnterTechnique({
					.shader = 0xA400,
					.shaderType = 4,
					.fxpFilename = "Lighting",
				});
				Check(technique.IsActive(), "scope-join technique did not enter");
				{
					auto geometry = runtime.EnterGeometry({
						.shader = 0xA400,
						.renderPass = 0xA200,
						.geometry = 0xA300,
						.shaderType = 4,
					});
					Check(geometry.IsActive(), "scope-join geometry did not enter");
					runtime.RecordDraw(0xA000, DrawOperation::kDraw, 3, 0);
				}
			}
		}

		auto snapshot = runtime.StopCapture();
		Check(snapshot.has_value(), "scope-join capture did not stop");
		const auto draw = std::find_if(snapshot->events.begin(), snapshot->events.end(),
			[](const EventRecord& a_event) { return a_event.kind == EventKind::kDraw; });
		Check(draw != snapshot->events.end(), "scope-join draw is missing");
		Check(draw->scopes.renderPass.observationId != 0 && draw->scopes.technique.observationId != 0 &&
				  draw->scopes.geometry.observationId != 0,
			"draw did not retain all active typed scopes");

		const auto declaredBeforeDraw = [&](EventKind a_kind, std::uint64_t a_observationId, ScopeKind a_scope) {
			const auto declaration = std::find_if(snapshot->events.begin(), draw,
				[&](const EventRecord& a_event) {
					if (a_event.kind != a_kind)
						return false;
					switch (a_scope) {
					case ScopeKind::kRenderPass:
						return a_event.scopes.renderPass.observationId == a_observationId;
					case ScopeKind::kTechnique:
						return a_event.scopes.technique.observationId == a_observationId;
					case ScopeKind::kGeometry:
						return a_event.scopes.geometry.observationId == a_observationId;
					default:
						return false;
					}
				});
			return declaration != draw;
		};
		Check(declaredBeforeDraw(EventKind::kRenderPassEnter, draw->scopes.renderPass.observationId,
				  ScopeKind::kRenderPass),
			"draw references an undeclared render-pass scope");
		Check(declaredBeforeDraw(EventKind::kTechniqueBegin, draw->scopes.technique.observationId,
				  ScopeKind::kTechnique),
			"draw references an undeclared technique scope");
		Check(declaredBeforeDraw(EventKind::kGeometrySetupBegin, draw->scopes.geometry.observationId,
				  ScopeKind::kGeometry),
			"draw references an undeclared geometry scope");
	}

	void TestVisibilitySubmissionJoinsActualDraw()
	{
		Runtime runtime;
		auto config = Config();
		config.maxEvents = 64;
		config.maxBytes = Collector::RequiredStorageBytes(config);
		runtime.SetImmediateContext(0xF000);
		Check(runtime.StartCapture(config) == StartResult::kStarted,
			"visibility capture did not start");
		runtime.SetFrameContext({ 100, 0, 0, Eye::kUnknown, 0 });
		runtime.RecordVisibilityCandidate(0xF100, 12, 100, 700);

		const ResourceObservationInput visibilityResource{
			.d3dObject = 0xF200,
			.dimension = ResourceDimension::kBuffer,
			.widthOrBytes = 16384,
			.bindFlags = 0x8,
			.miscFlags = 0x40,
			.structureByteStride = 4,
		};
		const ResourceViewInput visibilityView{
			.resource = visibilityResource,
			.view = {
				.kind = TargetViewKind::kShaderResource,
				.d3dObject = 0xF300,
				.dimension = 1,
				.elementCount = 4096,
			},
		};
		const auto versionId = runtime.RecordVisibilityResultReady(
			0xF000,
			{
				.resource = visibilityResource,
				.firstSubresource = 0,
				.subresourceCount = 1,
				.writeEpoch = 9,
				.producerFrame = 100,
				.engineFrame = 700,
				.readinessDomain = ResourceReadinessDomain::kSameImmediateContextOrder,
			},
			visibilityView,
			24);
		Check(versionId != 0, "visibility resource version was not declared");
		const auto versionGeneration = runtime.ActiveCaptureGeneration();
		const auto submissionId = runtime.DeclareVisibilitySubmission(
			0xF000,
			{
				.renderPass = 0xF400,
				.geometry = 0xF500,
				.objectIndex = 12,
				.category = 1,
				.resourceVersionObservationId = versionId,
				.requestedView = visibilityView,
				.effectiveView = visibilityView,
				.slot = 127,
				.bindingMatches = true,
			});
		Check(submissionId != 0, "visibility submission was not declared");
		runtime.ClearPendingVisibilitySubmission(0xF000);
		runtime.RecordDraw(0xF000, DrawOperation::kDrawIndexed, 6, 0, 0);
		const auto replacementSubmissionId = runtime.DeclareVisibilitySubmission(
			0xF000,
			{
				.renderPass = 0xF400,
				.geometry = 0xF500,
				.objectIndex = 12,
				.category = 1,
				.resourceVersionObservationId = versionId,
				.requestedView = visibilityView,
				.effectiveView = visibilityView,
				.slot = 127,
				.bindingMatches = true,
			});
		Check(replacementSubmissionId != 0, "replacement visibility submission was not declared");
		runtime.RecordDraw(0xF000, DrawOperation::kDrawIndexed, 36, 0, 0);
		runtime.RecordDraw(0xF000, DrawOperation::kDrawIndexed, 12, 0, 0);

		const ResourceObservationInput submittedTexture{
			.d3dObject = 0xF600,
			.dimension = ResourceDimension::kTexture2D,
			.widthOrBytes = 2468,
			.height = 2740,
			.depthOrArraySize = 1,
			.mipLevels = 1,
		};
		runtime.RecordEyeSubmission(
			submittedTexture, Eye::kLeft, 1, 0.0f, 0.0f, 0.5f, 1.0f, 0, 77);
		runtime.RecordCullDecision(
			versionId, versionGeneration, 12, false, 2, 2, 0, 0, 100);

		auto snapshot = runtime.StopCapture();
		Check(snapshot.has_value(), "visibility capture did not stop");
		const auto version = std::find_if(snapshot->events.begin(), snapshot->events.end(),
			[](const EventRecord& a_event) { return a_event.kind == EventKind::kResourceVersionObserved; });
		Check(version != snapshot->events.end() && version->payload.words[0] == versionId &&
				  version->payload.words[4] == 9,
			"visibility resource version identity is incomplete");
		const auto candidate = std::find_if(snapshot->events.begin(), snapshot->events.end(),
			[](const EventRecord& a_event) { return a_event.kind == EventKind::kVisibilityCandidate; });
		Check(candidate != snapshot->events.end() && candidate->payload.words[2] == 100 &&
				  candidate->payload.words[3] == 700,
			"visibility candidate frame domains are incomplete");
		const auto ready = std::find_if(snapshot->events.begin(), snapshot->events.end(),
			[](const EventRecord& a_event) { return a_event.kind == EventKind::kVisibilityResultReady; });
		Check(ready != snapshot->events.end() && ready->payload.words[3] == 100 &&
				  ready->payload.words[4] == 700,
			"visibility result frame domains are incomplete");
		const auto consumed = std::find_if(snapshot->events.begin(), snapshot->events.end(),
			[](const EventRecord& a_event) { return a_event.kind == EventKind::kVisibilityConsumed; });
		Check(consumed != snapshot->events.end() && consumed->submissionObservationId == submissionId &&
				  consumed->payload.words[4] == versionId && consumed->payload.words[5] == consumed->payload.words[6],
			"effective visibility binding was not joined to its submission");
		std::vector<const EventRecord*> draws;
		for (const auto& event : snapshot->events) {
			if (event.kind == EventKind::kDraw)
				draws.push_back(std::addressof(event));
		}
		Check(draws.size() == 3 && draws[0]->submissionObservationId == 0 &&
				  draws[1]->submissionObservationId == replacementSubmissionId &&
				  draws[2]->submissionObservationId == 0,
			"submission identity was not consumed by exactly one actual draw");
		const auto eye = std::find_if(snapshot->events.begin(), snapshot->events.end(),
			[](const EventRecord& a_event) { return a_event.kind == EventKind::kEyeSubmitted; });
		Check(eye != snapshot->events.end() && eye->frame.eye == Eye::kLeft &&
				  eye->payload.words[0] != 0,
			"accepted eye submission did not attribute its resource");
		const auto decision = std::find_if(snapshot->events.begin(), snapshot->events.end(),
			[](const EventRecord& a_event) { return a_event.kind == EventKind::kCullDecision; });
		Check(decision != snapshot->events.end() && decision->payload.words[0] == versionId &&
				  decision->payload.words[1] == 12 && decision->payload.words[2] == 0 &&
				  decision->payload.words[3] == 2,
			"completed visibility readback was not joined to its resource version");

		Check(runtime.StartCapture(config) == StartResult::kStarted,
			"replacement visibility capture did not start");
		runtime.RecordCullDecision(versionId, versionGeneration, 12, false, 2, 2, 0, 0, 100);
		auto replacement = runtime.StopCapture();
		Check(replacement && std::none_of(
								 replacement->events.begin(), replacement->events.end(),
								 [](const EventRecord& a_event) { return a_event.kind == EventKind::kCullDecision; }),
			"stale readback version crossed a capture generation boundary");
	}

	void TestEventKindSelectionPreservesDependenciesAndCapacity()
	{
		const auto eyeOnly = EventKindBit(EventKind::kEyeSubmitted);
		const auto resolved = ResolveEventKindDependencies(eyeOnly);
		Check((resolved & eyeOnly) != 0, "requested eye event was not retained");
		Check((resolved & EventKindBit(EventKind::kResourceObserved)) != 0,
			"eye submission did not resolve its resource identity dependency");
		Check((resolved & EventKindBit(EventKind::kDraw)) == 0,
			"eye submission unexpectedly enabled draw capture");

		const auto techniquePair = ResolveEventKindDependencies(EventKindBit(EventKind::kTechniqueBegin));
		Check((techniquePair & EventKindBit(EventKind::kTechniqueEnd)) != 0 &&
				  (techniquePair & EventKindBit(EventKind::kShaderObserved)) != 0,
			"scope selection did not resolve its paired boundary and identity");
		const auto drawDependencies = ResolveEventKindDependencies(EventKindBit(EventKind::kDraw));
		Check((drawDependencies & EventKindBit(EventKind::kCommandRecordingObserved)) != 0 &&
				  (drawDependencies & EventKindBit(EventKind::kDeviceContextObserved)) != 0,
			"draw selection did not retain deferred recording and context provenance");

		Runtime runtime;
		auto config = Config();
		config.maxEvents = 2;
		config.requestedEventKindMask = eyeOnly;
		config.maxBytes = Collector::RequiredStorageBytes(config);
		Check(runtime.StartCapture(config) == StartResult::kStarted,
			"filtered eye capture did not start");
		for (std::uint32_t index = 0; index < 100; ++index)
			runtime.RecordVisibilityCandidate(0x1000 + index, index, 1, 1);
		runtime.RecordEyeSubmission(
			{ .d3dObject = 0x9000, .dimension = ResourceDimension::kTexture2D, .widthOrBytes = 2048, .height = 2048, .depthOrArraySize = 1, .mipLevels = 1 },
			Eye::kRight, 2, 0.5f, 0.0f, 1.0f, 1.0f, 0, 9);

		auto snapshot = runtime.StopCapture();
		Check(snapshot && snapshot->events.size() == 2,
			"filtered calls consumed selected-event capacity");
		Check(snapshot->events[0].kind == EventKind::kResourceObserved &&
				  snapshot->events[1].kind == EventKind::kEyeSubmitted,
			"eye capture did not retain its resolved event sequence");
		Check(snapshot->statistics.filtered == 100 &&
				  snapshot->statistics.droppedEventLimit == 0 &&
				  snapshot->statistics.droppedByteLimit == 0,
			"filtered calls were reported as loss or truncation");
		Check(snapshot->config.requestedEventKindMask == eyeOnly &&
				  snapshot->config.eventKindMask == resolved,
			"capture did not retain requested and resolved event selections");
	}

	void TestSemanticIdentityCataloguesAreBoundedAndRevisioned()
	{
		Collector collector;
		auto config = Config();
		config.maxSceneObjectObservations = 2;
		config.maxGeometryObservations = 2;
		config.maxMaterialStateObservations = 2;
		config.maxBytes = Collector::RequiredStorageBytes(config);
		Check(collector.Start(config) == StartResult::kStarted,
			"semantic identity capture did not start");

		const SceneObjectObservationInput object{
			.reference = 0x1100,
			.referenceFormId = 0x01001234,
			.baseFormId = 0x0000ABCD,
			.referenceName = "Breezehome chair reference",
			.baseFormName = "Chair",
		};
		const auto objectFirst = collector.ObserveSceneObject(object);
		const auto objectRepeated = collector.ObserveSceneObject(object);
		auto objectChanged = object;
		objectChanged.baseFormId = 0x0000ABCE;
		const auto objectSecond = collector.ObserveSceneObject(objectChanged);
		const auto objectOverflow = collector.ObserveSceneObject({ .reference = 0x1200 });

		GeometryObservationInput geometry{
			.geometry = 0x2100,
			.runtimeTypeName = "BSTriShape",
			.name = "Chair01:0",
			.geometryType = 1,
			.vertexDescriptor = 0x12345678,
			.sceneObjectObservationId = objectFirst.observationId,
			.worldTransformAvailable = true,
			.worldBoundAvailable = true,
		};
		geometry.worldTransform[0] = 1.0f;
		geometry.worldTransform[5] = 1.0f;
		geometry.worldTransform[10] = 1.0f;
		geometry.worldBound = { 1.0f, 2.0f, 3.0f, 4.0f };
		const auto geometryFirst = collector.ObserveGeometry(geometry);
		const auto geometryRepeated = collector.ObserveGeometry(geometry);
		geometry.worldBound[0] = 5.0f;
		const auto geometrySecond = collector.ObserveGeometry(geometry);
		const auto geometryOverflow = collector.ObserveGeometry({ .geometry = 0x2200 });

		MaterialStateObservationInput material{
			.shaderProperty = 0x3100,
			.shaderPropertyRuntimeTypeName = "BSLightingShaderProperty",
			.shaderPropertyFlags = 0x40,
			.alpha = 1.0f,
			.engineMaterialType = 2,
			.material = 0x3200,
			.materialType = 3,
			.feature = 4,
			.hashKey = 5,
			.shaderPropertyAvailable = true,
			.materialAvailable = true,
		};
		material.textureBindings[0] = {
			.role = MaterialTextureRole::kRuntimeMaterialList,
			.bindingIndex = 0,
			.niSourceTexture = 0x3210,
			.path = "textures\\architecture\\whiterun\\wrwood.dds",
			.resourceObservationId = 42,
		};
		material.textureBindingCount = 1;
		const auto materialFirst = collector.ObserveMaterialState(material);
		const auto materialRepeated = collector.ObserveMaterialState(material);
		material.textureBindings[0].resourceObservationId = 43;
		const auto materialSecond = collector.ObserveMaterialState(material);
		const auto materialOverflow = collector.ObserveMaterialState({
			.shaderProperty = 0x3300,
			.shaderPropertyAvailable = true,
		});

		auto snapshot = collector.Stop();
		Check(snapshot.has_value(), "semantic identity capture did not stop");
		Check(objectFirst.firstSeen && objectRepeated.observationId == objectFirst.observationId &&
				  !objectRepeated.firstSeen && objectSecond.pointerGeneration == 2 &&
				  objectOverflow.observationId == 0,
			"scene-object identity was not deduplicated, versioned, and bounded");
		Check(geometryFirst.firstSeen && geometryRepeated.observationId == geometryFirst.observationId &&
				  !geometryRepeated.firstSeen && geometrySecond.pointerGeneration == 2 &&
				  geometryOverflow.observationId == 0,
			"geometry identity was not deduplicated, versioned, and bounded");
		Check(materialFirst.firstSeen && materialRepeated.observationId == materialFirst.observationId &&
				  !materialRepeated.firstSeen && materialSecond.stateRevision == 2 &&
				  materialSecond.fingerprint != materialFirst.fingerprint && materialOverflow.observationId == 0,
			"material state was not deduplicated, revisioned, and bounded");
		Check(snapshot->sceneObjectObservations.size() == 2 &&
				  snapshot->geometryObservations.size() == 2 &&
				  snapshot->materialStateObservations.size() == 2,
			"semantic catalogues did not preserve their exact admitted records");
		Check(snapshot->statistics.droppedSceneObjectObservations == 1 &&
				  snapshot->statistics.droppedGeometryObservations == 1 &&
				  snapshot->statistics.droppedMaterialStateObservations == 1,
			"semantic catalogue overflow was not reported independently");
		Check(snapshot->geometryObservations[0].sceneObjectObservationId == objectFirst.observationId &&
				  std::string_view(snapshot->geometryObservations[0].name.data()) == "Chair01:0",
			"geometry did not retain its scene-object link and bounded name");
		Check(snapshot->materialStateObservations[0].textureBindingCount == 1 &&
				  snapshot->materialStateObservations[0].textureBindings[0].role == MaterialTextureRole::kRuntimeMaterialList &&
				  snapshot->materialStateObservations[0].textureBindings[0].resourceObservationId == 42 &&
				  std::string_view(snapshot->materialStateObservations[0].textureBindings[0].path.data()) ==
					  "textures\\architecture\\whiterun\\wrwood.dds" &&
				  snapshot->materialStateObservations[1].textureBindings[0].resourceObservationId == 43,
			"material revisions did not preserve their bounded texture bindings");
	}

	void TestGeometrySelectionFiltersBeforeSemanticWork()
	{
		Runtime runtime;
		auto config = Config();
		config.geometryShaderTypeMask = std::uint64_t{ 1 } << 7;
		config.executionWithinSelectedGeometry = true;
		config.maxBytes = Collector::RequiredStorageBytes(config);
		Check(runtime.StartCapture(config) == StartResult::kStarted,
			"geometry-filtered capture did not start");
		runtime.SetImmediateContext(0x9000);
		runtime.RecordDraw(0x9000, DrawOperation::kDraw, 3);
		{
			auto rejected = runtime.EnterGeometry({
				.geometry = 0x1000,
				.shaderType = 6,
				.geometryObservation = { .name = "rejected" },
			});
			Check(!rejected.IsActive(), "unselected shader type entered a geometry scope");
		}
		{
			auto selected = runtime.EnterGeometry({
				.geometry = 0x2000,
				.shaderType = 7,
				.geometryObservation = { .name = "selected" },
			});
			Check(selected.IsActive(), "selected shader type did not enter a geometry scope");
		}
		runtime.RecordDraw(0x9000, DrawOperation::kDraw, 3);

		auto snapshot = runtime.StopCapture();
		Check(snapshot && snapshot->statistics.filtered == 3,
			"geometry and out-of-scope execution filtering was not reported exactly");
		Check(snapshot->geometryObservations.size() == 1 &&
				  std::string_view(snapshot->geometryObservations[0].name.data()) == "selected",
			"unselected geometry polluted the semantic catalogue");
		const auto draw = std::find_if(snapshot->events.begin(), snapshot->events.end(),
			[](const EventRecord& a_event) { return a_event.kind == EventKind::kDraw; });
		const auto setup = std::find_if(snapshot->events.begin(), snapshot->events.end(),
			[](const EventRecord& a_event) { return a_event.kind == EventKind::kGeometrySetupBegin; });
		Check(draw != snapshot->events.end() && setup != snapshot->events.end() &&
				  draw->scopes.geometry.observationId == 0 &&
				  draw->preparedGeometrySetupObservationId == setup->scopes.geometry.observationId,
			"selected draw did not consume the prepared geometry identity after setup returned");
		Check(draw->commandStreamSequence == 2,
			"filtered execution did not advance the observed command-stream sequence");
	}

	void TestPreparedGeometryHandoffRejectsStaleCandidates()
	{
		Runtime runtime;
		auto config = Config();
		config.geometryShaderTypeMask = std::uint64_t{ 1 } << 7;
		config.executionWithinSelectedGeometry = true;
		config.maxBytes = Collector::RequiredStorageBytes(config);
		Check(runtime.StartCapture(config) == StartResult::kStarted,
			"stale prepared-geometry capture did not start");
		runtime.SetImmediateContext(0xA000);
		{
			auto selected = runtime.EnterGeometry({ .geometry = 0x1000, .shaderType = 7 });
			Check(selected.IsActive(), "selected geometry was not prepared");
		}
		{
			auto rejected = runtime.EnterGeometry({ .geometry = 0x2000, .shaderType = 6 });
			Check(!rejected.IsActive(), "unselected geometry unexpectedly entered");
		}
		runtime.RecordDraw(0xA000, DrawOperation::kDraw, 3);
		auto first = runtime.StopCapture();
		Check(first && std::none_of(first->events.begin(), first->events.end(), [](const EventRecord& a_event) { return a_event.kind == EventKind::kDraw; }) &&
				  first->statistics.filtered == 3,
			"unselected geometry did not invalidate the prior prepared draw candidate");

		Check(runtime.StartCapture(config) == StartResult::kStarted,
			"generation-source capture did not start");
		{
			auto selected = runtime.EnterGeometry({ .geometry = 0x3000, .shaderType = 7 });
			Check(selected.IsActive(), "generation-source geometry was not prepared");
		}
		Check(runtime.StopCapture().has_value(), "generation-source capture did not stop");

		Check(runtime.StartCapture(config) == StartResult::kStarted,
			"generation-isolation capture did not start");
		runtime.RecordDraw(0xA000, DrawOperation::kDraw, 3);
		auto isolated = runtime.StopCapture();
		Check(isolated && std::none_of(isolated->events.begin(), isolated->events.end(), [](const EventRecord& a_event) { return a_event.kind == EventKind::kDraw; }) &&
				  isolated->statistics.filtered == 1,
			"a prepared geometry identity leaked into a later capture generation");
	}

	void TestGeometryBoundaryBindsExactSemanticObservations()
	{
		Runtime runtime;
		Check(runtime.StartCapture(Config()) == StartResult::kStarted,
			"semantic geometry capture did not start");
		{
			auto scope = runtime.EnterGeometry({
				.shader = 0x1000,
				.renderPass = 0x2000,
				.geometry = 0x3000,
				.shaderType = 7,
				.passEnum = 8,
				.renderFlags = 9,
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
			Check(scope.IsActive(), "semantic geometry boundary did not enter");
		}

		auto snapshot = runtime.StopCapture();
		Check(snapshot && snapshot->events.size() == 5,
			"semantic geometry boundary emitted the wrong event sequence");
		Check(snapshot->events[0].kind == EventKind::kObjectObserved &&
				  snapshot->events[1].kind == EventKind::kGeometryObserved &&
				  snapshot->events[2].kind == EventKind::kMaterialObserved &&
				  snapshot->events[3].kind == EventKind::kGeometrySetupBegin &&
				  snapshot->events[4].kind == EventKind::kGeometrySetupEnd,
			"semantic declarations were not emitted before their setup consumer");
		Check(snapshot->sceneObjectObservations.size() == 1 &&
				  snapshot->geometryObservations.size() == 1 &&
				  snapshot->materialStateObservations.size() == 1,
			"semantic geometry declarations were not retained");
		const auto& setup = snapshot->events[3];
		Check(setup.payload.words[6] == snapshot->geometryObservations[0].observationId &&
				  setup.payload.words[7] == snapshot->materialStateObservations[0].observationId,
			"geometry boundary did not bind exact geometry and material identities");
		Check(snapshot->geometryObservations[0].sceneObjectObservationId ==
				  snapshot->sceneObjectObservations[0].observationId,
			"geometry declaration did not bind its optional scene object");
	}

	void TestDeferredRecordingMaterializesAndExecutesCommandList()
	{
		Runtime runtime;
		auto config = Config();
		config.maxEvents = 64;
		config.maxBytes = Collector::RequiredStorageBytes(config);
		Check(runtime.StartCapture(config) == StartResult::kStarted,
			"deferred command-list capture did not start");
		runtime.SetImmediateContext(0x1000);
		runtime.RegisterDeferredContext(0x2000, 7);
		runtime.BindStage(0x2000, ShaderStage::kVertex, 0x3000);
		runtime.BindStage(0x2000, ShaderStage::kPixel, 0x4000);
		runtime.RecordDraw(0x2000, DrawOperation::kDrawIndexed, 36, 0, 0);
		runtime.RecordFinishCommandList(0x2000, 0x5000, false, 0);
		runtime.BindStage(0x1000, ShaderStage::kVertex, 0x6000);
		runtime.RecordExecuteCommandList(0x1000, 0x5000, false);
		runtime.RecordExecuteCommandList(0x1000, 0x5000, true);
		runtime.RecordDraw(0x1000, DrawOperation::kDraw, 3);

		auto snapshot = runtime.StopCapture();
		Check(snapshot.has_value(), "deferred command-list capture did not stop");
		const auto findFirst = [&](EventKind a_kind) {
			return std::find_if(snapshot->events.begin(), snapshot->events.end(),
				[&](const EventRecord& a_event) { return a_event.kind == a_kind; });
		};
		const auto context = findFirst(EventKind::kDeviceContextObserved);
		const auto recording = findFirst(EventKind::kCommandRecordingObserved);
		const auto recordedDraw = findFirst(EventKind::kDraw);
		const auto list = findFirst(EventKind::kCommandListObserved);
		const auto finish = findFirst(EventKind::kFinishCommandList);
		Check(context != snapshot->events.end() && context->payload.words[3] == static_cast<std::uint64_t>(DeviceContextKind::kDeferred) &&
				  context->payload.words[4] == static_cast<std::uint64_t>(ContextCreationEvidence::kCreateDeferredContext) &&
				  context->payload.words[5] == 7,
			"deferred context identity did not retain kind, creation evidence, and flags");
		Check(recording != snapshot->events.end() && recordedDraw != snapshot->events.end() &&
				  recordedDraw->deviceContextObservationId == context->payload.words[0] &&
				  recordedDraw->scopes.commandList.token == 0 &&
				  recordedDraw->scopes.commandList.observationId == recording->payload.words[0],
			"recorded draw did not retain its deferred context and recording epoch");
		Check(list != snapshot->events.end() && finish != snapshot->events.end() &&
				  list->payload.words[4] == recording->payload.words[0] &&
				  list->payload.words[5] == 0 &&
				  (list->payload.words[6] & static_cast<std::uint64_t>(
												CommandRecordingIncompleteReason::kHookCoverageUnqualified)) != 0 &&
				  finish->payload.words[0] == recording->payload.words[0] &&
				  finish->payload.words[1] == list->payload.words[0] &&
				  finish->payload.words[5] == 0 &&
				  finish->payload.words[6] == list->payload.words[6],
			"FinishCommandList did not materialize the exact source recording");
		std::vector<const EventRecord*> executions;
		for (const auto& event : snapshot->events) {
			if (event.kind == EventKind::kExecuteCommandList)
				executions.push_back(&event);
		}
		Check(executions.size() == 2 && executions[0]->payload.words[0] == list->payload.words[0] &&
				  executions[1]->payload.words[0] == list->payload.words[0] &&
				  executions[0]->payload.words[2] == recording->payload.words[0],
			"repeated execution did not reuse the command-list and source-recording identities");
		const auto immediateDraw = std::find_if(
			std::next(recordedDraw), snapshot->events.end(),
			[](const EventRecord& a_event) { return a_event.kind == EventKind::kDraw; });
		Check(immediateDraw != snapshot->events.end() && immediateDraw->payload.words[2] == 0,
			"RestoreContextState=false did not invalidate the immediate pipeline tracker");
	}

	void TestExecuteRestoreStateIsIndependentOfCaptureAdmission()
	{
		Runtime resetRuntime;
		resetRuntime.SetImmediateContext(0x1000);
		resetRuntime.BindStage(0x1000, ShaderStage::kVertex, 0x6000);
		resetRuntime.BindStage(0x1000, ShaderStage::kPixel, 0x7000);
		resetRuntime.BindStage(0x1000, ShaderStage::kCompute, 0x8000);
		resetRuntime.RecordExecuteCommandList(0x1000, 0x5000, false);
		Check(resetRuntime.StartCapture(Config()) == StartResult::kStarted,
			"post-execute reset capture did not start");
		resetRuntime.RecordDraw(0x1000, DrawOperation::kDraw, 3);
		resetRuntime.RecordDispatch(0x1000, DispatchOperation::kDispatch, 1, 1, 1);
		auto resetSnapshot = resetRuntime.StopCapture();
		Check(resetSnapshot.has_value(), "post-execute reset capture did not stop");
		const auto resetDraw = std::find_if(resetSnapshot->events.begin(), resetSnapshot->events.end(),
			[](const EventRecord& a_event) { return a_event.kind == EventKind::kDraw; });
		const auto resetDispatch = std::find_if(resetSnapshot->events.begin(), resetSnapshot->events.end(),
			[](const EventRecord& a_event) { return a_event.kind == EventKind::kDispatch; });
		Check(resetDraw != resetSnapshot->events.end() && resetDraw->payload.words[2] == 0 &&
				  resetDraw->payload.words[3] == 0 && resetDispatch != resetSnapshot->events.end() &&
				  resetDispatch->payload.words[2] == 0,
			"restore-false execution outside capture retained stale stage identities");

		Runtime preserveRuntime;
		preserveRuntime.SetImmediateContext(0x1000);
		preserveRuntime.BindStage(0x1000, ShaderStage::kVertex, 0x6000);
		preserveRuntime.RecordExecuteCommandList(0x1000, 0x5000, true);
		Check(preserveRuntime.StartCapture(Config()) == StartResult::kStarted,
			"post-execute preservation capture did not start");
		preserveRuntime.RecordDraw(0x1000, DrawOperation::kDraw, 3);
		auto preserveSnapshot = preserveRuntime.StopCapture();
		Check(preserveSnapshot.has_value(), "post-execute preservation capture did not stop");
		const auto preserveDraw = std::find_if(
			preserveSnapshot->events.begin(), preserveSnapshot->events.end(),
			[](const EventRecord& a_event) { return a_event.kind == EventKind::kDraw; });
		Check(preserveDraw != preserveSnapshot->events.end() && preserveDraw->payload.words[2] != 0,
			"restore-true execution outside capture did not preserve the tracked stage identity");
	}

	void TestDeferredRecordingReportsPartialFilteredAndFailedFinishes()
	{
		Runtime runtime;
		runtime.RegisterDeferredContext(0x2100, 0);
		auto config = Config();
		config.maxEvents = 32;
		config.requestedEventKindMask = EventKindBit(EventKind::kFinishCommandList);
		config.maxBytes = Collector::RequiredStorageBytes(config);
		Check(runtime.StartCapture(config) == StartResult::kStarted,
			"partial deferred recording capture did not start");
		runtime.RecordDraw(0x2100, DrawOperation::kDraw, 3);
		runtime.RecordFinishCommandList(0x2100, 0x5100, true, 0);
		auto partial = runtime.StopCapture();
		Check(partial.has_value(), "partial deferred recording capture did not stop");
		const auto list = std::find_if(partial->events.begin(), partial->events.end(),
			[](const EventRecord& a_event) { return a_event.kind == EventKind::kCommandListObserved; });
		Check(list != partial->events.end() && list->payload.words[5] == 0 &&
				  (list->payload.words[6] & static_cast<std::uint64_t>(
												CommandRecordingIncompleteReason::kPartialAtCaptureStart)) != 0 &&
				  (list->payload.words[6] & static_cast<std::uint64_t>(
												CommandRecordingIncompleteReason::kEventNotRecorded)) != 0,
			"partial or filtered deferred work was incorrectly reported as complete");

		config.requestedEventKindMask = kAllEventKindsMask;
		config.maxBytes = Collector::RequiredStorageBytes(config);
		Check(runtime.StartCapture(config) == StartResult::kStarted,
			"failed FinishCommandList capture did not start");
		runtime.RecordFinishCommandList(0x2100, 0xDEAD, false, static_cast<std::int32_t>(0x80004005u));
		auto failed = runtime.StopCapture();
		Check(failed.has_value(), "failed FinishCommandList capture did not stop");
		const auto finish = std::find_if(failed->events.begin(), failed->events.end(),
			[](const EventRecord& a_event) { return a_event.kind == EventKind::kFinishCommandList; });
		Check(finish != failed->events.end() && finish->payload.words[1] == 0 &&
				  finish->payload.words[2] == 0 &&
				  static_cast<std::int32_t>(finish->payload.words[4]) < 0 &&
				  finish->payload.words[5] == 0,
			"failed FinishCommandList did not preserve its no-list failure outcome");
	}

	void TestDiagnosticCatalogueAdmissionFailuresFailOpen()
	{
		auto config = Config();
		config.maxEvents = 128;
		config.maxBytes = Collector::RequiredStorageBytes(config);

		Runtime inactiveContextRuntime;
		inactiveContextRuntime.FailNextDeferredContextCatalogueAdmissionForTesting();
		inactiveContextRuntime.RegisterDeferredContext(0xB000, 0);
		Check(inactiveContextRuntime.StartCapture(config) == StartResult::kStarted,
			"inactive context admission-failure capture did not start");
		inactiveContextRuntime.RecordDraw(0xB000, DrawOperation::kDraw, 3);
		auto inactiveContextSnapshot = inactiveContextRuntime.StopCapture();
		Check(inactiveContextSnapshot.has_value() && inactiveContextSnapshot->events.empty(),
			"inactive context admission failure fabricated a later deferred identity");

		Runtime contextRuntime;
		Check(contextRuntime.StartCapture(config) == StartResult::kStarted,
			"context admission-failure capture did not start");
		contextRuntime.FailNextDeferredContextCatalogueAdmissionForTesting();
		contextRuntime.RegisterDeferredContext(0xB100, 0);
		contextRuntime.RegisterDeferredContext(0xB100, 0);
		contextRuntime.FailNextDeferredContextCatalogueAdmissionForTesting();
		contextRuntime.RegisterDeferredContext(0xB100, 0, false);
		contextRuntime.RegisterDeferredContext(0xB200, 0);
		auto contextSnapshot = contextRuntime.StopCapture();
		Check(contextSnapshot.has_value(), "context admission-failure capture did not stop");
		Check(std::count_if(contextSnapshot->events.begin(), contextSnapshot->events.end(),
				  [](const EventRecord& event) {
					  return event.kind == EventKind::kDeviceContextObserved && event.payload.words[1] == 0xB100;
				  }) == 1 &&
				  std::none_of(contextSnapshot->events.begin(), contextSnapshot->events.end(),
					  [](const EventRecord& event) {
						  return event.kind == EventKind::kDeviceContextObserved && event.payload.words[1] == 0xB200;
					  }),
			"deferred-context admission failure fabricated or lost catalogue identity");

		Runtime finishRuntime;
		Check(finishRuntime.StartCapture(config) == StartResult::kStarted,
			"finish admission-failure capture did not start");
		finishRuntime.RegisterDeferredContext(0xB300, 0);
		finishRuntime.RecordDraw(0xB300, DrawOperation::kDraw, 3);
		finishRuntime.FailNextCommandListCatalogueAdmissionForTesting();
		finishRuntime.RecordFinishCommandList(0xB300, 0xB400, false, 0);
		auto finishSnapshot = finishRuntime.StopCapture();
		Check(finishSnapshot.has_value(), "finish admission-failure capture did not stop");
		const auto finish = std::find_if(finishSnapshot->events.begin(), finishSnapshot->events.end(),
			[](const EventRecord& event) { return event.kind == EventKind::kFinishCommandList; });
		Check(finish != finishSnapshot->events.end() && finish->payload.words[1] == 0 &&
				  finish->payload.words[2] == 0xB400 && finish->payload.words[5] == 0 &&
				  (finish->payload.words[6] & static_cast<std::uint64_t>(
												  CommandRecordingIncompleteReason::kEventNotRecorded)) != 0,
			"finish admission failure did not preserve a truthful successful D3D outcome");
		Check(std::count_if(finishSnapshot->events.begin(), finishSnapshot->events.end(),
				  [](const EventRecord& event) { return event.kind == EventKind::kCommandRecordingObserved; }) == 2,
			"finish admission failure did not advance to the next recording epoch");

		Runtime executeRuntime;
		executeRuntime.SetImmediateContext(0xB500);
		Check(executeRuntime.StartCapture(config) == StartResult::kStarted,
			"execute admission-failure capture did not start");
		const ResourceViewInput srv{
			.resource = { .d3dObject = 0xB510, .dimension = ResourceDimension::kTexture2D },
			.view = { .kind = TargetViewKind::kShaderResource, .d3dObject = 0xB520 },
		};
		const ResourceViewInput uav{
			.resource = { .d3dObject = 0xB550, .dimension = ResourceDimension::kTexture2D },
			.view = { .kind = TargetViewKind::kUnorderedAccess, .d3dObject = 0xB560 },
		};
		executeRuntime.BindStage(0xB500, ShaderStage::kVertex, 0xB530);
		executeRuntime.BindResourceViews(
			0xB500, ResourceBindingKind::kShaderResource, ResourceStage::kPixel, 2, 1, &srv,
			false, ResourceBindingSource::kPostCallQuery);
		executeRuntime.BindResourceViews(
			0xB500, ResourceBindingKind::kShaderResource, ResourceStage::kCompute, 0, 1, &srv,
			false, ResourceBindingSource::kPostCallQuery);
		executeRuntime.BindResourceViews(
			0xB500, ResourceBindingKind::kUnorderedAccess, ResourceStage::kCompute, 1, 1, &uav,
			false, ResourceBindingSource::kPostCallQuery);
		executeRuntime.FailNextCommandListCatalogueAdmissionForTesting();
		executeRuntime.RecordExecuteCommandList(0xB500, 0xB540, false);
		const auto recoveryGeneration = executeRuntime.ClaimResourceViewStateSeed(0xB500);
		Check(recoveryGeneration == executeRuntime.ActiveCaptureGeneration(),
			"restore-false execute failure did not request an effective resource observation");
		executeRuntime.BindResourceViews(
			0xB500, ResourceBindingKind::kShaderResource, ResourceStage::kPixel, 2, 1, nullptr,
			false, ResourceBindingSource::kCaptureStateSnapshot, recoveryGeneration);
		executeRuntime.BindResourceViews(
			0xB500, ResourceBindingKind::kShaderResource, ResourceStage::kCompute, 0, 1, nullptr,
			false, ResourceBindingSource::kCaptureStateSnapshot, recoveryGeneration);
		executeRuntime.BindResourceViews(
			0xB500, ResourceBindingKind::kUnorderedAccess, ResourceStage::kCompute, 1, 1, nullptr,
			false, ResourceBindingSource::kCaptureStateSnapshot, recoveryGeneration);
		executeRuntime.RecordDraw(0xB500, DrawOperation::kDraw, 3);
		executeRuntime.RecordDispatch(0xB500, DispatchOperation::kDispatch, 1, 1, 1);
		auto executeSnapshot = executeRuntime.StopCapture();
		Check(executeSnapshot.has_value(), "execute admission-failure capture did not stop");
		Check(std::none_of(executeSnapshot->events.begin(), executeSnapshot->events.end(),
				  [](const EventRecord& event) {
					  return event.kind == EventKind::kCommandListObserved ||
			                 event.kind == EventKind::kExecuteCommandList;
				  }),
			"first-seen execute admission failure fabricated command-list evidence");
		const auto nullBindings = std::count_if(executeSnapshot->events.begin(), executeSnapshot->events.end(),
			[](const EventRecord& event) {
				return event.kind == EventKind::kResourceViewBind && event.payload.words[0] == 0 &&
			           event.payload.words[4] ==
			               static_cast<std::uint64_t>(ResourceBindingSource::kCaptureStateSnapshot);
			});
		const auto draw = std::find_if(executeSnapshot->events.begin(), executeSnapshot->events.end(),
			[](const EventRecord& event) { return event.kind == EventKind::kDraw; });
		const auto dispatch = std::find_if(executeSnapshot->events.begin(), executeSnapshot->events.end(),
			[](const EventRecord& event) { return event.kind == EventKind::kDispatch; });
		Check(nullBindings == 3 && draw != executeSnapshot->events.end() &&
				  draw->payload.words[2] == 0 && dispatch != executeSnapshot->events.end() &&
				  dispatch->payload.words[2] == 0,
			"execute admission failure did not preserve dispatch recovery and explicit unbind evidence");
	}

	void WaitForDeferredPublicationPause(Runtime& a_runtime, std::thread& a_worker)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		while (!a_runtime.IsDeferredPublicationPausedForTesting() &&
			   std::chrono::steady_clock::now() < deadline) {
			std::this_thread::yield();
		}
		if (!a_runtime.IsDeferredPublicationPausedForTesting()) {
			a_runtime.ResumeDeferredPublicationForTesting();
			a_worker.join();
			Check(false, "deferred publication did not reach the test barrier");
		}
	}

	void TestImmediateStagePublicationRetainsCaptureGeneration()
	{
		constexpr std::uintptr_t immediateContext = 0xC010;
		constexpr std::uintptr_t deferredContext = 0xC020;
		constexpr std::uintptr_t sourceShader = 0xC030;
		constexpr std::uintptr_t unrelatedShader = 0xC040;
		constexpr std::uint64_t staleDrawArgument = 0xC050;
		constexpr std::uint64_t successorDrawArgument = 0xC060;
		auto config = Config();
		config.maxEvents = 128;
		config.maxStageShaderObservations = 8;
		config.maxBytes = Collector::RequiredStorageBytes(config);

		enum class Publisher
		{
			kSetter,
			kCacheMiss,
			kTechnique,
		};
		const std::array publishers{
			Publisher::kSetter,
			Publisher::kCacheMiss,
			Publisher::kTechnique,
		};

		const auto verifyDrawShader = [&](const CaptureSnapshot& a_snapshot, std::string_view a_case) {
			const auto draw = std::find_if(a_snapshot.events.begin(), a_snapshot.events.end(),
				[](const EventRecord& event) {
					return event.kind == EventKind::kDraw &&
				           event.payload.words[4] == successorDrawArgument;
				});
			Check(draw != a_snapshot.events.end() && draw->payload.words[2] != 0,
				std::format("{} did not retain an authoritative successor shader", a_case));
			const auto shader = draw == a_snapshot.events.end() ? a_snapshot.events.end() :
			                                                      std::find_if(a_snapshot.events.begin(), a_snapshot.events.end(),
																	  [draw](const EventRecord& event) {
																		  return event.kind == EventKind::kStageShaderObserved &&
				                                                                 event.payload.words[0] == draw->payload.words[2];
																	  });
			Check(shader != a_snapshot.events.end() && shader->payload.words[1] == sourceShader,
				std::format("{} relabelled the source shader as a successor observation", a_case));
		};

		for (const auto publisher : publishers) {
			Runtime runtime;
			runtime.SetImmediateContext(immediateContext);
			if (publisher != Publisher::kSetter)
				runtime.BindStage(immediateContext, ShaderStage::kVertex, sourceShader);
			Check(runtime.StartCapture(config) == StartResult::kStarted,
				"immediate publication source capture did not start");
			runtime.PauseNextImmediateStagePublicationForTesting();
			std::thread staleWorker([&] {
				switch (publisher) {
				case Publisher::kSetter:
					runtime.BindStage(immediateContext, ShaderStage::kVertex, sourceShader);
					break;
				case Publisher::kCacheMiss:
					runtime.RecordDraw(immediateContext, DrawOperation::kDraw, staleDrawArgument);
					break;
				case Publisher::kTechnique:
					runtime.RecordTechniqueResolution({
						.shaderFound = true,
						.vertex = {
							.route = ShaderSelectionRoute::kEngine,
							.shader = {
								.stage = ShaderStage::kVertex,
								.d3dObject = sourceShader,
							},
						},
					});
					break;
				}
			});
			WaitForDeferredPublicationPause(runtime, staleWorker);

			auto first = runtime.StopCapture();
			Check(first.has_value() && runtime.StartCapture(config) == StartResult::kStarted,
				"immediate publication capture turnover failed");
			runtime.RegisterDeferredContext(deferredContext, 0);
			runtime.BindStage(deferredContext, ShaderStage::kVertex, unrelatedShader);
			runtime.ResumeDeferredPublicationForTesting();
			staleWorker.join();
			runtime.RecordDraw(immediateContext, DrawOperation::kDraw, successorDrawArgument);

			auto second = runtime.StopCapture();
			Check(second.has_value(), "immediate publication successor capture did not stop");
			Check(std::none_of(second->events.begin(), second->events.end(),
					  [](const EventRecord& event) {
						  return event.kind == EventKind::kDraw &&
				                 event.payload.words[4] == staleDrawArgument;
					  }),
				"stale immediate draw entered the successor capture");
			verifyDrawShader(*second, "capture-turnover publication");
		}

		Runtime stableRuntime;
		stableRuntime.SetImmediateContext(immediateContext);
		Check(stableRuntime.StartCapture(config) == StartResult::kStarted,
			"stable immediate publication capture did not start");
		stableRuntime.PauseNextImmediateStagePublicationForTesting();
		std::thread stableWorker([&] {
			stableRuntime.BindStage(immediateContext, ShaderStage::kVertex, sourceShader);
		});
		WaitForDeferredPublicationPause(stableRuntime, stableWorker);
		stableRuntime.ResumeDeferredPublicationForTesting();
		stableWorker.join();
		stableRuntime.RecordDraw(immediateContext, DrawOperation::kDraw, successorDrawArgument);
		auto stable = stableRuntime.StopCapture();
		Check(stable.has_value(), "stable immediate publication capture did not stop");
		verifyDrawShader(*stable, "stable-generation publication");
	}

	void TestDeferredPublicationRetainsCaptureGeneration()
	{
		constexpr std::uintptr_t oldContext = 0xC100;
		constexpr std::uint64_t staleArgument = 0xA11CE;
		auto config = Config();
		config.maxEvents = 128;
		config.maxBytes = Collector::RequiredStorageBytes(config);

		const auto runTurnover = [&](std::uintptr_t a_successorContext, bool a_allocateUnrelatedFirst) {
			Runtime runtime;
			Check(runtime.StartCapture(config) == StartResult::kStarted,
				"deferred turnover source capture did not start");
			runtime.RegisterDeferredContext(oldContext, 0);
			runtime.PauseNextDeferredPublicationForTesting();
			std::thread staleWorker([&] {
				runtime.RecordDraw(oldContext, DrawOperation::kDraw, staleArgument);
			});
			WaitForDeferredPublicationPause(runtime, staleWorker);

			auto first = runtime.StopCapture();
			const auto successorStarted = runtime.StartCapture(config) == StartResult::kStarted;
			if (successorStarted) {
				if (a_allocateUnrelatedFirst) {
					runtime.RegisterDeferredContext(0xC200, 0);
					runtime.RecordDraw(0xC200, DrawOperation::kDraw, 0xC200);
				}
				runtime.RegisterDeferredContext(a_successorContext, 0);
			}
			runtime.ResumeDeferredPublicationForTesting();
			staleWorker.join();
			Check(first.has_value() && successorStarted,
				"deferred turnover capture transition failed");

			runtime.RecordDraw(a_successorContext, DrawOperation::kDraw, 0xB0B);
			runtime.RecordFinishCommandList(a_successorContext, 0xC400, false, 0);
			auto second = runtime.StopCapture();
			Check(second.has_value(), "deferred turnover successor capture did not stop");
			Check(std::none_of(second->events.begin(), second->events.end(),
					  [](const EventRecord& event) {
						  return event.kind == EventKind::kDraw && event.payload.words[4] == staleArgument;
					  }),
				"stale deferred work was relabelled into the successor capture");
			Check(std::any_of(second->events.begin(), second->events.end(),
					  [a_successorContext](const EventRecord& event) {
						  return event.kind == EventKind::kDraw && event.payload.words[0] == a_successorContext &&
				                 event.payload.words[4] == 0xB0B;
					  }),
				"valid successor deferred work was not recorded");
			const auto list = std::find_if(second->events.begin(), second->events.end(),
				[](const EventRecord& event) {
					return event.kind == EventKind::kCommandListObserved && event.payload.words[1] == 0xC400;
				});
			Check(list != second->events.end() &&
					  (list->payload.words[6] & static_cast<std::uint64_t>(
													CommandRecordingIncompleteReason::kEventNotRecorded)) == 0,
				"stale deferred fallback mutated the successor recording");
		};

		// Matching ordinals across different and reused context pointers are the
		// dangerous cases; an unrelated allocation also covers non-matching IDs.
		runTurnover(0xC300, false);
		runTurnover(0xC300, true);
		runTurnover(oldContext, false);

		Runtime stoppedRuntime;
		Check(stoppedRuntime.StartCapture(config) == StartResult::kStarted,
			"deferred no-successor capture did not start");
		stoppedRuntime.RegisterDeferredContext(oldContext, 0);
		stoppedRuntime.PauseNextDeferredPublicationForTesting();
		std::thread stoppedWorker([&] {
			stoppedRuntime.RecordDraw(oldContext, DrawOperation::kDraw, staleArgument);
		});
		WaitForDeferredPublicationPause(stoppedRuntime, stoppedWorker);
		auto stoppedSnapshot = stoppedRuntime.StopCapture();
		stoppedRuntime.ResumeDeferredPublicationForTesting();
		stoppedWorker.join();
		Check(stoppedSnapshot.has_value() &&
				  std::none_of(stoppedSnapshot->events.begin(), stoppedSnapshot->events.end(),
					  [](const EventRecord& event) {
						  return event.kind == EventKind::kDraw && event.payload.words[4] == staleArgument;
					  }),
			"stale deferred work was retained after capture stopped");

		Runtime sameGenerationRuntime;
		Check(sameGenerationRuntime.StartCapture(config) == StartResult::kStarted,
			"same-generation deferred capture did not start");
		sameGenerationRuntime.RegisterDeferredContext(oldContext, 0);
		sameGenerationRuntime.PauseNextDeferredPublicationForTesting();
		std::thread sameGenerationWorker([&] {
			sameGenerationRuntime.RecordDraw(oldContext, DrawOperation::kDraw, staleArgument);
		});
		WaitForDeferredPublicationPause(sameGenerationRuntime, sameGenerationWorker);
		sameGenerationRuntime.ResumeDeferredPublicationForTesting();
		sameGenerationWorker.join();
		auto sameGenerationSnapshot = sameGenerationRuntime.StopCapture();
		Check(sameGenerationSnapshot.has_value() &&
				  std::any_of(sameGenerationSnapshot->events.begin(), sameGenerationSnapshot->events.end(),
					  [](const EventRecord& event) {
						  return event.kind == EventKind::kDraw && event.payload.words[4] == staleArgument;
					  }),
			"same-generation deferred work was incorrectly rejected");
	}

	void TestDeferredControlPublicationRetainsRecordingLifetime()
	{
		constexpr std::uintptr_t oldContext = 0xD100;
		constexpr std::uintptr_t successorShader = 0xD300;
		auto config = Config();
		config.maxEvents = 128;
		config.maxStageShaderObservations = 8;
		config.maxBytes = Collector::RequiredStorageBytes(config);

		enum class FinishOutcome
		{
			kSuccess,
			kFailure,
			kNoList,
			kCatalogueAdmissionFailure,
		};
		const std::array outcomes{
			FinishOutcome::kSuccess,
			FinishOutcome::kFailure,
			FinishOutcome::kNoList,
			FinishOutcome::kCatalogueAdmissionFailure,
		};
		const std::array successorContexts{
			oldContext,
			std::uintptr_t{ 0xD400 },
			std::uintptr_t{ 0xD500 },
		};

		for (const auto outcome : outcomes) {
			for (const auto restoreState : { false, true }) {
				for (std::size_t successorIndex = 0; successorIndex < successorContexts.size(); ++successorIndex) {
					Runtime runtime;
					Check(runtime.StartCapture(config) == StartResult::kStarted,
						"finish turnover source capture did not start");
					runtime.RegisterDeferredContext(oldContext, 0);
					runtime.RecordDraw(oldContext, DrawOperation::kDraw, 1);
					if (outcome == FinishOutcome::kCatalogueAdmissionFailure)
						runtime.FailNextCommandListCatalogueAdmissionForTesting();
					runtime.PauseNextDeferredPublicationForTesting();
					const auto commandList = outcome == FinishOutcome::kNoList ? 0 : std::uintptr_t{ 0xD200 };
					const auto result = outcome == FinishOutcome::kFailure ?
					                        static_cast<std::int32_t>(0x80004005u) :
					                        0;
					std::thread staleWorker([&] {
						runtime.RecordFinishCommandList(oldContext, commandList, restoreState, result);
					});
					WaitForDeferredPublicationPause(runtime, staleWorker);

					auto first = runtime.StopCapture();
					Check(first.has_value() && runtime.StartCapture(config) == StartResult::kStarted,
						"finish turnover capture transition failed");
					if (successorIndex == 2)
						runtime.RegisterDeferredContext(0xD600, 0);
					const auto successorContext = successorContexts[successorIndex];
					runtime.RegisterDeferredContext(successorContext, 0);
					runtime.BindStage(successorContext, ShaderStage::kVertex, successorShader);
					runtime.BindStage(successorContext, ShaderStage::kCompute, successorShader + 1);
					runtime.ResumeDeferredPublicationForTesting();
					staleWorker.join();
					runtime.RecordDraw(successorContext, DrawOperation::kDraw, 2);
					runtime.RecordDispatch(successorContext, DispatchOperation::kDispatch, 1, 1, 1);

					auto second = runtime.StopCapture();
					Check(second.has_value(), "finish turnover successor capture did not stop");
					const auto draw = std::find_if(second->events.begin(), second->events.end(),
						[](const EventRecord& event) {
							return event.kind == EventKind::kDraw && event.payload.words[4] == 2;
						});
					Check(draw != second->events.end() && draw->payload.words[2] != 0,
						"stale finish cleanup erased the successor stage binding");
					const auto stage = std::find_if(second->events.begin(), second->events.end(),
						[draw](const EventRecord& event) {
							return event.kind == EventKind::kStageShaderObserved &&
						           event.payload.words[0] == draw->payload.words[2];
						});
					Check(stage != second->events.end() && stage->payload.words[1] == successorShader,
						"stale finish cleanup relabelled the successor stage binding");
					const auto dispatch = std::find_if(second->events.begin(), second->events.end(),
						[](const EventRecord& event) { return event.kind == EventKind::kDispatch; });
					Check(dispatch != second->events.end() && dispatch->payload.words[2] != 0,
						"stale finish cleanup erased the successor compute binding");
					const auto computeStage = std::find_if(second->events.begin(), second->events.end(),
						[dispatch](const EventRecord& event) {
							return event.kind == EventKind::kStageShaderObserved &&
						           event.payload.words[0] == dispatch->payload.words[2];
						});
					Check(computeStage != second->events.end() &&
							  computeStage->payload.words[1] == successorShader + 1,
						"stale finish cleanup relabelled the successor compute binding");
					const auto recordingCount = std::count_if(second->events.begin(), second->events.end(),
						[draw](const EventRecord& event) {
							return event.kind == EventKind::kCommandRecordingObserved &&
						           event.payload.words[1] == draw->deviceContextObservationId;
						});
					Check(recordingCount == 1,
						std::format("stale finish cleanup left {} successor recording epochs for outcome {}, restore {}, context {}",
							recordingCount, static_cast<int>(outcome), restoreState, successorIndex));
					Check(std::none_of(second->events.begin(), second->events.end(),
							  [](const EventRecord& event) {
								  return event.kind == EventKind::kFinishCommandList;
							  }),
						"stale finish publication entered the successor capture");
				}
			}
		}

		Runtime stoppedRuntime;
		Check(stoppedRuntime.StartCapture(config) == StartResult::kStarted,
			"finish no-successor capture did not start");
		stoppedRuntime.RegisterDeferredContext(oldContext, 0);
		stoppedRuntime.PauseNextDeferredFinishCleanupForTesting();
		std::thread stoppedWorker([&] {
			stoppedRuntime.RecordFinishCommandList(oldContext, 0xD200, false, 0);
		});
		WaitForDeferredPublicationPause(stoppedRuntime, stoppedWorker);
		auto stoppedSnapshot = stoppedRuntime.StopCapture();
		stoppedRuntime.ResumeDeferredPublicationForTesting();
		stoppedWorker.join();
		Check(stoppedSnapshot.has_value(), "finish no-successor capture did not stop");

		for (const auto restoreState : { false, true }) {
			Runtime runtime;
			Check(runtime.StartCapture(config) == StartResult::kStarted,
				"finish cleanup-turnover source capture did not start");
			runtime.RegisterDeferredContext(oldContext, 0);
			runtime.PauseNextDeferredFinishCleanupForTesting();
			std::thread staleWorker([&] {
				runtime.RecordFinishCommandList(oldContext, 0xD200, restoreState, 0);
			});
			WaitForDeferredPublicationPause(runtime, staleWorker);

			auto first = runtime.StopCapture();
			Check(first.has_value() && std::any_of(first->events.begin(), first->events.end(),
										   [](const EventRecord& event) {
											   return event.kind == EventKind::kFinishCommandList;
										   }),
				"finish did not publish before the cleanup turnover barrier");
			Check(runtime.StartCapture(config) == StartResult::kStarted,
				"finish cleanup-turnover successor capture did not start");
			runtime.RegisterDeferredContext(oldContext, 0);
			runtime.BindStage(oldContext, ShaderStage::kVertex, successorShader);
			runtime.ResumeDeferredPublicationForTesting();
			staleWorker.join();
			runtime.RecordDraw(oldContext, DrawOperation::kDraw, 4);

			auto second = runtime.StopCapture();
			Check(second.has_value(), "finish cleanup-turnover successor capture did not stop");
			const auto draw = std::find_if(second->events.begin(), second->events.end(),
				[](const EventRecord& event) {
					return event.kind == EventKind::kDraw && event.payload.words[4] == 4;
				});
			Check(draw != second->events.end() && draw->payload.words[2] != 0,
				"post-publication finish cleanup erased the successor stage binding");
			Check(std::count_if(second->events.begin(), second->events.end(),
					  [draw](const EventRecord& event) {
						  return event.kind == EventKind::kCommandRecordingObserved &&
				                 event.payload.words[1] == draw->deviceContextObservationId;
					  }) == 1,
				"post-publication finish cleanup restarted the successor recording epoch");
		}

		for (std::size_t successorIndex = 0; successorIndex < successorContexts.size(); ++successorIndex) {
			Runtime runtime;
			Check(runtime.StartCapture(config) == StartResult::kStarted,
				"stage turnover source capture did not start");
			runtime.RegisterDeferredContext(oldContext, 0);
			runtime.PauseNextDeferredPublicationForTesting();
			std::thread staleWorker([&] {
				runtime.BindStage(oldContext, ShaderStage::kVertex, 0xD700);
			});
			WaitForDeferredPublicationPause(runtime, staleWorker);

			auto first = runtime.StopCapture();
			Check(first.has_value() && runtime.StartCapture(config) == StartResult::kStarted,
				"stage turnover capture transition failed");
			if (successorIndex == 2)
				runtime.RegisterDeferredContext(0xD600, 0);
			const auto successorContext = successorContexts[successorIndex];
			runtime.RegisterDeferredContext(successorContext, 0);
			runtime.BindStage(successorContext, ShaderStage::kVertex, successorShader);
			runtime.ResumeDeferredPublicationForTesting();
			staleWorker.join();
			runtime.RecordDraw(successorContext, DrawOperation::kDraw, 3);

			auto second = runtime.StopCapture();
			Check(second.has_value(), "stage turnover successor capture did not stop");
			const auto draw = std::find_if(second->events.begin(), second->events.end(),
				[](const EventRecord& event) {
					return event.kind == EventKind::kDraw && event.payload.words[4] == 3;
				});
			Check(draw != second->events.end() && draw->payload.words[2] != 0,
				"stale stage publication erased the successor binding");
			const auto stage = std::find_if(second->events.begin(), second->events.end(),
				[draw](const EventRecord& event) {
					return event.kind == EventKind::kStageShaderObserved &&
				           event.payload.words[0] == draw->payload.words[2];
				});
			Check(stage != second->events.end() && stage->payload.words[1] == successorShader,
				"stale stage publication replaced the successor binding");
		}
	}
}

int main()
{
	try {
		TestInactiveRuntime();
		TestNestedBoundaries();
		TestShaderIdentityGenerations();
		TestCpuFrameUpdatePreservesEyeContext();
		TestShaderObservationBoundIsExplicit();
		TestResolvedStageShaderIdentity();
		TestStageShaderObservationBoundIsExplicit();
		TestImmediateContextDrawAndDispatchState();
		TestCaptureStartSeedsInheritedStageIdentity();
		TestCreatedStagePointerReuseAdvancesIdentity();
		TestStageShaderEvidenceEnrichesWithoutPointerReuse();
		TestImmediateContextOutputMergerState();
		TestCaptureStartClaimsOneEffectiveOutputMergerSnapshot();
		TestOutputMergerBoundsAreExplicit();
		TestResourceFlowStateIsTypedAndOrdered();
		TestCpuMapUnmapBoundariesArePairedAndTimed();
		TestEffectiveResourceViewQueriesAreRevisionedAndDeltaEncoded();
		TestRestoreFalseReobservesEffectiveResourceViews();
		TestExecutionJoinsDeclaredScopes();
		TestVisibilitySubmissionJoinsActualDraw();
		TestEventKindSelectionPreservesDependenciesAndCapacity();
		TestSemanticIdentityCataloguesAreBoundedAndRevisioned();
		TestGeometrySelectionFiltersBeforeSemanticWork();
		TestPreparedGeometryHandoffRejectsStaleCandidates();
		TestGeometryBoundaryBindsExactSemanticObservations();
		TestDeferredRecordingMaterializesAndExecutesCommandList();
		TestExecuteRestoreStateIsIndependentOfCaptureAdmission();
		TestDeferredRecordingReportsPartialFilteredAndFailedFinishes();
		TestDiagnosticCatalogueAdmissionFailuresFailOpen();
		TestImmediateStagePublicationRetainsCaptureGeneration();
		TestDeferredPublicationRetainsCaptureGeneration();
		TestDeferredControlPublicationRetainsRecordingLifetime();
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
