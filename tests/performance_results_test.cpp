#include "Menu/PerformanceFpsVR.h"
#include "Menu/PerformanceResults.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <deque>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace PerformanceResults;
using namespace std::chrono_literals;

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	template <class Action>
	bool Rejects(Action&& action)
	{
		try {
			action();
		} catch (const std::exception&) {
			return true;
		}
		return false;
	}

	void Wait(Store& store)
	{
		const auto deadline = std::chrono::steady_clock::now() + 5s;
		while (store.Busy() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(1ms);
		Require(!store.Busy(), "worker did not finish");
	}

	Json Record()
	{
		return { { "schema", 1 }, { "id", NewId() }, { "started", Timestamp() }, { "kind", "feature_batch" }, { "status", "complete" },
			{ "context", { { "runtime", "VR" }, { "timingDefinitions", 2 } } }, { "settings", Json::object() }, { "failure", nullptr },
			{ "results", Json::array({ { { "id", "test" }, { "label", "=test,<script>alert(1)</script>\"\nline" }, { "relativeTo", "Off" },
							 { "metrics", { { "gpu", { { "unit", "ms" }, { "value", 0.0 }, { "standardError", 0.05 } } },
											  { "cpu", { { "unit", "ms" }, { "value", nullptr } } } } } } }) } };
	}

	void TestTriplicateSummary()
	{
		Require(SceneLabel({ { "cellName", "" }, { "cell", 38706 } }) == "Cell 0x00009732", "unnamed exterior cells must retain a scene identity");
		Require(SceneLabel({ { "cellName", " " }, { "locationName", "Riverwood" }, { "cell", 38706 } }) == "Riverwood", "location names must replace empty cell names");
		Require(SceneLabel({ { "cellName", "Dragonsreach" }, { "locationName", "Whiterun" } }) == "Dragonsreach", "named interior cells must retain their display name");
		auto run = Record();
		run["kind"] = "quick_scan";
		run["scanCount"] = 3;
		run["results"] = Json::array();
		for (unsigned index = 1; index <= 3; ++index)
			run["results"].push_back({ { "id", std::to_string(index) + ":Upscaling" }, { "label", "Upscaling" }, { "scanIndex", index },
				{ "coverage", "Instrumented passes" }, { "metrics", { { "gpu", { { "unit", "ms" }, { "value", index * 1.0 } } }, { "cpu", { { "unit", "ms" }, { "value", 0.0 } } } } } });
		const auto rows = SummaryMetrics(run);
		Require(rows.size() == 2 && Metrics(run).size() == 6, "summaries must retain individual repeat measurements");
		Require(rows[0]["value"] == 0.0 && rows[0]["standardError"] == 0.0, "valid zero timings and zero SE must be retained");
		Require(rows[1]["value"] == 2.0 && std::abs(rows[1]["standardError"].get<double>() - std::sqrt(1.0 / 3.0)) < 1e-12,
			"SE must use sample variance across the three scan means");
		Require(rows[1]["repeatValues"] == Json::array({ 1.0, 2.0, 3.0 }), "report must retain each repeat value in order");
		auto second = run;
		for (auto& row : second["results"]) row["metrics"]["gpu"]["value"] = 10.0;
		const auto report = HtmlSession(Json::array({ run, second, run }));
		Require(report.find("Experiments side by side") != std::string::npos && report.find("Experiment 3") != std::string::npos &&
					report.find("2.000 &plusmn; 0.577") != std::string::npos && report.find("10.000 &plusmn; 0.000") != std::string::npos,
			"experiments must have separate columns and independent within-run SE");
		Require(report.find("<th>p-value</th>") == std::string::npos && report.find("<th>Current</th>") == std::string::npos &&
					report.find("<th>Scan 3</th>") != std::string::npos,
			"quick scans must not show On/Off-only columns");
		Require(Csv(run).find("within_run_repeats") != std::string::npos, "CSV must include the same summary statistics as HTML");
		{
			auto available = run, unavailable = run;
			for (auto& row : available["results"]) {
				row["metrics"]["cpu"]["value"] = nullptr;
				row["metrics"]["gpu"]["value"] = 0.0;
			}
			for (auto& row : unavailable["results"])
				for (auto& metric : row["metrics"]) metric["value"] = nullptr;
			const auto html = Html(available, &unavailable);
			const auto begin = html.find("<h2>Experiments side by side");
			const auto table = html.substr(begin, html.find("<section", begin) - begin);
			Require(table.find(">cpu<") == std::string::npos && table.find(">gpu<") != std::string::npos &&
						table.find("0.000 &plusmn; 0.000") != std::string::npos && table.find("<td>Unavailable</td>") != std::string::npos &&
						table.find("Unavailable &plusmn;") == std::string::npos,
				"comparison tables must hide rows missing everywhere, retain zeroes and label a missing run once");
			const auto difference = html.substr(html.find("<h2>Comparison:"));
			Require(difference.find(">cpu<") == std::string::npos && difference.find(">gpu<") != std::string::npos,
				"pairwise differences must retain a row measured by only one run");
			Require(HtmlSession(Json::array({ unavailable, unavailable })).find("Experiments side by side") == std::string::npos,
				"all-unavailable experiments must not produce an empty comparison table");
		}
		auto mixedSources = run;
		mixedSources["results"][1]["source"] = "csx_whole_frame";
		Require(Rejects([&] { SummaryMetrics(mixedSources); }), "different timing sources must not be pooled into a triplicate");
		auto precise = run;
		for (unsigned i = 0; i < 3; ++i)
			precise["results"][i]["metrics"]["gpu"]["value"] = 0.001 + i * 0.0000001;
		Require(Html(precise).find("e-08") != std::string::npos, "small nonzero standard errors must not be displayed as zero");
		auto wholeFrame = run;
		for (unsigned index = 1; index <= 3; ++index)
			wholeFrame["results"].push_back({ { "id", std::to_string(index) + ":$whole_frame" }, { "label", "Whole frame (CSX)" },
				{ "source", "csx_whole_frame" }, { "scanIndex", index }, { "metrics", { { "fps", { { "unit", "fps" }, { "value", 100.0 * index } } }, { "frame", { { "unit", "ms" }, { "value", 10.0 } } }, { "cpu", { { "unit", "ms" }, { "value", 3.0 } } }, { "gpu", { { "unit", "ms" }, { "value", 8.0 } } } } } });
		const auto nativeReport = HtmlSession(Json::array({ wholeFrame, run }));
		Require(nativeReport.find("CSX Performance Tuning whole-frame timings") != std::string::npos &&
					nativeReport.find("CSX FPS") != std::string::npos && nativeReport.find("200.000") != std::string::npos &&
					Csv(wholeFrame).find("csx_whole_frame") != std::string::npos,
			"native frame, FPS, CPU and GPU timings must survive summary, HTML and CSV independently of fpsVR");
		run["results"][2]["metrics"]["gpu"]["value"] = nullptr;
		Require(SummaryMetrics(run)[1]["value"].is_null() && SummaryMetrics(run)[1]["standardError"].is_null(),
			"a missing repeat must not be replaced with zero or produce a complete triplicate");
		run["results"].push_back(run["results"][0]);
		run["results"].back()["id"] = "1:Upscaling";
		Require(Rejects([&] { SummaryMetrics(run); }), "duplicate repeat identity must be rejected");
	}

	void TestRecordingOwnership()
	{
		using namespace PerformanceFpsVR;
		struct FakeLogger : Logger
		{
			std::deque<Observation> observations;
			bool running = true, writer = false;
			bool unreadableNewFile = false, ignoreStop = false;
			std::string newRecordingFile, discoveredFile;
			int toggles = 0, waits = 0, rejectToggle = 0;
			std::atomic_bool* cancelOnWait = nullptr;
			bool Running() const override { return running; }
			Observation Observe(bool samples) override
			{
				auto value = observations.front();
				if (samples && unreadableNewFile && value.file == "owned.csv")
					throw std::runtime_error("Malformed CSV samples");
				if (observations.size() > 1)
					observations.pop_front();
				return value;
			}
			Observation DiscoverRecording() override
			{
				auto value = observations.front();
				if (!discoveredFile.empty())
					value.file = discoveredFile;
				return value;
			}
			void Toggle() override
			{
				++toggles;
				RequireCommandSuccess(0, toggles == rejectToggle ? "Can't connect to fpsVR..." : "");
				const bool wasRecording = writer;
				if (!ignoreStop || !writer)
					writer = !writer;
				if (!wasRecording && writer && !newRecordingFile.empty())
					observations = { { newRecordingFile, 20, 2 }, { newRecordingFile, 30, 3 } };
			}
			void Wait(std::chrono::milliseconds) override
			{
				++waits;
				if (writer && observations.size() == 1) {
					observations.front().bytes += 10;
					observations.front().lastSample += 1;
				}
				if (cancelOnWait)
					*cancelOnWait = true;
			}
		};
		auto idle = [] {
			FakeLogger logger;
			logger.observations = { { "old.csv", 10, 1 }, { "old.csv", 10, 1 }, { "owned.csv", 20, 2 }, { "owned.csv", 30, 3 } };
			return logger;
		};
		std::atomic_bool cancel = false;
		{
			auto logger = idle();
			Recording recording;
			recording.Prepare(logger, cancel);
			Require(recording.confirmed, "a growing shared-access CSV must prove recording without a file lock");
			recording.Stop(logger);
			Require(!logger.writer && logger.toggles == 2, "shared-access recording must stop after scanning");
		}
		{
			auto logger = idle();
			Recording recording;
			recording.Prepare(logger, cancel);
			Require(recording.owned && recording.confirmed && logger.toggles == 1, "owned logger requires advancing samples after start");
			recording.Stop(logger);
			recording.Stop(logger);
			Require(logger.toggles == 2 && !logger.writer, "owned logger must stop exactly once");
		}
		{
			auto logger = idle();
			logger.observations = { { "borrowed.csv", 10, 1 }, { "borrowed.csv", 20, 2 } };
			logger.writer = true;
			logger.newRecordingFile = "fresh.csv";
			Recording recording;
			recording.Prepare(logger, cancel);
			Require(recording.confirmed && recording.owned && recording.observation.file == "fresh.csv" && logger.toggles == 2,
				"an active old recording must stop before a fresh per-run CSV starts");
			recording.Stop(logger);
			Require(!recording.owned && logger.toggles == 3 && !logger.writer, "the fresh replacement recording must stop after scanning");
		}
		{
			auto logger = idle();
			logger.observations = { { "borrowed.csv", 10, 1 }, { "borrowed.csv", 20, 2 } };
			logger.writer = logger.ignoreStop = true;
			logger.newRecordingFile = "fresh.csv";
			Recording recording;
			Require(Rejects([&] { recording.Prepare(logger, cancel); }), "an unconfirmed old-recording stop must block the fresh start");
			recording.Stop(logger);
			Require(logger.toggles == 1 && logger.writer && !recording.confirmed, "failed interruption must never send a restart toggle");
		}
		{
			auto logger = idle();
			logger.observations = { { "borrowed.csv", 10, 1 }, { "borrowed.csv", 20, 2 } };
			logger.writer = true;
			logger.rejectToggle = 2;
			logger.newRecordingFile = "fresh.csv";
			Recording recording;
			Require(Rejects([&] { recording.Prepare(logger, cancel); }), "a failed fresh-start acknowledgement must omit fpsVR");
			recording.Stop(logger);
			Require(logger.toggles == 2 && !logger.writer && !recording.owned,
				"a failed fresh start must not restart the stopped old recording during cleanup");
		}
		{
			auto logger = idle();
			logger.observations = { { "borrowed.csv", 10, 1 }, { "borrowed.csv", 20, 2 } };
			logger.writer = true;
			logger.cancelOnWait = &cancel;
			logger.newRecordingFile = "fresh.csv";
			Recording recording;
			Require(Rejects([&] { recording.Prepare(logger, cancel); }), "cancellation while interrupting an old recording must prevent a fresh start");
			recording.Stop(logger);
			Require(logger.toggles == 1 && !logger.writer && !recording.confirmed, "cancellation must leave the old recording stopped");
			cancel = false;
		}
		{
			auto logger = idle();
			logger.cancelOnWait = &cancel;
			Recording recording;
			Require(Rejects([&] { recording.Prepare(logger, cancel); }) && logger.toggles == 0, "cancellation during admission must prevent a late start");
			cancel = false;
		}
		for (bool running : { false, true }) {
			auto logger = idle();
			logger.running = running;
			if (running)
				logger.observations[1].file = "foreign.csv";
			Recording recording;
			Require(Rejects([&] { recording.Prepare(logger, cancel); }) && logger.toggles == 0, "exited fpsVR and a changed recording must block start");
		}
		{
			auto logger = idle();
			logger.rejectToggle = 1;
			Recording recording;
			Require(Rejects([&] { recording.Prepare(logger, cancel); }), "zero-exit connection failure must reject admission");
			recording.Stop(logger);
			Require(logger.toggles == 1 && !recording.owned, "failed acknowledgement must not claim a foreign recording");
		}
		{
			auto logger = idle();
			logger.unreadableNewFile = true;
			Recording recording;
			Require(Rejects([&] { recording.Prepare(logger, cancel); }) && recording.owned, "malformed new CSV must retain cleanup ownership");
			recording.Stop(logger);
			Require(logger.toggles == 2 && !logger.writer, "failed sample validation must still stop the owned recording");
		}
		{
			auto logger = idle();
			logger.observations = { { "owned.csv", 10, 1 }, { "owned.csv", 20, 2 } };
			logger.writer = logger.unreadableNewFile = true;
			logger.newRecordingFile = "fresh.csv";
			Recording recording;
			recording.Prepare(logger, cancel);
			Require(recording.confirmed && recording.observation.file == "fresh.csv",
				"malformed historical CSV samples must not enter the fresh per-run recording");
			recording.Stop(logger);
			Require(logger.toggles == 3 && !logger.writer, "both malformed old and fresh per-run recordings must be stopped");
		}
		for (bool duringAdmission : { true, false }) {
			auto logger = idle();
			Recording recording;
			if (duringAdmission) {
				logger.observations.back().file = "foreign.csv";
				Require(Rejects([&] { recording.Prepare(logger, cancel); }), "a replacement logger must invalidate admission");
			} else {
				recording.Prepare(logger, cancel);
				logger.observations = { { "foreign.csv", 50, 5 } };
				Require(Rejects([&] { recording.Stop(logger); }), "a replacement logger must not be stopped");
			}
			recording.Stop(logger);
			Require(logger.toggles == 1, "ownership changes must never trigger a second toggle");
		}
		{
			auto logger = idle();
			Recording recording;
			recording.Prepare(logger, cancel);
			logger.discoveredFile = "foreign.csv";
			Require(Rejects([&] { recording.Stop(logger); }), "a newer CSV must be discovered even while selected-file observations stay pinned");
			Require(logger.toggles == 1, "a different latest recording must not receive a stop toggle");
		}
		{
			auto logger = idle();
			Recording recording;
			recording.Prepare(logger, cancel);
			logger.writer = false;
			Require(Rejects([&] { recording.Stop(logger); }), "a quiet CSV alone must not claim a confirmed stop");
			Require(logger.toggles == 1, "an externally stopped recording must not be restarted");
		}
		{
			auto logger = idle();
			Recording recording;
			recording.Prepare(logger, cancel);
			logger.ignoreStop = true;
			Require(Rejects([&] { recording.Stop(logger); }), "continued CSV growth must reject stop confirmation");
			recording.Stop(logger);
			Require(logger.toggles == 2, "an unconfirmed stop must never be toggled a second time");
		}
		Require(Rejects([] { RequireCommandSuccess(1, ""); }) && Rejects([] { RequireCommandSuccess(0, "Usage: fpsVRcmd"); }), "failed and unsupported commands must be rejected");
	}
}

