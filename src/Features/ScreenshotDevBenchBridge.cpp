#include "Features/ScreenshotDevBenchBridge.h"

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "Features/ScreenshotFeature.h"
#	include "Globals.h"

#	include <DevBenchAPI.h>
#	include <nlohmann/json.hpp>

#	include <atomic>
#	include <chrono>
#	include <format>
#	include <functional>
#	include <future>
#	include <memory>
#	include <stdexcept>
#	include <string>

namespace
{
	using json = nlohmann::json;

	constexpr auto kMainThreadTimeout = std::chrono::milliseconds(5000);
	constexpr unsigned int kDevBenchToolExtensionRevision = 5;
	std::atomic_bool g_installAttempted{ false };
	std::atomic_bool g_registered{ false };

	json RunOnMainThread(std::function<json()> a_run)
	{
		auto* taskInterface = SKSE::GetTaskInterface();
		if (!taskInterface)
			return { { "error", "SKSE task interface unavailable" } };

		auto promise = std::make_shared<std::promise<json>>();
		auto cancelled = std::make_shared<std::atomic_bool>(false);
		auto future = promise->get_future();
		taskInterface->AddTask([promise, cancelled, run = std::move(a_run)]() mutable {
			if (cancelled->load(std::memory_order_acquire))
				return;
			try {
				promise->set_value(run());
			} catch (const std::exception& e) {
				promise->set_value(json{ { "error", "main-thread task failed" }, { "detail", e.what() } });
			} catch (...) {
				promise->set_value(json{ { "error", "main-thread task failed" } });
			}
		});

		if (future.wait_for(kMainThreadTimeout) != std::future_status::ready) {
			cancelled->store(true, std::memory_order_release);
			return { { "error", "main thread did not run within 5000ms" } };
		}
		return future.get();
	}

	json BuildFrameStatus(const ScreenshotFeature::CaptureSessionFrameStatus& a_frame)
	{
		return {
			{ "index", a_frame.index },
			{ "compositorCycleToken", a_frame.compositorCycleToken },
			{ "finished", a_frame.finished },
			{ "succeeded", a_frame.succeeded },
			{ "combinedPath", a_frame.combinedPath.string() },
			{ "leftEyePath", a_frame.eyePaths[0].string() },
			{ "rightEyePath", a_frame.eyePaths[1].string() },
			{ "error", a_frame.error },
		};
	}

	json BuildStatus(const ScreenshotFeature& a_feature)
	{
		const auto status = a_feature.GetCaptureSessionStatus();
		json frames = json::array();
		for (const auto& frame : status.frames)
			frames.push_back(BuildFrameStatus(frame));
		return {
			{ "id", status.id },
			{ "state", ScreenshotFeature::GetCaptureSessionStateName(status.state) },
			{ "source", ScreenshotFeature::GetCaptureSourceName(status.request.source) },
			{ "label", status.request.label },
			{ "requestedFrameCount", status.request.frameCount },
			{ "frameIntervalCompositorCycles", status.request.frameInterval },
			{ "previewFramesPerSecond", status.request.previewFramesPerSecond },
			{ "saveCombined", status.request.saveCombined },
			{ "saveSeparateEyes", status.request.saveSeparateEyes },
			{ "previewVideoRequested", status.request.writePreviewVideo },
			{ "outputDirectory", status.outputDirectory.string() },
			{ "manifestPath", status.manifestPath.string() },
			{ "previewVideoPath", status.previewVideoPath.string() },
			{ "previewVideoFinished", status.previewVideoFinished },
			{ "previewVideoSucceeded", status.previewVideoSucceeded },
			{ "previewVideoError", status.previewVideoError },
			{ "framesQueued", status.framesQueued },
			{ "framesFinished", status.framesFinished },
			{ "framesSaved", status.framesSaved },
			{ "framesFailed", status.framesFailed },
			{ "backpressureDrops", status.backpressureDrops },
			{ "incompleteStereoDrops", status.incompleteStereoDrops },
			{ "cancelRequested", status.cancelRequested },
			{ "error", status.error },
			{ "frames", std::move(frames) },
		};
	}

