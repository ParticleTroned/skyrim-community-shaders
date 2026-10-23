#include "Diagnostics/ISLDiagnosticsInternal.h"
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "Api/ServiceFoundation.h"
#	include "BuildProvenance.h"
#	include "CSEditor/EditorWindow.h"
#	include "Diagnostics/ReflexMeasurementInternal.h"
#	include "Features/InverseSquareLighting.h"
#	include "Features/LightLimitFix/ShadowCulling.h"
#	include "Features/VR.h"
#	include "Globals.h"
#	include "State.h"
#	include <DevBenchAPI.h>

namespace CSX::Diagnostics::ISL
{
	namespace RM = ReflexMeasurement;
	namespace
	{
		CSX::Api::ServiceFoundation& Foundation()
		{
			static CSX::Api::ServiceFoundation service({ "communityshaders.isl_diagnostics", 1, 0, 1 }, { 32, 64 });
			return service;
		}
		template <class Enum>
		Enum Choice(const json& args, const char* field, std::initializer_list<const char*> choices)
		{
			const auto value = args.at(field).get<std::string>();
			size_t index = 0;
			for (const auto* choice : choices) {
				if (value == choice)
					return Enum(index);
				++index;
			}
			throw std::invalid_argument(std::string("invalid ") + field);
		}
		json Dispatch(const json& args)
		{
			auto& s = RM::State();
			auto& d = Data();
			if (args.at("schema") != 1)
				throw std::invalid_argument("schema=1 required");
			if (args.at("expectedBuildId").get<std::string>().empty())
				throw std::invalid_argument("expectedBuildId required");
			if (auto mismatch = BuildProvenance::ValidateExpectedBuild(args))
				throw std::invalid_argument(mismatch->dump());
			if (args.at("clientId") != s.owner || args.at("sessionId") != s.session || !s.islPurpose)
				throw std::invalid_argument("use the owning ISL-purpose reflex_measurement session");
			if (s.acquiring)
				throw std::logic_error("only reflex_measurement.stop is admitted during delay, capture and post-roll");
			if (d.sessionActive)
				TouchSession();
			const auto action = args.at("action").get<std::string>();
			if (action == "export")
				return Export(args);
			if (action == "acknowledge_export") {
				if (!d.frozen || args.at("captureId") != d.capture || d.contiguous != d.frozenRows.size() || args.at("total") != d.frozenRows.size())
					throw std::logic_error("complete contiguous sidecar export and validated total required");
				d.acknowledged = true;
				s.captureReleased = s.rawAcknowledged;
				return { { "captureId", d.capture }, { "sidecarAcknowledged", true }, { "released", s.captureReleased } };
			}
			return RM::OnMain([&] {
				if (action == "status" || action == "condition") {
					if (d.sessionActive)
						TouchSession();
					return ConditionJson();
				}
				if (action == "restore") {
					RestoreLive();
					return RestorationJson();
				}
				if (!d.sessionActive)
					throw std::logic_error("ISL session inactive or expired; restore and prepare again");
				TouchSession();
				if (action == "configure")
					return Configure(args);
				if (action == "stage_startup")
					return StartupStage(args);
				if (action == "arm_prune_deferral") {
					if (!s.captureReleased)
						throw std::logic_error("acknowledge both exports before arming next capture");
					const auto frames = args.at("frames").get<int64_t>();
					if (frames < 1 || frames > 240)
						throw std::invalid_argument("frames must be 1..240");
					std::scoped_lock lock(d.pruneMutex);
					if (d.pruneArm || d.prune.active)
						throw std::logic_error("deferral already armed or pending execution");
					d.pruneArm = uint32_t(frames);
					return json{ { "armed", true }, { "frames", frames }, { "activation", "next actual capture admission" }, { "maximumMs", 5000 } };
				}
				throw std::invalid_argument("unsupported ISL action");
			});
		}
		void Handler(void*, const char* input, void* sink, DevBenchAPI::WriteFn write) noexcept
		{
			auto& s = RM::State();
			if (s.acquiring.load(std::memory_order_acquire)) {
				++s.controlCallsDuringCapture;
				MarkContaminated();
			}
			std::scoped_lock lock(s.commands);
			json response;
			try {
				const auto args = json::parse(input ? input : "{}");
				response = Foundation().Dispatch(args, [](const json& request) {
					auto output = Foundation().MakeEnvelope(request, true);
					output["result"] = Dispatch(request);
					BuildProvenance::AttachProducer(output);
					return output;
				});
			} catch (const std::exception& error) {
				response = { { "ok", false }, { "error", { { "code", "isl_diagnostics_failed" }, { "message", error.what() } } } };
			} catch (...) {
				response = { { "ok", false }, { "error", { { "code", "internal_error" }, { "message", "unknown ISL diagnostic failure" } } } };
			}
			try {
				const auto text = response.dump();
				write(sink, text.c_str());
			} catch (...) {
				write(sink, R"({"ok":false,"error":{"code":"serialization_failed"}})");
			}
		}
	}
	json Configure(const json& args)
	{
		auto& d = Data();
		if (!globals::state || globals::state->IsPersistentMutationBlocked())
			throw std::logic_error("game state is not safe for configuration");
		const auto& gates = args.at("gates");
		if (!gates.is_object() || gates.size() != 5)
			throw std::invalid_argument("configure requires exactly the five live gates");
		Policy next;
		next.luminance = Choice<Luminance>(gates, "luminanceMode", { "normal", "native", "compute_discard_then_native" });
		next.renderer = Choice<Renderer>(gates, "rendererMode", { "normal", "vanilla_output", "vanilla_direct" });
		next.particle = Choice<Particle>(gates, "particleDetectionMode", { "normal", "scan_off", "bypass_before_lock" });
		next.observation = Choice<Observation>(gates, "observationLevel", { "off", "counters", "timed" });
		next.editorBypass = Choice<unsigned>(gates, "editorOverrideMode", { "normal", "bypass" }) != 0;
		const auto& editor = EditorWindow::GetSingleton()->lightEditor;
		const bool clean = !editor.IsEnabled() && !editor.HasDeferredWork();
		if (!next.Valid(d.suppressed, clean))
			throw std::invalid_argument("illegal gate combination: direct requires startup suppression, native luminance and inactive settled editor");
		next.generation = Policy::Unpack(d.requested.load()).generation + 1;
		auto& llf = globals::features::lightLimitFix;
		auto& isl = globals::features::inverseSquareLighting;
		auto shadow = llf.settings.ShadowCulling;
		if (args.contains("shadowCulling"))
			shadow = LightLimitFixDetail::ShadowCulling::Parse(args.at("shadowCulling"), shadow);
		const bool enabled = args.value("islEnabled", isl.settings.Enabled);
		if ((args.contains("shadowCulling") || args.contains("islEnabled")) &&
			(json(llf.settings.ShadowCulling) != d.ownedShadow || isl.settings.Enabled != d.ownedEnabled)) {
			d.restorationConflict = true;
			throw std::logic_error("settings changed outside this owner; preserve them and restore the session");
		}
		if (args.contains("shadowCulling") || args.contains("islEnabled")) {
			d.settingsOwned = true;
			llf.settings.ShadowCulling = shadow;
			d.ownedShadow = shadow;
			if (isl.settings.Enabled != enabled)
				isl.SetPerformanceCostMeasurementEnabled(enabled);
			d.ownedEnabled = enabled;
		}
		d.requested.store(next.Pack(), std::memory_order_release);
		auto result = ConditionJson();
		result["accepted"] = true;
		return result;
	}
	void Install()
	{
		static std::atomic_bool registered{};
		auto* host = DevBenchAPI::GetDevBenchInterface001();
		if (!host || registered.exchange(true))
			return;
		static constexpr auto descriptor = R"({"description":"Owned DevBench-only ISL subtraction diagnostics. First prepare reflex_measurement with purpose=isl; share its session and capture IDs. Configure publishes all five gates atomically at safe prepass; condition reports requested/effective generations and readiness. No control calls during admission delay, acquisition or post-roll. Raw capture remains schema 1 with exactly four streams; export this separate bounded sidecar and acknowledge BOTH before reuse. Startup radius suppression consumes a build-bound one-launch request and requires restart to restore. Prune deferral binds to next actual admission, never a preset. No claim of exact SteamVR Late Start reconstruction or exact application/compositor mapping. Preserve pacing and scene time unless explicitly requested.","inputSchema":{"type":"object","required":["contractMajor","schema","expectedBuildId","clientId","commandId","sessionId","action"],"properties":{"contractMajor":{"const":1},"schema":{"const":1},"expectedBuildId":{"type":"string","minLength":1},"clientId":{"type":"string","minLength":1,"maxLength":128},"commandId":{"type":"string","minLength":1,"maxLength":128},"sessionId":{"type":"integer","minimum":1},"captureId":{"type":"integer","minimum":1},"action":{"enum":["status","configure","condition","arm_prune_deferral","stage_startup","export","acknowledge_export","restore"]},"gates":{"type":"object","additionalProperties":false,"required":["luminanceMode","rendererMode","particleDetectionMode","editorOverrideMode","observationLevel"],"properties":{"luminanceMode":{"enum":["normal","native","compute_discard_then_native"]},"rendererMode":{"enum":["normal","vanilla_output","vanilla_direct"]},"particleDetectionMode":{"enum":["normal","scan_off","bypass_before_lock"]},"editorOverrideMode":{"enum":["normal","bypass"]},"observationLevel":{"enum":["off","counters","timed"]}}},"shadowCulling":{"type":"object"},"islEnabled":{"type":"boolean"},"engineRadiusWrites":{"enum":["normal","suppress_from_startup"]},"frames":{"type":"integer","minimum":1,"maximum":240},"offset":{"type":"integer","minimum":0},"limit":{"type":"integer","minimum":1,"maximum":256},"total":{"type":"integer","minimum":0}}}})";
		host->RegisterTool("communityshaders.isl_diagnostics", descriptor, &Handler, nullptr);
	}
}
#endif
