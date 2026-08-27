#include "RenderMap/Runtime.h"

#include <algorithm>
#include <iostream>
#include <memory>
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
		Check(snapshot->events.size() == 7, "runtime boundary event count is wrong");
		Check(snapshot->events[0].kind == EventKind::kRenderPassEnter, "render-pass begin is missing");
		Check(snapshot->events[1].kind == EventKind::kShaderObserved, "first-seen shader event is missing");
		Check(snapshot->events[2].kind == EventKind::kTechniqueBegin, "technique begin is missing");
		Check(snapshot->events[3].kind == EventKind::kTechniqueEnd, "technique end is missing");
		Check(snapshot->events[4].kind == EventKind::kGeometrySetupBegin, "geometry begin is missing");
		Check(snapshot->events[5].kind == EventKind::kGeometrySetupEnd, "geometry end is missing");
		Check(snapshot->events[6].kind == EventKind::kRenderPassExit, "render-pass end is missing");

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
		Check(snapshot->events[4].payload.schema == static_cast<std::uint16_t>(PayloadSchema::kGeometryBoundary),
			"geometry payload schema is wrong");
		Check(snapshot->events[4].payload.words[2] == 0x2000, "geometry pointer evidence is wrong");
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
				.shader = 0x4444, .shaderType = 5, .fxpFilename = "ImageSpace",
				.imageSpaceName = "ISCopy",
			});
		}
		{
			auto repeated = runtime.EnterTechnique({
				.shader = 0x4444, .shaderType = 5, .fxpFilename = "ImageSpace",
				.imageSpaceName = "ISCopy",
			});
		}
		{
			auto changed = runtime.EnterTechnique({
				.shader = 0x4444, .shaderType = 5, .fxpFilename = "ImageSpace",
				.imageSpaceName = "ISBlur3",
			});
		}
		runtime.RetireShaderObservation(0x4444);
		{
			auto reused = runtime.EnterTechnique({
				.shader = 0x4444, .shaderType = 5, .fxpFilename = "ImageSpace",
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
				.pixel = {
					.route = ShaderSelectionRoute::kEngine,
					.shader = {
						.stage = ShaderStage::kPixel, .wrapper = 0x4000, .d3dObject = 0x5000,
						.wrapperDescriptor = 18, .bytecodeSize = 256,
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
			"Data/ShaderCache/Lighting/11.vso", "stage cache path was not retained");
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
				.shader = { .stage = ShaderStage::kVertex, .wrapper = 0x2000, .d3dObject = 0x3000,
					.wrapperDescriptor = 17, .bytecodeSize = 128 },
			},
			.pixel = {
				.route = ShaderSelectionRoute::kEngine,
				.shader = { .stage = ShaderStage::kPixel, .wrapper = 0x4000, .d3dObject = 0x5000,
					.wrapperDescriptor = 18, .bytecodeSize = 256 },
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
			dispatch->payload.words[5] == 2, "dispatch arguments are wrong");
		Check(dispatch->deviceContextObservationId == context->deviceContextObservationId,
			"dispatch did not join to the declared immediate context");
		Check(dispatch->commandStreamSequence == 5,
			"dispatch command-stream sequence is not monotonic after draw");
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
			.d3dObject = 0xE100, .dimension = ResourceDimension::kTexture2D,
			.widthOrBytes = 1024, .height = 512, .depthOrArraySize = 2, .mipLevels = 4,
			.format = 28, .sampleCount = 1, .bindFlags = 0x28,
		};
		const ResourceObservationInput destination{
			.d3dObject = 0xE200, .dimension = ResourceDimension::kTexture2D,
			.widthOrBytes = 1024, .height = 512, .depthOrArraySize = 2, .mipLevels = 4,
			.format = 28, .sampleCount = 1, .bindFlags = 0x28,
		};
		const ResourceViewInput srv{
			.resource = source,
			.view = { .kind = TargetViewKind::kShaderResource, .d3dObject = 0xE300,
				.format = 28, .dimension = 5, .mipSlice = 1, .arraySize = 3 },
		};
		const ResourceViewInput rtv{
			.resource = destination,
			.view = { .kind = TargetViewKind::kRenderTarget, .d3dObject = 0xE400,
				.format = 28, .dimension = 5, .mipSlice = 0, .firstArraySlice = 1, .arraySize = 1 },
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
					.shader = 0xA400, .shaderType = 4, .fxpFilename = "Lighting",
				});
				Check(technique.IsActive(), "scope-join technique did not enter");
				{
					auto geometry = runtime.EnterGeometry({
						.shader = 0xA400, .renderPass = 0xA200, .geometry = 0xA300,
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
			ScopeKind::kRenderPass), "draw references an undeclared render-pass scope");
		Check(declaredBeforeDraw(EventKind::kTechniqueBegin, draw->scopes.technique.observationId,
			ScopeKind::kTechnique), "draw references an undeclared technique scope");
		Check(declaredBeforeDraw(EventKind::kGeometrySetupBegin, draw->scopes.geometry.observationId,
			ScopeKind::kGeometry), "draw references an undeclared geometry scope");
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
		runtime.RecordVisibilityCandidate(0xF100, 12, 100);

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
		TestImmediateContextOutputMergerState();
		TestOutputMergerBoundsAreExplicit();
		TestResourceFlowStateIsTypedAndOrdered();
		TestExecutionJoinsDeclaredScopes();
		TestVisibilitySubmissionJoinsActualDraw();
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
