#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <algorithm>
#	include <array>
#	include <cmath>
#	include <numeric>
#	include <set>
#	include <string>
#	include <vector>
#	include <nlohmann/json.hpp>

namespace ReflexDiagnostics::Report
{
	using json = nlohmann::json;
	struct Condition
	{
		const char* name;
		int mode;
		bool query;
		bool cap;
		uint32_t seconds;
	};
	inline constexpr std::array kConditions{
		Condition{ "passive_off", 0, false, false, 30 },
		Condition{ "passive_on", 1, false, false, 30 },
		Condition{ "passive_boost", 2, false, false, 30 },
		Condition{ "passive_off_repeat", 0, false, false, 30 },
		Condition{ "queried_off", 0, true, false, 30 },
		Condition{ "queried_on", 1, true, false, 30 },
		Condition{ "queried_boost", 2, true, false, 30 },
		Condition{ "queried_off_repeat", 0, true, false, 30 },
		Condition{ "cap_20fps", 0, true, true, 10 }
	};

	inline json RenderSettings(json settings)
	{
		if (settings.is_object())
			for (const auto* field : { "reflexLowLatencyMode", "reflexLowLatencyBoost", "reflexUseFPSLimit", "reflexFPSLimit" }) settings.erase(field);
		return settings;
	}

	/** A requested mode is not evidence that the SDK accepted the corresponding options. */
	inline bool AppliedOptionsMatch(const json& record, int mode, bool cap, double fps)
	{
		const auto applied = record.value("lastSuccessfulOptions", json(nullptr));
		const auto resolved = record.value("resolvedOptions", json(nullptr));
		const auto result = record.value("lastOptionsResult", json(nullptr));
		const auto limit = cap && std::isfinite(fps) ? static_cast<uint32_t>(std::lround(1000000.0 / std::clamp(fps, 20.0, 240.0))) : 0u;
		return applied.is_object() && resolved.is_object() && applied == resolved &&
		       applied.value("mode", -1) == mode && applied.value("frameLimitUs", UINT32_MAX) == limit &&
		       result.is_object() && result.value("code", -1) == 0 &&
		       !record.value("blockedByFrameGeneration", true) && !record.value("frameGenerationQuarantined", true);
	}

	/** Verify restored preferences and accepted SDK options independently of driver availability. */
	inline json RestorationAssessment(const json& capture, const json& expected)
	{
		json result{ { "verified", false }, { "requestedSettingsRestored", false }, { "appliedOptionsVerified", false } };
		const auto& records = capture.at("records");
		if (records.empty() || !expected.value("restored", false) || !expected.value("changed", false))
			return result;
		const auto& last = records.back();
		const int mode = !expected.at("lowLatency").get<bool>() ? 0 : expected.at("boost").get<bool>() ? 2 :
		                                                                                                 1;
		const bool requested = last["requested"]["mode"] == mode && last["requested"]["boost"] == expected["boost"] &&
		                       last["requested"]["fpsLimitEnabled"] == expected["cap"] && last["requested"]["fpsLimit"] == expected["fps"];
		const bool applied = AppliedOptionsMatch(last, mode, expected["cap"].get<bool>(), expected["fps"].get<double>());
		result["requestedSettingsRestored"] = requested;
		result["appliedOptionsVerified"] = applied;
		result["verified"] = requested && applied && !capture.value("active", true) && capture.value("inFlight", 1u) == 0 &&
		                     capture.value("stopReason", std::string{}) == "duration" && capture.value("droppedRecords", 1u) == 0;
		result["requested"] = last["requested"];
		result["lastSuccessfulOptions"] = last["lastSuccessfulOptions"];
		return result;
	}

	/** Nearest-rank percentiles retain long stalls rather than clipping them. */
	inline json Distribution(std::vector<double> values)
	{
		std::erase_if(values, [](double v) { return !std::isfinite(v) || v < 0; });
		if (values.empty())
			return { { "count", 0 }, { "mean", nullptr }, { "p50", nullptr }, { "p95", nullptr }, { "p99", nullptr }, { "max", nullptr }, { "stddev", nullptr } };
		std::sort(values.begin(), values.end());
		const double mean = std::accumulate(values.begin(), values.end(), 0.0) / values.size();
		double variance = 0;
		for (double v : values)
			variance += (v - mean) * (v - mean);
		const auto percentile = [&](double p) { return values[static_cast<std::size_t>(std::ceil(p * values.size())) - 1]; };
		return { { "count", values.size() }, { "mean", mean }, { "p50", percentile(.50) }, { "p95", percentile(.95) },
			{ "p99", percentile(.99) }, { "max", values.back() }, { "stddev", std::sqrt(variance / values.size()) } };
	}