int main(int argc, char** argv)
try {
	TestRecordingOwnership();
	TestTriplicateSummary();
	const auto root = std::filesystem::temp_directory_path() / ("csx-performance-test-" + NewId());
	std::filesystem::create_directories(root);
	struct Cleanup
	{
		std::filesystem::path root;
		~Cleanup()
		{
			std::error_code error;
			std::filesystem::remove_all(root, error);
		}
	} cleanup{ root };
	auto record = Record();
	Validate(record);
	Require(Html(record).find("<script>") == std::string::npos, "HTML injection");
	Require(Html(record).find("&lt;script&gt;") != std::string::npos, "HTML escaping missing");
	Require(Csv(record).find("\"'=test") != std::string::npos, "CSV formula injection");
	Require(Csv(record).find("Unavailable") != std::string::npos, "missing replaced with zero");
	record["context"]["build"] = { { "sourceCommit", "commit-under-test" }, { "buildId", "build-under-test" }, { "sourceDirty", true } };
	Require(Csv(record).find("source_commit,build_id,source_dirty") != std::string::npos && Csv(record).find("commit-under-test") != std::string::npos,
		"explicit build identity columns are required");
	const Json captures = Json::array({ { { "scanIndex", 1u }, { "phase", "complete" }, { "startSteamVrSeconds", 2.0 }, { "endSteamVrSeconds", 4.0 } },
		{ { "scanIndex", 2u }, { "phase", "complete" }, { "startSteamVrSeconds", 5.0 }, { "endSteamVrSeconds", 7.0 } } });
	const std::string raw = "sep=,\r\nFrametimes/test\r\nSteamVR Time, GPU frametime, CPU frametime\r\n1,99,99\r\n2,1,2\r\n3,2,4\r\n4,3,6\r\n5,0,0\r\n6,4,8\r\n7,5,10\r\n8,99,99\r\n";
	std::istringstream csv(raw);
	const auto matched = PerformanceFpsVR::Correlate(csv, captures);
	Require(matched.size() == 2 && matched[0]["samples"] == 3 && matched[0]["gpu"]["meanMs"] == 2.0 && matched[1]["gpu"]["meanMs"] == 3.0,
		"fpsVR matching must exclude warm-up/gaps and retain valid zero samples");
	Require(matched[0]["cpu"]["p95Ms"].get<double>() > 5.7 && matched[0]["cpu"]["p95Ms"].get<double>() < 5.9, "fpsVR percentile interpolation");
	auto partial = captures;
	partial.push_back({ { "scanIndex", 3u }, { "phase", "cancelled" } });
	std::istringstream partialCsv(raw);
	Require(PerformanceFpsVR::Correlate(partialCsv, partial).size() == 2, "cancelled repeats must not discard completed fpsVR matches");
	auto unavailableWarmup = raw;
	unavailableWarmup.replace(unavailableWarmup.find("1,99,99"), 7, "1,unavailable,unavailable");
	std::istringstream warmupCsv(unavailableWarmup);
	Require(PerformanceFpsVR::Correlate(warmupCsv, captures) == matched, "unavailable timings outside scan windows must not invalidate matched samples");
	std::string quoted = "\xEF\xBB\xBFsep=,\r\nFrametimes/test\r\n\"SteamVR Time\", \"GPU frametime\",\"CPU frametime\",\"extra, column\"\r\n0,0,0,unused\r\n";
	for (int i = 1; i <= 8; ++i) quoted += "\"" + std::to_string(i) + "\",\"2\",\"4\",\"escaped \"\"quote\"\", comma\"\r\n";
	std::istringstream quotedCsv(quoted);
	const auto quotedMatched = PerformanceFpsVR::Correlate(quotedCsv, captures);
	Require(quotedMatched[0]["gpu"]["meanMs"] == 2.0 && quotedMatched[1]["cpu"]["meanMs"] == 4.0,
		"quoted headers and numeric fields, BOM, extra columns and zero clock origin must be supported");
	for (const auto& bad : { raw + "4,1,1\n", raw + std::string(65538, 'x') + "\n",
			 std::string("sep=,\nFrametimes/x\nSteamVR Time,GPU frametime,CPU frametime,CPU frametime\n") }) {
		Require(Rejects([&] { std::istringstream input(bad); PerformanceFpsVR::Correlate(input, captures); }), "ambiguous or oversized CSV must be rejected");
	}
	for (int issue = 0; issue < 4; ++issue) {
		auto windows = captures;
		if (issue == 0)
			windows[1]["scanIndex"] = 1;
		if (issue == 1)
			windows[1]["startSteamVrSeconds"] = 4.0;
		if (issue == 2)
			windows[0]["endSteamVrSeconds"] = 100.0;
		if (issue == 3)
			windows[0]["scanIndex"] = -1;
		Require(Rejects([&] { std::istringstream input(raw); PerformanceFpsVR::Correlate(input, windows); }), "invalid matching windows must be rejected");
	}
	for (int issue = 0; issue < 8; ++issue) {
		auto malformed = record;
		malformed["fpsVR"] = { { "status", "complete" }, { "scans", matched } };
		if (issue == 0)
			malformed["fpsVR"]["scans"][0]["cpu"]["meanMs"] = "not a number";
		if (issue == 1)
			malformed["fpsVR"]["reason"] = nullptr;
		if (issue == 2)
			malformed["fpsVR"]["scans"][0]["samples"] = -1;
		if (issue == 3)
			malformed["context"]["build"] = Json::array();
		if (issue == 4)
			malformed["scanCount"] = 1.0;
		if (issue == 5)
			malformed["results"][0]["sampling"] = nullptr;
		if (issue == 6)
			malformed["fpsVR"]["status"] = "omitted";
		if (issue == 7)
			malformed["fpsVR"]["recordingStopped"] = "unknown";
		Require(Rejects([&] { Validate(malformed); }), "malformed optional metadata must not reach the report or UI");
	}
	{
		auto indoor = Record(), outdoor = Record();
		indoor["context"]["cellName"] = "Indoor lights";
		outdoor["context"]["cellName"] = "Outdoor water and grass";
		indoor["context"]["build"] = outdoor["context"]["build"] = record["context"]["build"];
		indoor["settings"]["NeuralRendering"] = { { "Enabled", false } };
		outdoor["settings"]["NeuralRendering"] = { { "Enabled", true }, { "Mode", "Full" } };
		indoor["settings"]["Upscaling"] = outdoor["settings"]["Upscaling"] = { { "Method", "DLSS" }, { "RenderScale", 0.77 } };
		indoor["fpsVR"] = { { "status", "complete" }, { "scans", matched } };
		indoor["fpsVR"]["recordingStopped"] = false;
		outdoor["fpsVR"] = { { "status", "omitted" }, { "reason", "fpsVR was not running" }, { "scans", Json::array() } };
		const auto report = HtmlSession(Json::array({ indoor, outdoor }));
		Require(report.find("Indoor lights") != std::string::npos && report.find("Outdoor water and grass") != std::string::npos &&
					report.find("Settings for this run") != std::string::npos && report.find("false") != std::string::npos && report.find("true") != std::string::npos &&
					report.find("fpsVR whole-frame timings") != std::string::npos && report.find("<script>") == std::string::npos,
			"session reports must preserve independent settings, fpsVR omissions and escaped labels");
		Require(report.find("stop unconfirmed") != std::string::npos && report.find("p95 ms") != std::string::npos,
			"recording cleanup failures must remain visible without discarding valid fpsVR measurements");
		if (argc == 2) {
			std::ofstream preview(std::filesystem::path(reinterpret_cast<const char8_t*>(argv[1])), std::ios::binary);
			preview << report;
			Require(static_cast<bool>(preview), "could not write requested report preview");
		}
	}
	for (const auto bad : { raw + "4,1,1\r\n", std::string("sep=,\nFrametimes/x\nSteamVR Time,GPU frametime,CPU frametime\n3,1,1\n4,1,1\n") }) {
		bool refused = false;
		try {
			std::istringstream input(bad);
			PerformanceFpsVR::Correlate(input, captures);
		} catch (const std::exception&) {
			refused = true;
		}
		Require(refused, "clock reset or incomplete fpsVR coverage must not produce a match");
	}
	auto second = record;
	second["results"][0]["metrics"]["gpu"]["value"] = 2.0;
	const auto difference = Compare(record, second);
	Require(difference["rows"][0]["difference"].is_null(), "unavailable comparison invented");
	Require(difference["rows"][1]["difference"] == 2.0, "zero baseline lost");
	second["kind"] = "quick_scan";
	Require(!Compare(record, second)["compatible"].get<bool>(), "mixed scan and cost semantics");
	second = record;
	second["context"]["runtime"] = "AE";
	Require(Compare(record, second)["rows"][1]["difference"].is_null(), "mixed runtime comparison");
	auto invalid = record;
	invalid["id"] = "../escape";
	bool rejected = false;
	try {
		Validate(invalid);
	} catch (const std::exception&) {
		rejected = true;
	}
	Require(rejected, "unsafe identity accepted");
	std::atomic_bool fail{ false }, entered{ false }, hold{ false };
	auto writer = [&](const auto& path, std::string_view text, bool overwrite) {
		entered = true;
		while (hold) std::this_thread::sleep_for(1ms);
		if (fail)
			throw std::runtime_error("test disk failure");
		if (!overwrite && std::filesystem::exists(path))
			throw std::runtime_error("file exists");
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		output << text;
		if (!output)
			throw std::runtime_error("test write failure");
	};
	int picks = 0;
	{
		Store store(root, writer, [&](const auto& initial) { ++picks; Require(initial == store.Folder(), "wrong picker default"); return root; });
		Wait(store);
		Require(store.TryPause(), "cannot reserve measurement");
		entered = false;
		store.Save(record);
		std::this_thread::sleep_for(20ms);
		Require(!entered, "disk activity during measurement");
		Require(!store.TryPause(), "pending checkpoint ignored");
		store.Resume();
		Wait(store);
		Require(std::filesystem::exists(store.Folder() / (record["id"].get<std::string>() + ".json")), "save missing");
		store.Load(record["id"], false);
		Wait(store);
		Require(store.Snapshot().first && *store.Snapshot().first == record, "round trip changed values");
		store.Export(record["id"], "", "both");
		Wait(store);
		Require(picks == 1 && store.Snapshot().error.empty(), "folder picker/export failed");
		std::size_t reports = 0;
		for (const auto& file : std::filesystem::directory_iterator(root))
			if (file.path().extension() == ".csv" || file.path().extension() == ".html")
				++reports;
		Require(reports == 2, "on-demand formats missing");
		record["automaticCsv"] = true;
		store.Save(record, [&](Json& saved) { saved["fpsVR"] = { { "status", "complete" }, { "scans", matched } }; });
		Wait(store);
		Require(std::filesystem::exists(store.Folder() / (record["id"].get<std::string>() + ".csv")) &&
					std::filesystem::exists(store.Folder() / (record["id"].get<std::string>() + ".html")) && picks == 1,
			"batch export must run without a folder picker");
		store.Load(record["id"], false);
		Wait(store);
		Require(store.Snapshot().first->contains("fpsVR"), "external measurements must finalize before the record is saved");
		Require(store.Snapshot().sessionRuns == 1 && std::filesystem::exists(std::filesystem::path(reinterpret_cast<const char8_t*>(store.Snapshot().sessionReport.c_str()))), "same run must not duplicate in session report");
		const auto unicodeFolder = root / L"reports-\u65e5\u672c";
		std::filesystem::create_directory(unicodeFolder);
		store.Export(record["id"], "", "html", unicodeFolder);
		Wait(store);
		Require(store.Snapshot().error.empty(), "Unicode export folder failed");
		record["status"] = "running";
		store.Save(record);
		Wait(store);
		store.Load(record["id"], false);
		Wait(store);
		Require(store.Snapshot().first->at("status") == "interrupted", "partial run not identified");
		fail = true;
		record["status"] = "cancelled";
		store.Save(record);
		Wait(store);
		Require(store.Snapshot().unsaved == 1 && !store.Snapshot().error.empty(), "failed save lost");
		fail = false;
		store.Retry();
		Wait(store);
		Require(store.Snapshot().unsaved == 0, "retry failed");
		auto another = record;
		another["id"] = NewId();
		store.Save(another);
		Wait(store);
		Require(store.Snapshot().sessionRuns == 2, "multiple measurements must share one session report");
		const auto firstPath = store.Folder() / (record["id"].get<std::string>() + ".json");
		for (bool missing : { false, true }) {
			if (missing)
				std::filesystem::remove(firstPath);
			else
				writer(firstPath, "corrupt saved JSON", true);
			store.Save(another, [](Json& saved) { saved["results"].clear(); throw std::runtime_error("external reader failed"); });
			Wait(store);
			Require(store.Snapshot().error.empty() && store.Snapshot().unsaved == 0, "an older missing or corrupt run must not prevent saving new results");
			std::ifstream report(std::filesystem::path(reinterpret_cast<const char8_t*>(store.Snapshot().sessionReport.c_str())));
			const std::string html((std::istreambuf_iterator<char>(report)), {});
			Require(html.find("Data unavailable") != std::string::npos && html.find("external reader failed") != std::string::npos,
				"session report must expose missing data and external errors");
			store.Load(another["id"], false);
			Wait(store);
			Require(store.Snapshot().first->at("results") == another["results"], "external finalizer failure must preserve primary measurements");
		}
		store.Save(record);
		Wait(store);
		store.Export({}, {}, "html", unicodeFolder, true);
		Wait(store);
		Require(store.Snapshot().error.empty(), "session HTML export failed");
		store.Load("../escape", false);
		Wait(store);
		Require(!store.Snapshot().error.empty(), "load path traversal");
		hold = true;
		entered = false;
		store.Save(record);
		const auto deadline = std::chrono::steady_clock::now() + 5s;
		while (!entered && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(1ms);
		const bool reserved = store.TryPause();
		hold = false;
		Require(!reserved, "measurement started during disk write");
		Wait(store);
	}
	{
		Store store(root, writer, [](const auto&) { return std::filesystem::path{}; });
		Wait(store);
		store.Refresh();
		Wait(store);
		Require(store.Snapshot().history->size() == 2 && store.Snapshot().history->at(0)["status"] == "cancelled" && store.Snapshot().sessionRuns == 0, "history must survive while a new process starts a fresh session");
		store.Export(record["id"], "", "html");
		Wait(store);
		Require(store.Snapshot().message == "Export cancelled.", "picker cancellation mishandled");
		std::ifstream session(root / "CSX-Performance.log");
		std::string bytes((std::istreambuf_iterator<char>(session)), {});
		Require(bytes.find("cancelled") == std::string::npos, "session log not reset");
		const auto oldId = store.Snapshot().history->at(0).at("id").get<std::string>();
		const auto otherOldId = store.Snapshot().history->at(1).at("id").get<std::string>();
		store.Export(oldId, otherOldId, "html", root);
		Wait(store);
		Require(store.Snapshot().error.find("current game session") != std::string::npos,
			"HTML comparisons must not pull previous-session measurements into a new session");
		auto current = Record();
		store.Save(current);
		Wait(store);
		std::ifstream currentReport(std::filesystem::path(reinterpret_cast<const char8_t*>(store.Snapshot().sessionReport.c_str())));
		const std::string html((std::istreambuf_iterator<char>(currentReport)), {});
		Require(store.Snapshot().sessionRuns == 1 && html.find(current.at("id").get<std::string>()) != std::string::npos &&
					html.find(oldId) == std::string::npos && html.find(otherOldId) == std::string::npos,
			"automatic session HTML must contain current measurements only while preserving older disk history");
		store.Export(current.at("id"), oldId, "both", root);
		Wait(store);
		Require(store.Snapshot().error.find("current game session") != std::string::npos,
			"mixed current and previous-session HTML comparisons must be rejected");
		auto currentSecond = Record();
		store.Save(currentSecond);
		Wait(store);
		store.Export(current.at("id"), currentSecond.at("id"), "html", root);
		Wait(store);
		Require(store.Snapshot().error.empty() && store.Snapshot().sessionRuns == 2,
			"two current-session measurements must remain comparable");
	}
	std::cout << "Performance history, exports, exclusion and failure recovery passed\n";
	return 0;
} catch (const std::exception& error) {
	std::cerr << error.what() << '\n';
	return 1;
}
