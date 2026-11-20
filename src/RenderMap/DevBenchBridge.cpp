#include "RenderMap/DevBenchBridge.h"

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "Api/ServiceFoundation.h"
#	include "BuildProvenance.h"
#	include "Globals.h"
#	include "RenderMap/Artifacts.h"
#	include "RenderMap/CaptureStart.h"
#	include "RenderMap/CapturePublication.h"
#	include "RenderMap/Controller.h"
#	include "RenderMap/DevBenchCaptureBounds.h"
#	include "RenderMap/Serialization.h"
#	include "RenderMap/ShaderBytecodeCatalogue.h"
#	include "RenderMap/PayloadSchemaNames.h"
#	include "ShaderCache.h"

#	include <DevBenchAPI.h>
#	include <nlohmann/json.hpp>

#	include <algorithm>
#	include <atomic>
#	include <chrono>
#	include <cstdint>
#	include <limits>
#	include <iterator>
#	include <mutex>
#	include <optional>
#	include <stdexcept>
#	include <string>
#	include <unordered_map>
#	include <unordered_set>

namespace
{
	using json = nlohmann::json;
	using CSX::RenderMap::ControlStatus;
	json BuildShaderMetadataStatus()
	{
		return {
			{ "maximumBytecodeIdentities", CSX::RenderMap::kMaximumPersistentStageShaders },
			{ "maximumStageIdentities", CSX::RenderMap::kMaximumPersistentStageShaders },
			{ "maximumAliasesPerStage", CSX::RenderMap::kMaximumEngineShaderAliasesPerStage },
			{ "maximumRetainedDumpBytes", CSX::RenderMap::kMaximumRetainedShaderDumpBytes },
			{ "includedInCaptureMaxBytes", false },
			{ "failureCount", CSX::RenderMap::ShaderMetadataFailureCount() },
		};
	}

	constexpr std::uint32_t kContractMajor = 1;
	constexpr std::uint32_t kContractMinor = 21;
	constexpr std::uint32_t kSchemaRevision = 23;
	using namespace CSX::RenderMap::DevBenchBounds;
	constexpr auto kPlannedEventKinds =
		CSX::RenderMap::EventKindBit(CSX::RenderMap::EventKind::kFrameBegin) |
		CSX::RenderMap::EventKindBit(CSX::RenderMap::EventKind::kFrameEnd) |
		CSX::RenderMap::EventKindBit(CSX::RenderMap::EventKind::kSceneAccumulationBegin) |
		CSX::RenderMap::EventKindBit(CSX::RenderMap::EventKind::kSceneAccumulationEnd) |
		CSX::RenderMap::EventKindBit(CSX::RenderMap::EventKind::kEyeBegin) |
		CSX::RenderMap::EventKindBit(CSX::RenderMap::EventKind::kEyeEnd) |
		CSX::RenderMap::EventKindBit(CSX::RenderMap::EventKind::kRenderPassCreated) |
		CSX::RenderMap::EventKindBit(CSX::RenderMap::EventKind::kPipelineObjectCreated) |
		CSX::RenderMap::EventKindBit(CSX::RenderMap::EventKind::kPipelineBind) |
		CSX::RenderMap::EventKindBit(CSX::RenderMap::EventKind::kDepthSourceReady);
	constexpr auto kSelectableEventKinds = CSX::RenderMap::kAllEventKindsMask &
	                                       ~(CSX::RenderMap::EventKindBit(CSX::RenderMap::EventKind::kCaptureMarker) |
											   CSX::RenderMap::EventKindBit(CSX::RenderMap::EventKind::kGap) |
											   kPlannedEventKinds);
	std::atomic_bool g_registered{ false };
	std::mutex g_artifactMutex;
	std::unordered_map<std::string, CSX::RenderMap::CaptureArtifactContext> g_artifactContexts;
	std::unordered_map<std::string, CSX::RenderMap::PreparedCapturePublication> g_artifactBundles;

