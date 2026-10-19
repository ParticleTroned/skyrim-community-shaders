#include "ReflexDiagnosticSuite.h"

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "ReflexDiagnostics.h"
#	include "ReflexPresentation.h"
#	include "ReflexReport.h"
#	include "ReflexTestSettings.h"
#	include "ReflexRunState.h"
#	include "Api/DevBenchMainThreadDispatch.h"
#	include "BuildProvenance.h"
#	include "Features/Upscaling.h"
#	include "Menu.h"
#	include "RE/S/SendHUDMessage.h"
#	include "ShaderCache.h"
#	include "Utils/FileSystem.h"
#	include "Utils/StringUtils.h"
#	include "Utils/WinApi.h"
#	include <atomic>
#	include <chrono>
#	include <filesystem>
#	include <sstream>
#	include <thread>

namespace ReflexDiagnostics::Suite
{
	using json = nlohmann::json;
	namespace
	{
		struct Runtime
		{
			RunState state;
			std::atomic_bool cancel{ false }, recovering{ false };
			std::mutex launchMutex, statusMutex, savedMutex;
			json status{ { "state", "idle" }, { "message", "Ready. No DevBench required." } };
			SavedSettings saved;
			std::jthread worker;
		};
		Runtime& GetRuntime()
		{
			static Runtime runtime;
			return runtime;
		}
		void Status(const char* state, const std::string& message, const std::filesystem::path& path = {})
		{
			auto& runtime = GetRuntime();
			std::scoped_lock lock(runtime.statusMutex);
			runtime.status["state"] = state;
			runtime.status["message"] = message;
			if (!path.empty())
				runtime.status["directory"] = Util::PathToUtf8(path);
			logger::info("[ReflexReport] {}: {}", state, message);
		}
		bool Wait(std::stop_token token, double seconds, bool allowCancel = true)
		{
			const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
			while (std::chrono::steady_clock::now() < end) {
				if (token.stop_requested() || (allowCancel && GetRuntime().cancel) || globals::game::quitGame)
					return false;
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
			return true;
		}
		void Write(const std::filesystem::path& path, const std::string& data)
		{
			std::string error;
			if (!Util::FileHelpers::WriteTextFileAtomic(path, data, error, false))
				throw std::runtime_error(error);
		}
		std::filesystem::path CreateEvidenceDirectory()
		{
			const auto log = Util::PathHelpers::GetLogPath();
			if (log.empty())
				throw std::runtime_error("SKSE log directory unavailable");
			const auto directory = log.parent_path() / std::format("CSX-Reflex-{}-{}", GetCurrentProcessId(), Tick());
			if (!std::filesystem::create_directory(directory))
				throw std::runtime_error("Could not create unique Reflex evidence directory");
			return directory;
		}
		json MainTask(std::function<json()> run)
		{
			return CSX::Api::RunDevBenchMainThreadTask(SKSE::GetTaskInterface(), std::move(run));
		}
		json ApplyCondition(const Report::Condition& condition)
		{
			return MainTask([condition] {
				const auto calendar = globals::game::calendar;
				const auto player = RE::PlayerCharacter::GetSingleton();
				if (!calendar || !calendar->gameHour || !calendar->timeScale || !player || !player->GetParentCell() ||
					!globals::game::ui || globals::game::ui->GameIsPaused() || !globals::menu || globals::menu->IsEnabled ||
					!globals::shaderCache || globals::shaderCache->IsCompiling())
					return json{ { "error", "Load a save, finish shader compilation and close all menus before the countdown ends." } };
				const auto originalSettings = globals::features::upscaling.CapturePerformanceSettingsState();
				auto& runtime = GetRuntime();
				std::scoped_lock lock(runtime.savedMutex);
				auto& s = globals::features::upscaling.settings;
				runtime.saved.Capture(s, calendar->GetHour(), calendar->GetTimescale());
				s.reflexLowLatencyMode = condition.mode != 0;
				s.reflexLowLatencyBoost = condition.mode == 2;
				s.reflexUseFPSLimit = condition.cap;
				s.reflexFPSLimit = 20;
				calendar->gameHour->value = 12;
				calendar->timeScale->value = 0;
				RE::SendHUDMessage::ShowHUDMessage(std::format("Reflex test: {}", condition.name).c_str(), nullptr, true);
				return json{ { "condition", condition.name }, { "gameHour", calendar->GetHour() },
					{ "timeScale", calendar->GetTimescale() }, { "cellId", player->GetParentCell()->GetFormID() },
					{ "previousUpscalingSettings", originalSettings },
					{ "upscalingSettings", globals::features::upscaling.CapturePerformanceSettingsState() } };
			});
		}
		json Restore()
		{
			auto& runtime = GetRuntime();
			std::scoped_lock lock(runtime.savedMutex);
			if (!runtime.saved.valid)
				return { { "restored", true }, { "changed", false } };
			const auto calendar = globals::game::calendar;
			if (!calendar || !calendar->gameHour || !calendar->timeScale)
				return { { "error", "calendar unavailable for restoration" } };
			const auto saved = runtime.saved;
			auto& settings = globals::features::upscaling.settings;
			runtime.saved.Restore(settings, calendar->gameHour->value, calendar->timeScale->value);
			return { { "restored", true }, { "changed", true }, { "lowLatency", saved.lowLatency }, { "boost", saved.boost },
				{ "cap", saved.cap }, { "fps", saved.fps }, { "gameHour", saved.hour }, { "timeScale", saved.timeScale } };
		}
		json FinishCapture(uint64_t session)
		{
			(void)DispatchOwned({ { "action", "reflex_trace_status" }, { "expectedSessionId", session } });
			auto stopped = DispatchOwned({ { "action", "reflex_trace_stop" }, { "expectedSessionId", session } });
			if (stopped.contains("error"))
				throw std::runtime_error(stopped.dump());
			for (int attempt = 0; (stopped["capture"].value("inFlight", 0u) || !Presentation::Drained()) && attempt < 100; ++attempt) {
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
				stopped = DispatchOwned({ { "action", "reflex_trace_status" }, { "expectedSessionId", session } });
			}
			if (stopped["capture"].value("inFlight", 1u) || !Presentation::Drained())
				throw std::runtime_error("Reflex hook did not drain; retained evidence was not reset");
			auto result = stopped.at("capture");
			result["environmentAtEnd"] = Environment(false);
			uint64_t after = 0;
			while (after < result.at("retainedRecords").get<uint64_t>()) {
				auto page = DispatchOwned({ { "action", "reflex_trace_read" }, { "expectedSessionId", session }, { "afterSequence", after }, { "limit", 256 } });
				if (page.contains("error"))
					throw std::runtime_error(page.dump());
				const auto& records = page["capture"]["records"];
				if (records.empty())
					throw std::runtime_error("Reflex pagination ended before retained evidence was read");
				after = records.back().at("sequence").get<uint64_t>();
				for (const auto& record : records) result["records"].push_back(record);
			}
			uint64_t presentAfter = 0;
			while (presentAfter < result.at("desktopPresents").at("retainedRecords").get<uint64_t>()) {
				auto page = DispatchOwned({ { "action", "reflex_trace_read" }, { "expectedSessionId", session },
					{ "afterSequence", after }, { "afterPresentSequence", presentAfter }, { "limit", 256 } });
				if (page.contains("error"))
					throw std::runtime_error(page.dump());
				const auto& records = page["capture"]["desktopPresents"]["records"];
				if (records.empty())
					throw std::runtime_error("Present pagination ended before retained evidence was read");
				presentAfter = records.back().at("sequence").get<uint64_t>();
				for (const auto& record : records) result["desktopPresents"]["records"].push_back(record);
			}
			return result;
		}
		void Recover(uint64_t session)
		{
			(void)Util::SetCurrentThreadCooperativeBackgroundPriority();
			std::filesystem::path directory;
			try {
				directory = CreateEvidenceDirectory();
				Status("recovering", "Exporting the retained capture before releasing it.", directory);
				const auto capture = FinishCapture(session);
				Write(directory / "recovered_capture.json", capture.dump());
				const auto reset = DispatchOwned({ { "action", "reflex_trace_reset" }, { "expectedSessionId", session } });
				if (reset.contains("error"))
					throw std::runtime_error(reset.dump());
				Status("recovered", "Retained capture exported. Send this folder with the original report, then start a new test if needed.", directory);
			} catch (const std::exception& error) {
				Status("recovery_failed", error.what(), directory);
			}
			GetRuntime().recovering = false;
			GetRuntime().state.WorkerFinished();
		}
		std::string TextReport(const json& report)
		{
			std::ostringstream out;
			out << "CSX REFLEX DIAGNOSTIC REPORT\nStatus: " << report.value("status", "incomplete") << "\n";
			out << "Build ID: " << BuildProvenance::GetBuildId() << "\n\n";
			out << "Presentation creation and actual state: " << report.value("presentation", json(nullptr)).dump(2) << "\n\n";
			if (report.contains("error"))
				out << "Error: " << report["error"].dump() << "\n";
			for (const auto& condition : report["conditions"]) {
				const auto& s = condition["summary"];
				out << s["condition"].get<std::string>() << ": " << s["driverAssessment"].get<std::string>() << "\n";
				out << "  Desktop presentation: " << s["presentation"].dump() << "\n";
				out << "  Journal complete: " << s["journalComplete"] << "; controlled VR coverage: " << s["controlledVrComparison"] << "\n";
				out << "  Sleep calls/failures: " << s["sleepCalls"] << "/" << s["sleepFailures"] << "; sleep mean/p95 ms: " << s["sleepMs"]["mean"] << "/" << s["sleepMs"]["p95"] << "\n";
				out << "  Client-frame p50/p95/p99/max ms: " << s["clientFrameMs"]["p50"] << "/" << s["clientFrameMs"]["p95"] << "/" << s["clientFrameMs"]["p99"] << "/" << s["clientFrameMs"]["max"] << "\n";
				out << "  Compositor dropped/mispresented/missing frames: " << s["droppedCompositorFrames"] << "/" << s["mispresentedFrames"] << "/" << s["missingCompositorFrames"] << "\n\n";
			}
			for (const auto& comparison : report["comparisons"])
				out << comparison.dump(2) << "\n";
			if (report.contains("limiterPositiveControl"))
				out << "\nLimiter positive control: " << report["limiterPositiveControl"].dump(2) << "\n";
			out << "\nRestoration: " << report.value("restoration", json(nullptr)).dump() << "\n";
			out << "Restoration verification: " << report.value("restorationVerification", json(nullptr)).dump() << "\n";
			for (const auto* field : { "captureResetError", "interruptedExportError" })
				if (report.contains(field))
					out << field << ": " << report[field].dump() << "\n";
			out << "\nDriver ON confirms enabled mode, not a guaranteed smoothness benefit. API success and sleep duration alone do not prove pacing.\n"
				<< "Compare passive and queried Off/On/Boost conditions, compositor percentiles and dropped/repeated frames.\n"
				<< "The 20 FPS condition is a positive control: client cadence near 50 ms plus accepted sleep calls supports a working limiter path.\n"
				<< "No general regression verdict is valid if coverage failed or scene/load changed. SE/AE have hook timing only, not VR compositor evidence.\n"
				<< "Newer driver fields (gameSleepSeen, sleepIntervalUs) may be zero on older drivers; missing or zero values are not automatically failure.\n";
			return out.str();
		}
		void SaveProgress(const std::filesystem::path& directory, const json& report)
		{
			Write(directory / "report.json", report.dump(2));
			Write(directory / "report.txt", TextReport(report));
		}
		json VerifyRestoration(std::stop_token token, const std::filesystem::path& directory, const json& expected)
		{
			const auto started = DispatchOwned({ { "action", "reflex_trace_start" }, { "durationSeconds", 2 }, { "queryState", true } });
			if (started.contains("error"))
				return started;
			const auto session = started["capture"]["sessionId"].get<uint64_t>();
			(void)Wait(token, 2.2, false);
			const auto capture = FinishCapture(session);
			Write(directory / "restoration_verification.json", capture.dump());
			const auto reset = DispatchOwned({ { "action", "reflex_trace_reset" }, { "expectedSessionId", session } });
			auto result = Report::RestorationAssessment(capture, expected);
			result["file"] = "restoration_verification.json";
			result["records"] = capture["retainedRecords"];
			result["reset"] = reset;
			if (reset.contains("error"))
				result["verified"] = false;
			return result;
		}
		void Run(std::stop_token token)
		{
			(void)Util::SetCurrentThreadCooperativeBackgroundPriority();
			std::filesystem::path directory;
			uint64_t session = 0;
			json report{ { "schemaVersion", 3 }, { "status", "incomplete" }, { "conditions", json::array() }, { "comparisons", json::array() } };
			try {
				directory = CreateEvidenceDirectory();
				Status("preparing", "Close the menu and remain in the same scene. Test starts after preparation and an 8-second countdown.", directory);
				const auto environment = Environment(true);
				report["presentation"] = environment["presentation"];
				Write(directory / "environment.json", environment.dump(2));
				SaveProgress(directory, report);
				if (!Wait(token, 8))
					throw std::runtime_error("cancelled before measurement");
				for (const auto& condition : Report::kConditions) {
					Status("settling", std::format("{}: applying settings, then settling for 5 seconds", condition.name));
					const auto setup = ApplyCondition(condition);
					if (setup.contains("error"))
						throw std::runtime_error(setup.dump());
					if (!Wait(token, 5))
						throw std::runtime_error("cancelled during settling");
					const auto started = DispatchOwned({ { "action", "reflex_trace_start" }, { "durationSeconds", condition.seconds }, { "queryState", condition.query } });
					if (started.contains("error"))
						throw std::runtime_error(started.dump());
					session = started["capture"]["sessionId"].get<uint64_t>();
					Status("recording", std::format("{}: recording {} seconds", condition.name, condition.seconds));
					const bool elapsed = Wait(token, condition.seconds + .2);
					// Status enforces the deadline even if a paused game stopped invoking the input hook.
					(void)DispatchOwned({ { "action", "reflex_trace_status" }, { "expectedSessionId", session } });
					auto capture = FinishCapture(session);
					const std::string filename = std::string(condition.name) + ".json";
					capture["setup"] = setup;
					capture["endSettings"] = MainTask([] { return globals::features::upscaling.CapturePerformanceSettingsState(); });
					Write(directory / filename, capture.dump());
					report["conditions"].push_back({ { "file", filename }, { "summary", Report::Analyze(capture, condition) } });
					const auto reset = DispatchOwned({ { "action", "reflex_trace_reset" }, { "expectedSessionId", session } });
					if (reset.contains("error"))
						throw std::runtime_error(reset.dump());
					session = 0;
					SaveProgress(directory, report);
					if (!elapsed)
						throw std::runtime_error("cancelled; completed and interrupted captures retained");
				}
				for (const auto base : { 0u, 4u })
					for (const auto offset : { 1u, 2u, 3u })
						report["comparisons"].push_back(Report::Compare(report["conditions"][base]["summary"], report["conditions"][base + offset]["summary"]));
				report["limiterPositiveControl"] = Report::LimiterControl(report["conditions"][4]["summary"], report["conditions"][8]["summary"]);
				report["status"] = "completed";
			} catch (const std::exception& error) {
				report["status"] = "failed_or_cancelled";
				report["error"] = error.what();
				logger::error("[ReflexReport] {}", error.what());
			}
			if (session) {
				try {
					const auto capture = FinishCapture(session);
					if (!directory.empty()) {
						Write(directory / "interrupted.json", capture.dump());
						const auto reset = DispatchOwned({ { "action", "reflex_trace_reset" }, { "expectedSessionId", session } });
						if (reset.contains("error"))
							report["captureResetError"] = reset;
					}
				} catch (const std::exception& error) {
					report["interruptedExportError"] = error.what();
				}
			}
			report["restoration"] = globals::game::quitGame ? json{ { "restored", false }, { "processExiting", true } } : MainTask([] { return Restore(); });
			if (!report["restoration"].value("restored", false))
				GetRuntime().state.RequireRestoration();
			try {
				if (directory.empty())
					throw std::runtime_error("No report directory was created: " + report.value("error", std::string{}));
				if (!directory.empty()) {
					if (report["restoration"].value("changed", false) && !GetRuntime().state.RestorationPending() && !globals::game::quitGame) {
						try {
							report["restorationVerification"] = VerifyRestoration(token, directory, report["restoration"]);
						} catch (const std::exception& error) {
							report["restorationVerification"] = { { "error", error.what() } };
						}
					}
					if (report["status"] == "completed" &&
						(GetRuntime().state.RestorationPending() || !report.value("restorationVerification", json::object()).value("verified", false)))
						report["status"] = "completed_with_restoration_warning";
					if (!globals::game::quitGame)
						Write(directory / "environment_end.json", Environment(true).dump(2));
					SaveProgress(directory, report);
				}
				const bool clean = report["status"] == "completed";
				Status(GetRuntime().state.RestorationPending() ? "restore_pending" : clean ? "saved" :
																							 "saved_with_errors",
					clean ? "Report saved. Send the entire CSX-Reflex folder and CSX.log." :
							"Report saved with errors or incomplete restoration verification. Read report.txt and send the entire folder and CSX.log.",
					directory);
				if (!globals::game::quitGame)
					(void)MainTask([clean] { RE::SendHUDMessage::ShowHUDMessage(clean ? "Reflex diagnostic report saved. Send the CSX-Reflex folder." : "Reflex report saved with warnings. Check the Reflex page and report.txt.", nullptr, true); return json{ { "notified", true } }; });
			} catch (const std::exception& error) {
				Status("export_failed", error.what(), directory);
			}
			GetRuntime().state.WorkerFinished();
		}
	}

