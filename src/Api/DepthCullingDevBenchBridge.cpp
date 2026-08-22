#include "Api/DepthCullingDevBenchBridge.h"

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "Api/ServiceFoundation.h"
#	include "BuildProvenance.h"
#	include "Features/VR.h"
#	include "Globals.h"

#	include <DevBenchAPI.h>
#	include <nlohmann/json.hpp>

#	include <atomic>
#	include <cstdint>
#	include <limits>
#	include <mutex>
#	include <string>

namespace
{
	using json = nlohmann::json;
	using Snapshot = CSX::VRDepthCullingDiagnostics::Snapshot;
	constexpr auto kToolName = "communityshaders.depth_culling_diagnostics";
	std::atomic_bool g_registered{ false };

	CSX::Api::ServiceFoundation& Foundation()
	{
		static CSX::Api::ServiceFoundation foundation({ kToolName, 1, 0, 1 });
		static std::once_flag metadataInitialized;
		std::call_once(metadataInitialized, [&] {
			foundation.SetServerMetadataProvider([] {
				auto producer = BuildProvenance::GetProducer();
				producer["serviceSessionId"] = Foundation().SessionId();
				return producer;
			});
		});
		return foundation;
	}

	json FrameValue(std::uint32_t a_frame)
	{
		return a_frame == CSX::VRDepthCullingDiagnostics::kNoFrame ? json(nullptr) : json(a_frame);
	}

	json SnapshotJson(const Snapshot& a_snapshot)
	{
		const auto failOpenDraws =
			a_snapshot.contextUnavailable + a_snapshot.featureDisabled + a_snapshot.stateUnavailable +
			a_snapshot.notInWorld + a_snapshot.reflections + a_snapshot.resultNotReady +
			a_snapshot.geometryUnavailable + a_snapshot.objectFrameMismatch +
			a_snapshot.objectIndexUnavailable + a_snapshot.objectIndexOutOfRange +
			a_snapshot.resultBufferUnavailable + a_snapshot.srvUnavailable;
		const auto accountedBindAttempts = a_snapshot.boundDraws + failOpenDraws;
		const auto unaccountedBindAttempts = a_snapshot.bindAttempts > accountedBindAttempts ?
			a_snapshot.bindAttempts - accountedBindAttempts : 0;

		return {
			{ "collecting", a_snapshot.collecting },
			{ "frames", {
				{ "first", FrameValue(a_snapshot.firstFrame) },
				{ "last", FrameValue(a_snapshot.lastFrame) },
				{ "lastReady", FrameValue(a_snapshot.lastReadyFrame) },
				{ "lastEngineDepthCulling", FrameValue(a_snapshot.lastDepthCullingFrame) },
				{ "observed", a_snapshot.framesObserved },
				{ "featureEnabled", a_snapshot.enabledFrames },
			} },
			{ "scene", {
				{ "lastObjectCount", a_snapshot.lastObjectCount },
				{ "accumulationCalls", a_snapshot.accumulationCalls },
				{ "previousResultsNeutralized", a_snapshot.previousResultsNeutralized },
				{ "readyPassCalls", a_snapshot.readyPassCalls },
				{ "readyFrames", a_snapshot.readyFrames },
			} },
			{ "draws", {
				{ "bindAttempts", a_snapshot.bindAttempts },
				{ "boundToCurrentFrameVisibility", a_snapshot.boundDraws },
				{ "currentFrameBound", a_snapshot.currentFrameBoundDraws },
				{ "lastCompletedFrameBound", a_snapshot.lastCompletedFrameBoundDraws },
				{ "failOpen", failOpenDraws },
				{ "accounted", accountedBindAttempts },
				{ "unaccounted", unaccountedBindAttempts },
			} },
			{ "failOpenReasons", {
				{ "contextUnavailable", a_snapshot.contextUnavailable },
				{ "featureDisabled", a_snapshot.featureDisabled },
				{ "stateUnavailable", a_snapshot.stateUnavailable },
				{ "notInWorld", a_snapshot.notInWorld },
				{ "reflections", a_snapshot.reflections },
				{ "resultNotReady", a_snapshot.resultNotReady },
				{ "geometryUnavailable", a_snapshot.geometryUnavailable },
				{ "objectFrameMismatch", a_snapshot.objectFrameMismatch },
				{ "objectIndexUnavailable", a_snapshot.objectIndexUnavailable },
				{ "objectIndexOutOfRange", a_snapshot.objectIndexOutOfRange },
				{ "resultBufferUnavailable", a_snapshot.resultBufferUnavailable },
				{ "srvUnavailable", a_snapshot.srvUnavailable },
			} },
		};
	}