	/** Assess coverage separately from driver state and observable frame delivery. */
	/** Desktop evidence is independent of the headset compositor and must cover the same window. */
	inline json AnalyzePresentation(const json& capture, const Condition& condition)
	{
		const auto start = capture.value("environment", json::object()).value("presentation", json::object());
		const auto end = capture.value("environmentAtEnd", json::object()).value("presentation", json::object());
		const auto desc = start.value("descriptor", json(nullptr));
		const auto receipt = start.value("creation", json(nullptr));
		const auto trace = capture.value("desktopPresents", json::object());
		const auto records = trace.value("records", json::array());
		const double frequency = capture.value("qpcFrequency", 0.0);
		std::vector<double> durations, intervals, observer;
		std::set<uint32_t> syncValues, flagValues;
		std::set<bool> foregroundStates;
		json results = json::object();
		uint64_t invalid = 0, tests = 0, occluded = 0, nonSuccess = 0, wrongSwap = 0;
		uint64_t first = 0, previous = 0, sequence = 0;
		const auto swap = start.value("swapChain", json(nullptr));
		const auto swapPointer = swap.is_object() ? swap.value("pointer", 0ull) : 0ull;
		for (const auto& r : records) {
			const auto begin = r.value("beginTick", 0ull), finish = r.value("endTick", 0ull);
			const auto entry = r.value("entryTick", 0ull);
			const bool validTime = frequency > 0 && entry && begin >= entry && finish >= begin && (!previous || begin > previous);
			invalid += !validTime || r.value("sequence", 0ull) != ++sequence || !r.value("returned", false) ||
			           !r.value("windowValid", false) || !r.value("visible", false) || r.value("minimized", true) ||
			           entry < trace.value("startTick", UINT64_MAX) || entry >= trace.value("deadlineTick", 0ull);
			wrongSwap += !swapPointer || r.value("swapChain", 0ull) != swapPointer;
			tests += (r.value("flags", 0u) & 1u) != 0;
			syncValues.insert(r.value("syncInterval", UINT32_MAX));
			flagValues.insert(r.value("flags", UINT32_MAX));
			foregroundStates.insert(r.value("window", 0ull) != 0 && r.value("window", 0ull) == r.value("foreground", 0ull));
			if (r.contains("result") && r["result"].is_number_integer()) {
				const auto hr = r["result"].get<int32_t>();
				const auto key = std::to_string(hr);
				results[key] = results.value(key, 0ull) + 1;
				occluded += hr == 0x087A0001;
				nonSuccess += hr != 0;
			} else
				++nonSuccess;
			if (validTime) {
				durations.push_back((finish - begin) * 1000.0 / frequency);
				observer.push_back(r.value("observerTicks", 0ull) * 1000.0 / frequency);
				if (previous)
					intervals.push_back((begin - previous) * 1000.0 / frequency);
			}
			if (!first)
				first = begin;
			previous = begin;
		}
		const double span = frequency > 0 && previous >= first ? (previous - first) / frequency : 0;
		const bool verified = receipt.is_object() && receipt.value("variantApplied", false) &&
		                      !receipt.value("fallback", true) && desc.is_object() && start.value("descriptorResult", -1) == 0 &&
		                      (!receipt.value("requestedFlip", false) || desc.value("swapEffect", 0u) == 4);
		const bool stable = desc.is_object() && desc == end.value("descriptor", json(nullptr)) &&
		                    start.value("swapChain", json(nullptr)) == end.value("swapChain", json(nullptr)) &&
		                    start.value("rendererDevice", json(nullptr)) == end.value("rendererDevice", json(nullptr)) &&
		                    start.value("streamlineBoundToRenderer", false) && end.value("streamlineBoundToRenderer", false);
		const bool complete = !trace.value("active", true) && !trace.value("inFlight", 1u) && !trace.value("droppedRecords", 1u) &&
		                      trace.value("sessionId", 0ull) == capture.value("sessionId", UINT64_MAX) &&
		                      trace.value("startTick", 0ull) == capture.value("startTick", UINT64_MAX) &&
		                      trace.value("deadlineTick", 0ull) == capture.value("deadlineTick", UINT64_MAX) &&
		                      records.size() == trace.value("retainedRecords", 0ull) && records.size() >= condition.seconds * 10 &&
		                      span >= condition.seconds - 1.0 && span <= condition.seconds + 1.0;
		const bool controlled = verified && stable && complete && !invalid && !wrongSwap && !tests && !nonSuccess &&
		                        foregroundStates.size() == 1 && syncValues.size() == 1 && flagValues.size() == 1;
		return { { "coveragePermitsComparison", controlled }, { "variantVerified", verified }, { "stableDescriptorAndDevice", stable },
			{ "journalComplete", complete }, { "actualDescriptor", desc }, { "requestedFlip", receipt.is_object() && receipt.value("requestedFlip", false) },
			{ "spanSeconds", span }, { "records", records.size() }, { "invalidRecords", invalid }, { "wrongSwapChainRecords", wrongSwap },
			{ "testPresents", tests }, { "occludedPresents", occluded }, { "nonSuccessPresents", nonSuccess }, { "results", results },
			{ "syncIntervals", syncValues }, { "flags", flagValues }, { "foregroundStates", foregroundStates },
			{ "callMs", Distribution(durations) }, { "intervalMs", Distribution(intervals) }, { "observerMs", Distribution(observer) },
			{ "interpretation", "Desktop Present only, not HMD delivery or measured display latency. A creation fallback is not a valid flip test. Compare actual swap effects across launches: if default is already flip-discard there is no swap-effect contrast. Observer time excludes journal commit." } };
	}

