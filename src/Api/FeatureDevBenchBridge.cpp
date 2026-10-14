#include "Api/FeatureDevBenchBridge.h"
#include "LegacyUtilityCompatibility.h"

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "Api/DevBenchMainThreadDispatch.h"
#	include "Api/FeatureService.h"
#	include "Features/TextureStreaming.h"
#	include "Features/LightLimitFix.h"
#	include "Features/LightLimitFix/ShadowCulling.h"
#	include "Features/TextureStreaming/Settings.h"
#	include "Api/ServiceFoundation.h"
#	include "BuildProvenance.h"
#	include "FeatureIssues.h"
#	include "Globals.h"
#	include "Utils/GpuMemoryBudget.h"
#	include "PresetCompatibility.h"
#	include <DevBenchAPI.h>
#	include <nlohmann/json.hpp>
#	include <atomic>
#	include <exception>
#	include <functional>
#	include <mutex>
#	include <optional>
#	include <stdexcept>
#	include <string>

namespace
{
	using json = nlohmann::json;
	using namespace CSX::FeatureAPI;
	std::atomic_bool g_registered{ false };

	CSX::Api::ServiceFoundation& Foundation()
	{
		static CSX::Api::ServiceFoundation value({ ServiceName, 1, 1, 17 });
		static std::once_flag once;
		std::call_once(once, [&] { value.SetServerMetadataProvider([] { auto p = BuildProvenance::GetProducer(); p["serviceSessionId"] = Foundation().SessionId(); return p; }); });
		return value;
	}

	const char* StatusName(Status value)
	{
		switch (value) {
		case Status::kSuccess:
			return "success";
		case Status::kInvalidArgument:
			return "invalid_argument";
		case Status::kStructureTooSmall:
			return "structure_too_small";
		case Status::kUnavailable:
			return "unavailable";
		case Status::kWrongThread:
			return "wrong_thread";
		case Status::kRevisionConflict:
			return "revision_conflict";
		case Status::kPreflightRequired:
			return "preflight_required";
		case Status::kPreflightExpired:
			return "preflight_expired";
		case Status::kPreflightMismatch:
			return "preflight_mismatch";
		case Status::kFeatureNotFound:
			return "feature_not_found";
		case Status::kBlocked:
			return "blocked";
		case Status::kPersistenceFailed:
			return "persistence_failed";
		default:
			return "internal_error";
		}
	}

	json OnMain(std::function<json()> run)
	{
		return CSX::Api::RunDevBenchMainThreadTask(SKSE::GetTaskInterface(), std::move(run));
	}

	json SnapshotJson(const Snapshot001& v)
	{
		return { { "available", v.available != 0 }, { "persistentMutationBlocked", v.persistentMutationBlocked != 0 },
			{ "saveLoadSafeModeActive", v.saveLoadSafeModeActive != 0 }, { "featureCount", v.featureCount },
			{ "loadedFeatureCount", v.loadedFeatureCount }, { "activeConstraintCount", v.activeConstraintCount },
			{ "stateRevision", v.stateRevision }, { "capabilities", v.capabilities }, { "buildId", v.buildId ? v.buildId : "" } };
	}

	json FeatureIssueJson(const FeatureIssues::FeatureIssueInfo& issue)
	{
		return {
			{ "shortName", issue.shortName },
			{ "displayName", issue.displayName },
			{ "version", issue.version },
			{ "issueType", magic_enum::enum_name(issue.issueType) },
			{ "rejectionReason", issue.rejectionReason },
			{ "replacementFeature", issue.replacementFeature },
			{ "replacementFeatureDisplayName", issue.replacementFeatureDisplayName },
			{ "replacementFeatureInstalled", issue.replacementFeatureInstalled },
			{ "replacementFeatureModLink", issue.replacementFeatureModLink },
			{ "userMessage", issue.userMessage },
			{ "minimumVersionRequired", issue.minimumVersionRequired },
			{ "modifiedShaderDirectory", issue.modifiedShaderDirectory },
			{ "iniPath", issue.iniPath },
			{ "removedInVersion", issue.removedInVersion.string(".") },
		};
	}

	MutationRequest001 Mutation(const json& args, std::string& feature, std::string& token)
	{
		if (!args.contains("mutation") || !args["mutation"].is_object())
			throw std::runtime_error("mutation object is required");
		const auto& m = args["mutation"];
		if (m.value("action", std::string{}) != "set_disabled_at_boot")
			throw std::runtime_error("mutation.action is unknown");
		if (!m.contains("expectedStateRevision") || !m["expectedStateRevision"].is_number_unsigned())
			throw std::runtime_error("mutation.expectedStateRevision is required and must be unsigned");
		feature = m.value("featureShortName", std::string{});
		token = m.value("preflightToken", std::string{});
		std::uint64_t flags = 0;
		if (m.value("persist", false))
			flags |= kMutationPersist;
		if (m.value("allowDisruptive", false))
			flags |= kMutationAllowDisruptive;
		return { .structSize = sizeof(MutationRequest001), .action = MutationAction::kSetDisabledAtBoot, .expectedStateRevision = m["expectedStateRevision"].get<std::uint64_t>(), .flags = flags, .featureShortName = feature.empty() ? nullptr : feature.c_str(), .disabled = m.value("disabled", false) ? 1u : 0u, .preflightToken = token.empty() ? nullptr : token.c_str() };
	}