	json BuildResult(const json& a_args)
	{
		const auto action = a_args.value("action", std::string{});
		if (action != "registry" && action != "snapshot" && action != "start" && action != "stop" && action != "reset")
			return Foundation().MakeError(a_args, "unknown_action", "action is not supported", "validation", false, "action");

		if (action == "registry") {
			auto response = Foundation().MakeEnvelope(a_args, true);
			response["result"] = {
				{ "service", kToolName },
				{ "major", 1 },
				{ "minor", 0 },
				{ "schemaRevision", 1 },
				{ "mainThreadAffine", false },
				{ "actions", json::array({ "registry", "snapshot", "start", "stop", "reset" }) },
				{ "mutations", json::array({ "start", "stop", "reset" }) },
				{ "gpuVisibilityReadback", false },
				{ "notes", json::array({
					"Collection is disabled by default and absent from non-DevBench builds.",
					"boundToCurrentFrameVisibility proves SRV/object-index binding, not the number of GPU-rejected objects.",
				}) },
			};
			return response;
		}

		auto& diagnostics = globals::features::vr.GetDepthCullingDiagnostics();
		if (action == "start")
			diagnostics.Start();
		else if (action == "stop")
			diagnostics.Stop();
		else if (action == "reset")
			diagnostics.Reset();

		auto response = Foundation().MakeEnvelope(a_args, true);
		response["result"] = {
			{ "action", action },
			{ "snapshot", SnapshotJson(diagnostics.Capture()) },
		};
		return response;
	}

	void ToolHandler(void*, const char* a_argsJson, void* a_sink, DevBenchAPI::WriteFn a_write) noexcept
	{
		json output;
		try {
			const auto args = a_argsJson && *a_argsJson ? json::parse(a_argsJson) : json::object();
			if (auto mismatch = BuildProvenance::ValidateExpectedBuild(args)) {
				output = Foundation().MakeError(
					args,
					mismatch->value("code", std::string("producer_mismatch")),
					mismatch->value("error", std::string("loaded CSX build does not match the request")),
					"validation",
					false,
					"expectedBuildId");
			} else {
				output = Foundation().Dispatch(args, &BuildResult);
			}
		} catch (const std::exception& e) {
			output = Foundation().MakeError(json::object(), "invalid_request", e.what());
		} catch (...) {
			output = Foundation().MakeError(json::object(), "internal_error", "unknown depth-culling diagnostics error", "dispatch", true);
		}

		try {
			const auto serialized = output.dump();
			a_write(a_sink, serialized.c_str());
		} catch (...) {
			a_write(a_sink, R"({"ok":false,"error":{"code":"serialization_failed"}})");
		}
	}
}

namespace CSX::Api::DepthCullingDevBenchBridge
{
	void Install()
	{
		if (g_registered.load(std::memory_order_acquire))
			return;
		auto* devBench = DevBenchAPI::GetDevBenchInterface001();
		if (!devBench) {
			logger::info("DepthCullingDevBenchBridge: devbench host not present; diagnostics tool not registered");
			return;
		}

		const char* descriptor = R"({
			"description":"Bounded, opt-in current-frame depth-culling hand-off telemetry. No GPU readback or visibility-buffer stall.",
			"inputSchema":{
				"type":"object",
				"required":["contractMajor","clientId","commandId","action"],
				"properties":{
					"contractMajor":{"type":"integer","const":1},
					"clientId":{"type":"string","minLength":1,"maxLength":128},
					"commandId":{"type":"string","minLength":1,"maxLength":128},
					"expectedBuildId":{"type":"string"},
					"action":{"type":"string","enum":["registry","snapshot","start","stop","reset"]}
				}
			}
		})";
		devBench->RegisterTool(kToolName, descriptor, &ToolHandler, nullptr);
		g_registered.store(true, std::memory_order_release);
		logger::info("DepthCullingDevBenchBridge: registered {} with devbench build {}", kToolName, devBench->GetBuildNumber());
	}

	bool IsRegistered()
	{
		return g_registered.load(std::memory_order_acquire);
	}
}

#else

namespace CSX::Api::DepthCullingDevBenchBridge
{
	void Install() {}
	bool IsRegistered() { return false; }
}

#endif
