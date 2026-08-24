#include "RenderMap/Runtime.h"

#include <algorithm>
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
			.captureNumericId = 77,
			.maxFrames = 2,
			.maxEvents = 32,
			.maxBytes = Collector::EventRecordSize() * 32,
			.maxDuration = std::chrono::minutes(1),
			.maxScopeDepth = 8,
		};
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
		config.maxBytes = Collector::EventRecordSize() * 64;
		config.maxStageShaderObservations = 8;
		runtime.SetImmediateContext(0x9000);
		runtime.BindStage(0x9000, ShaderStage::kVertex, 0x3000);
		runtime.BindStage(0x9000, ShaderStage::kPixel, 0x5000);
		runtime.BindStage(0x9000, ShaderStage::kCompute, 0x7000);
		Check(runtime.StartCapture(config) == StartResult::kStarted, "draw capture did not start");
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
			"draw and dispatch did not share or create the expected stage identities");
		const auto draw = std::find_if(snapshot->events.begin(), snapshot->events.end(),
			[](const EventRecord& a_event) { return a_event.kind == EventKind::kDraw; });
		Check(draw != snapshot->events.end(), "immediate-context draw was not recorded");
		Check(draw->payload.schema == static_cast<std::uint16_t>(PayloadSchema::kDrawCall),
			"draw payload schema is wrong");
		Check(draw->payload.words[0] == 0x9000 && draw->payload.words[2] != 0 && draw->payload.words[3] != 0,
			"draw did not retain context and selected stage references");
		Check(draw->payload.words[4] == 24 && draw->payload.words[5] == 3 && draw->payload.words[6] == 2,
			"draw arguments are wrong");
		Check(std::count_if(snapshot->events.begin(), snapshot->events.end(),
			[](const EventRecord& a_event) { return a_event.kind == EventKind::kDraw; }) == 1,
			"non-immediate context draw was recorded");
		const auto dispatch = std::find_if(snapshot->events.begin(), snapshot->events.end(),
			[](const EventRecord& a_event) { return a_event.kind == EventKind::kDispatch; });
		Check(dispatch != snapshot->events.end() && dispatch->payload.words[2] != 0,
			"dispatch did not reference its compute shader");
		Check(dispatch->payload.words[3] == 8 && dispatch->payload.words[4] == 4 &&
			dispatch->payload.words[5] == 2, "dispatch arguments are wrong");
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
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
