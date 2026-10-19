#include "Features/Upscaling/ReflexReport.h"
#include "Features/Upscaling/ReflexRunState.h"
#include "Features/Upscaling/ReflexTestSettings.h"

#include <iostream>
#include <stdexcept>

using namespace ReflexDiagnostics::Report;
namespace
{
	void Require(bool value, const char* reason)
	{
		if (!value)
			throw std::runtime_error(reason);
	}
	json Fixture(int mode = 1, bool cap = false, uint32_t seconds = 30, double frameMs = 75)
	{
		json records = json::array(), presents = json::array();
		for (int i = 0; i < static_cast<int>(seconds * 1000 / frameMs); ++i) {
			presents.push_back({ { "sequence", i + 1 }, { "entryTick", static_cast<uint64_t>(1000 + i * frameMs) },
				{ "beginTick", static_cast<uint64_t>(1000 + i * frameMs) }, { "endTick", static_cast<uint64_t>(1001 + i * frameMs) },
				{ "swapChain", 123 }, { "window", 456 }, { "foreground", 456 }, { "windowValid", true }, { "visible", true }, { "minimized", false },
				{ "flags", 0 }, { "syncInterval", 0 }, { "returned", true }, { "result", 0 } });
			records.push_back({ { "context", { { "available", true }, { "paused", false }, { "menuOpen", false }, { "compiling", false }, { "cellId", 42 }, { "gameHour", 12.0 } } },
				{ "requested", { { "mode", mode }, { "fpsLimitEnabled", cap }, { "fpsLimit", 20 }, { "boost", mode == 2 } } },
				{ "lastSuccessfulOptions", { { "mode", mode }, { "frameLimitUs", cap ? 50000 : 0 }, { "markersEffective", false } } },
				{ "resolvedOptions", { { "mode", mode }, { "frameLimitUs", cap ? 50000 : 0 }, { "markersEffective", false } } },
				{ "lastOptionsResult", { { "code", 0 } } }, { "blockedByFrameGeneration", false }, { "frameGenerationQuarantined", false },
				{ "sleep", { { "result", { { "code", 0 } } }, { "durationTicks", 1 }, { "reason", "called" } } },
				{ "hookIntervalTicks", 11 },
				{ "state", { { "result", { { "code", 0 } } }, { "lowLatencyAvailable", true }, { "durationTicks", 1 } } },
				{ "driver", { { "result", 0 }, { "lowLatencyEnabled", mode != 0 }, { "gameSleepSeen", true } } },
				{ "compositor", { { "frame", i + 1 }, { "systemTime", 1.0 + i * frameMs / 1000 }, { "clientMs", frameMs }, { "gpuMs", 8.0 }, { "submitMs", .01 }, { "dropped", 0 }, { "mispresented", 0 }, { "presents", 1 } } } });
		}
		json presentation{ { "creation", { { "variantApplied", true }, { "fallback", false }, { "requestedFlip", false } } },
			{ "descriptor", { { "swapEffect", 0 } } }, { "descriptorResult", 0 }, { "swapChain", { { "pointer", 123 } } },
			{ "rendererDevice", { { "pointer", 789 } } }, { "streamlineBoundToRenderer", true } };
		return { { "schemaVersion", 3 }, { "sessionId", 1 }, { "startTick", 1000 }, { "deadlineTick", 1000 + seconds * 1000 },
			{ "environment", { { "presentation", presentation } } }, { "environmentAtEnd", { { "presentation", presentation } } },
			{ "desktopPresents", { { "sessionId", 1 }, { "startTick", 1000 }, { "deadlineTick", 1000 + seconds * 1000 }, { "active", false },
									 { "inFlight", 0 }, { "droppedRecords", 0 }, { "retainedRecords", presents.size() }, { "records", presents } } },
			{ "setup", { { "upscalingSettings", { { "mode", "dlss" } } } } }, { "endSettings", { { "mode", "dlss" } } }, { "queryState", true }, { "records", records }, { "qpcFrequency", 1000 }, { "active", false }, { "inFlight", 0 },
			{ "droppedRecords", 0 }, { "retainedRecords", records.size() }, { "stopReason", "duration" } };
	}
}
int main()
{
	try {
		struct Settings
		{
			bool reflexLowLatencyMode = true, reflexLowLatencyBoost = true, reflexUseFPSLimit = true;
			float reflexFPSLimit = 72;
		} settings;
		ReflexDiagnostics::Suite::SavedSettings saved;
		float hour = 19, timeScale = 7;
		saved.Capture(settings, hour, timeScale);
		settings = { false, false, false, 20 };
		hour = 12;
		timeScale = 0;
		saved.Capture(settings, hour, timeScale);
		json persisted{ { "unrelated", 123 }, { "reflexFPSLimit", 20 } };
		saved.Preserve(persisted);
		Require(persisted["reflexFPSLimit"] == 72 && persisted["unrelated"] == 123, "test override leaked into saved config");
		saved.Restore(settings, hour, timeScale);
		Require(settings.reflexLowLatencyMode && settings.reflexLowLatencyBoost && settings.reflexUseFPSLimit && settings.reflexFPSLimit == 72 && hour == 19 && timeScale == 7 && !saved.valid, "cancel/completion lost original settings or time");
		settings.reflexFPSLimit = 90;
		saved.Restore(settings, hour, timeScale);
		Require(settings.reflexFPSLimit == 90, "duplicate restoration overwrote a later user change");
		const auto& on = kConditions[5];
		Require(kConditions.size() == 9 && !kConditions[0].query && !kConditions[2].query && kConditions[4].query && kConditions.back().cap, "protocol must observe passive state before queries and cap");
		const auto original = Fixture();
		auto summary = Analyze(original, on);
		Require(summary["controlledVrComparison"] == true && summary["driverEnabledQueries"] == 400, "valid evidence rejected");
		Require(summary["driverAssessment"].get<std::string>().find("driver confirms") != std::string::npos, "driver enabled readback missing");
		for (const auto* reason : { "absent", "dropped", "focus", "occluded", "unreturned", "wrong_swap", "fallback", "recreated", "mode_not_applied", "clock", "short", "changed_sync", "test_present", "session" }) {
			auto broken = original;
			auto& trace = broken["desktopPresents"];
			auto& sample = trace["records"][10];
			const std::string key = reason;
			if (key == "absent")
				broken.erase("desktopPresents");
			if (key == "dropped")
				trace["droppedRecords"] = 1;
			if (key == "focus")
				sample["foreground"] = 1;
			if (key == "occluded")
				sample["result"] = 0x087A0001;
			if (key == "unreturned")
				sample["returned"] = false;
			if (key == "wrong_swap")
				sample["swapChain"] = 1;
			if (key == "fallback")
				broken["environment"]["presentation"]["creation"]["fallback"] = true;
			if (key == "recreated")
				broken["environmentAtEnd"]["presentation"]["descriptor"]["swapEffect"] = 4;
			if (key == "mode_not_applied")
				broken["environment"]["presentation"]["creation"]["requestedFlip"] = true;
			if (key == "clock")
				sample["endTick"] = 1;
			if (key == "short")
				trace["records"].erase(trace["records"].begin() + 20, trace["records"].end());
			if (key == "changed_sync")
				sample["syncInterval"] = 1;
			if (key == "test_present")
				sample["flags"] = 1;
			if (key == "session")
				trace["sessionId"] = 2;
			Require(Analyze(broken, on)["controlledVrComparison"] == false, reason);
		}
		auto unavailable = original;
		for (auto& r : unavailable["records"]) {
			r["driver"]["lowLatencyEnabled"] = false;
			r["state"]["lowLatencyAvailable"] = false;
		}
		auto failed = Analyze(unavailable, on);
		Require(failed["sleepFailures"] == 0 && failed["sdkUnavailableQueries"] == 400 && failed["driverEnabledQueries"] == 0, "API success masked disabled driver");
		Require(failed["driverAssessment"].get<std::string>().find("OFF despite requested ON") != std::string::npos, "disabled-driver verdict missing");
		auto passive = original;
		passive["queryState"] = false;
		for (auto& r : passive["records"]) {
			r["driver"]["result"] = nullptr;
			r["state"]["result"] = nullptr;
		}
		Require(Analyze(passive, kConditions[1])["driverAssessment"].get<std::string>().starts_with("inconclusive"), "passive capture invented driver state");
		for (const auto* reason : { "dropped", "paused", "mixed", "gap", "capacity", "night", "clock_reset", "compiling", "short_window", "unapplied", "option_failure", "end_settings", "query_policy", "zero_clock", "blocked", "compositor_fault", "long_window" }) {
			auto broken = original;
			const std::string key = reason;
			if (key == "dropped")
				broken["droppedRecords"] = 1;
			if (key == "paused")
				broken["records"][10]["context"]["paused"] = true;
			if (key == "mixed")
				broken["records"][10]["requested"]["mode"] = 0;
			if (key == "gap")
				broken["records"].erase(10);
			if (key == "capacity")
				broken["stopReason"] = "capacity";
			if (key == "night")
				broken["records"][10]["context"]["gameHour"] = 0;
			if (key == "clock_reset")
				broken["records"][10]["compositor"]["systemTime"] = .5;
			if (key == "short_window")
				for (auto& r : broken["records"]) r["compositor"]["systemTime"] = r["compositor"]["systemTime"].get<double>() / 10;
			if (key == "compiling")
				broken["records"][10]["context"]["compiling"] = true;
			if (key == "unapplied")
				broken["records"][10]["lastSuccessfulOptions"]["mode"] = 0;
			if (key == "option_failure")
				broken["records"][10]["lastOptionsResult"]["code"] = -1;
			if (key == "end_settings")
				broken["endSettings"]["mode"] = "taa";
			if (key == "query_policy")
				broken["queryState"] = false;
			if (key == "zero_clock")
				broken["qpcFrequency"] = 0;
			if (key == "blocked")
				broken["records"][10]["blockedByFrameGeneration"] = true;
			if (key == "compositor_fault")
				broken["records"][399]["compositorFault"] = true;
			if (key == "long_window")
				for (auto& r : broken["records"]) r["compositor"]["systemTime"] = r["compositor"]["systemTime"].get<double>() * 2;
			Require(Analyze(broken, on)["controlledVrComparison"] == false, reason);
		}
		auto stall = original;
		stall["records"][399]["compositor"]["clientMs"] = 1200;
		Require(Analyze(stall, on)["clientFrameMs"]["max"] == 1200, "long stutter filtered out");
		auto off = Analyze(Fixture(0), kConditions[4]);
		const auto comparison = Compare(off, summary);
		Require(comparison["coveragePermitsComparison"] == true && comparison["causalProof"] == false && comparison["p95DeltaMs"] == 0, "comparison overclaimed causal proof");
		Require(Distribution({})["mean"].is_null(), "missing metric converted to zero");
		auto fast = Analyze(Fixture(0, false, 30, 25), kConditions[4]);
		auto capped = Analyze(Fixture(0, true, 10, 50), kConditions[8]);
		Require(LimiterControl(fast, capped)["consistentWithWorkingLimiter"] == true, "matched positive control rejected");
		Require(LimiterControl(off, capped)["assessment"] == "inconclusive", "slow baseline called a limiter failure");
		capped["cells"] = { 99 };
		Require(LimiterControl(fast, capped)["eligibleComparison"] == false, "different-scene limiter comparison accepted");
		auto restored = Fixture();
		const json expected{ { "restored", true }, { "changed", true }, { "lowLatency", true }, { "boost", false }, { "cap", false }, { "fps", 20 } };
		Require(RestorationAssessment(restored, expected)["verified"] == true, "restored options rejected");
		restored["records"].back()["lastSuccessfulOptions"]["frameLimitUs"] = 50000;
		Require(RestorationAssessment(restored, expected)["verified"] == false, "lingering test cap accepted as restoration");
		restored = Fixture();
		restored["stopReason"] = "requested";
		Require(RestorationAssessment(restored, expected)["verified"] == false, "cancelled restoration capture accepted");
		auto sparse = original;
		for (std::size_t i = 1; i < sparse["records"].size(); ++i) sparse["records"][i]["driver"]["result"] = nullptr;
		Require(Analyze(sparse, on)["driverReadbackCoverage"] == false, "one driver readback represented the entire condition");
		for (bool restoreFirst : { false, true }) {
			ReflexDiagnostics::Suite::RunState state;
			Require(state.TryStart() && !state.TryStart(), "concurrent report admitted");
			state.RequireRestoration();
			if (restoreFirst)
				state.RestorationFinished();
			else
				state.WorkerFinished();
			Require(state.Busy() && !state.TryStart(), "restoration or export released the other owner's lease");
			if (restoreFirst)
				state.WorkerFinished();
			else
				state.RestorationFinished();
			Require(!state.Busy() && state.TryStart(), "completed report did not release ownership");
		}
		return 0;
	} catch (const std::exception& e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