	inline json Analyze(const json& capture, const Condition& condition)
	{
		std::vector<double> sleep, hook, frame, gpu, submit, query, observer, driverInterval;
		json hardwareSamples = json::object();
		std::set<uint32_t> cells;
		uint64_t invalidContext = 0, mismatched = 0, calls = 0, failures = 0, unavailable = 0;
		uint64_t unapplied = 0, compositorFaults = 0;
		uint64_t driverQueries = 0, driverFailures = 0, driverEnabled = 0, driverSleep = 0, sdkQueries = 0, sdkFailures = 0;
		uint64_t droppedFrames = 0, mispresented = 0, repeated = 0, gaps = 0, resets = 0, invalidFrames = 0;
		uint32_t previousFrame = 0;
		double previousTime = 0, firstTime = 0;
		json skipReasons = json::object();
		const double frequency = capture.at("qpcFrequency").get<double>();
		const double ms = frequency > 0 ? 1000.0 / frequency : 0;
		for (const auto& r : capture.at("records")) {
			compositorFaults += r.value("compositorFault", false);
			observer.push_back(r.value("observerTicks", 0.0) * ms);
			for (const auto& g : r.value("gpuTelemetry", json::array())) {
				const auto index = std::to_string(g["index"].get<uint32_t>());
				for (const auto* field : { "graphicsKHz", "memoryKHz", "utilizationPercent", "pstate" })
					if (g[field].is_number())
						hardwareSamples[index][field].push_back(g[field]);
			}
			const auto& context = r.at("context");
			const bool validContext = context.value("available", false) && !context.value("paused", true) &&
			                          !context.value("menuOpen", true) && !context.value("compiling", true) && context.value("cellId", 0u) != 0 &&
			                          context["gameHour"].is_number() && context["gameHour"].get<double>() >= 12 && context["gameHour"].get<double>() <= 12.05;
			invalidContext += !validContext;
			cells.insert(context.value("cellId", 0u));
			mismatched += r["requested"]["mode"] != condition.mode || r["requested"]["fpsLimitEnabled"] != condition.cap ||
			              (condition.cap && r["requested"]["fpsLimit"] != 20);
			unapplied += !AppliedOptionsMatch(r, condition.mode, condition.cap, 20);
			if (r["sleep"]["result"].is_object()) {
				++calls;
				failures += r["sleep"]["result"]["code"] != 0;
				sleep.push_back(r["sleep"]["durationTicks"].get<double>() * ms);
			} else {
				const auto reason = r["sleep"]["reason"].get<std::string>();
				skipReasons[reason] = skipReasons.value(reason, uint64_t{ 0 }) + 1;
			}
			if (r["hookIntervalTicks"].get<uint64_t>() != 0)
				hook.push_back(r["hookIntervalTicks"].get<double>() * ms);
			if (r["state"]["result"].is_object()) {
				++sdkQueries;
				sdkFailures += r["state"]["result"]["code"] != 0;
				unavailable += r["state"]["lowLatencyAvailable"] == false;
				query.push_back(r["state"]["durationTicks"].get<double>() * ms);
			}
			if (!r["driver"]["result"].is_null()) {
				++driverQueries;
				driverFailures += r["driver"]["result"] != 0;
				driverEnabled += r["driver"]["lowLatencyEnabled"] == true;
				driverSleep += r["driver"]["gameSleepSeen"] == true;
				if (r["driver"].contains("sleepIntervalUs") && r["driver"]["sleepIntervalUs"].is_number())
					driverInterval.push_back(r["driver"]["sleepIntervalUs"].get<double>());
			}
			const auto history = r.contains("compositorHistory") ? r["compositorHistory"] :
			                     r["compositor"].is_object()     ? json::array({ r["compositor"] }) :
			                                                       json::array();
			for (const auto& c : history) {
				if (!c.is_object())
					continue;
				const auto index = c["frame"].get<uint32_t>();
				const auto time = c["systemTime"].is_number() ? c["systemTime"].get<double>() : 0.0;
				if (time <= 0 || !std::isfinite(time)) {
					++invalidFrames;
					continue;
				}
				if (previousTime > 0 && (index < previousFrame || time < previousTime)) {
					++resets;
				} else if (previousTime > 0 && (index == previousFrame || time == previousTime)) {
					continue;
				} else if (previousTime > 0 && index > previousFrame + 1) {
					gaps += index - previousFrame - 1;
				}
				previousFrame = index;
				previousTime = time;
				if (!firstTime)
					firstTime = time;
				if (!validContext || !c["clientMs"].is_number() || !std::isfinite(c["clientMs"].get<double>()) || c["clientMs"].get<double>() <= 0) {
					++invalidFrames;
					continue;
				}
				frame.push_back(c["clientMs"].get<double>());
				if (c["gpuMs"].is_number())
					gpu.push_back(c["gpuMs"].get<double>());
				if (c["submitMs"].is_number())
					submit.push_back(c["submitMs"].get<double>());
				droppedFrames += c["dropped"].get<uint32_t>();
				mispresented += c["mispresented"].get<uint32_t>();
				repeated += c["presents"].get<uint32_t>() > 1;
			}
		}
		json hardware = json::object();
		for (auto& [index, fields] : hardwareSamples.items())
			for (auto& [field, values] : fields.items())
				hardware[index][field] = Distribution(values.get<std::vector<double>>());
		const auto renderSettings = RenderSettings(capture.value("setup", json::object()).value("upscalingSettings", json(nullptr)));
		const auto endSettings = RenderSettings(capture.value("endSettings", json(nullptr)));
		const bool stableSettings = renderSettings.is_object() && endSettings == renderSettings;
		const bool journalComplete = frequency > 0 && !capture["records"].empty() &&
		                             capture.value("queryState", !condition.query) == condition.query &&
		                             !capture.value("active", true) && capture.value("inFlight", 1u) == 0 &&
		                             capture.value("droppedRecords", 1u) == 0 && capture["records"].size() == capture.value("retainedRecords", 0u) &&
		                             capture.value("stopReason", std::string{}) == "duration";
		const auto presentation = AnalyzePresentation(capture, condition);
		const bool controlled = presentation["coveragePermitsComparison"] == true && journalComplete && stableSettings && !unapplied && !mismatched && !invalidContext && cells.size() == 1 && !frame.empty() &&
		                        !compositorFaults && !gaps && !resets && !invalidFrames && frame.size() >= condition.seconds * 10 &&
		                        previousTime - firstTime >= condition.seconds - 1.0 && previousTime - firstTime <= condition.seconds + 1.0;
		std::string verdict = "inconclusive: no driver readback (passive capture or unsupported query)";
		if (driverQueries && driverFailures == driverQueries)
			verdict = "driver sleep-status queries failed";
		else if (driverQueries > driverFailures) {
			if (condition.mode && !driverEnabled)
				verdict = "driver reports low-latency mode OFF despite requested ON";
			else if (condition.mode && driverEnabled != driverQueries - driverFailures)
				verdict = "driver low-latency state changed during the condition";
			else if (condition.mode)
				verdict = "driver confirms low-latency mode ON; frame-delivery comparison determines observed benefit";
			else
				verdict = driverEnabled ? "driver reports low-latency ON despite requested OFF" : "driver confirms low-latency mode OFF";
		}
		if ((condition.mode || condition.cap) && !calls)
			verdict = "Reflex sleep was never called; inspect skipReasons";
		else if (failures)
			verdict = "Reflex sleep API failures observed";
		else if (unavailable)
			verdict += "; SDK reported low latency unavailable";
		if (unapplied)
			verdict += "; requested options were not consistently accepted by the SDK";
		const bool driverCoverage = condition.query && driverQueries >= std::max(2u, condition.seconds / 2) && !driverFailures;
		if (condition.query && !driverCoverage)
			verdict += "; driver readback coverage is incomplete";
		return { { "condition", condition.name }, { "mode", condition.mode }, { "queryState", condition.query }, { "cap", condition.cap },
			{ "presentation", presentation }, { "journalComplete", journalComplete }, { "controlledVrComparison", controlled }, { "invalidContextRecords", invalidContext },
			{ "settingsMismatchRecords", mismatched }, { "cells", cells }, { "driverAssessment", verdict },
			{ "unappliedOptionsRecords", unapplied }, { "stableRenderSettings", stableSettings }, { "driverReadbackCoverage", driverCoverage },
			{ "compositorFaultRecords", compositorFaults },
			{ "sleepCalls", calls }, { "sleepFailures", failures }, { "skipReasons", skipReasons }, { "sleepMs", Distribution(sleep) },
			{ "gpuTelemetry", hardware }, { "driverSleepIntervalUs", Distribution(driverInterval) }, { "renderSettings", renderSettings },
			{ "observerMs", Distribution(observer) }, { "compositorSpanSeconds", previousTime - firstTime },
			{ "hookIntervalMs", Distribution(hook) }, { "sdkQueryMs", Distribution(query) }, { "sdkQueries", sdkQueries },
			{ "sdkFailures", sdkFailures }, { "sdkUnavailableQueries", unavailable }, { "driverQueries", driverQueries },
			{ "driverQueryFailures", driverFailures }, { "driverEnabledQueries", driverEnabled }, { "driverGameSleepQueries", driverSleep },
			{ "clientFrameMs", Distribution(frame) }, { "gpuMs", Distribution(gpu) }, { "submitMs", Distribution(submit) },
			{ "droppedCompositorFrames", droppedFrames }, { "mispresentedFrames", mispresented }, { "repeatedFrames", repeated },
			{ "missingCompositorFrames", gaps }, { "compositorResets", resets }, { "invalidCompositorFrames", invalidFrames } };
	}