	json BuildCapabilities()
	{
		constexpr auto automationAccess = ScreenshotCaptureSessionPolicy::ResolveAccess(
			ScreenshotCaptureSessionPolicy::CaptureSurface::BoundedProductionSession,
			false);
		return {
			{ "tool", "communityshaders.capture" },
			{ "runtime", globals::game::isVR ? "VR" : "flat" },
			{ "sources", json::array({ "hmd_stereo", "framed_stereo", "framed_eye" }) },
			{ "maxFrameCount", ScreenshotCaptureSessionPolicy::kMaxFrameCount },
			{ "maxFrameIntervalCompositorCycles", ScreenshotCaptureSessionPolicy::kMaxFrameInterval },
			{ "maxPreviewFramesPerSecond", ScreenshotCaptureSessionPolicy::kMaxPreviewFramesPerSecond },
			{ "exactStereoPairing", true },
			{ "pairingKey", "accepted OpenVR compositor cycle token" },
			{ "losslessFrameSequence", true },
			{ "previewVideo", "mjpeg-avi" },
			{ "performanceMeasurementSeparated", true },
			{ "automatedControl",
				{
					{ "available", globals::game::isVR && automationAccess.allowed },
					{ "requiresDeveloperMode", automationAccess.requiresDeveloperMode },
					{ "logLevelIndependent", true },
				} },
		};
	}

	std::optional<ScreenshotFeature::VRCaptureSource> ParseSource(const std::string& a_source)
	{
		if (a_source == "hmd_stereo")
			return ScreenshotFeature::VRCaptureSource::HMDSubmission;
		if (a_source == "framed_stereo")
			return ScreenshotFeature::VRCaptureSource::FramedStereo;
		if (a_source == "framed_eye")
			return ScreenshotFeature::VRCaptureSource::FramedEye;
		return std::nullopt;
	}

	std::optional<uint32_t> ReadBoundedUnsigned(
		const json& a_args,
		const char* a_name,
		uint32_t a_defaultValue,
		uint32_t a_maximum,
		json& a_error)
	{
		const auto value = a_args.find(a_name);
		if (value == a_args.end())
			return a_defaultValue;
		uint64_t parsed = 0;
		if (value->is_number_unsigned()) {
			parsed = value->get<uint64_t>();
		} else if (value->is_number_integer()) {
			const auto signedValue = value->get<int64_t>();
			if (signedValue > 0)
				parsed = static_cast<uint64_t>(signedValue);
		} else {
			a_error = { { "error", std::format("{} must be an integer", a_name) } };
			return std::nullopt;
		}
		if (parsed == 0 || parsed > a_maximum) {
			a_error = {
				{ "error", std::format("{} must be between 1 and {}", a_name, a_maximum) },
			};
			return std::nullopt;
		}
		return static_cast<uint32_t>(parsed);
	}