	bool Busy() { return GetRuntime().state.Busy(); }
	void RestorePending()
	{
		auto& runtime = GetRuntime();
		if (runtime.state.RestorationPending() && Restore().value("restored", false)) {
			runtime.state.RestorationFinished();
			Status("restored", "Original settings restored after the task queue became available; report retains the restoration warning.");
		}
	}
	void PreservePersistedSettings(json& settings)
	{
		auto& runtime = GetRuntime();
		std::scoped_lock lock(runtime.savedMutex);
		runtime.saved.Preserve(settings);
	}
	json Dispatch(const json& args)
	{
		auto& runtime = GetRuntime();
		const auto action = args.value("action", std::string{});
		std::scoped_lock launch(runtime.launchMutex);
		if (action == "reflex_report_start" || action == "reflex_report_recover") {
			if (!runtime.state.TryStart())
				return { { "error", "Reflex report already running or restoration pending" } };
			try {
				const auto capture = DispatchOwned({ { "action", "reflex_trace_status" } });
				const auto session = capture.at("capture").value("sessionId", 0ull);
				if (action == "reflex_report_start" && session)
					throw std::runtime_error("Use Stop and export retained capture before starting another test.");
				if (action == "reflex_report_recover" && !session)
					throw std::runtime_error("There is no retained capture to recover.");
				runtime.cancel = false;
				runtime.recovering = action == "reflex_report_recover";
				{
					std::scoped_lock lock(runtime.statusMutex);
					runtime.status = { { "state", "starting" }, { "message", "Preparing Reflex capture." } };
				}
				if (action == "reflex_report_recover")
					runtime.worker = std::jthread([session](std::stop_token) { Recover(session); });
				else
					runtime.worker = std::jthread(Run);
			} catch (const std::exception& error) {
				runtime.recovering = false;
				runtime.state.WorkerFinished();
				return { { "error", error.what() } };
			}
		} else if (action == "reflex_report_cancel") {
			if (runtime.recovering)
				return { { "error", "Retained evidence export is finalizing; wait for its completion." } };
			runtime.cancel = true;
		} else if (action != "reflex_report_status")
			return { { "error", "unknown Reflex report action" } };
		std::scoped_lock lock(runtime.statusMutex);
		return { { "action", action }, { "busy", runtime.state.Busy() }, { "report", runtime.status } };
	}
	void DrawControls()
	{
		ImGui::SeparatorText("Reflex diagnostic test");
		Presentation::DrawControls();
		ImGui::TextWrapped("One test records Off, On, Boost and a repeated Off baseline with and without state queries, then a 10-second 20 FPS check. Allow about five minutes. No DevBench or Debug logging required.");
		ImGui::TextWrapped("Stay in the same scene. The test temporarily sets noon and freezes game time, then restores time and Reflex settings. Do not save the game during the test.");
		if (!Busy()) {
			if (ImGui::Button("Run Reflex diagnostic test")) {
				const auto result = Dispatch({ { "action", "reflex_report_start" } });
				if (result.contains("error"))
					Status("error", result["error"].get<std::string>());
			}
			if (ImGui::Button("Stop and export retained capture")) {
				const auto result = Dispatch({ { "action", "reflex_report_recover" } });
				if (result.contains("error"))
					Status("error", result["error"].get<std::string>());
			}
		} else if (GetRuntime().recovering) {
			ImGui::TextDisabled("Finishing retained evidence export...");
		} else if (ImGui::Button("Cancel Reflex test and restore settings")) {
			(void)Dispatch({ { "action", "reflex_report_cancel" } });
		}
		const auto status = Dispatch({ { "action", "reflex_report_status" } });
		ImGui::TextWrapped("%s", status["report"].value("message", std::string{}).c_str());
		if (status["report"].contains("directory"))
			ImGui::TextWrapped("Report folder: %s", status["report"]["directory"].get<std::string>().c_str());
	}
}
#endif
