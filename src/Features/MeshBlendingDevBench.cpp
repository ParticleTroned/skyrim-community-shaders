#include "MeshBlending.h"

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "Api/MeshBlendingDevBenchPolicy.h"
#	include "Globals.h"
#	include "State.h"

json MeshBlending::ControlDevBench(const json& a_request)
{
	if (!a_request.is_object()) {
		throw std::invalid_argument("meshBlending must be an object");
	}
	for (const auto& [name, value] : a_request.items()) {
		if (name != "settings" && name != "discoveryCapture" && name != "resetDiagnostics") {
			throw std::invalid_argument("Unknown meshBlending argument: " + name);
		}
		if (name != "settings" && !value.is_boolean()) {
			throw std::invalid_argument("meshBlending." + name + " must be boolean");
		}
	}

	json currentSettings;
	SaveSettings(currentSettings);
	const auto replacement = a_request.contains("settings") ?
	                             CSX::Api::MeshBlendingDevBench::ApplySettingsPatch(currentSettings, a_request.at("settings")) :
	                             currentSettings;
	const bool mutation = replacement != currentSettings || a_request.contains("discoveryCapture") ||
	                      a_request.value("resetDiagnostics", false);
	if (mutation && !loaded) {
		return { { "error", "Mesh Blending is not loaded" }, { "errorCode", "feature_unavailable" } };
	}
	if (mutation && (!globals::state || globals::state->IsPersistentMutationBlocked())) {
		return { { "error", "Mesh Blending controls are unavailable while the game is saving or loading" }, { "errorCode", "runtime_unavailable" } };
	}
	if (replacement != currentSettings) {
		auto settingsToLoad = replacement;
		LoadSettings(settingsToLoad);
		SaveSettings(currentSettings);
	}
	if (a_request.contains("discoveryCapture")) {
		SetDiscoveryCaptureEnabled(a_request.at("discoveryCapture").get<bool>());
	}
	if (a_request.value("resetDiagnostics", false)) {
		diagnostics = {};
	}

	return {
		{ "available", loaded },
		{ "runtimeEnabled", IsRuntimeEnabled() },
		{ "settings", std::move(currentSettings) },
		{ "persisted", false },
		{ "ruleFileWritable", ruleFileMutationAllowed.load(std::memory_order_acquire) },
		{ "ruleFileError", ruleFileLoadError },
		{ "discoveryCapture", IsDiscoveryCaptureActive() },
		{ "diagnosticsEnabled", globals::state && globals::state->IsDeveloperMode() },
		{ "diagnostics", { { "frame", diagnostics.currentFrame },
							 { "drawCalls", diagnostics.drawCalls },
							 { "activeDraws", diagnostics.activeDraws },
							 { "sourceRejects", diagnostics.sourceRejects },
							 { "scopeRejects", diagnostics.scopeRejects },
							 { "distanceRejects", diagnostics.distanceRejects },
							 { "cacheHits", diagnostics.cacheHits },
							 { "cacheMisses", diagnostics.cacheMisses },
							 { "cacheEvictions", diagnostics.cacheEvictions },
							 { "rootTraversals", diagnostics.rootTraversals },
							 { "nodesVisited", diagnostics.nodesVisited },
							 { "traversalLimitRejects", diagnostics.traversalLimitRejects },
							 { "landscapeRegistryHits", diagnostics.landscapeRegistryHits },
							 { "landscapeRegistryMisses", diagnostics.landscapeRegistryMisses } } }
	};
}

#endif
