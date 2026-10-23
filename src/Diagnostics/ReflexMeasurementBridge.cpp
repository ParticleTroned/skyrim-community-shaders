#include "Diagnostics/ISLDiagnosticsInternal.h"
#include "Diagnostics/ReflexMeasurementInternal.h"
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "BuildProvenance.h"
#	include "CSEditor/EditorWindow.h"
#	include "Features/Upscaling.h"
#	include "Globals.h"
#	include <DevBenchAPI.h>

namespace CSX::Diagnostics::ReflexMeasurement
{
	namespace
	{
		json Scene()
		{
			json result{ { "cellFormId", nullptr }, { "position", nullptr }, { "weatherFormId", nullptr },
				{ "gamePaused", globals::game::ui ? json(globals::game::ui->GameIsPaused()) : json(nullptr) } };
			if (auto* player = RE::PlayerCharacter::GetSingleton()) {
				const auto p = player->GetPosition();
				if (std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z))
					result["position"] = { p.x, p.y, p.z };
				if (auto* cell = player->GetParentCell())
					result["cellFormId"] = cell->GetFormID();
			}
			if (auto* sky = globals::game::sky; sky && sky->currentWeather)
				result["weatherFormId"] = sky->currentWeather->GetFormID();
			result["unavailableReason"] = "null scene fields indicate unavailable game state; positions use Skyrim world units";
			auto& upscaling = globals::features::upscaling;
			result["upscaling"] = { { "selectedMethod", static_cast<uint32_t>(upscaling.GetUpscaleMethod()) },
				{ "qualityMode", upscaling.settings.qualityMode }, { "dlssPreset", upscaling.settings.dlssPreset },
				{ "renderScaleMode", upscaling.settings.renderScaleMode }, { "frameGenerationMode", upscaling.settings.frameGenerationMode },
				{ "frameLimitMode", upscaling.settings.frameLimitMode } };
			return result;
		}
		json Condition()
		{
			return OnMain([] {
				auto& s = State();
				{
					std::scoped_lock lock(s.preferences);
					s.lease.Observe(CurrentPreferences());
					s.preferenceConflict = s.lease.conflict;
				}
				const auto b = Backend().InspectDiagnosticBinding();
				const auto p = CurrentPreferences();
				const bool constrained = globals::features::upscaling.IsFrameGenerationDx12PathActive();
				const auto& r = b.submitted;
				const bool accepted = b.status.acceptedValid && !b.status.pending && b.status.health == Reflex::Health::Healthy &&
				                      b.status.accepted == EffectiveOptions(p, constrained);
				return json{ { "requested", PreferencesJson(p) }, { "backend", StatusJson(b.status) }, { "acceptedCondition", accepted },
					{ "scene", Scene() },
					{ "gameHour", globals::game::calendar ? json(globals::game::calendar->GetHour()) : json(nullptr) },
					{ "frameGenerationConstraint", constrained }, { "frame", globals::state ? json(globals::state->frameCount) : json(nullptr) }, { "qpc", Clock() },
					{ "lastSettingsSubmission", b.submissionEnter ? json{ { "version", r.version }, { "size", sizeof(r) },
																		{ "bLowLatencyMode", r.bLowLatencyMode }, { "bLowLatencyBoost", r.bLowLatencyBoost }, { "minimumIntervalUs", r.minimumIntervalUs },
																		{ "bUseMarkersToOptimize", r.bUseMarkersToOptimize }, { "bUseMinQueueTime", r.bUseMinQueueTime }, { "result", b.submissionResult },
																		{ "generation", b.status.deviceGeneration }, { "enterQpc", b.submissionEnter }, { "exitQpc", b.submissionExit } } :
																	json(nullptr) } };
			});
		}
		json Prepare(const json& args)
		{
			auto& s = State();
			if (s.leaseActive)
				throw std::logic_error("restore or resolve the existing preference lease first");
			if (s.journal.Outstanding())
				throw std::logic_error("previous capture still has admitted work in flight");
			if (!s.captureReleased)
				throw std::logic_error("previous owner must finish export and acknowledge it before a new session");
			// Inspect before acquiring ownership so a failed inspection cannot strand a lease.
			const auto purpose = args.value("purpose", std::string("reflex"));
			if (purpose != "reflex" && purpose != "isl")
				throw std::invalid_argument("purpose must be reflex or isl");
			auto environment = Environment();
			OnMain([&] {
				std::scoped_lock lock(s.preferences);
				const auto p = CurrentPreferences();
				if (!std::isfinite(p.fps))
					throw std::runtime_error("original FPS preference is not finite; repair before measurement");
				if (purpose == "isl")
					ISL::PrepareSession();
				s.islPurpose = purpose == "isl";
				s.lease.Acquire(p);
				s.leaseActive = true;
				s.preferenceConflict = false;
				return json{ { "acquired", true } };
			});
			s.owner = args.at("clientId").get<std::string>();
			++s.session;
			s.preparedAt = Clock();
			s.environment = std::move(environment);
			return { { "sessionId", s.session }, { "environment", s.environment }, { "restoration", LeaseJson() } };
		}
		json Configure(const json& args)
		{
			auto& s = State();
			const auto mode = args.at("reflexMode").get<std::string>();
			const auto fps = args.at("capFps").get<float>();
			if ((mode != "off" && mode != "on" && mode != "boost") || !std::isfinite(fps) || (fps != 0 && (fps < 20 || fps > 240)))
				throw std::invalid_argument("mode must be off/on/boost; capFps must be 0 or 20..240");
			return OnMain([&] {
				std::scoped_lock lock(s.preferences);
				const auto current = CurrentPreferences();
				Preferences next{ mode != "off", mode == "boost", fps != 0, false, fps == 0 ? current.fps : fps };
				if (!s.lease.Write(current, next))
					throw std::runtime_error("preference lease inactive, restoring, or conflicted; user changes preserved");
				ApplyPreferences(next);
				return json{ { "requested", PreferencesJson(next) }, { "backend", StatusJson(Backend().GetStatus()) } };
			});
		}
		json Start(const json& args)
		{
			auto& s = State();
			if (s.journal.Outstanding() || !s.captureReleased)
				throw std::logic_error("previous capture is unfinished or its export is unacknowledged");
			const auto selected = args.at("mode").get<std::string>();
			const auto duration = args.at("durationMs").get<uint32_t>();
			const auto capacity = args.at("capacity").get<size_t>();
			if (capacity < 16 || capacity > 262144)
				throw std::invalid_argument("capacity must be 16..262144");
			if ((selected != "A" && selected != "B" && selected != "C") || duration < 100 || duration > 300000)
				throw std::invalid_argument("mode A/B/C; durationMs 100..300000");
			s.condition = Condition();
			const bool probe = args.value("probe", false);
			s.condition["probe"] = probe;
			s.condition["purpose"] = s.islPurpose ? "isl" : "reflex";
			if (s.islPurpose) {
				const auto isl = OnMain([] { return ISL::ConditionJson(); });
				if (!isl.at("ready").get<bool>() || !ISL::Data().sessionActive)
					throw std::runtime_error("ISL gates or session not ready");
				s.condition["isl"] = isl;
				const auto& p = s.condition.at("requested");
				s.condition["nativeChannel"] = !p.at("enabled").get<bool>() && !p.at("capped").get<bool>() ? "NOT_REQUESTED" : s.condition.at("acceptedCondition").get<bool>() ? "AVAILABLE" :
				                                                                                                                                                                 "UNAVAILABLE";
			}
			{
				std::scoped_lock lock(s.preferences);
				if (!s.leaseActive || s.lease.conflict || s.lease.restoring)
					throw std::runtime_error("preference lease unavailable");
			}
			if (!s.islPurpose && !probe && (!s.condition.at("acceptedCondition").get<bool>() || s.condition.at("frameGenerationConstraint").get<bool>() || s.condition.at("lastSettingsSubmission").is_null() || s.condition["lastSettingsSubmission"]["result"] != NVAPI_OK || s.condition["lastSettingsSubmission"]["bUseMarkersToOptimize"] != 0))
				throw std::runtime_error("condition lacks native acceptance and actual settings receipt; capture refused");
			if (!s.islPurpose && !probe && s.condition.at("backend").at("generation") != s.environment.at("backend").at("generation"))
				throw std::runtime_error("native generation changed since identity inspection; restore and prepare a fresh session");
			s.queryPeriodMs = args.value("queryPeriodMs", 1000u);
			s.admissionDelayMs = args.value("admissionDelayMs", 2500u);
			if (s.queryPeriodMs < 100 || s.queryPeriodMs > 10000)
				throw std::invalid_argument("queryPeriodMs must be 100..10000");
			if (s.admissionDelayMs < 1000 || s.admissionDelayMs > 30000)
				throw std::invalid_argument("admissionDelayMs must be 1000..30000");
			s.capturePreferenceConflict = false;
			s.controlCallsDuringCapture = 0;
			s.captureControlCalls = 0;
			s.cursor = {};
			s.sourceStart = s.sourceTarget = 0;
			s.sourceStartQpc = s.sourceStopQpc = 0;
			s.maxHistoryReturned = s.lastHistoryReturned = 0;
			if (args.value("latency", false))
				throw std::invalid_argument(LatencyLimitation);
			s.workerError.clear();
			s.termination = "running";
			s.queries.clear();
			s.queries.reserve(QueryCapacity * 2);
			s.queryStarted.clear();
			s.queryStarted.reserve(QueryCapacity);
			s.readbackRequested = args.value("readback", false);
			if (s.readbackRequested && selected != "C")
				throw std::invalid_argument("queries during acquisition require explicitly selected mode C");
			LARGE_INTEGER frequency{};
			if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
				throw std::runtime_error("QPC frequency unavailable");
			s.queryPeriodTicks = frequency.QuadPart * s.queryPeriodMs / 1000;
			s.nextQueryAfter = 0;
			s.queryEnabled = selected == "C" && s.readbackRequested;
			s.compositorRequested = selected != "A";
			s.responseWritten = false;
			s.captureSession = 0;
			s.captureOwner = s.owner;
			s.requestedDurationMs = duration;
			s.requestedCapacity = capacity;
			s.journal.Start(selected == "A" ? Mode::Minimal : selected == "B" ? Mode::Passive :
																				Mode::Queried,
				capacity, Clock(), false);
			s.captureSession = s.session;
			try {
				if (s.islPurpose)
					OnMain([&] { ISL::PrepareCapture(s.journal.id, capacity); return json{}; });
			} catch (...) {
				s.captureSession = 0;
				s.journal.CloseTimed(Clock);
				throw;
			}
			s.captureReleased = false;
			s.rawAcknowledged = false;
			s.contiguousExport = 0;
			s.acquiring = true;
			try {
				s.worker = std::jthread([duration](std::stop_token token) {
					auto& state = State();
					const SKSE::stl::scope_exit stopped([&] { state.acquiring.store(false, std::memory_order_release); });
					try {
						Run(token, duration);
					} catch (const std::exception& error) {
						state.workerError = error.what();
					} catch (...) {
						state.workerError = "unhandled capture finalization failure";
					}
					if (state.islPurpose)
						ISL::CloseCapture();
					if (state.journal.active)
						state.journal.CloseTimed(Clock);
				});
			} catch (...) {
				s.acquiring = false;
				s.termination = "admission_failed";
				s.workerError = "capture worker could not start";
				s.journal.CloseTimed(Clock);
				FreezeCapture(Clock(), false, false);
				throw;
			}
			s.startPending = true;
			return { { "captureId", s.journal.id }, { "durationMs", duration }, { "mode", selected }, { "condition", s.condition },
				{ "admissionDelayMs", s.admissionDelayMs },
				{ "baselineMeaning", selected == "A" ? "minimal application observer; not fully disabled" : "instrumented observer" } };
		}
		json Stop(const json& args)
		{
			auto& s = State();
			if (s.islPurpose)
				ISL::CloseCapture();
			if (s.journal.active)
				s.journal.CloseTimed(Clock);
			if (s.worker.joinable())
				s.worker.request_stop();
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(PostrollMs + 1000);
			while (s.acquiring && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(10));
			if (s.acquiring)
				return { { "captureId", s.journal.id }, { "frozen", false }, { "stopPending", true }, { "reason", "worker has not completed; ownership and evidence retained" } };
			if (s.worker.joinable())
				s.worker.join();
			if (s.termination == "running")
				s.termination = "cancelled";
			return { { "captureId", s.journal.id }, { "frozen", s.journal.frozen }, { "termination", s.termination }, { "workerError", s.workerError },
				{ "released", s.captureReleased }, { "requestedStopReason", args.value("reason", std::string("unspecified")) } };
		}
		json Restore()
		{
			auto& s = State();
			OnMain([&] {
				std::scoped_lock lock(s.preferences);
				if (s.islPurpose)
					ISL::RestoreLive();
				if (!(s.islPurpose && s.lease.ReleaseUnchanged(CurrentPreferences(), Clock())) && s.lease.Restore(CurrentPreferences(), Clock()))
					ApplyPreferences(s.lease.original);
				s.leaseActive = s.lease.active;
				s.preferenceConflict = s.lease.conflict;
				return json{ { "attempted", true } };
			});
			return { { "restoration", LeaseJson() }, { "remaining", Condition() } };
		}
		json Dispatch(const json& args)
		{
			auto& s = State();
			const auto action = args.at("action").get<std::string>();
			if (s.acquiring.load(std::memory_order_acquire) && action != "stop" && action != "recover_and_stop")
				throw std::logic_error("only stop is accepted during acquisition; inspect/serialize outside timing");
			if (action == "prepare")
				return Prepare(args);
			if (action == "recover_and_stop") {
				if (args.at("clientId").get<std::string>() != s.owner)
					throw std::invalid_argument("measurement owner not found");
				if (s.captureSession != s.session)
					return json{ { "sessionId", s.session }, { "captureId", nullptr }, { "reason", "no capture owned by this session" } };
				auto result = Stop(args);
				result["sessionId"] = s.session;
				return result;
			}
			if (args.at("clientId").get<std::string>() != s.owner || args.at("sessionId").get<uint64_t>() != s.session)
				throw std::invalid_argument("measurement ownership mismatch");
			if (action == "stop" || action == "export" || action == "acknowledge_export") {
				if (args.at("captureId").get<uint64_t>() != s.journal.id || s.captureSession != s.session || s.captureOwner != s.owner)
					throw std::invalid_argument("capture ownership mismatch");
			}
			if (action == "stop")
				return Stop(args);
			if (s.worker.joinable())
				s.worker.join();
			if (action == "configure")
				return Configure(args);
			if (action == "condition")
				return Condition();
			if (action == "set_noon")
				return OnMain([] {
					auto* calendar = globals::game::calendar;
					if (!calendar || !calendar->gameHour)
						throw std::runtime_error("game calendar unavailable");
					const float previous = calendar->GetHour();
					if (globals::game::sky && EditorWindow::GetSingleton()->IsWeatherLocked())
						globals::game::sky->lastWeatherUpdate = 12.0f;
					calendar->gameHour->value = 12.0f;
					return json{ { "previousGameHour", previous }, { "gameHour", calendar->GetHour() }, { "qpc", Clock() } };
				});
			if (action == "start")
				return Start(args);
			if (action == "export") {
				if (s.islPurpose && ISL::Data().sessionActive)
					ISL::TouchSession();
				if (s.captureReleased)
					throw std::logic_error("capture export was acknowledged and released");
				return Export(args);
			}
			if (action == "acknowledge_export") {
				const auto total = s.journal.app.size() + s.journal.native.size() + s.journal.compositor.size() + s.queries.size();
				if (!s.journal.frozen || s.contiguousExport != total || args.at("total").get<size_t>() != total)
					throw std::logic_error("complete contiguous export and matching validated total required");
				s.rawAcknowledged = true;
				s.captureReleased = !s.islPurpose || ISL::ExportAcknowledged();
				return json{ { "captureId", s.journal.id }, { "released", s.captureReleased }, { "total", total } };
			}
			if (action == "restore")
				return Restore();
			if (action == "restoration")
				return json{ { "restoration", LeaseJson() }, { "remaining", Condition() } };
			if (action == "readiness") {
				if (s.islPurpose)
					return json{ { "purpose", "isl" }, { "nativeChannel", s.condition.value("nativeChannel", "UNAVAILABLE") }, { "reflexEfficacy", "NOT_EVALUATED" }, { "rawFrozen", s.journal.frozen }, { "qualityComplete", s.journal.quality.Complete() } };
				return Readiness(args);
			}
			if (action == "readback") {
				if (!s.journal.frozen || s.captureSession != s.session || s.queries.empty())
					throw std::logic_error("readback requires a frozen C-mode capture in this session");
				return ReadbackJson(s.queries.back());
			}
			throw std::invalid_argument("unsupported action");
		}
		void Handler(void*, const char* input, void* sink, DevBenchAPI::WriteFn write) noexcept
		{
			auto& s = State();
			const auto intrusion = s.journal.BeginTimed(Clock);
			if (intrusion.admitted) {
				++s.controlCallsDuringCapture;
				s.journal.gate.fetch_sub(1, std::memory_order_release);
			}
			std::scoped_lock commandLock(s.commands);
			json output;
			try {
				const auto args = json::parse(input ? input : "{}");
				if (args.at("contractMajor") != 1 || args.at("schema") != Schema || args.at("expectedBuildId").get<std::string>().empty())
					throw std::invalid_argument("contractMajor=1, schema=1 and expectedBuildId required");
				if (auto mismatch = BuildProvenance::ValidateExpectedBuild(args))
					throw std::invalid_argument(mismatch->dump());
				const auto owner = args.at("clientId").get<std::string>();
				if (owner.empty() || owner.size() > 128)
					throw std::invalid_argument("invalid clientId");
				output = { { "ok", true }, { "schema", Schema }, { "result", Dispatch(args) } };
				BuildProvenance::AttachProducer(output);
			} catch (const std::exception& e) {
				output = { { "ok", false }, { "error", { { "code", "measurement_failed" }, { "message", e.what() } } } };
			} catch (...) {
				output = { { "ok", false }, { "error", { { "code", "internal_error" }, { "message", "unknown measurement failure" } } } };
			}
			if (s.startPending && output.at("ok") == false) {
				s.startPending = false;
				s.worker.request_stop();
				if (s.worker.joinable() && !s.acquiring)
					s.worker.join();
			}
			try {
				const auto text = output.dump();
				write(sink, text.c_str());
				if (s.startPending) {
					s.startPending = false;
					s.responseWritten.store(true, std::memory_order_release);
				}
			} catch (...) {
				if (s.startPending) {
					s.startPending = false;
					s.worker.request_stop();
					if (s.worker.joinable() && !s.acquiring)
						s.worker.join();
				}
				write(sink, R"({"ok":false,"error":{"code":"serialization_failed"}})");
			}
		}
	}
	void Install()
	{
		static std::atomic_bool registered{};
		auto* host = DevBenchAPI::GetDevBenchInterface001();
		if (!host || registered.exchange(true))
			return;
		static constexpr auto descriptor = R"({"description":"DevBench-only shared Reflex/ISL measurement. purpose=isl preserves pacing by default, allows independently unavailable native evidence, and requires BOTH raw and ISL sidecar export acknowledgements. An unchanged ISL pacing lease releases without native restoration; changed pacing still requires accepted restoration. prepare leases preferences and inspects modules outside timing. configure/condition must confirm native acceptance before start. Bounded preallocated modes: A minimal app observer (not disabled), B passive native and independent completed OpenVR history, C separately labelled optional GetSleepStatus queries. During timing only stop is admitted; it closes admission and drains/post-rolls up to 2s. export pages frozen streams; validate all pages/counts/boundaries/losses before readiness. readiness/readback inspect frozen C-mode receipts without issuing blocking driver calls on the control thread. Getter corroboration is not cap readback or latency proof. Software latency is unsupported: no proven simulation-to-eye-GPU boundary, so no latency markers/queries are emitted. restore/restoration return requested and later native-accepted receipts and preserve observed user changes. No legacy profiler polling. Reset never resets recovery, frame guards or FG quarantine. Build/schema/client/session/capture identity is required.","inputSchema":{"type":"object","required":["contractMajor","schema","expectedBuildId","clientId","action"],"properties":{"contractMajor":{"const":1},"schema":{"const":1},"expectedBuildId":{"type":"string","minLength":1},"clientId":{"type":"string","minLength":1,"maxLength":128},"action":{"enum":["prepare","configure","condition","set_noon","start","stop","recover_and_stop","export","acknowledge_export","readiness","readback","restore","restoration"]},"sessionId":{"type":"integer","minimum":1},"captureId":{"type":"integer","minimum":1},"reflexMode":{"enum":["off","on","boost"]},"capFps":{"type":"number","minimum":0,"maximum":240},"mode":{"enum":["A","B","C"]},"durationMs":{"type":"integer","minimum":100,"maximum":300000},"capacity":{"type":"integer","minimum":16,"maximum":262144},"admissionDelayMs":{"type":"integer","minimum":1000,"maximum":30000},"queryPeriodMs":{"type":"integer","minimum":100,"maximum":10000},"purpose":{"enum":["reflex","isl"],"description":"ISL permits an independently unavailable native channel; Reflex-purpose acceptance remains strict."},"probe":{"type":"boolean","description":"Bounded acquisition check may record unavailable/early-exit paths; never authorizes efficacy conclusions."},"readback":{"type":"boolean"},"latency":{"type":"boolean"},"total":{"type":"integer","minimum":0},"offset":{"type":"integer","minimum":0},"limit":{"type":"integer","minimum":1,"maximum":256},"reason":{"type":"string"}}}})";
		host->RegisterTool("communityshaders.reflex_measurement", descriptor, &Handler, nullptr);
	}
}
#endif
