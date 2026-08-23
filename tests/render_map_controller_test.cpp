#include "RenderMap/Controller.h"
#include "RenderMap/Serialization.h"

#include <chrono>
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
		Check(completed && completed->snapshot.events.size() == 6, "completed capture is incomplete");
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
		Check(status.completedCaptureIds.size() == 1, "completed capture was not retained");

		const auto summary = SerializeCaptureSummary(*capture);
		Check(summary["captureId"] == capture->descriptor.captureId, "summary capture ID is wrong");
		Check(summary["completion"]["eventCount"] == 6, "summary event count is wrong");
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
		Check(first["scopes"]["renderPass"].get<std::string>().starts_with("obs-render-pass-"),
			"render-pass observation ID is wrong");
		Check(first["payload"]["renderPassPointer"] == "0x1000", "pointer evidence is wrong");

		const auto finalPage = SerializeEventPage(*capture, 5, 100, 42);
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
}

int main()
{
	try {
		TestControllerAndSerialization();
		TestCompletedHistoryBound();
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