	json BuildCaptureResult(const json& a_args)
	{
		const std::string action = a_args.value("action", std::string("status"));
		if (action == "capabilities")
			return { { "action", action }, { "capabilities", BuildCapabilities() } };
		if (action == "status") {
			return RunOnMainThread([]() {
				return json{
					{ "action", "status" },
					{ "status", BuildStatus(globals::features::screenshotFeature) },
				};
			});
		}
		if (action == "cancel") {
			return RunOnMainThread([]() {
				constexpr auto access = ScreenshotCaptureSessionPolicy::ResolveAccess(
					ScreenshotCaptureSessionPolicy::CaptureSurface::BoundedProductionSession,
					false);
				if (!access.allowed)
					return json{ { "error", "automated capture control is unavailable" } };
				auto& feature = globals::features::screenshotFeature;
				if (!feature.CancelCaptureSession("cancelled by DevBench"))
					return json{ { "error", "no capture session is active" }, { "status", BuildStatus(feature) } };
				return json{ { "action", "cancel" }, { "status", BuildStatus(feature) } };
			});
		}
		if (action == "start") {
			const std::string sourceName = a_args.value("source", std::string("hmd_stereo"));
			const auto source = ParseSource(sourceName);
			if (!source)
				return { { "error", "source must be hmd_stereo, framed_stereo, or framed_eye" } };
			json validationError;
			const auto frameCount = ReadBoundedUnsigned(
				a_args,
				"frameCount",
				30,
				ScreenshotCaptureSessionPolicy::kMaxFrameCount,
				validationError);
			if (!frameCount)
				return validationError;
			const auto frameInterval = ReadBoundedUnsigned(
				a_args,
				"frameInterval",
				6,
				ScreenshotCaptureSessionPolicy::kMaxFrameInterval,
				validationError);
			if (!frameInterval)
				return validationError;
			const auto previewFramesPerSecond = ReadBoundedUnsigned(
				a_args,
				"previewFramesPerSecond",
				15,
				ScreenshotCaptureSessionPolicy::kMaxPreviewFramesPerSecond,
				validationError);
			if (!previewFramesPerSecond)
				return validationError;

			ScreenshotFeature::CaptureSessionRequest request;
			request.label = a_args.value("label", std::string("calibration"));
			request.source = *source;
			request.frameCount = *frameCount;
			request.frameInterval = *frameInterval;
			request.previewFramesPerSecond = *previewFramesPerSecond;
			request.saveCombined = a_args.value("saveCombined", true);
			request.saveSeparateEyes = a_args.value("saveSeparateEyes", sourceName != "framed_eye");
			request.writePreviewVideo = a_args.value("writePreviewVideo", true);
			request.outputPath = a_args.value("outputPath", std::string());

			return RunOnMainThread([request = std::move(request)]() {
				if (!globals::game::isVR)
					return json{ { "error", "submitted-eye capture sessions require Skyrim VR" } };
				constexpr auto access = ScreenshotCaptureSessionPolicy::ResolveAccess(
					ScreenshotCaptureSessionPolicy::CaptureSurface::BoundedProductionSession,
					false);
				if (!access.allowed)
					return json{ { "error", "automated capture control is unavailable" } };
				auto& feature = globals::features::screenshotFeature;
				std::string error;
				if (!feature.StartCaptureSession(request, error))
					return json{ { "error", error }, { "status", BuildStatus(feature) } };
				return json{ { "action", "start" }, { "status", BuildStatus(feature) } };
			});
		}

		return {
			{ "error", "unknown action" },
			{ "action", action },
			{ "supported", json::array({ "capabilities", "status", "start", "cancel" }) },
		};
	}

	void RunHandler(
		const char* a_argsJson,
		void* a_sink,
		DevBenchAPI::WriteFn a_write) noexcept
	{
		json output;
		try {
			json args = json::object();
			if (a_argsJson && *a_argsJson)
				args = json::parse(a_argsJson);
			if (!args.is_object())
				throw std::runtime_error("arguments must be a JSON object");
			output = BuildCaptureResult(args);
		} catch (const std::exception& e) {
			output = { { "error", "invalid request" }, { "detail", e.what() } };
		} catch (...) {
			output = { { "error", "unknown handler error" } };
		}

		try {
			const auto serialized = output.dump();
			a_write(a_sink, serialized.c_str());
		} catch (...) {
			a_write(a_sink, R"({"error":"response serialization failed"})");
		}
	}

	void CaptureToolHandler(
		void*,
		const char* a_argsJson,
		void* a_sink,
		DevBenchAPI::WriteFn a_write)
	{
		RunHandler(a_argsJson, a_sink, a_write);
	}

