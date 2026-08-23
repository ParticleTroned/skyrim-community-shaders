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
		static CSX::Api::ServiceFoundation foundation({ kToolName, 1, 3, 4 });
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

	const char* ControlModeName(CSX::VRDepthCullingDiagnostics::ControlMode a_mode)
	{
		return a_mode == CSX::VRDepthCullingDiagnostics::ControlMode::ForcedVisible ?
			"forced_visible" :
			"live";
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
		const auto occlusionRatio = a_snapshot.visibilityObjectsSampled ?
			static_cast<double>(a_snapshot.occludedObjects) / static_cast<double>(a_snapshot.visibilityObjectsSampled) :
			0.0;
		const auto classifiedDrawCount = a_snapshot.occludedDraws + a_snapshot.visibleDraws;
		const auto occludedDrawRatio = classifiedDrawCount ?
			static_cast<double>(a_snapshot.occludedDraws) / static_cast<double>(classifiedDrawCount) :
			0.0;
		const auto pipelineMean = [&](std::uint64_t a_total) {
			return a_snapshot.pipelineStatsSamples ?
				static_cast<double>(a_total) / static_cast<double>(a_snapshot.pipelineStatsSamples) :
				0.0;
		};
		const auto pipelineRegionMeanMilliseconds = a_snapshot.pipelineTimingSamples ?
			static_cast<double>(a_snapshot.pipelineRegionNanoseconds) /
			static_cast<double>(a_snapshot.pipelineTimingSamples) / 1'000'000.0 :
			0.0;

		return {
			{ "collecting", a_snapshot.collecting },
			{ "collectionEpoch", a_snapshot.collectionEpoch },
			{ "controlMode", ControlModeName(a_snapshot.controlMode) },
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
				{ "boundByFamily", {
					{ "lighting", a_snapshot.boundLightingDraws },
					{ "distantTree", a_snapshot.boundDistantTreeDraws },
					{ "grass", a_snapshot.boundGrassDraws },
				} },
			} },
			{ "visibilityReadback", {
				{ "copiesQueued", a_snapshot.readbackCopiesQueued },
				{ "copiesCompleted", a_snapshot.readbackCopiesCompleted },
				{ "copiesDropped", a_snapshot.readbackCopiesDropped },
				{ "mapsNotReady", a_snapshot.readbackMapsNotReady },
				{ "errors", a_snapshot.readbackErrors },
				{ "sampledFrames", a_snapshot.visibilityFramesSampled },
				{ "lastSampleFrame", FrameValue(a_snapshot.lastVisibilityFrame) },
				{ "lastSampleObjectCount", a_snapshot.lastVisibilityObjectCount },
				{ "lastSampleOccludedObjects", a_snapshot.lastOccludedObjects },
				{ "objectsSampled", a_snapshot.visibilityObjectsSampled },
				{ "occludedObjects", a_snapshot.occludedObjects },
				{ "visibleObjects", a_snapshot.visibleObjects },
				{ "occlusionRatio", occlusionRatio },
				{ "occludedObjectsWithCoveredDraws", a_snapshot.occludedObjectsWithCoveredDraws },
				{ "visibleObjectsWithCoveredDraws", a_snapshot.visibleObjectsWithCoveredDraws },
			} },
			{ "classifiedDraws", {
				{ "occluded", a_snapshot.occludedDraws },
				{ "visible", a_snapshot.visibleDraws },
				{ "occludedRatio", occludedDrawRatio },
				{ "lighting", {
					{ "occluded", a_snapshot.occludedLightingDraws },
					{ "visible", a_snapshot.visibleLightingDraws },
				} },
				{ "distantTree", {
					{ "occluded", a_snapshot.occludedDistantTreeDraws },
					{ "visible", a_snapshot.visibleDistantTreeDraws },
				} },
				{ "grass", {
					{ "occluded", a_snapshot.occludedGrassDraws },
					{ "visible", a_snapshot.visibleGrassDraws },
				} },
			} },
			{ "pipelineStatistics", {
				{ "queriesBegun", a_snapshot.pipelineQueriesBegun },
				{ "queriesEnded", a_snapshot.pipelineQueriesEnded },
				{ "queriesCompleted", a_snapshot.pipelineQueriesCompleted },
				{ "queriesDropped", a_snapshot.pipelineQueriesDropped },
				{ "queriesNotReady", a_snapshot.pipelineQueriesNotReady },
				{ "errors", a_snapshot.pipelineQueryErrors },
				{ "samples", a_snapshot.pipelineStatsSamples },
				{ "gpuRegionTiming", {
					{ "samples", a_snapshot.pipelineTimingSamples },
					{ "disjoint", a_snapshot.pipelineTimestampDisjoint },
					{ "totalNanoseconds", a_snapshot.pipelineRegionNanoseconds },
					{ "meanMilliseconds", pipelineRegionMeanMilliseconds },
				} },
				{ "totals", {
					{ "iaVertices", a_snapshot.pipelineIAVertices },
					{ "iaPrimitives", a_snapshot.pipelineIAPrimitives },
					{ "vsInvocations", a_snapshot.pipelineVSInvocations },
					{ "gsInvocations", a_snapshot.pipelineGSInvocations },
					{ "gsPrimitives", a_snapshot.pipelineGSPrimitives },
					{ "clipperInvocations", a_snapshot.pipelineClipperInvocations },
					{ "clipperPrimitives", a_snapshot.pipelineClipperPrimitives },
					{ "psInvocations", a_snapshot.pipelinePSInvocations },
					{ "hsInvocations", a_snapshot.pipelineHSInvocations },
					{ "dsInvocations", a_snapshot.pipelineDSInvocations },
					{ "csInvocations", a_snapshot.pipelineCSInvocations },
				} },
				{ "meanPerSample", {
					{ "iaVertices", pipelineMean(a_snapshot.pipelineIAVertices) },
					{ "iaPrimitives", pipelineMean(a_snapshot.pipelineIAPrimitives) },
					{ "vsInvocations", pipelineMean(a_snapshot.pipelineVSInvocations) },
					{ "gsInvocations", pipelineMean(a_snapshot.pipelineGSInvocations) },
					{ "gsPrimitives", pipelineMean(a_snapshot.pipelineGSPrimitives) },
					{ "clipperInvocations", pipelineMean(a_snapshot.pipelineClipperInvocations) },
					{ "clipperPrimitives", pipelineMean(a_snapshot.pipelineClipperPrimitives) },
					{ "psInvocations", pipelineMean(a_snapshot.pipelinePSInvocations) },
					{ "hsInvocations", pipelineMean(a_snapshot.pipelineHSInvocations) },
					{ "dsInvocations", pipelineMean(a_snapshot.pipelineDSInvocations) },
					{ "csInvocations", pipelineMean(a_snapshot.pipelineCSInvocations) },
				} },
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
				{ "forcedVisibleSrvUnavailable", a_snapshot.forcedVisibleSrvUnavailable },
			} },
		};
	}

	json BuildResult(const json& a_args)
	{
		const auto action = a_args.value("action", std::string{});
		if (action != "registry" && action != "snapshot" && action != "start" && action != "stop" && action != "reset" && action != "configure")
			return Foundation().MakeError(a_args, "unknown_action", "action is not supported", "validation", false, "action");

		if (action == "registry") {
			auto response = Foundation().MakeEnvelope(a_args, true);
			response["result"] = {
				{ "service", kToolName },
				{ "major", 1 },
				{ "minor", 3 },
				{ "schemaRevision", 4 },
				{ "mainThreadAffine", false },
				{ "actions", json::array({ "registry", "snapshot", "start", "stop", "reset", "configure" }) },
				{ "mutations", json::array({ "start", "stop", "reset", "configure" }) },
				{ "gpuVisibilityReadback", true },
				{ "gpuPipelineStatistics", true },
				{ "gpuTimestampQueries", true },
				{ "controlModes", json::array({ "live", "forced_visible" }) },
				{ "notes", json::array({
					"Collection is disabled by default and absent from non-DevBench builds.",
					"GPU results are copied to a staging ring and mapped with DO_NOT_WAIT; diagnostics never wait for the GPU.",
					"Pipeline statistics and timestamp queries cover the post-occlusion GPU region through the following frame's early prepass and are read with D3D11_ASYNC_GETDATA_DONOTFLUSH.",
					"forced_visible preserves the OBB pass, binding, lookup, branch, and draw submissions but binds an all-visible result buffer to suppress only the shader early exit.",
				}) },
			};
			return response;
		}

		auto& diagnostics = globals::features::vr.GetDepthCullingDiagnostics();
		if (action == "configure") {
			const auto mode = a_args.value("mode", std::string{});
			if (mode == "live")
				diagnostics.SetControlMode(CSX::VRDepthCullingDiagnostics::ControlMode::Live);
			else if (mode == "forced_visible")
				diagnostics.SetControlMode(CSX::VRDepthCullingDiagnostics::ControlMode::ForcedVisible);
			else
				return Foundation().MakeError(a_args, "invalid_mode", "mode must be live or forced_visible", "validation", false, "mode");
		} else if (action == "start")
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
			"description":"Bounded, opt-in current-frame depth-culling visibility telemetry and controlled GPU-work comparison. GPU readback never stalls.",
			"inputSchema":{
				"type":"object",
				"required":["contractMajor","clientId","commandId","action"],
				"properties":{
					"contractMajor":{"type":"integer","const":1},
					"clientId":{"type":"string","minLength":1,"maxLength":128},
					"commandId":{"type":"string","minLength":1,"maxLength":128},
					"expectedBuildId":{"type":"string"},
					"action":{"type":"string","enum":["registry","snapshot","start","stop","reset","configure"]},
					"mode":{"type":"string","enum":["live","forced_visible"]}
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