	json StreamingPressureResult(const json& args)
	{
		std::optional<StreamingTextures::PressureSimulationRequest> request;
		try {
			for (const auto& [name, value] : args.items()) {
				(void)value;
				if (name != "action" && name != "contractMajor" && name != "clientId" && name != "commandId" &&
					name != "expectedBuildId" && name != "pressureSimulation")
					throw std::invalid_argument("Unknown texture_streaming_pressure argument");
			}
			if (args.contains("pressureSimulation"))
				request = StreamingTextures::ParsePressureSimulation(args.at("pressureSimulation"));
		} catch (const std::exception& e) {
			return Foundation().MakeError(args, "invalid_pressure_simulation", e.what(), "validation", false, "pressureSimulation");
		}
		auto result = OnMain([request] {
			auto& streaming = TextureStreaming::Instance();
			if (request) {
				if (request->durationMilliseconds && (!streaming.loaded || streaming.GetStatus().at("externalStreamingConflict").get<bool>()))
					return json{ { "errorCode", "feature_unavailable" }, { "error", "Texture Streaming feature is unavailable" } };
				if (!Util::GpuMemoryBudget::Get().ConfigureStreamingPressure(globals::d3d::device, request->durationMilliseconds, request->freeMiB))
					return json{ { "errorCode", "budget_unavailable" }, { "error", "Pressure test requires fresh physical GPU memory telemetry" } };
			}
			return streaming.GetStatus();
		});
		if (result.contains("error"))
			return Foundation().MakeError(args, result.value("errorCode", std::string("main_thread_dispatch_failed")),
				result.at("error"), "admission", result.value("errorCode", std::string{}) == "budget_unavailable");
		auto response = Foundation().MakeEnvelope(args, true);
		response["result"] = std::move(result);
		return response;
	}