	void CaptureInspectExtensionHandler(
		void*,
		const char*,
		void* a_sink,
		DevBenchAPI::WriteFn a_write)
	{
		const json result{
			{ "registered", g_registered.load(std::memory_order_acquire) },
			{ "tool", "communityshaders.capture" },
			{ "usage", R"(Invoke communityshaders.capture directly, or dispatch it through a DevBench scenario tool step when dynamic tools are not exposed.)" },
			{ "actions", json::array({ "capabilities", "status", "start", "cancel" }) },
		};
		const auto serialized = result.dump();
		a_write(a_sink, serialized.c_str());
	}

	DevBenchAPI::IDevBenchInterface001* GetDevBenchToolExtensionInterface()
	{
		const auto messaging = SKSE::GetMessagingInterface();
		if (!messaging)
			return nullptr;
		DevBenchAPI::DevBenchMessage message;
		messaging->Dispatch(
			DevBenchAPI::DevBenchMessage::kMessage_GetInterface,
			&message,
			sizeof(DevBenchAPI::DevBenchMessage*),
			DevBenchAPI::DevBenchPluginName);
		if (!message.GetApiFunction)
			return nullptr;
		return static_cast<DevBenchAPI::IDevBenchInterface001*>(
			message.GetApiFunction(kDevBenchToolExtensionRevision));
	}
}

namespace ScreenshotDevBenchBridge
{
	void Install()
	{
		if (g_installAttempted.exchange(true, std::memory_order_acq_rel))
			return;
		auto* devBench = DevBenchAPI::GetDevBenchInterface001();
		if (!devBench) {
			logger::info("ScreenshotDevBenchBridge: devbench host not present; capture tool not registered");
			return;
		}

		static constexpr const char* descriptor =
			R"({"description":"Capture exact accepted OpenVR stereo pairs or bounded lossless frame sequences through CSX's production screenshot path. Automated start/cancel are log-level-independent and do not require Developer Mode; developer-only diagnostic texture capture is not exposed. Capture is intentionally separate from profiler measurement; run visual and timing passes independently. status reports every accepted compositor cycle, output path, failure, and dropped-pair/backpressure count.","inputSchema":{"type":"object","properties":{"action":{"type":"string","enum":["capabilities","status","start","cancel"],"default":"status"},"source":{"type":"string","enum":["hmd_stereo","framed_stereo","framed_eye"]},"label":{"type":"string"},"frameCount":{"type":"integer","minimum":1,"maximum":240},"frameInterval":{"type":"integer","minimum":1,"maximum":120},"previewFramesPerSecond":{"type":"integer","minimum":1,"maximum":60},"saveCombined":{"type":"boolean"},"saveSeparateEyes":{"type":"boolean"},"writePreviewVideo":{"type":"boolean"},"outputPath":{"type":"string"}},"required":["action"]}})";
		devBench->RegisterTool(
			"communityshaders.capture",
			descriptor,
			&CaptureToolHandler,
			nullptr);
		if (devBench->GetBuildNumber() >= 10500) {
			static constexpr const char* inspectDescriptor =
				R"({"description":"Reports the CSX screenshot and sequence capture tool registration."})";
			if (auto* extensionDevBench = GetDevBenchToolExtensionInterface()) {
				extensionDevBench->RegisterToolExtension(
					"inspect",
					"communityshaders.capture",
					inspectDescriptor,
					&CaptureInspectExtensionHandler,
					nullptr);
			}
		}
		g_registered.store(true, std::memory_order_release);
		logger::info(
			"ScreenshotDevBenchBridge: registered communityshaders.capture with devbench build {}",
			devBench->GetBuildNumber());
	}

	bool IsBuilt() { return true; }
	bool IsRegistered() { return g_registered.load(std::memory_order_acquire); }
}

#else

namespace ScreenshotDevBenchBridge
{
	void Install() {}
	bool IsBuilt() { return false; }
	bool IsRegistered() { return false; }
}

#endif