	inline json Compare(const json& baseline, const json& candidate)
	{
		const bool comparable = baseline["controlledVrComparison"] == true && candidate["controlledVrComparison"] == true &&
		                        baseline["cells"] == candidate["cells"] && baseline["queryState"] == candidate["queryState"] &&
		                        baseline["presentation"]["actualDescriptor"] == candidate["presentation"]["actualDescriptor"] &&
		                        baseline["presentation"]["foregroundStates"] == candidate["presentation"]["foregroundStates"] &&
		                        baseline["presentation"]["syncIntervals"] == candidate["presentation"]["syncIntervals"] &&
		                        baseline["presentation"]["flags"] == candidate["presentation"]["flags"] &&
		                        baseline["renderSettings"].is_object() && baseline["renderSettings"] == candidate["renderSettings"];
		json result{ { "baseline", baseline["condition"] }, { "candidate", candidate["condition"] },
			{ "coveragePermitsComparison", comparable }, { "causalProof", false },
			{ "interpretation", "Same-session descriptive comparison. Scene, load, movement and external compositor settings must also match. Lower p95/p99 is better; sleep-call duration alone does not establish benefit." } };
		for (const auto* metric : { "p50", "p95", "p99", "max", "stddev" }) {
			const auto& a = baseline["clientFrameMs"][metric];
			const auto& b = candidate["clientFrameMs"][metric];
			result[std::string(metric) + "DeltaMs"] = a.is_number() && b.is_number() ? json(b.get<double>() - a.get<double>()) : json(nullptr);
		}
		return result;
	}

	inline json LimiterControl(const json& baseline, const json& cap)
	{
		const auto comparison = Compare(baseline, cap);
		const auto& before = baseline["clientFrameMs"]["p50"];
		const auto& after = cap["clientFrameMs"]["p50"];
		const bool eligible = comparison["coveragePermitsComparison"] == true && baseline["cap"] == false && cap["cap"] == true && baseline["mode"] == 0 && cap["mode"] == 0 &&
		                      before.is_number() && before.get<double>() < 45 && after.is_number() && cap["sleepCalls"].get<uint64_t>() > 0 && cap["sleepFailures"] == 0;
		const bool observed = eligible && after.get<double>() >= 45 && after.get<double>() <= 60;
		return { { "targetFps", 20 }, { "targetIntervalMs", 50 }, { "observedClientMedianMs", after },
			{ "consistentWithWorkingLimiter", observed }, { "eligibleComparison", eligible },
			{ "assessment", !eligible ? "inconclusive" : observed ? "limiter_response_observed" :
																	"expected_cadence_not_observed" },
			{ "interpretation", "Requires accepted cap options and a faster matched uncapped baseline. A 50 ms cadence supports the limiter path, not an uncapped low-latency benefit or a causal regression verdict." } };
	}
}
#endif