	json BuildResult(const json& args)
	{
		const auto action = args.value("action", std::string{});
		const bool known = action == "shadow_culling" || action == "texture_streaming_pressure" || action == "texture_streaming" || action == "registry" || action == "snapshot" || action == "features" || action == "issues" || action == "preset_compatibility" || action == "settings" || action == "constraints" || action == "preflight" || action == "execute";
		if (!known)
			return Foundation().MakeError(args, "unknown_action", "action is not supported", "validation", false, "action");
		if (action == "texture_streaming_pressure")
			return StreamingPressureResult(args);
		if (args.contains("pressureSimulation"))
			return Foundation().MakeError(args, "invalid_pressure_simulation", "pressureSimulation requires texture_streaming_pressure", "validation", false, "pressureSimulation");
		if (args.contains("shadowCulling")) {
			try {
				if (action != "shadow_culling")
					throw std::invalid_argument("shadowCulling requires the shadow_culling action");
				(void)LightLimitFixDetail::ShadowCulling::Parse(args.at("shadowCulling"));
			} catch (const std::exception& error) {
				return Foundation().MakeError(args, "invalid_settings", error.what(), "validation", false, "shadowCulling");
			}
		}
		std::optional<bool> debugForceStreaming;
		try {
			debugForceStreaming = StreamingTextures::ParseDebugForceStreaming(args);
		} catch (const std::exception& e) {
			return Foundation().MakeError(args, "invalid_settings", e.what(), "validation", false, "debugForceStreaming");
		}
		if (action == "registry") {
			auto response = Foundation().MakeEnvelope(args, true);
			response["result"] = { { "service", ServiceName }, { "major", 1 }, { "minor", 1 }, { "schemaRevision", 17 },
				{ "capabilities", ServiceCapabilities }, { "mainThreadAffine", true }, { "registryMainThreadAffine", false },
				{ "preflightTokenLifetimeMs", 30000 }, { "actions", json::array({ "registry", "snapshot", "features", "issues", "preset_compatibility", "settings", "shadow_culling", "texture_streaming", "texture_streaming_pressure", "constraints", "preflight", "execute" }) },
				{ "mutations", json::array({ "set_disabled_at_boot" }) }, { "legacyInterfacesPreserved", true } };
			return response;
		}
		if (action == "preflight" || action == "execute")
			try {
				std::string f, t;
				(void)Mutation(args, f, t);
			} catch (const std::exception& e) {
				return Foundation().MakeError(args, "invalid_mutation", e.what(), "validation", false, "mutation");
			}
		if (args.contains("textureStreaming")) {
			try {
				const auto& settings = args.at("textureStreaming");
				if (action != "texture_streaming" || !settings.is_object() || settings.size() != 2u + settings.contains("categories") + settings.contains("modes") + settings.contains("options") ||
					!settings.contains("enabled") || !settings.at("enabled").is_boolean() || !settings.contains("maximumMipDrop"))
					throw std::invalid_argument("textureStreaming requires the texture_streaming action, enabled, maximumMipDrop and optional categories/modes/options");
				(void)StreamingTextures::ParseMaximumMipDrop(settings.at("maximumMipDrop"));
				if (settings.contains("modes")) {
					const auto modes = StreamingTextures::ParseModes(settings.at("modes"));
					if (debugForceStreaming && *debugForceStreaming != modes.continuous)
						throw std::invalid_argument("debugForceStreaming conflicts with modes.continuous");
				}
				if (settings.contains("options"))
					(void)StreamingTextures::ParseOptions(settings.at("options"));
				if (settings.contains("categories"))
					(void)StreamingTextures::ParseCategories(settings.at("categories"));
			} catch (const std::exception& e) {
				return Foundation().MakeError(args, "invalid_settings", e.what(), "validation", false, "textureStreaming");
			}
		}
		auto result = OnMain([action, args, debugForceStreaming] {
			const auto* api = CSX::Api::GetFeatureService001();
			if (!api)
				return json{ { "error", "feature API unavailable" } };
			if (action == "shadow_culling") {
				auto& lighting = globals::features::lightLimitFix;
				if (args.contains("shadowCulling")) {
					const auto status = LightLimitFixDetail::ShadowCulling::Status(lighting.settings.ShadowCulling);
					if (!lighting.loaded || !status.at("available").get<bool>())
						return json{ { "error", "CSX native shadow culling is unavailable; SPS retains control when loaded" }, { "errorCode", "feature_unavailable" } };
					lighting.settings.ShadowCulling = LightLimitFixDetail::ShadowCulling::Parse(args.at("shadowCulling"), lighting.settings.ShadowCulling);
				}
				return LightLimitFixDetail::ShadowCulling::Status(lighting.settings.ShadowCulling);
			}
			if (action == "texture_streaming") {
				auto& streaming = TextureStreaming::Instance();
				if ((args.contains("textureStreaming") || debugForceStreaming.has_value()) && !streaming.loaded)
					return json{ { "error", "Texture Streaming feature is not loaded" }, { "errorCode", "feature_unavailable" } };
				if (debugForceStreaming.value_or(false)) {
					const bool enabled = args.contains("textureStreaming") ? args.at("textureStreaming").at("enabled").get<bool>() : streaming.GetStatus().at("enabled").get<bool>();
					if (!enabled)
						return json{ { "error", "Enable Texture Streaming before forcing reductions" } };
				}
				if (args.contains("textureStreaming")) {
					const auto& settings = args.at("textureStreaming");
					streaming.Configure(settings.at("enabled"), StreamingTextures::ParseMaximumMipDrop(settings.at("maximumMipDrop")),
						settings.contains("categories") ? std::optional(StreamingTextures::ParseCategories(settings.at("categories"))) : std::nullopt,
						settings.contains("modes") ? std::optional(StreamingTextures::ParseModes(settings.at("modes"))) : std::nullopt,
						settings.contains("options") ? std::optional(StreamingTextures::ParseOptions(settings.at("options"))) : std::nullopt);
				}
				if (debugForceStreaming)
					streaming.SetDebugForceStreaming(*debugForceStreaming);
				return streaming.GetStatus();
			}
			if (action == "snapshot") {
				Snapshot001 v;
				const auto s = api->GetSnapshot(api->context, &v);
				return json{ { "status", StatusName(s) }, { "snapshot", SnapshotJson(v) } };
			}
			if (action == "issues") {
				json issues = json::array();
				for (const auto& issue : FeatureIssues::GetFeatureIssues()) issues.push_back(FeatureIssueJson(issue));
				return json{ { "featureIssues", std::move(issues) }, { "hasIssues", FeatureIssues::HasFeatureIssues() },
					{ "hasObsoleteShaderModifyingFeatures", FeatureIssues::HasObsoleteShaderModifyingFeatures() },
					{ "hasPotentialShaderModifyingFeatures", FeatureIssues::HasPotentialShaderModifyingFeatures() } };
			}
			if (action == "preset_compatibility")
				return json{ { "presetCompatibility", PresetCompatibility::ToJson(PresetCompatibility::GetPublished()) } };
			if (action == "features") {
				json values = json::array();
				const auto count = api->GetFeatureCount(api->context);
				for (std::uint32_t i = 0; i < count; ++i) {
					FeatureDescriptor001 v;
					if (api->GetFeatureDescriptor(api->context, i, &v) != Status::kSuccess)
						continue;
					values.push_back({ { "shortName", v.shortName ? v.shortName : "" }, { "name", v.name ? v.name : "" }, { "displayName", v.displayName ? v.displayName : "" },
						{ "category", v.category ? v.category : "" }, { "installedVersion", v.installedVersion ? v.installedVersion : "" }, { "requiredVersion", v.requiredVersion ? v.requiredVersion : "" },
						{ "shaderDefineName", v.shaderDefineName ? v.shaderDefineName : "" }, { "failureMessage", v.failureMessage ? v.failureMessage : "" }, { "summary", v.summary ? v.summary : "" },
						{ "summaryItems", json::parse(v.summaryItemsJson ? v.summaryItemsJson : "[]") }, { "loaded", v.loaded != 0 }, { "core", v.core != 0 }, { "inMenu", v.inMenu != 0 },
						{ "supportsVR", v.supportsVR != 0 }, { "disabledAtBoot", v.disabledAtBoot != 0 }, { "runtimeDisabledByMissingDependency", v.runtimeDisabledByMissingDependency != 0 },
						{ "hiddenFromUserView", v.hiddenFromUserView != 0 }, { "hasFeatureSettings", v.hasFeatureSettings != 0 },
						{ "hasShaderDefine", v.hasShaderDefine != 0 }, { "activeConstraintCount", v.activeConstraintCount } });
				}
				return json{ { "count", count }, { "features", std::move(values) } };
			}
			if (action == "settings") {
				const auto name = args.value("featureShortName", std::string{});
				SettingsSnapshot001 v;
				const auto s = api->GetFeatureSettings(api->context, name.c_str(), &v);
				return json{ { "status", StatusName(s) }, { "featureShortName", name }, { "canonicalFeatureShortName", LegacyUtilityCompatibility::IsAlias(name) ? "AdaptiveBrightness" : name }, { "compatibilityAlias", LegacyUtilityCompatibility::IsAlias(name) }, { "settings", s == Status::kSuccess ? json::parse(v.settingsJson ? v.settingsJson : "{}") : json(nullptr) } };
			}
			if (action == "constraints") {
				json values = json::array();
				const auto count = api->GetConstraintCount(api->context);
				for (std::uint32_t i = 0; i < count; ++i) {
					ConstraintDescriptor001 v;
					if (api->GetConstraintDescriptor(api->context, i, &v) != Status::kSuccess)
						continue;
					values.push_back({ { "sourceFeatureShortName", v.sourceFeatureShortName ? v.sourceFeatureShortName : "" }, { "targetFeatureShortName", v.targetFeatureShortName ? v.targetFeatureShortName : "" },
						{ "targetSettingPath", v.targetSettingPath ? v.targetSettingPath : "" }, { "forcedValue", json::parse(v.forcedValueJson ? v.forcedValueJson : "null") },
						{ "reason", v.reason ? v.reason : "" }, { "recommendDisableAtBoot", v.recommendDisableAtBoot != 0 } });
				}
				return json{ { "count", count }, { "constraints", std::move(values) } };
			}
			std::string feature, token;
			auto mutation = Mutation(args, feature, token);
			if (action == "preflight") {
				Preflight001 v;
				const auto s = api->Preflight(api->context, &mutation, &v);
				return json{ { "status", StatusName(s) }, { "allowed", v.allowed != 0 }, { "disruptive", v.disruptive != 0 },
					{ "willPersist", v.willPersist != 0 }, { "stateRevision", v.stateRevision }, { "requiredFlags", v.requiredFlags }, { "preflightToken", v.token ? v.token : "" },
					{ "reasonCode", v.reasonCode ? v.reasonCode : "" }, { "message", v.message ? v.message : "" } };
			}
			MutationReceipt001 v;
			const auto s = api->Execute(api->context, &mutation, &v);
			Snapshot001 current;
			(void)api->GetSnapshot(api->context, &current);
			return json{ { "status", StatusName(s) }, { "applied", v.applied != 0 }, { "changed", v.changed != 0 }, { "persisted", v.persisted != 0 },
				{ "previousStateRevision", v.previousStateRevision }, { "stateRevision", v.stateRevision }, { "message", v.message ? v.message : "" }, { "current", SnapshotJson(current) } };
		});
		if (result.value("errorCode", std::string{}) == "feature_unavailable")
			return Foundation().MakeError(args, "feature_unavailable", result.at("error"), "admission", false, "textureStreaming");
		if (result.contains("error"))
			return Foundation().MakeError(args, "main_thread_dispatch_failed", result.value("detail", result.value("error", std::string("feature API dispatch failed"))), "dispatch", true);
		auto response = Foundation().MakeEnvelope(args, true);
		response["result"] = std::move(result);
		return response;
	}

