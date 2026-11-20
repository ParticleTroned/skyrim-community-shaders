#include "FSRColorContractDevBenchBridge.h"

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "BuildProvenance.h"
#	include "Features/Upscaling.h"
#	include "Globals.h"

#	include <DevBenchAPI.h>
#	include <nlohmann/json.hpp>

#	include <atomic>
#	include <cstdint>
#	include <cmath>
#	include <exception>
#	include <stdexcept>
#	include <string>

namespace
{
	using json = nlohmann::json;
	std::atomic_bool installAttempted{ false };

	json DispatchJson(const FidelityFX::RuntimeUpscalerDispatchSnapshot& dispatch)
	{
		return {
			{ "valid", dispatch.valid },
			{ "frame", dispatch.frame },
			{ "path", static_cast<std::uint32_t>(dispatch.path) },
			{ "serial", dispatch.serial },
			{ "contextGeneration", dispatch.contextGeneration },
			{ "contextIndex", dispatch.contextIndex },
			{ "renderWidth", dispatch.renderWidth },
			{ "renderHeight", dispatch.renderHeight },
			{ "displayWidth", dispatch.displayWidth },
			{ "displayHeight", dispatch.displayHeight },
			{ "highDynamicRangeInput", dispatch.highDynamicRangeInput },
			{ "autoExposure", dispatch.autoExposure },
			{ "exposureResourceBound", dispatch.exposureResourceBound },
			{ "preExposure", dispatch.preExposure },

			{ "configuredSharpnessAtDispatch", dispatch.valid && std::isfinite(dispatch.configuredSharpness) ? json(dispatch.configuredSharpness) : json(nullptr) },
			{ "effectiveSharpness", dispatch.valid ? json(dispatch.effectiveSharpness) : json(nullptr) },
			{ "sharpeningEnabled", dispatch.valid ? json(dispatch.sharpeningEnabled) : json(nullptr) },
			{ "dispatchQpc", dispatch.valid && dispatch.dispatchQpc ? json(dispatch.dispatchQpc) : json(nullptr) },
			{ "submittedInputs", FSRDispatchInputTelemetry::ToJson(dispatch.submittedInputs, dispatch.valid) },
		};
	}

	json SnapshotJson(const FidelityFX::FsrColorContractStatusSnapshot& a_status)
	{
		const auto& contract = a_status.contract;
		const auto& dispatch = a_status.dispatch;
		return {
			{ "requested", {
							   { "revision", contract.revision },
							   { "highDynamicRangeInput", contract.highDynamicRangeInput },
							   { "autoExposure", contract.autoExposure },
						   } },
			{ "hostContext", {
								 { "valid", contract.hostContextValid },
								 { "highDynamicRangeInput", contract.hostContextHighDynamicRangeInput },
								 { "autoExposure", contract.hostContextAutoExposure },
								 { "generation", contract.hostContextGeneration },
							 } },
			{ "runtimeContext", {
									{ "valid", contract.runtimeContextValid },
									{ "highDynamicRangeInput", contract.runtimeContextHighDynamicRangeInput },
									{ "autoExposure", contract.runtimeContextAutoExposure },
									{ "generation", contract.runtimeContextGeneration },
								} },
			{ "lastSuccessfulDispatch", DispatchJson(dispatch) },
			{ "lastSuccessfulEyeDispatches", json::array({ DispatchJson(a_status.eyeDispatches[0]), DispatchJson(a_status.eyeDispatches[1]) }) },
			{ "sourceColorContractChanged", false },
		};
	}

	json SnapshotJson()
	{
		return SnapshotJson(
			Upscaling::fidelityFX.GetDevBenchFsrColorContractStatusSnapshot());
	}

