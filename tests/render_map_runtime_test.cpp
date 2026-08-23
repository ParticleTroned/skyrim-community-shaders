#include "RenderMap/Runtime.h"

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
		Check(snapshot->events.size() == 6, "runtime boundary event count is wrong");
		Check(snapshot->events[0].kind == EventKind::kRenderPassEnter, "render-pass begin is missing");
		Check(snapshot->events[1].kind == EventKind::kTechniqueBegin, "technique begin is missing");
		Check(snapshot->events[2].kind == EventKind::kTechniqueEnd, "technique end is missing");
		Check(snapshot->events[3].kind == EventKind::kGeometrySetupBegin, "geometry begin is missing");
		Check(snapshot->events[4].kind == EventKind::kGeometrySetupEnd, "geometry end is missing");
		Check(snapshot->events[5].kind == EventKind::kRenderPassExit, "render-pass end is missing");

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
		Check(snapshot->events[1].payload.schema == static_cast<std::uint16_t>(PayloadSchema::kTechniqueBoundary),
			"technique payload schema is wrong");
		Check(snapshot->events[1].payload.words[3] == 9, "technique descriptor evidence is wrong");
		Check(snapshot->events[3].payload.schema == static_cast<std::uint16_t>(PayloadSchema::kGeometryBoundary),
			"geometry payload schema is wrong");
		Check(snapshot->events[3].payload.words[2] == 0x2000, "geometry pointer evidence is wrong");
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
}

int main()
{
	try {
		TestInactiveRuntime();
		TestNestedBoundaries();
		TestCpuFrameUpdatePreservesEyeContext();
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