	CSX::Api::ServiceFoundation& Foundation()
	{
		static CSX::Api::ServiceFoundation foundation(
			{ "communityshaders.render-map", kContractMajor, kContractMinor, kSchemaRevision });
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

	json UnavailableInput()
	{
		return {
			{ "availability", "unavailable" },
			{ "path", nullptr },
			{ "sha256", nullptr },
			{ "schemaMajor", nullptr },
		};
	}

	json BuildShaderCompilationProvenance()
	{
		if (!globals::shaderCache || !globals::state) {
			return {
				{ "availability", "unavailable" },
				{ "evidenceClass", "unavailable" },
				{ "capturedAt", "capture-start" },
			};
		}

		const auto compile = globals::shaderCache->GetCompileContextSnapshot();
		return {
			{ "availability", "observed" },
			{ "evidenceClass", "runtime-observed" },
			{ "capturedAt", "capture-start" },
			{ "shaderCacheAbiId", compile.shaderCacheAbiId },
			{ "shaderCompilerIdentity", compile.shaderCompilerIdentity },
			{ "compileMode", compile.developerMode ? "debug" : "optimized" },
			{ "virtualReality", compile.virtualReality },
			{ "compilerFlags", {
								   { "partialPrecision", compile.partialPrecision },
								   { "avoidFlowControl", compile.avoidFlowControl },
							   } },
			{ "globalDefines", {
								   { "canonicalText", compile.shaderDefinesCanonical },
								   { "cachePathSuffix", compile.shaderDefinesSuffix },
							   } },
			{ "globalCompileState", {
										{ "algorithm", "xxh3-128" },
										{ "digest", compile.globalCompileStateDigest },
										{ "identityBasis", json::array({
															   "compile-mode",
															   "virtual-reality",
															   "partial-precision",
															   "avoid-flow-control",
															   "shader-cache-abi",
															   "global-defines",
														   }) },
									} },
			{ "compatibilityRegistry", {
										   { "phase", "unknown" },
										   { "revision", 1 },
										   { "registrationCount", 0 },
										   { "compatibilitySetDigest", nullptr },
										   { "complete", false },
										   { "registrations", json::array() },
									   } },
			{ "qualification",
				"globalCompileState is observed; current main-VR does not expose a shader-specific "
				"compatibility registry, so compatibilityRegistry is explicitly incomplete" },
		};
	}

	CSX::RenderMap::CaptureArtifactContext BuildArtifactContext()
	{
		const auto build = BuildProvenance::GetProducer();
		const auto sourceCommit = build.value("sourceCommit", std::string{});
		auto logDirectory = logger::log_directory();
		auto shaderCompilation = BuildShaderCompilationProvenance();
		const auto shaderVR = shaderCompilation.value("availability", std::string{}) == "observed" ?
		                          std::optional<bool>{ shaderCompilation.at("virtualReality").get<bool>() } :
		                          std::nullopt;
		auto skyrimIdentity = CSX::RenderMap::BuildSkyrimModuleIdentity(
			REL::Module::IsVR(), REL::Module::get().version().string("."), shaderVR);
		return {
			.outputRoot = logDirectory ? *logDirectory / "CSX" / "RenderMapCaptures" : std::filesystem::path{},
			.createdAtUtc = CSX::Api::ServiceFoundation::TimestampUtc(),
			.producer = {
				{ "name", "CommunityShaders" },
				{ "version", build.value("buildIdShort", std::string("unavailable")) },
				{ "gitCommit", sourceCommit.size() == 40 ? json(sourceCommit) : json(nullptr) },
				{ "dirty", build.value("sourceDirty", false) },
			},
			.capabilities = {
				"thread-local-render-scopes",
				"bounded-in-memory-capture",
				"typed-shader-observations",
				"resolved-technique-stage-observations",
				"typed-output-merger-target-observations",
				"typed-resource-and-view-observations",
				"ordered-resource-bindings",
				"resource-flow-observations",
				"resource-mutation-observations",
				"resource-cpu-access-observations",
				"atomic-events-jsonl",
				"atomic-capture-manifest",
				"explicit-gap-events",
				"capture-start-shader-compilation-provenance",
			},
			.inputs = {
				{ "shaderManifest", UnavailableInput() },
				{ "engineMap", UnavailableInput() },
				{ "csxBuildManifest", UnavailableInput() },
			},
			.environment = {
				{ "skyrim", std::move(skyrimIdentity) },
				{ "csx", { { "name", "CommunityShaders.dll" }, { "version", build.value("buildIdShort", std::string("unavailable")) }, { "sha256", nullptr } } },
				{ "runtimeRoute", "unknown" },
				{ "modEnvironment", {
										{ "manager", "other" },
										{ "instance", nullptr },
										{ "profile", nullptr },
										{ "modlistSha256", nullptr },
										{ "pluginLoadOrderSha256", nullptr },
									} },
				{ "graphics", {
								  { "gpu", nullptr },
								  { "driver", nullptr },
								  { "renderWidth", nullptr },
								  { "renderHeight", nullptr },
								  { "presetSha256", nullptr },
								  { "settingsSha256", nullptr },
							  } },
				{ "shaderCache", { { "identity", "unavailable" }, { "inventorySha256", nullptr }, { "coldAtStart", false } } },
			},
			.scenario = {
				{ "id", "unspecified" },
				{ "saveFingerprint", nullptr },
				{ "cell", nullptr },
				{ "worldspace", nullptr },
				{ "weather", nullptr },
				{ "gameHour", nullptr },
				{ "cameraMarker", nullptr },
				{ "notes", "No scenario metadata was supplied to the v1.0 live controller." },
			},
			.extensions = {
				{ "csx.shaderCompilation", std::move(shaderCompilation) },
			},
		};
	}

	void PruneArtifactState(std::string_view a_protectedCaptureId = {})
	{
		const auto status = CSX::RenderMap::GetCaptureController().GetStatus();
		std::unordered_set<std::string> retained(
			status.completedCaptureIds.begin(), status.completedCaptureIds.end());
		if (status.active)
			retained.insert(status.active->captureId);
		for (auto it = g_artifactContexts.begin(); it != g_artifactContexts.end();) {
			it = (retained.contains(it->first) || it->first == a_protectedCaptureId) ? std::next(it) : g_artifactContexts.erase(it);
		}
		for (auto it = g_artifactBundles.begin(); it != g_artifactBundles.end();) {
			it = (retained.contains(it->first) || it->first == a_protectedCaptureId) ? std::next(it) : g_artifactBundles.erase(it);
		}
	}

	json ControlFailure(const json& a_args, ControlStatus a_status)
	{
		switch (a_status) {
		case ControlStatus::kBusy:
			return Foundation().MakeError(a_args, "capture_busy", "a render-map capture is already active", "execution", true);
		case ControlStatus::kInvalidBounds:
			return Foundation().MakeError(a_args, "invalid_bounds", "capture bounds are invalid", "validation", false);
		case ControlStatus::kAllocationFailed:
			return Foundation().MakeError(a_args, "allocation_failed", "the bounded capture buffer could not be allocated", "execution", true);
		case ControlStatus::kNotCapturing:
			return Foundation().MakeError(a_args, "not_capturing", "no matching capture is active", "execution", false);
		case ControlStatus::kCaptureNotFound:
			return Foundation().MakeError(a_args, "capture_not_found", "the capture ID is not active or retained", "validation", false, "captureId");
		case ControlStatus::kDraining:
			return Foundation().MakeError(a_args, "capture_draining", "capture hooks are draining; retry stop with the same captureId", "execution", true, "captureId");
		default:
			return Foundation().MakeError(a_args, "internal_error", "unexpected render-map controller status", "execution", true);
		}
	}

	bool HasUnsigned(const json& a_args, std::string_view a_name)
	{
		const auto found = a_args.find(a_name);
		return found != a_args.end() && found->is_number_unsigned();
	}

	json InvalidBound(
		const json& a_args,
		std::string_view a_field,
		std::uint64_t a_value,
		std::uint64_t a_minimum,
		std::uint64_t a_maximum)
	{
		auto error = Foundation().MakeError(
			a_args, "invalid_bounds", "capture bound is outside the advertised range",
			"validation", false, a_field);
		error["error"]["details"] = {
			{ "value", a_value },
			{ "minimum", a_minimum },
			{ "maximum", a_maximum },
		};
		return error;
	}

	json BuildResult(const json& a_args)
	{
		const auto action = a_args.value("action", std::string{});
		const bool known = action == "registry" || action == "status" || action == "start" ||
		                   action == "stop" || action == "capture_events";
		if (!known)
			return Foundation().MakeError(a_args, "unknown_action", "action is not supported", "validation", false, "action");

		if (action == "registry") {
			const auto defaultConfig = DefaultConfig();
			auto defaultCatalogueConfig = defaultConfig;
			defaultCatalogueConfig.maxEvents = 0;
			const auto fixedCatalogueBytes = CSX::RenderMap::Collector::RequiredStorageBytes(defaultCatalogueConfig);
			defaultCatalogueConfig.maxEvents = 1;
			const auto minimumMaxBytes = CSX::RenderMap::Collector::RequiredStorageBytes(defaultCatalogueConfig);
			auto response = Foundation().MakeEnvelope(a_args, true);
			response["result"] = {
				{ "service", "communityshaders.render-map" },
				{ "major", kContractMajor },
				{ "minor", kContractMinor },
				{ "schemaRevision", kSchemaRevision },
				{ "actions", json::array({ "registry", "status", "start", "stop", "capture_events" }) },
				{ "eventSchemas", json(CSX::RenderMap::PayloadSchemaNames::kAll) },
				{ "shaderMetadata", BuildShaderMetadataStatus() },
				{ "eventKinds", CSX::RenderMap::SerializeEventKindMask(kSelectableEventKinds) },
				{ "plannedEventKinds", CSX::RenderMap::SerializeEventKindMask(kPlannedEventKinds) },
				{ "eventSelection", {
										{ "optional", true },
										{ "omitted", "all" },
										{ "emptyAllowed", false },
										{ "dependencyExpansion", true },
										{ "plannedKindsSelectable", false },
										{ "reporting", "requested and resolved sets are returned and retained" },
									} },
				{ "geometrySelection", {
										   { "shaderTypes", "optional non-empty array of numeric engine shader types 0 through 63" },
										   { "executionWithinSelectedGeometry", "optional; when true, draws require either an active selected geometry scope or the one-shot same-thread next-draw handoff from selected SetupGeometry" },
										   { "filteredIsLoss", false },
									   } },
				{ "executionCoverage", {
										   { "deviceContext", "immediate-only" },
										   { "typedDeviceContextIdentity", true },
										   { "commandStreamSequence", true },
										   { "outputMergerTargets", "immediate-requested-post-call-effective-and-first-draw-snapshot" },
										   { "shaderResourceViews", "all-immediate-stages-requested-and-post-call-effective" },
										   { "unorderedAccessViews", "compute-and-output-merger-requested-and-post-call-effective" },
										   { "resourceFlow", "copy-and-resolve" },
										   { "cpuResourceAccess", "immediate-context-map-unmap-with-qpc-duration-and-pairing" },
										   { "vrEyeAttribution", "openvr-submit-resource-and-bounds" },
										   { "visibilitySubmissionJoin", "explicit-next-draw-identity" },
										   { "preparedGeometryJoin", "same-thread-next-immediate-context-draw-after-selected-setup" },
										   { "visibilityBindingVerification", "effective-vs-srv-slot" },
										   { "deferredContexts", false },
										   { "commandLists", false },
										   { "commandListQualification", "implemented-selectable; live deferred-vtable coverage pending" },
									   } },
				{ "pointerPolicies", json::array({ "retain" }) },
				{ "singleActiveCapture", true },
				{ "completedCaptureHistory", 4 },
				{ "durableArtifacts", {
										  { "automaticOnStop", true },
										  { "events", "events.jsonl" },
										  { "manifest", "capture-manifest.json" },
										  { "overwrite", "never" },
										  { "captureStartProvenanceRequired", true },
									  } },
				{ "startAdmission", "provenance-and-response-before-hook-activation" },
				{ "limits", {
								{ "maximumFrames", kMaximumFrames },
								{ "maximumDurationMs", kMaximumDurationMs },
								{ "maximumEvents", kMaximumEvents },
								{ "maximumBytes", kMaximumBytes },
								{ "maximumScopeDepth", CSX::RenderMap::kMaximumScopeDepth },
								{ "maximumEventPage", 500 },
								{ "maximumShaderObservations", kMaximumShaderObservations },
								{ "maximumStageShaderObservations", kMaximumStageShaderObservations },
								{ "maximumResourceObservations", kMaximumResourceObservations },
								{ "maximumTargetViewObservations", kMaximumTargetViewObservations },
								{ "maximumTargetBindingObservations", kMaximumTargetBindingObservations },
								{ "maximumSceneObjectObservations", kMaximumSceneObjectObservations },
								{ "maximumGeometryObservations", kMaximumGeometryObservations },
								{ "maximumMaterialStateObservations", kMaximumMaterialStateObservations },
							} },
				{ "defaults", {
								  { "maxFrames", kDefaultFrames },
								  { "maxDurationMs", kDefaultDurationMs },
								  { "maxEvents", kDefaultEvents },
								  { "maxBytes", kDefaultBytes },
								  { "maxScopeDepth", kDefaultScopeDepth },
								  { "maxShaderObservations", kDefaultShaderObservations },
								  { "maxStageShaderObservations", kDefaultStageShaderObservations },
								  { "maxResourceObservations", kDefaultResourceObservations },
								  { "maxTargetViewObservations", kDefaultTargetViewObservations },
								  { "maxTargetBindingObservations", kDefaultTargetBindingObservations },
								  { "maxSceneObjectObservations", kDefaultSceneObjectObservations },
								  { "maxGeometryObservations", kDefaultGeometryObservations },
								  { "maxMaterialStateObservations", kDefaultMaterialStateObservations },
								  { "fixedCatalogueBytes", fixedCatalogueBytes },
								  { "eventStorageUnitBytes", minimumMaxBytes - fixedCatalogueBytes },
								  { "minimumMaxBytes", minimumMaxBytes },
								  { "budgetSemantics", "maxBytes must admit the fixed catalogues and at least one complete event slot; remaining bytes bound event storage" },
							  } },
				{ "mainThreadAffine", false },
				{ "automaticStop", false },
			};
			return response;
		}

		if (action == "status") {
			auto response = Foundation().MakeEnvelope(a_args, true);
			response["result"] = CSX::RenderMap::SerializeControllerStatus(CSX::RenderMap::GetCaptureController().GetStatus());
			response["result"]["shaderMetadata"] = BuildShaderMetadataStatus();
			return response;
		}

		if (action == "start") {
			auto requestedEventKindMask = kSelectableEventKinds;
			if (a_args.contains("eventKinds")) {
				if (!a_args["eventKinds"].is_array() || a_args["eventKinds"].empty())
					return Foundation().MakeError(a_args, "invalid_field", "eventKinds must be a non-empty array when supplied", "validation", false, "eventKinds");
				requestedEventKindMask = 0;
				std::unordered_set<std::string> names;
				for (const auto& value : a_args["eventKinds"]) {
					if (!value.is_string())
						return Foundation().MakeError(a_args, "invalid_field", "eventKinds entries must be strings", "validation", false, "eventKinds");
					const auto name = value.get<std::string>();
					if (!names.insert(name).second)
						return Foundation().MakeError(a_args, "invalid_field", "eventKinds entries must be unique", "validation", false, "eventKinds");
					bool found = false;
					for (std::uint16_t index = 0; index < static_cast<std::uint16_t>(CSX::RenderMap::EventKind::kCount); ++index) {
						const auto kind = static_cast<CSX::RenderMap::EventKind>(index);
						if ((kSelectableEventKinds & CSX::RenderMap::EventKindBit(kind)) != 0 &&
							name == CSX::RenderMap::EventKindName(kind)) {
							requestedEventKindMask |= CSX::RenderMap::EventKindBit(kind);
							found = true;
							break;
						}
					}
					if (!found)
						return Foundation().MakeError(a_args, "invalid_field", "eventKinds contains an unknown or synthetic event kind", "validation", false, "eventKinds");
				}
			}
			std::uint64_t geometryShaderTypeMask = (std::numeric_limits<std::uint64_t>::max)();
			if (a_args.contains("geometryShaderTypes")) {
				if (!a_args["geometryShaderTypes"].is_array() || a_args["geometryShaderTypes"].empty())
					return Foundation().MakeError(a_args, "invalid_field", "geometryShaderTypes must be a non-empty array when supplied", "validation", false, "geometryShaderTypes");
				geometryShaderTypeMask = 0;
				for (const auto& value : a_args["geometryShaderTypes"]) {
					if (!value.is_number_integer())
						return Foundation().MakeError(a_args, "invalid_field", "geometryShaderTypes entries must be integers", "validation", false, "geometryShaderTypes");
					const auto shaderType = value.get<std::int64_t>();
					if (shaderType < 0 || shaderType >= 64)
						return Foundation().MakeError(a_args, "invalid_field", "geometryShaderTypes entries must be between 0 and 63", "validation", false, "geometryShaderTypes");
					const auto bit = std::uint64_t{ 1 } << static_cast<std::uint32_t>(shaderType);
					if ((geometryShaderTypeMask & bit) != 0)
						return Foundation().MakeError(a_args, "invalid_field", "geometryShaderTypes entries must be unique", "validation", false, "geometryShaderTypes");
					geometryShaderTypeMask |= bit;
				}
			}
			if (a_args.contains("executionWithinSelectedGeometry") && !a_args["executionWithinSelectedGeometry"].is_boolean())
				return Foundation().MakeError(a_args, "invalid_field", "executionWithinSelectedGeometry must be a boolean", "validation", false, "executionWithinSelectedGeometry");
			const auto executionWithinSelectedGeometry = a_args.value("executionWithinSelectedGeometry", false);
			if (const auto violation = Validate(a_args)) {
				if (violation->invalidType)
					return Foundation().MakeError(a_args, "invalid_field", "capture bound must be an unsigned integer", "validation", false, violation->bound.field);
				return InvalidBound(a_args, violation->bound.field,
					a_args.at(violation->bound.field).get<std::uint64_t>(), 1, violation->bound.maximum);
			}
			const auto maxFrames = a_args.value("maxFrames", kDefaultFrames);
			const auto maxDurationMs = a_args.value("maxDurationMs", kDefaultDurationMs);
			const auto maxEvents = a_args.value("maxEvents", kDefaultEvents);
			const auto maxScopeDepth = a_args.value("maxScopeDepth", kDefaultScopeDepth);
			const auto maxShaderObservations = a_args.value("maxShaderObservations", kDefaultShaderObservations);
			const auto maxStageShaderObservations = a_args.value("maxStageShaderObservations", kDefaultStageShaderObservations);
			const auto maxResourceObservations = a_args.value("maxResourceObservations", kDefaultResourceObservations);
			const auto maxTargetViewObservations = a_args.value("maxTargetViewObservations", kDefaultTargetViewObservations);
			const auto maxTargetBindingObservations = a_args.value("maxTargetBindingObservations", kDefaultTargetBindingObservations);
			const auto maxSceneObjectObservations = a_args.value("maxSceneObjectObservations", kDefaultSceneObjectObservations);
			const auto maxGeometryObservations = a_args.value("maxGeometryObservations", kDefaultGeometryObservations);
			const auto maxMaterialStateObservations = a_args.value("maxMaterialStateObservations", kDefaultMaterialStateObservations);
			CSX::RenderMap::CollectorConfig config{
				.maxFrames = maxFrames,
				.maxEvents = maxEvents,
				.maxBytes = kMaximumBytes,
				.maxDuration = std::chrono::milliseconds(maxDurationMs),
				.maxScopeDepth = static_cast<std::uint8_t>(maxScopeDepth),
				.maxShaderObservations = maxShaderObservations,
				.maxStageShaderObservations = maxStageShaderObservations,
				.maxResourceObservations = maxResourceObservations,
				.maxTargetViewObservations = maxTargetViewObservations,
				.maxTargetBindingObservations = maxTargetBindingObservations,
				.maxSceneObjectObservations = maxSceneObjectObservations,
				.maxGeometryObservations = maxGeometryObservations,
				.maxMaterialStateObservations = maxMaterialStateObservations,
				.geometryShaderTypeMask = geometryShaderTypeMask,
				.executionWithinSelectedGeometry = executionWithinSelectedGeometry,
				.requestedEventKindMask = requestedEventKindMask,
			};
			const auto maxBytes = a_args.value("maxBytes", kDefaultBytes);
			config.maxBytes = maxBytes;
			auto catalogueConfig = config;
			catalogueConfig.maxEvents = 0;
			const auto fixedCatalogueBytes = CSX::RenderMap::Collector::RequiredStorageBytes(catalogueConfig);
			catalogueConfig.maxEvents = 1;
			const auto minimumMaxBytes = CSX::RenderMap::Collector::RequiredStorageBytes(catalogueConfig);
			if (maxBytes < minimumMaxBytes) {
				auto error = Foundation().MakeError(
					a_args, "invalid_bounds",
					"maxBytes does not leave space for one complete event slot after the fixed observation catalogues",
					"validation", false, "maxBytes");
				error["error"]["details"] = {
					{ "maxBytes", maxBytes },
					{ "fixedCatalogueBytes", fixedCatalogueBytes },
					{ "eventStorageUnitBytes", minimumMaxBytes - fixedCatalogueBytes },
					{ "minimumMaxBytes", minimumMaxBytes },
				};
				return error;
			}

			CSX::RenderMap::CaptureArtifactContext artifactContext;
			try {
				artifactContext = BuildArtifactContext();
			} catch (const std::invalid_argument& e) {
				return Foundation().MakeError(a_args, "capture_provenance_unavailable", e.what(), "execution", true);
			} catch (const std::exception& e) {
				return Foundation().MakeError(a_args, "allocation_failed", e.what(), "execution", true);
			}
			CSX::RenderMap::CaptureDescriptor descriptor;
			CSX::RenderMap::CaptureStartPhase phase = CSX::RenderMap::CaptureStartPhase::kContext;
			json response;
			ControlStatus status;
			{
				std::lock_guard lock(g_artifactMutex);
				status = CSX::RenderMap::StartPreparedCapture(
					CSX::RenderMap::GetCaptureController(), config, descriptor, phase,
					[&](const auto& a_capture) {
						g_artifactContexts.insert_or_assign(a_capture.captureId, std::move(artifactContext));
					},
					[&](const auto& a_capture) {
						response = Foundation().MakeEnvelope(a_args, true);
						response["result"] = {
							{ "captureId", a_capture.captureId },
							{ "numericId", a_capture.numericId },
							{ "state", "capturing" },
							{ "bounds", CSX::RenderMap::SerializeBounds(a_capture.config) },
						};
					},
					[&](const std::string& a_captureId) noexcept { g_artifactContexts.erase(a_captureId); });
			}
			if (status == ControlStatus::kAllocationFailed && phase != CSX::RenderMap::CaptureStartPhase::kRuntime)
				return Foundation().MakeError(a_args, "allocation_failed",
					phase == CSX::RenderMap::CaptureStartPhase::kContext ?
						"capture provenance could not be retained; capture was not activated" :
						"capture response could not be materialized; capture was not activated",
					"execution", true);
			if (status != ControlStatus::kSuccess)
				return ControlFailure(a_args, status);
			return response;
		}

		if (action == "stop") {
			std::string captureId;
			if (a_args.contains("captureId")) {
				if (!a_args["captureId"].is_string() || a_args["captureId"].get_ref<const std::string&>().empty())
					return Foundation().MakeError(a_args, "invalid_field", "captureId must be a non-empty string when supplied", "validation", false, "captureId");
				captureId = a_args["captureId"].get<std::string>();
			}
			std::shared_ptr<const CSX::RenderMap::CompletedCapture> capture;
			const auto status = CSX::RenderMap::GetCaptureController().Stop(captureId, capture);
			if (status != ControlStatus::kSuccess)
				return ControlFailure(a_args, status);
			captureId = capture->descriptor.captureId;

			std::lock_guard lock(g_artifactMutex);
			PruneArtifactState(captureId);
			const auto context = g_artifactContexts.find(captureId);
			if (!g_artifactBundles.contains(captureId) && context == g_artifactContexts.end())
				return Foundation().MakeError(a_args, "capture_provenance_unavailable",
					"capture-start provenance is not retained; artifacts cannot be reconstructed",
					"execution", false, "captureId");
			auto response = CSX::RenderMap::WithPreparedCaptureArtifacts(g_artifactBundles, captureId, [&] { return CSX::RenderMap::WriteCaptureArtifacts(*capture, context->second, GetCurrentProcessId()); }, [&](const CSX::RenderMap::CaptureArtifactBundle& a_artifacts) {
				auto prepared = Foundation().MakeEnvelope(a_args, true);
				prepared["result"] = CSX::RenderMap::SerializeCaptureSummary(*capture);
				prepared["result"]["artifacts"] = CSX::RenderMap::SerializeArtifactBundle(a_artifacts);
				if (!a_artifacts.success)
					prepared["result"]["warnings"] = json::array({ {
						{ "code", "artifact_write_failed" },
						{ "message", a_artifacts.error },
					} });
				return prepared; });
			g_artifactContexts.erase(captureId);
			return response;
		}

		if (!a_args.contains("captureId") || !a_args["captureId"].is_string() || a_args["captureId"].get_ref<const std::string&>().empty())
			return Foundation().MakeError(a_args, "invalid_field", "captureId must be a non-empty string", "validation", false, "captureId");
		const auto& captureId = a_args["captureId"].get_ref<const std::string&>();

		if ((a_args.contains("offset") && !HasUnsigned(a_args, "offset")) ||
			(a_args.contains("limit") && !HasUnsigned(a_args, "limit"))) {
			return Foundation().MakeError(a_args, "invalid_field", "offset and limit must be unsigned integers", "validation", false);
		}
		const auto offset = a_args.value("offset", 0ull);
		const auto limit = a_args.value("limit", 100ull);
		if (limit == 0 || limit > 500 || offset > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
			return Foundation().MakeError(a_args, "invalid_page", "capture event page is outside the advertised limits", "validation", false);
		const auto capture = CSX::RenderMap::GetCaptureController().GetCompleted(captureId);
		if (!capture)
			return ControlFailure(a_args, ControlStatus::kCaptureNotFound);
		auto response = Foundation().MakeEnvelope(a_args, true);
		response["result"] = CSX::RenderMap::SerializeEventPage(
			*capture,
			static_cast<std::size_t>(offset),
			static_cast<std::size_t>(limit),
			GetCurrentProcessId());
		return response;
	}

	void ToolHandler(void*, const char* a_argsJson, void* a_sink, DevBenchAPI::WriteFn a_write) noexcept
	{
		json output;
		try {
			const auto args = a_argsJson && *a_argsJson ? json::parse(a_argsJson) : json::object();
			if (auto mismatch = BuildProvenance::ValidateExpectedBuild(args)) {
				output = Foundation().MakeError(args, mismatch->value("code", std::string("producer_mismatch")),
					mismatch->value("error", std::string("loaded CSX build does not match the request")),
					"validation", false, "expectedBuildId");
			} else {
				output = Foundation().Dispatch(args, &BuildResult);
			}
		} catch (const std::exception& e) {
			output = Foundation().MakeError(json::object(), "invalid_request", e.what());
		} catch (...) {
			output = Foundation().MakeError(json::object(), "internal_error", "unknown render-map API error", "dispatch", true);
		}
		try {
			const auto serialized = output.dump();
			a_write(a_sink, serialized.c_str());
		} catch (...) {
			a_write(a_sink, R"({"ok":false,"error":{"code":"serialization_failed"}})");
		}
	}
}

namespace CSX::RenderMap::DevBenchBridge
{
	void Install()
	{
		if (g_registered.load(std::memory_order_acquire))
			return;
		auto* devBench = DevBenchAPI::GetDevBenchInterface001();
		if (!devBench) {
			logger::info("RenderMapDevBenchBridge: devbench host not present; render-map tool not registered");
			return;
		}
		const char* descriptor = R"({
			"description":"Versioned, explicitly bounded CSX render-map diagnostic capture. Capture is off by default; start retains runtime provenance and its response before hook activation. Registry/status expose independent persistent shader limits and diagnostic failureCount; post-success shader observations cannot change native results. Stop requires the original capture-start provenance; events are read only after stop.",
			"inputSchema":{"type":"object","required":["contractMajor","clientId","commandId","action"],"properties":{
				"contractMajor":{"type":"integer","const":1},"clientId":{"type":"string","minLength":1,"maxLength":128},
				"commandId":{"type":"string","minLength":1,"maxLength":128},"expectedBuildId":{"type":"string"},
				"action":{"type":"string","enum":["registry","status","start","stop","capture_events"],"description":"registry/status include shaderMetadata identity/alias/dump limits, failureCount and includedInCaptureMaxBytes=false. start rejects inconsistent runtime provenance before activation; stop reports capture_provenance_unavailable when start-time context is missing."},
				"captureId":{"type":"string","minLength":1},
				"eventKinds":{"type":"array","minItems":1,"uniqueItems":true,"items":{"type":"string"}},
				"geometryShaderTypes":{"type":"array","minItems":1,"uniqueItems":true,"items":{"type":"integer","minimum":0,"maximum":63}},
				"executionWithinSelectedGeometry":{"type":"boolean"},
				"maxFrames":{"type":"integer","minimum":1,"maximum":600},
				"maxDurationMs":{"type":"integer","minimum":1,"maximum":10000},"maxEvents":{"type":"integer","minimum":1,"maximum":65536},
				"maxBytes":{"type":"integer","minimum":1,"maximum":67108864},"maxScopeDepth":{"type":"integer","minimum":1,"maximum":32},
				"maxShaderObservations":{"type":"integer","minimum":1,"maximum":8192},
				"maxStageShaderObservations":{"type":"integer","minimum":1,"maximum":32768},
				"maxResourceObservations":{"type":"integer","minimum":1,"maximum":32768},
				"maxTargetViewObservations":{"type":"integer","minimum":1,"maximum":32768},
				"maxTargetBindingObservations":{"type":"integer","minimum":1,"maximum":32768},
				"maxSceneObjectObservations":{"type":"integer","minimum":1,"maximum":32768},
				"maxGeometryObservations":{"type":"integer","minimum":1,"maximum":65536},
				"maxMaterialStateObservations":{"type":"integer","minimum":1,"maximum":65536},
				"offset":{"type":"integer","minimum":0},"limit":{"type":"integer","minimum":1,"maximum":500}
			}}
		})";
		devBench->RegisterTool("communityshaders.render_map", descriptor, &ToolHandler, nullptr);
		g_registered.store(true, std::memory_order_release);
		logger::info("RenderMapDevBenchBridge: registered communityshaders.render_map with devbench build {}", devBench->GetBuildNumber());
	}

	bool IsRegistered()
	{
		return g_registered.load(std::memory_order_acquire);
	}
}

#endif