	json BuildResult(const json& a_args)
	{
		for (const auto& [key, value] : a_args.items()) {
			if (key != "action" && key != "expectedRevision" &&
				key != "highDynamicRangeInput" && key != "autoExposure" &&
				key != "expectedBuildId") {
				return { { "error", "unknown parameter" }, { "parameter", key } };
			}
		}

		const auto action = a_args.value("action", std::string("status"));
		if (action != "status" && action != "set")
			return { { "error", "action must be status or set" } };
		if (action == "status") {
			if (a_args.contains("expectedRevision") ||
				a_args.contains("highDynamicRangeInput") ||
				a_args.contains("autoExposure")) {
				return { { "error", "status does not accept mutation parameters" } };
			}
			return SnapshotJson();
		}

		if (!a_args.contains("expectedRevision") ||
			!a_args.at("expectedRevision").is_number_unsigned()) {
			return { { "error", "set requires an unsigned expectedRevision" } };
		}
		if (!a_args.contains("highDynamicRangeInput") ||
			!a_args.at("highDynamicRangeInput").is_boolean() ||
			!a_args.contains("autoExposure") ||
			!a_args.at("autoExposure").is_boolean()) {
			return { { "error", "set requires boolean highDynamicRangeInput and autoExposure" } };
		}

		const auto setResult = Upscaling::fidelityFX.SetDevBenchFsrColorContract(
			a_args.at("expectedRevision").get<std::uint64_t>(),
			a_args.at("highDynamicRangeInput").get<bool>(),
			a_args.at("autoExposure").get<bool>());
		auto result = SnapshotJson(setResult.status);
		result["accepted"] = setResult.accepted;
		result["resultingRevision"] = setResult.resultingRevision;
		if (!setResult.accepted)
			result["error"] = "expectedRevision did not match the current request";
		return result;
	}

	void ToolHandler(void*, const char* a_argsJson, void* a_sink, DevBenchAPI::WriteFn a_write) noexcept
	{
		json output;
		try {
			const auto args = a_argsJson && *a_argsJson ? json::parse(a_argsJson) : json::object();
			if (!args.is_object())
				throw std::runtime_error("arguments must be an object");
			if (auto mismatch = BuildProvenance::ValidateExpectedBuild(args))
				output = std::move(*mismatch);
			else
				output = BuildResult(args);
		} catch (const std::exception& error) {
			output = { { "error", "invalid request" }, { "detail", error.what() } };
		} catch (...) {
			output = { { "error", "unknown FSR colour-contract handler error" } };
		}
		BuildProvenance::AttachProducer(output);
		try {
			const auto serialized = output.dump();
			a_write(a_sink, serialized.c_str());
		} catch (...) {
			a_write(a_sink, R"({"error":"response serialization failed"})");
		}
	}
}

void FSRColorContractDevBenchBridge::Install()
{
	if (installAttempted.exchange(true))
		return;
	auto* devBench = DevBenchAPI::GetDevBenchInterface001();
	if (!devBench)
		return;
	static const std::string descriptor = json{
		{ "description", "Inspect or set DevBench-only FSR processing flags without changing the compositor source-colour contract. The production default remains HDR-input plus auto-exposure. set uses expectedRevision compare-and-set, invalidates prior dispatch evidence, and causes host/runtime FSR contexts to be recreated at their existing render-thread safe points. status reports requested flags, effective context flags and generations, plus synchronized successful per-eye dispatch identity, dimensions, configured sharpness at dispatch, effective sharpening value/enabled state and QPC timing. submittedInputs schema 1 adds actual submitted reset, X/Y jitterOffsetPixels and frameTimeDeltaMilliseconds with explicit availability and nullable values. Last-success availability is not frame freshness or internal exposure/history convergence. Failed SDK calls invalidate affected diagnostic dispatch evidence. Invalid dispatch sharpness/timing evidence is null; no sharpness setter is added. This tool does not alter DLSS/DLAA, source transfer, provider selection, persistence, or resolution." },
		{ "inputSchema", {
							 { "type", "object" },
							 { "additionalProperties", false },
							 { "properties", {
												 { "action", { { "type", "string" }, { "enum", { "status", "set" } }, { "default", "status" }, { "description", "Status retains last-success inputs under submittedInputs schema 1; qualify frame/serial/context freshness independently. Unavailable inputs are null; this is not vendor convergence telemetry." } } },
												 { "expectedRevision", { { "type", "integer" }, { "minimum", 0 } } },
												 { "highDynamicRangeInput", { { "type", "boolean" } } },
												 { "autoExposure", { { "type", "boolean" } } },
												 { "expectedBuildId", { { "type", "string" } } },
											 } },
						 } },
	}
	                                          .dump();
	devBench->RegisterTool(
		"communityshaders.fsr_color_contract",
		descriptor.c_str(),
		&ToolHandler,
		nullptr);
	logger::info("FSRColorContractDevBenchBridge: registered processing controls");
}

#else

void FSRColorContractDevBenchBridge::Install() {}

#endif