	void Handler(void*, const char* text, void* sink, DevBenchAPI::WriteFn write) noexcept
	{
		json output;
		try {
			json args = text && *text ? json::parse(text) : json::object();
			if (auto mismatch = BuildProvenance::ValidateExpectedBuild(args))
				output = Foundation().MakeError(args, mismatch->value("code", std::string("producer_mismatch")), mismatch->value("error", std::string("loaded build mismatch")), "validation", false, "expectedBuildId");
			else
				output = Foundation().Dispatch(args, &BuildResult);
		} catch (const std::exception& e) {
			output = Foundation().MakeError(json::object(), "invalid_request", e.what());
		} catch (...) {
			output = Foundation().MakeError(json::object(), "internal_error", "unknown feature API error", "dispatch", true);
		}
		try {
			const auto serialized = output.dump();
			write(sink, serialized.c_str());
		} catch (...) {
			write(sink, R"({"ok":false,"error":{"code":"serialization_failed"}})");
		}
	}
}

namespace CSX::Api::FeatureDevBenchBridge
{
	void Install()
	{
		if (g_registered.load(std::memory_order_acquire))
			return;
		auto* host = DevBenchAPI::GetDevBenchInterface001();
		if (!host) {
			logger::info("FeatureDevBenchBridge: devbench host not present; feature API tool not registered");
			return;
		}
		const char* descriptor = R"({"description":"Versioned CSX feature catalog, settings/constraint inspection, detected feature issues, legacy CSUtility/OSUtility settings aliases backed by Adaptive Balance, preset compatibility diagnostics, texture streaming inspection/configuration, and guarded boot-configuration API. texture_streaming and texture_streaming_pressure include snapshot identity for the owned live-state copy: capture time, configuration epoch, inventory generation, device generation and last service attempt frame. lastServiceAttemptFrame is null before a frame is recorded and does not prove maintenance completed. rememberedSizes, restorationCache and pressureSimulation are independently sampled after the live-state copy; memory retains its own sampledAtMs and deviceGeneration identity. texture_streaming reports paused for blocked active work; detail distinguishes world loading from render-scale or NR transitions and clears when the pause ends. Developer-only materialDiagnostics gives cumulative first-failed material checks, geometry type and disallowed shader-flag bit observations, with one bounded representative per check. Texture demand uses projected static world bounds and distance for both eyes, with both material UV scales accounted for and without CPU vertex inspection. Shared terrain/static path consent is accumulated independently of scan order. Unnamed or excluded path aliases protect the shared resource, and the SRV must reference the tracked texture. Explicit exclusions protect characters, LOD, water, animated/projected sampling and special texture slots; ordinary lighting flags do not require whitelisting. Developer-only demandDiagnostics separates invalid demand, full detail and reducible demand, with bounded texture/limiting-mesh samples and raw eye inputs. Developer-only textureDiagnostics identifies the first failed texture resource contract, with one bounded path, engine metadata and actual DirectX texture/view descriptions per result. GPU descriptions are authoritative; zero engine dimensions or mip fields are unavailable and remain unset during replacement. Power-of-two rectangular textures and valid shorter mip chains are supported. These are observations, not unique meshes; unavailable material metadata values are null. diagnostics counters report scan, exclusion, demand and admission observations rather than unique textures. pressureEpisode summarizes each pressure interval, including committed reductions and completed GPU retirements; memory.recoveryDemandBytes is soft future NR demand that constrains optional refill only. Memory telemetry uses the shared sampled budget and reports logical capacities; optional textureStreaming applies session settings and disabling queues gradual restoration. issues returns boot warning data; preset_compatibility reports whether marked SettingsUser content was accepted or rejected. Optional categories independently admit landscape-folder static meshes, alpha-tested statics, diffuse/normal maps on emissive statics, and near-terrain layers. Terrain and Parallax default on for fresh or absent category settings; the other optional categories default off. Saved explicit selections are retained. The experimental Parallax category admits diffuse/normal maps on known static parallax materials, including Terrain Helper and PBR; terrain also requires Terrain. Height/displacement, inner-layer and auxiliary maps remain protected, including resource aliases. Parallax can change appearance and can be disabled independently. PBR emission requires the Emissive toggle. Auxiliary texture aliases and unknown material features are rejected. Shared terrain layers retain the highest demand across observed patches; terrain overlays, noise, LOD and DynDOLOD atlases stay protected. Omitted categories preserve the current session selection; a supplied categories object replaces the selection, with omitted fields disabled. Changes invalidate unfinished reductions and queue restoration. Water, LOD/DynDOLOD atlases, characters, armor/clothing, maps, books, fonts, effects, sky, height/displacement maps, glow maps and generated textures remain protected. Persistent modes independently select onPressure (default true) and continuous (default false). enabled is the independent master switch; requestedEnabled reports the saved choice even when SPS blocks streaming. Omitting modes retains both trigger choices, including while disabled. Turning both triggers off stops distance reductions while retaining an enabled texture-size cap. Info-mode streaming controls retain their state across menu pages. Older saved Enabled settings retain their intent; absent settings use onPressure. debugForceStreaming is a deprecated developer API alias for modes.continuous and now follows its saved-settings behavior; contradictory modes/alias input is rejected before mutation. reductionRequested identifies the effective reduction policy, not proof of a completed reduction. Shrinking copies resident lower mips directly on the GPU after a bounded source-header check; sourcePayloadReads counts full DDS reads for restoration only. Restorations reuse immutable pixels from a 512 MiB LRU RAM cache keyed by DDS resource serial and exact path/metadata; a new texture identity cannot reuse stale pixels. Cache entries are evicted under the byte/entry limits, cleared for low system-memory headroom (including a one-second idle-worker check) and device changes, and released after disabled streaming finishes restoration. transfers counts committed GPU-copy reductions and cached/file-backed restorations; source read counters count completed reads, including cancelled requests. restorationCache reports retained CPU pixel bytes, hit/miss/eviction counts and failed optional stores; GPU uploads and retirement remain required for cache hits. Loaded SPS.dll (both SE/AE and VR builds) blocks native streaming before its DDS hook is installed; the feature remains visible with disabled controls and a warning. Runtime settings cannot bypass this conflict. Saved trigger preferences are retained. externalStreamingConflict and unavailableReason explain the inhibition; disable the SPS mod and restart the game to use CSX native streaming. Options configure reserveMiB (256-8192 in 256 MiB steps), rememberedSizes (default true), and maximumSize (0/unlimited, 1024, 2048, 4096 or 8192). Reserve controls pressure entry and refill admission; optional refill uses an additional 1024 MiB recovery gap. Remembered sizes persist validated source metadata and only reduce later loads under a selected streaming trigger; a cap applies even without pressure. Disabling the master restores full detail as headroom permits; disabling both triggers stops distance streaming but keeps an enabled size cap. Exclusions and shared NR/render-scale admission remain enforced. Developer-only terrainDiagnostics separately counts committed terrain-consumer reductions/restorations and completed GPU retirements, with one bounded last-reduction path and actual replacement dimensions. Shared terrain/static consumers are included. Terrain material rejection observations reuse materialDiagnostics semantics. Ordinary static textures remain eligible with all optional coverage switches off. Terrain diagnostics also report current admitted records, desired reductions, measurable bounds, texture registration exclusions, DirectX contract checks, demand and record admission blockers independently of other categories. Each exclusion/admission check retains one bounded texture path and geometry sample. Failed terrain materials remain in materials; texture observations cover materials carrying Terrain consent. Shared terrain/static records count once on first terrain observation. Registered PBR terrain is identified through its material registry despite sharing the vanilla feature ID. Its actual base-colour and normal layer arrays are streamed; displacement/RMAOS, overlays, noise and GPU resource aliases remain protected. Generated default fillers cannot reject an otherwise eligible terrain patch and are never streamed themselves. texture_streaming_pressure inspects or configures a DevBench-only streaming budget simulation, independently of NR being enabled. Optional pressureSimulation starts a fixed lower budget from current physical usage plus pending allocations and freeMiB (0-65536), for durationMilliseconds (1-30000). Zero duration with no freeMiB clears it; omitting pressureSimulation inspects. It never allocates filler VRAM, increases physical headroom, changes saved settings or changes NR. Streaming demand, remembered loads and replacement/refill admission use the same effective budget; NR and render scale keep physical samples. Expiry and device replacement restore physical sampling automatically. pressureSimulation reports device generation and deadline; memory separates effective budgetBytes, physicalBudgetBytes and pressureSimulated. Use pressure mode with continuous off and unlimited size to test pressure entry, recovery hysteresis and reserve-limited refill. shadow_culling inspects or stages optional native shadow-map caster culling under Light Limit Fix Performance. shadowCulling is a partial object; all values are validated before mutation. Both VR eyes, low sun angles, near lights, large projected shadows, animated objects and special effects remain protected; optional NPC culling also checks nearby actor roots. Sun, torch, distant NPC and indoor torch culling default on. Native hooks and mutations are disabled while SPS.dll is loaded. Streaming now admits bounded 8K BC5/BC7 sources, protects inventory and loading-screen preview consumers and ranks required restorations by urgency with aging.","inputSchema":{"type":"object","required":["contractMajor","clientId","commandId","action"],"properties":{"contractMajor":{"type":"integer","const":1},"clientId":{"type":"string","minLength":1,"maxLength":128},"commandId":{"type":"string","minLength":1,"maxLength":128},"expectedBuildId":{"type":"string"},"action":{"type":"string","enum":["registry","snapshot","features","issues","preset_compatibility","settings","texture_streaming","texture_streaming_pressure","constraints","preflight","execute","shadow_culling"]},"textureStreaming":{"type":"object","additionalProperties":false,"required":["enabled","maximumMipDrop"],"properties":{"enabled":{"type":"boolean"},"maximumMipDrop":{"type":"integer","minimum":1,"maximum":3},"categories":{"type":"object","additionalProperties":false,"properties":{"landscapeStatics":{"type":"boolean","default":false},"alphaTestedStatics":{"type":"boolean","default":false},"emissiveStatics":{"type":"boolean","default":false},"terrain":{"type":"boolean","default":true},"parallax":{"type":"boolean","default":true}}},"modes":{"type":"object","additionalProperties":false,"required":["onPressure","continuous"],"properties":{"onPressure":{"type":"boolean","default":true},"continuous":{"type":"boolean","default":false}}},"options":{"type":"object","additionalProperties":false,"required":["reserveMiB","rememberedSizes","maximumSize"],"properties":{"reserveMiB":{"type":"integer","minimum":256,"maximum":8192,"multipleOf":256,"default":1024},"rememberedSizes":{"type":"boolean","default":true},"maximumSize":{"type":"integer","enum":[0,1024,2048,4096,8192],"default":0,"description":"Zero is unlimited; cap applies to eligible textures even nearby and without memory pressure."}}}}},"featureShortName":{"type":"string","description":"Canonical feature short name. settings also accepts CSUtility, CS Utility, OSUtility and OS Utility compatibility aliases. Legacy utility names return their old settings shape backed by Adaptive Balance; responses identify canonicalFeatureShortName and compatibilityAlias. Legacy vlIntensity reads Adaptive Balance godrayFinalBrightness (global final brightness, 0-5). Boot mutations require AdaptiveBrightness."},"mutation":{"type":"object","required":["action","expectedStateRevision","featureShortName","disabled"],"properties":{"action":{"type":"string","const":"set_disabled_at_boot"},"expectedStateRevision":{"type":"integer","minimum":0},"featureShortName":{"type":"string"},"disabled":{"type":"boolean"},"persist":{"type":"boolean"},"allowDisruptive":{"type":"boolean"},"preflightToken":{"type":"string"}}},"debugForceStreaming":{"type":"boolean","description":"Deprecated alias for modes.continuous; requires enabled streaming, now saved normally. Accepted only with texture_streaming; conflicting modes input is rejected."},"pressureSimulation":{"type":"object","additionalProperties":false,"properties":{"durationMilliseconds":{"type":"integer","minimum":0,"maximum":30000},"freeMiB":{"type":"integer","minimum":0,"maximum":65536}},"required":["durationMilliseconds"],"allOf":[{"if":{"properties":{"durationMilliseconds":{"minimum":1}},"required":["durationMilliseconds"]},"then":{"required":["freeMiB"]},"else":{"not":{"required":["freeMiB"]}}}]},"shadowCulling":{"type":"object","additionalProperties":false,"description":"Partial Light Limit Fix shadow-culling update; no settings are saved by this action.","properties":{"sun":{"type":"boolean"},"torch":{"type":"boolean"},"actors":{"type":"boolean"},"interiorTorches":{"type":"boolean"},"minimumDistanceMeters":{"type":"number","minimum":30,"maximum":300},"actorDistanceMeters":{"type":"number","minimum":50,"maximum":300}}}},"allOf":[{"if":{"properties":{"action":{"const":"texture_streaming_pressure"}},"required":["action"]},"then":{"propertyNames":{"enum":["action","contractMajor","clientId","commandId","expectedBuildId","pressureSimulation"]}},"else":{"not":{"required":["pressureSimulation"]}}}]},"outputSchema":{"type":"object","$defs":{"streamingObservations":{"type":"object","description":"Cumulative observation/check counters, not unique textures; logical retirement totals do not measure physical VRAM reclaimed.","properties":{"excludedObservations":{"type":"object","additionalProperties":{"type":"integer","minimum":0}},"blockedChecks":{"type":"object","additionalProperties":{"type":"integer","minimum":0}},"lastBlock":{"type":"string"},"nodesVisited":{"type":"integer","minimum":0},"geometryObservations":{"type":"integer","minimum":0},"textureObservations":{"type":"integer","minimum":0},"candidateRegistrations":{"type":"integer","minimum":0},"staticGeometryObservations":{"type":"integer","minimum":0},"supportedMaterialObservations":{"type":"integer","minimum":0},"measurableGeometryObservations":{"type":"integer","minimum":0},"scansCompleted":{"type":"integer","minimum":0},"demandChecks":{"type":"integer","minimum":0},"shrinkCandidates":{"type":"integer","minimum":0},"requiredRestoreCandidates":{"type":"integer","minimum":0},"retiredReplacements":{"type":"integer","minimum":0},"retiredReductions":{"type":"integer","minimum":0},"retiredLogicalReductionBytes":{"type":"integer","minimum":0},"retiredCancelledUploads":{"type":"integer","minimum":0}}},"streamingDiagnosticSample":{"type":"object","properties":{"texturePath":{"type":"string","maxLength":256},"geometryName":{"type":"string","maxLength":128}}}},"properties":{"result":{"properties":{"snapshot":{"type":"object","description":"Identity of the owned live-state copy captured for this response. Subsidiary remembered-size, restoration-cache and pressure-simulation status are independently sampled; memory retains its own telemetry sample identity.","additionalProperties":false,"required":["capturedAtMs","configurationEpoch","inventoryGeneration","deviceGeneration","lastServiceAttemptFrame"],"properties":{"capturedAtMs":{"type":"integer","minimum":0,"description":"Monotonic milliseconds when streaming state was copied under its mutex."},"configurationEpoch":{"type":"integer","minimum":0,"description":"Streaming configuration and lifecycle epoch at capture."},"inventoryGeneration":{"type":"integer","minimum":0,"description":"Authoritative consumer inventory generation at capture."},"deviceGeneration":{"type":"integer","minimum":0,"description":"Device generation recorded by the streaming service at capture."},"lastServiceAttemptFrame":{"type":["integer","null"],"minimum":0,"maximum":4294967295,"description":"Last frame admitted to the streaming service; null before a frame is recorded. This does not indicate completion of maintenance or GPU work."}}},"diagnostics":{"$ref":"#/$defs/streamingObservations","description":"measurableGeometryObservations counts valid world bounds; CPU vertex availability does not govern admission."},"inventory":{"type":"object","description":"Cached inventory and requested mip changes; desired counts are not admission decisions.","properties":{"scan":{"type":"integer","minimum":0},"completeScan":{"type":"integer","minimum":0},"frontierNodes":{"type":"integer","minimum":0},"desiredShrinks":{"type":"integer","minimum":0},"desiredRestores":{"type":"integer","minimum":0}}},"demand":{"type":"object","properties":{"eyeCount":{"type":"integer","minimum":0},"sourceFrame":{"type":"integer","minimum":0},"sampledAtMs":{"type":"integer","minimum":0},"mipBias":{"type":"number"}}},"pressureEpisode":{"type":"object","properties":{"active":{"type":"boolean"},"startedAtMs":{"type":"integer","minimum":0},"summaryReported":{"type":"boolean"},"committedShrinks":{"type":["integer","null"],"minimum":0},"committedRestores":{"type":["integer","null"],"minimum":0},"observations":{"anyOf":[{"$ref":"#/$defs/streamingObservations"},{"type":"null"}]}}},"memory":{"type":"object","properties":{"recoveryDemandBytes":{"type":"integer","minimum":0,"description":"Soft future recovery demand; only optional refill is constrained."},"physicalBudgetBytes":{"type":"integer","minimum":0,"description":"Physical DXGI budget before the streaming-only test limit."},"pressureSimulated":{"type":"boolean","description":"This streaming observation applied an active lower-budget test; physical usage, pending bytes and other owners remain unchanged."}}},"categories":{"type":"object","additionalProperties":false,"properties":{"landscapeStatics":{"type":"boolean","default":false},"alphaTestedStatics":{"type":"boolean","default":false},"emissiveStatics":{"type":"boolean","default":false},"terrain":{"type":"boolean","default":true},"parallax":{"type":"boolean","default":true}}},"materialDiagnostics":{"type":"object","description":"Developer-only cumulative first-failed classification observations and bounded representatives for unprotected static geometry.","properties":{"reasons":{"type":"object","additionalProperties":{"type":"integer","minimum":0}},"geometryTypes":{"type":"object","additionalProperties":{"type":"integer","minimum":0}},"disallowedFlagBits":{"type":"object","additionalProperties":{"type":"integer","minimum":0}},"samples":{"type":"object","additionalProperties":{"type":"object","properties":{"geometryName":{"type":"string","maxLength":128},"geometryType":{"type":"integer","minimum":0,"maximum":255},"shaderFlags":{"type":"integer","minimum":0},"disallowedFlags":{"type":"integer","minimum":0},"materialType":{"type":["integer","null"]},"materialFeature":{"type":["integer","null"]}}}}}},"debugForceStreaming":{"type":"boolean","description":"Deprecated mirror of modes.continuous."},"reductionRequested":{"type":"boolean","description":"Enabled distance streaming or size cap. Eligibility, demand and GPU safety still apply."},"textureDiagnostics":{"type":"object","description":"Developer-only cumulative first-failed resource contract checks and one bounded representative per result; unavailable DirectX descriptions are null.","properties":{"reasons":{"type":"object","additionalProperties":{"type":"integer","minimum":0}},"samples":{"type":"object","additionalProperties":{"type":"object","properties":{"texturePath":{"type":"string","maxLength":256},"unorderedAccess":{"type":"boolean"},"viewAvailable":{"type":"boolean"},"engineWidth":{"type":"integer","minimum":0,"description":"Zero means unavailable optional wrapper metadata."},"engineHeight":{"type":"integer","minimum":0,"description":"Zero means unavailable optional wrapper metadata."},"engineMips":{"type":"integer","minimum":0,"description":"Zero means unavailable optional wrapper metadata."},"texture":{"type":["object","null"],"properties":{"width":{"type":"integer","minimum":0},"height":{"type":"integer","minimum":0},"mipLevels":{"type":"integer","minimum":0},"arraySize":{"type":"integer","minimum":0},"format":{"type":"integer","minimum":0},"sampleCount":{"type":"integer","minimum":0},"sampleQuality":{"type":"integer","minimum":0},"usage":{"type":"integer","minimum":0},"bindFlags":{"type":"integer","minimum":0},"cpuAccessFlags":{"type":"integer","minimum":0},"miscFlags":{"type":"integer","minimum":0}}},"view":{"type":["object","null"],"properties":{"format":{"type":["integer","null"],"minimum":0},"dimension":{"type":["integer","null"],"minimum":0},"firstMip":{"type":["integer","null"],"minimum":0},"mipLevels":{"type":["integer","null"],"minimum":0}}}}}}}},"demandDiagnostics":{"type":"object","description":"Developer-only projected-bound demand observations after consumer validation; requiredEdge null means invalid or unavailable demand. One bounded first sample per result.","properties":{"reasons":{"type":"object","additionalProperties":{"type":"integer","minimum":0}},"samples":{"type":"object","additionalProperties":{"type":"object","properties":{"texturePath":{"type":"string","maxLength":256},"geometryName":{"type":"string","maxLength":128},"requiredEdge":{"type":["number","null"]},"fullWidth":{"type":"integer","minimum":0},"fullHeight":{"type":"integer","minimum":0},"mipLevels":{"type":"integer","minimum":0},"desiredDrop":{"type":"integer","minimum":0,"maximum":3},"consumers":{"type":"integer","minimum":0},"boundCenter":{"type":"array","minItems":3,"maxItems":3,"items":{"type":"number"}},"boundRadius":{"type":"number"},"currentFrame":{"type":"integer","minimum":0},"sourceFrame":{"type":"integer","minimum":0},"sampledAtMs":{"type":"integer","minimum":0},"mipBias":{"type":"number"},"eyeCount":{"type":"integer","minimum":0},"eyes":{"type":"array","maxItems":2,"items":{"type":"object","properties":{"position":{"type":"array","minItems":3,"maxItems":3,"items":{"type":"number"}},"renderWidth":{"type":"number"},"renderHeight":{"type":"number"},"outputWidth":{"type":"number"},"outputHeight":{"type":"number"},"projectionX":{"type":"number"},"projectionY":{"type":"number"}}}}}}}}},"transfers":{"type":"object","description":"Cumulative committed transfer counts and actual completed source reads; no physical memory or FPS claim.","properties":{"gpuCopyShrinks":{"type":"integer","minimum":0},"cachedRestores":{"type":"integer","minimum":0},"ddsRestores":{"type":"integer","minimum":0},"sourceHeaderReads":{"type":"integer","minimum":0},"sourcePayloadReads":{"type":"integer","minimum":0},"sourceBytesRead":{"type":"integer","minimum":0}}},"restorationCache":{"type":"object","description":"Bounded immutable CPU pixels, independent of GPU memory accounting; evicted pixels may remain pinned by one active restoration.","properties":{"limitBytes":{"type":"integer","minimum":0},"bytes":{"type":"integer","minimum":0},"entries":{"type":"integer","minimum":0},"generation":{"type":"integer","minimum":0},"hits":{"type":"integer","minimum":0},"misses":{"type":"integer","minimum":0},"evictions":{"type":"integer","minimum":0},"storeFailures":{"type":"integer","minimum":0}}},"modes":{"type":"object","additionalProperties":false,"required":["onPressure","continuous"],"properties":{"onPressure":{"type":"boolean","default":true},"continuous":{"type":"boolean","default":false}}},"externalStreamingConflict":{"type":"boolean","description":"Loaded SPS.dll inhibits CSX streaming before installing its DDS hook."},"unavailableReason":{"type":"string","description":"Feature load/conflict warning, empty when no warning is published."},"options":{"type":"object","additionalProperties":false,"required":["reserveMiB","rememberedSizes","maximumSize"],"properties":{"reserveMiB":{"type":"integer","minimum":256,"maximum":8192,"multipleOf":256,"default":1024},"rememberedSizes":{"type":"boolean","default":true},"maximumSize":{"type":"integer","enum":[0,1024,2048,4096,8192],"default":0,"description":"Zero is unlimited; cap applies to eligible textures even nearby and without memory pressure."}}},"rememberedSizes":{"type":"object","description":"Bounded persistent metadata for previously validated DDS sources. Reduced loads verify the current header and length and retain full-source restoration metadata. First-time, protected and externally capped loads are unchanged.","properties":{"hookReady":{"type":"boolean"},"ready":{"type":"boolean"},"entries":{"type":"integer"},"loadedReduced":{"type":"integer"},"rejectedSources":{"type":"integer"},"failures":{"type":"integer"},"activeLoads":{"type":"integer"},"unsaved":{"type":"boolean"},"error":{"type":"string"},"protectionLimit":{"type":"boolean","description":"Protection inventory is full; remembered loads fail closed for this session."},"previewLoadGuardActive":{"type":"boolean","description":"Inventory preview loading bypasses remembered dimension reductions."}}},"distanceReductionRequested":{"type":"boolean","description":"Enabled continuous mode or the selected pressure trigger; excludes the independent size cap."},"terrainDiagnostics":{"type":"object","description":"Developer-only terrain attribution; counts operations, not unique textures. A shared terrain/static resource counts as terrain. Retirement totals are logical capacity, not measured physical VRAM.","properties":{"committedShrinks":{"type":"integer","minimum":0},"committedRestores":{"type":"integer","minimum":0},"retiredReductions":{"type":"integer","minimum":0},"retiredLogicalReductionBytes":{"type":"integer","minimum":0},"materials":{"type":"object","description":"Developer-only cumulative first-failed classification observations and bounded representatives for unprotected static geometry.","properties":{"reasons":{"type":"object","additionalProperties":{"type":"integer","minimum":0}},"geometryTypes":{"type":"object","additionalProperties":{"type":"integer","minimum":0}},"disallowedFlagBits":{"type":"object","additionalProperties":{"type":"integer","minimum":0}},"samples":{"type":"object","additionalProperties":{"type":"object","properties":{"geometryName":{"type":"string","maxLength":128},"geometryType":{"type":"integer","minimum":0,"maximum":255},"shaderFlags":{"type":"integer","minimum":0},"disallowedFlags":{"type":"integer","minimum":0},"materialType":{"type":["integer","null"]},"materialFeature":{"type":["integer","null"]}}}}}},"lastReduction":{"type":["object","null"],"description":"Last committed terrain-consumer reduction; GPU completion is reported separately by retiredReductions.","properties":{"texturePath":{"type":"string","maxLength":256},"resourceSerial":{"type":"integer","minimum":0},"requiredCategories":{"type":"integer","minimum":0},"fullWidth":{"type":"integer","minimum":0},"fullHeight":{"type":"integer","minimum":0},"width":{"type":"integer","minimum":0},"height":{"type":"integer","minimum":0},"previousDrop":{"type":"integer","minimum":0},"drop":{"type":"integer","minimum":0}}},"records":{"type":"integer","minimum":0},"reducedTextures":{"type":"integer","minimum":0},"desiredShrinks":{"type":"integer","minimum":0},"inventory":{"$ref":"#/$defs/streamingObservations"},"exclusionSamples":{"type":"object","additionalProperties":{"$ref":"#/$defs/streamingDiagnosticSample"}},"blockedSamples":{"type":"object","additionalProperties":{"$ref":"#/$defs/streamingDiagnosticSample"}},"textures":{"type":"object","description":"Terrain-only texture contract observations, using textureDiagnostics semantics."},"demand":{"type":"object","description":"Terrain-only stereo demand observations, using demandDiagnostics semantics."}}},"pressureSimulation":{"type":"object","description":"DevBench-only streaming test configuration. active describes the timer; memory.pressureSimulated identifies an applied observation. Expiry retains the last configuration for inspection but restores physical sampling.","properties":{"active":{"type":"boolean"},"headroomMiB":{"type":"integer","minimum":0,"maximum":65536},"budgetBytes":{"type":"integer","minimum":0},"startedAtMs":{"type":"integer","minimum":0},"expiresAtMs":{"type":"integer","minimum":0},"remainingMilliseconds":{"type":"integer","minimum":0,"maximum":30000},"deviceGeneration":{"type":"integer","minimum":0}}},"requestedEnabled":{"type":"boolean","description":"Persistent master switch; enabled reports effective state after conflict inhibition."},"settings":{"type":"object","properties":{"sun":{"type":"boolean"},"torch":{"type":"boolean"},"actors":{"type":"boolean"},"interiorTorches":{"type":"boolean"},"minimumDistanceMeters":{"type":"number","minimum":30,"maximum":300},"actorDistanceMeters":{"type":"number","minimum":50,"maximum":300}},"additionalProperties":false},"available":{"type":"boolean"},"spsConflict":{"type":"boolean"},"error":{"type":"string"},"active":{"type":"boolean"},"shadowCameras":{"type":"integer","minimum":0},"eyes":{"type":"integer","minimum":0,"maximum":2},"sunSkipped":{"type":"integer","minimum":0},"torchSkipped":{"type":"integer","minimum":0},"npcSkipped":{"type":"integer","minimum":0},"previewActive":{"type":"boolean"},"maximumPayloadMiB":{"type":"integer","const":128}}}}}})";
		host->RegisterTool("communityshaders.feature_api", descriptor, &Handler, nullptr);
		g_registered.store(true, std::memory_order_release);
		logger::info("FeatureDevBenchBridge: registered communityshaders.feature_api with devbench build {}", host->GetBuildNumber());
	}
	bool IsRegistered() { return g_registered.load(std::memory_order_acquire); }
}

#else
namespace CSX::Api::FeatureDevBenchBridge
{
	void Install() {}
	bool IsRegistered() { return false; }
}
#endif
