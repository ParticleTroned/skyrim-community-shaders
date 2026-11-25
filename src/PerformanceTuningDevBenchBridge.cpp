#include "PerformanceTuningDevBenchBridge.h"

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "Api/DevBenchJson.h"

#	include "Api/DevBenchMainThreadDispatch.h"
#	include "BuildProvenance.h"
#	include "Menu/PerformanceTuningRenderer.h"
#	include "Menu/PerformancePage.h"
#	include "Presets/LookProfiles.h"
#	include "Utils/FileSystem.h"
#	include "Menu/PerformanceResultsUI.h"
#	include "Features/PerformanceOverlay/ABTesting/ABComparisonRunner.h"

#	include <DevBenchAPI.h>
#	include <nlohmann/json.hpp>

#	include <atomic>
#	include <cstddef>
#	include <cstdint>
#	include <exception>
#	include <functional>
#	include <stdexcept>
#	include <string>

namespace
{
	using json = nlohmann::json;
	constexpr std::size_t kDefaultTracePageSize = 128;
	constexpr std::size_t kMaximumTracePageSize = 512;
	std::atomic_bool g_installAttempted{ false };
	std::atomic_bool g_registered{ false };

	json RunOnMainThread(std::function<json()> a_run)
	{
		return CSX::Api::RunDevBenchMainThreadTask(SKSE::GetTaskInterface(), std::move(a_run));
	}

	json BuildResult(const json& a_args)
	{
		const std::string action = a_args.value("action", std::string("status"));
		if (action == "visuals_status" || action == "select_visuals_profile" || action == "restore_visuals_profile" || action == "clear_visuals_profile" ||
			action == "import_visuals_profile" || action == "export_visuals_profile" || action == "import_profile" || action == "export_profile") {
			for (const auto* field : { "file", "directory", "profileId" })
				if (a_args.contains(field) && (!a_args[field].is_string() || a_args[field].get_ref<const std::string&>().size() > 4096))
					return { { "error", std::string(field) + " must be a bounded path or identifier" } };
			return RunOnMainThread([a_args, action]() -> json {
				auto& profiles = Presets::LookProfiles::Get();
				const auto file = std::filesystem::u8path(a_args.value("file", ""));
				const auto directory = std::filesystem::u8path(a_args.value("directory", ""));
				const auto id = a_args.value("profileId", "");
				if (action == "import_profile")
					return MenuUI::PerformancePage::ImportProfile(file);
				if (action == "export_profile")
					return MenuUI::PerformancePage::ExportProfile(directory.empty() ? Presets::LookProfiles::RootPath() : directory, id);
				std::string error;
				bool accepted = true;
				if (action == "select_visuals_profile")
					accepted = profiles.Select(id, error);
				if (action == "clear_visuals_profile")
					accepted = profiles.Clear(error);
				if (action == "restore_visuals_profile")
					accepted = profiles.Restore(error);
				if (action == "import_visuals_profile")
					accepted = profiles.Import(file, id, error);
				if (action == "export_visuals_profile")
					accepted = profiles.Export(id, error, directory);
				auto result = profiles.Inspect();
				result["accepted"] = accepted;
				result["settingsPersisted"] = false;
				if (!accepted)
					result["error"] = error;
				return result;
			});
		}
		if (action == "results_status" || action == "refresh_results" || action == "load_result" || action == "export_result" || action == "export_session" || action == "retry_results")
			return RunOnMainThread([a_args] { return MenuUI::PerformanceResultsPage::Action(a_args); });
		if (action == "profile_status" || action == "apply_profile" || action == "capture_profile_choices" || action == "capture_all_profile_choices" || action == "set_user_profile_base" ||
			action == "included_features" || action == "select_included_feature" || action == "restore_included_features" || action == "save_included_features" || action == "capture_current_profile_settings" || action == "edit_profile_feature" || action == "apply_profile_feature" || action == "restore_profile_feature" || action == "close_profile_feature") {
			if (a_args.contains("profile") && !a_args["profile"].is_string())
				return { { "error", "profile must be a string" } };
			if (a_args.contains("featureShortName") && !a_args["featureShortName"].is_string())
				return { { "error", "featureShortName must be a string" } };
			const auto profile = a_args.value("profile", std::string{});
			const auto feature = a_args.value("featureShortName", std::string{});
			if (action == "select_included_feature" && (!a_args.contains("enabled") || !a_args["enabled"].is_boolean()))
				return { { "error", "enabled must be a boolean" } };
			const bool enabled = action == "select_included_feature" && a_args["enabled"].get<bool>();
			return RunOnMainThread([action, profile, feature, enabled]() -> json {
				if (action == "capture_current_profile_settings")
					return MenuUI::PerformancePage::CaptureCurrentSettings();
				if (action == "edit_profile_feature")
					return MenuUI::PerformancePage::BeginFeatureEdit(feature);
				if (action == "apply_profile_feature")
					return MenuUI::PerformancePage::ApplyFeatureEdit();
				if (action == "restore_profile_feature")
					return MenuUI::PerformancePage::RestoreFeatureEdit();
				if (action == "close_profile_feature")
					return MenuUI::PerformancePage::EndFeatureEdit();
				if (action == "included_features")
					return MenuUI::PerformancePage::IncludedFeaturesStatus();
				if (action == "select_included_feature")
					return MenuUI::PerformancePage::SelectIncludedFeature(feature, enabled);
				if (action == "restore_included_features")
					return MenuUI::PerformancePage::RestoreIncludedFeatures();
				if (action == "save_included_features")
					return MenuUI::PerformancePage::SaveIncludedFeatures();
				if (action == "apply_profile")
					return MenuUI::PerformancePage::Apply(profile);
				if (action == "capture_profile_choices")
					return MenuUI::PerformancePage::Capture(feature);
				if (action == "capture_all_profile_choices")
					return MenuUI::PerformancePage::CaptureAll();
				if (action == "set_user_profile_base")
					return MenuUI::PerformancePage::SetBase(profile);
				return MenuUI::PerformancePage::Status();
			});
		}
		if (action == "comparison_status")
			return ABComparisonRunner::Get().Status();
		if (action == "cancel" && ABComparisonRunner::Get().IsRunning())
			return ABComparisonRunner::Get().Submit({ { "action", "cancel_comparison" } });
		if (action == "capture_comparison_preset" || action == "clear_comparison_presets" ||
			action == "start_setting_comparison" || action == "start_preset_comparison" || action == "cancel_comparison")
			return ABComparisonRunner::Get().Submit(a_args);
		if (action != "status" && action != "start_quick_scan" && action != "start_quick_scan_batch" && action != "start_feature_cost" &&
			action != "start_feature_costs" && action != "set_feature_enabled" &&
			action != "start_upscaling_sweep" && action != "cancel") {
			return {
				{ "error", "unknown action" },
				{ "action", action },
				{ "supported", json::array({ "status", "start_quick_scan", "start_quick_scan_batch", "start_feature_cost", "start_feature_costs", "set_feature_enabled", "start_upscaling_sweep", "cancel", "comparison_status", "capture_comparison_preset", "clear_comparison_presets", "start_setting_comparison", "start_preset_comparison", "cancel_comparison", "profile_status", "apply_profile", "capture_profile_choices", "capture_all_profile_choices", "set_user_profile_base", "included_features", "select_included_feature", "restore_included_features", "save_included_features", "capture_current_profile_settings", "edit_profile_feature", "apply_profile_feature", "restore_profile_feature", "close_profile_feature", "visuals_status", "select_visuals_profile", "clear_visuals_profile", "restore_visuals_profile", "import_visuals_profile", "export_visuals_profile", "import_profile", "export_profile" }) },
			};
		}

		if (a_args.contains("traceAfterSequence") && !a_args.at("traceAfterSequence").is_number_unsigned()) {
			return { { "error", "traceAfterSequence must be a non-negative integer" } };
		}
		if (a_args.contains("maximumTraceSamples") && !a_args.at("maximumTraceSamples").is_number_unsigned()) {
			return { { "error", "maximumTraceSamples must be an integer from 1 to 512" } };
		}

		const std::uint64_t traceAfterSequence = a_args.value("traceAfterSequence", std::uint64_t{ 0 });
		const std::size_t maximumTraceSamples = a_args.value(
			"maximumTraceSamples",
			kDefaultTracePageSize);
		if (maximumTraceSamples < 1 || maximumTraceSamples > kMaximumTracePageSize) {
			return { { "error", "maximumTraceSamples must be an integer from 1 to 512" } };
		}

		const std::string matrix = a_args.value("matrix", std::string("auto"));
		if (matrix != "auto" && matrix != "nvidia" && matrix != "amd") {
			return {
				{ "error", "matrix must be auto, nvidia, or amd" },
				{ "matrix", matrix },
			};
		}
		if (a_args.contains("dlssPreset") && !a_args.at("dlssPreset").is_string())
			return { { "error", "dlssPreset must be J, K, L, M, F, or E" } };
		const std::string dlssPreset = a_args.value("dlssPreset", std::string());
		if (a_args.contains("featureShortName") && !a_args.at("featureShortName").is_string())
			return { { "error", "featureShortName must be a string" } };
		const std::string featureShortName = a_args.value("featureShortName", std::string());
		if (action == "set_feature_enabled" &&
			(!a_args.contains("enabled") || !a_args.at("enabled").is_boolean()))
			return { { "error", "set_feature_enabled requires a boolean enabled value" } };
		const bool enabled = a_args.value("enabled", false);
		if (a_args.contains("recordFpsVR") && !a_args["recordFpsVR"].is_boolean())
			return { { "error", "recordFpsVR must be a boolean" } };
		const bool recordFpsVR = a_args.value("recordFpsVR", false);

		return RunOnMainThread([action,
								   matrix,
								   dlssPreset,
								   featureShortName,
								   enabled,
								   recordFpsVR,
								   traceAfterSequence,
								   maximumTraceSamples]() -> json {
			if (action == "start_quick_scan" || action == "start_quick_scan_batch")
				return PerformanceTuningRenderer::StartApiQuickScan(action == "start_quick_scan_batch", recordFpsVR);
			if (action == "start_feature_cost")
				return PerformanceTuningRenderer::StartApiFeatureCostMeasurement(featureShortName);
			if (action == "start_feature_costs")
				return PerformanceTuningRenderer::StartApiFeatureCostBatch();
			if (action == "set_feature_enabled")
				return PerformanceTuningRenderer::SetApiFeatureEnabled(featureShortName, enabled);
			if (action == "start_upscaling_sweep")
				return PerformanceTuningRenderer::StartApiUpscalingCostSweep(matrix, dlssPreset);
			if (action == "cancel")
				return PerformanceTuningRenderer::CancelApiMeasurements();
			return PerformanceTuningRenderer::GetApiMeasurementStatus(
				traceAfterSequence,
				maximumTraceSamples);
		});
	}

	void ToolHandler(void*, const char* a_argsJson, void* a_sink, DevBenchAPI::WriteFn a_write) noexcept
	{
		json output;
		try {
			json args = json::object();
			if (a_argsJson && *a_argsJson)
				args = json::parse(a_argsJson);
			if (!args.is_object())
				throw std::runtime_error("arguments must be a JSON object");
			if (auto mismatch = BuildProvenance::ValidateExpectedBuild(args))
				output = std::move(*mismatch);
			else
				output = BuildResult(args);
		} catch (const std::exception& e) {
			output = { { "error", "invalid request" }, { "detail", e.what() } };
		} catch (...) {
			output = { { "error", "unknown handler error" } };
		}

		BuildProvenance::AttachProducer(output);
		try {
			const std::string serialized = CSX::Api::DumpForDevBench(output);
			a_write(a_sink, serialized.c_str());
		} catch (...) {
			a_write(a_sink, R"({"error":"response serialization failed"})");
		}
	}
}

namespace PerformanceTuningDevBenchBridge
{
	void Install()
	{
		if (g_installAttempted.exchange(true, std::memory_order_acq_rel))
			return;
		auto* devBench = DevBenchAPI::GetDevBenchInterface001();
		if (!devBench) {
			logger::info("PerformanceTuningDevBenchBridge: devbench host not present; tool not registered");
			return;
		}

		static constexpr const char* descriptor =
			R"json({"description":"capture_current_profile_settings copies ALL current CSX settings, including global settings and retained unavailable feature sections, to MyPerformance and saves normal settings. edit_profile_feature opens the native feature editor without the sidebar; featureShortName is required. Changes affect the game immediately. apply_profile_feature stores all settings for the edited feature in MyPerformance and saves normal CSX settings. restore_profile_feature restores opening game settings without saving. close_profile_feature returns to the tiles and retains live edits. included_features reports editingFeature. MyPerformance exports now preserve full settings as well as selections; version-1 profiles remain readable. export_session exports only finished measurements from this game process into one HTML report with expandable settings for each run; previous sessions are excluded. HTML comparisons require both runs from the current game session. Rows unavailable in every compared run are hidden; missing values in retained rows are Unavailable. results_status includes sessionRuns and sessionReport. Optional fpsVR preparation has a separate thirty-second deadline and overlaps scene settling; capture waits for both without restarting settling. start_quick_scan_batch captures three 300-frame scans with at least three-second gaps, settling within each gap, and automatically exports one CSV beside saved results. It preserves partial results on failure and includes source commit, Build ID, repeat index and UTC capture markers. Batch summaries use only that run's three scan means for mean and standard error; experiments remain separate columns. Small nonzero uncertainty is retained. Native CSX whole-frame Frame/FPS/CPU/GPU samples are captured independently of fpsVR; missing samples invalidate the affected native metric. A confirmed active fpsVR recording is stopped before a fresh per-run CSV starts. The fresh recording is stopped after the batch while the application remains open; an unconfirmed interruption prevents restart, and unconfirmed cleanup is reported separately from valid timings. Reply and event strings replace invalid UTF-8 with U+FFFD; request validation remains strict. WeatherPicker, PerformanceOverlay, Screenshot, and UnifiedWater are excluded from Performance Tuning, including batches and set_feature_enabled. Their native feature controls remain available. Overview toggles report requested enabled state separately from measurementEnabled and readiness; toggleBlockReason explains native scene/weather ownership. comparisonDetails identifies the precise off-state, including partial feature groups. Batch comparisons skip inactive or controlled features and stop if the gameplay scene changes. Cancellation restores state before completion. Run and inspect nonblocking closed-menu performance measurements. status performs no setup mutation and reports surfaced feature-cost capabilities, completed results, active ownership, readiness, and a paged 100 ms Game/GPU/CPU trace. VR timingDefinitionVersion 2 uses completed unique compositor frames: frameMs is ClientFrameIntervalMs, gameGpuMs is TotalRenderGpuMs, and gameCpuMs is NewFrameReadyMs - NewPosesReadyMs + CompositorRenderCpuMs (excluding pose wait). metricDefinitions describes runtime, formulas and display statistics. VR live counters show means and peaks over one second; Current-Off results remain means with standard errors. VR traces expose compositorFrameIndex, compositorSystemTimeSeconds, timingEpoch, sceneGpuMs and poseWaitMs; no new GPU queries are used. SE/AE uses QPC and asynchronous D3D11 whole-frame queries with unchanged measurement phases. Flat D3D12 frame-generation CPU/GPU metrics are unavailable. start_quick_scan settles for three uninterrupted ready seconds, then captures 300 frames for all instrumented features together, with a thirty-second deadline. It changes no feature settings, preserves the original menu and profiler enabled state, and rejects concurrent A/B or bounded profiler captures. quickScan reports per-feature CPU/GPU self-time means in milliseconds, never on/off savings. Sparse scopes include preceding idle frames only when those frames are valid; shared-only, unobserved, and incomplete histories remain null. Shared shaders, uninstrumented work and other GPU queues are excluded, with coverage on each result. Disabled and scene-inapplicable features do not delay the scan. Cancellation, cell or settings changes, global shader enable/disable, shader definition or blocking changes, compilation, profiler capacity loss and invalid queries invalidate the scan; renderConfiguration records the observed shader controls; invalidGpuFrames and invalidCpuFrames distinguish resolved progress from usable data. start_feature_cost measures one surfaced feature relative to Off, or None for Upscaling, using its current settings and the same protocol as the UI: two-second initial settling, six 500 ms current windows, three-second Off settling (ten seconds for Upscaling None), six 500 ms comparison windows, and one-second exact restoration with verification. Nominal run times are twelve seconds, or nineteen for Upscaling; readiness waits and timing interruptions can extend them. status.timing reports measurementIntervalMs, measurementBlockCount, upscalingComparisonWaitMs, restoreWaitMs and postRunCooldownMs; timing.comparisonWaitMs is the default for ordinary features, while active measurement and availableFeatureCosts report the actual comparisonWaitMs and expectedRunMs for their feature. It accepts either an open CS menu, which is restored afterward, or an already closed menu, which remains closed. start_feature_costs measures every eligible enabled runtime feature sequentially with the same on/off protocol and five-second inter-feature cooldown, restoring each feature before continuing and reopening the original menu after the batch. set_feature_enabled changes the runtime toggle and preserves its enabled configuration across off/on; toggles are rejected during any measurement. status includes featureBatch progress and per-metric costPercent, current, comparison, and costPercentBasis. Timing percentages are (current - off) / current * 100; FPS loss is (off - current) / off * 100. Missing metrics remain unavailable. cancel cancels DevBench-owned quick scans, batches, single measurements, sweeps and captured-state comparisons. start_upscaling_sweep measures every target relative to None using the same two-second initial settling, six 500 ms target windows, ten-second None wait, six 500 ms None windows, one-second exact restoration with verification, and five-second inter-case cooldown. auto selects the NVIDIA matrix on a DLSS-capable NVIDIA system or the AMD matrix on a runtime-capable AMD system. NVIDIA requires a caller-selected DLSS profile (J, K, L, M, F, or E), applies it to every DLSS/DLAA case, and also measures FSR 3.1.6. A start without dlssPreset returns promptRequired without changing runtime state. AMD measures FSR 3.1.6 and FSR Automatic across Native AA plus every render-scale quality and fails closed instead of labeling a latched host fallback as runtime FSR. Every response identifies the exact producing DLL; expectedBuildId fails closed on a stale build. Captured-state A/B actions: capture_comparison_preset retains up to four named in-memory presets of eligible reversible runtime feature states; comparison_status lists their feature names and run status without mutation; a successful capture publishes capturedStates in lastRequest. start_setting_comparison compares the current state with one scalar at path from preset for featureShortName; start_preset_comparison compares the current state with that captured preset as one bundle. Values come only from captured runtime states, not arbitrary configuration patches. Upscaling and VR cross-feature controls use their dedicated tools. Scene-owned, constrained and restart-dependent changes are refused. The runner is available only in DevBench builds. Commands return queued/requestId; comparison_status.lastRequest reports render-thread acceptance or rejection. Runs alternate A-B-A, with repeats 1-5 (default 2), intervalSeconds 1-15 (default 4), settleSeconds 1-15 (default 5, increased for feature requirements), then restore the baseline. Complete bracketing, at least ten samples and 80 percent coverage per interval are required for a verdict. Missing metrics remain null. Uncertainty is a heuristic envelope, not a confidence interval. VR GPU uses CSX TotalRenderGpuMs including compositor work; flat uses whole-frame D3D11 timings. Original source-frame identity routes late results. Collection is bounded to 4096 samples per metric/interval. Settings are checked at 250 ms intervals and before each switch; detected settings, scene, timing-source or ownership changes stop the run; restoration preserves intervening edits. cancel_comparison restores owned settings; restoration waits safely through load activity, with a 30-second admission deadline and a 30-second readiness deadline. Failed restoration retains measurement ownership and cancel_comparison retries it; a replacement configuration supersedes restoration. clear_comparison_presets releases captured presets when idle. No settings files are written. Active profiling and setting transitions add unmeasured overhead. profile_status inspects the four Performance profiles. apply_profile applies one profile; capture_profile_choices saves the named feature switch and recognised feature preset, or Upscaling selections, to MyPerformance; set_user_profile_base changes its base without applying it. Built-in and selection-only profiles preserve individual visual tuning except parameters deliberately owned by a selected feature preset. MyPerformance additionally applies any saved custom feature or global settings. profile_status matches compares performance switches, named presets and upscaling choices. Measurements, save/load activity and scene-owned settings block mutation. Unloaded and startup-disabled features are skipped in skippedUnavailableFeatures. Native Upscaling admission owns deferred settings; acceptance does not prove transition completion. A rejection restores native feature settings and reversible-toggle history. Save Settings persists selections; these actions do not write files. capture_all_profile_choices captures all currently editable performance choices, including disabled runtime features, to MyPerformance without applying settings. It shares the Compare and Quick scan all features save button. capturedFeatures lists saved feature IDs; skippedFeatures maps unavailable or controlled feature IDs to reasons, and retains their existing saved choices. The batch updates MyPerformance only after all captured choices validate. Active measurements block capture to avoid saving temporary comparison states. Save CSX settings persists MyPerformance. Included Features stages On/Off selections without changing live settings. included_features lists loaded, startup-enabled features with Enabled controls and their current/selected states, plus read-only Unified Water and Reverse Z tiles. editable is false for restart-only tiles; readOnlyReason explains the required settings page and restart. Reverse Z selected/currentEnabled reflect the requested startup setting rather than current-session activation. Restart-only switches cannot be changed by the tiles; legacy saved tile switches for them are discarded when loading profiles. Full settings snapshots retain requested startup settings, which take effect after restart. select_included_feature edits a draft using featureShortName and enabled. restore_included_features resets that draft from current Enabled states. save_included_features stores the draft in MyPerformance; only apply_profile applies it to the game. Draft edits and saves never change startup loading or live feature settings. Utilities with Enabled controls are eligible even without timing instrumentation. Availability is rechecked before saving; failures preserve MyPerformance. Saved results persist under SKSE/CSX/Performance. refresh_results queues a disk refresh; load_result selects resultId as first or comparison=true as second. results_status returns history, selected records, comparison and worker errors. export_result queues HTML/CSV generation for resultId and optional comparisonId. It opens a desktop folder picker (default history folder), or accepts an explicit existing absolute directory. Jobs are excluded from timed measurements. Poll results_status for completion. retry_results retries failed automatic saves. Visuals profile actions use the same validated transaction as the UI. visuals_status reports each pack's status (compatible, unavailable, invalid), concise summary, validation errors and unavailable requirements. Native Post Processing remains unavailable until its rendering and ownership support are integrated. Disabled unsupported documents and BMP/DDS/PNG assets survive profile exchange. One Visuals pack owns appearance; SettingsUser saves the selection and personal edits. Imports accept native pack folders/manifests and legacy Visuals files; E11 and missing required rendering capabilities are rejected without changing the current profile. Export writes a new complete pack under directory (default CommunityShaders/Presets). No existing profile is overwritten. import_profile stages a MyPerformance JSON without applying it; export_profile writes its base, saved choices and full settings as one JSON. Apply MyPerformance remains explicit. All reads/mutations run on the game thread; normal loading/measurement guards apply. Use Save settings to persist selections; exports do not save running settings.","inputSchema":{"type":"object","additionalProperties":false,"properties":{"recordFpsVR":{"type":"boolean","default":false,"description":"For quick scans, optionally use the running fpsVR command and verify raw samples. A confirmed active recording is stopped before a fresh per-run CSV starts, which is stopped after the batch. Absent or unverified fpsVR is omitted; uncertain toggles are never retried. The fpsVR app stays open. Requires VR. A combined HTML report is saved automatically."},"action":{"type":"string","enum":["status","start_quick_scan","start_quick_scan_batch","start_feature_cost","start_feature_costs","set_feature_enabled","start_upscaling_sweep","cancel","comparison_status","capture_comparison_preset","clear_comparison_presets","start_setting_comparison","start_preset_comparison","cancel_comparison","profile_status","apply_profile","capture_profile_choices","set_user_profile_base","capture_all_profile_choices","included_features","select_included_feature","restore_included_features","save_included_features","capture_current_profile_settings","edit_profile_feature","apply_profile_feature","restore_profile_feature","close_profile_feature","results_status","refresh_results","load_result","export_result","export_session","retry_results","visuals_status","select_visuals_profile","restore_visuals_profile","clear_visuals_profile","import_visuals_profile","export_visuals_profile","import_profile","export_profile"],"default":"status"},"featureShortName":{"type":"string","minLength":1,"description":"Feature short name; required by feature measurement/switch actions, capture_profile_choices and select_included_feature. Measurement actions accept only eligible features from status.availableFeatureCosts. Included Features selection accepts only IDs whose included_features entry has editable=true, including utility Enabled controls. Restart-only entries are read-only."},"matrix":{"type":"string","enum":["auto","nvidia","amd"],"default":"auto","description":"Hardware matrix. auto selects from the active adapter and runtime capabilities."},"dlssPreset":{"type":"string","enum":["J","K","L","M","F","E"],"description":"Required to start the NVIDIA matrix; omitted starts return a prompt without changing runtime state."},"traceAfterSequence":{"type":"integer","minimum":0,"description":"Return raw timing samples after this sequence. VR samples expose compositorFrameIndex, compositorSystemTimeSeconds and timingEpoch for their completed source, plus sceneGpuMs and poseWaitMs details. Flat samples also expose gpuCpuFrameCount and gpuCpuPresentId for delayed CPU/GPU source identity."},"maximumTraceSamples":{"type":"integer","minimum":1,"maximum":512,"default":128,"description":"Maximum raw timing samples returned by status."},"expectedBuildId":{"type":"string","description":"Exact 64-character CSX Build ID required for this operation."},"enabled":{"type":"boolean","description":"Required by set_feature_enabled (changes the running feature) or select_included_feature (stages On/Off for an editable MyPerformance tile without changing the running feature). Unified Water and Reverse Z tiles reject edits."},"repeats":{"type":"integer","minimum":1,"maximum":5,"default":2},"intervalSeconds":{"type":"integer","minimum":1,"maximum":15,"default":4},"settleSeconds":{"type":"integer","minimum":1,"maximum":15,"default":5},"preset":{"type":"string","minLength":1,"maxLength":64},"path":{"type":"string","maxLength":256,"description":"JSON pointer within the captured reversible feature state; empty selects a scalar root."},"profile":{"type":"string","enum":["Performance","Balanced","Quality","MyPerformance","User defined"],"description":"Required for apply_profile and set_user_profile_base. A user base must be one of the three built-in tiers."},"resultId":{"type":"string","maxLength":100,"description":"Saved run id from results_status.history."},"comparisonId":{"type":"string","maxLength":100},"comparison":{"type":"boolean","description":"Load into the second comparison selection."},"format":{"type":"string","enum":["html","csv","both"],"default":"both"},"directory":{"type":"string","description":"Optional existing absolute export folder; omission opens the desktop folder picker."},"file":{"type":"string","maxLength":4096,"description":"Source profile JSON file or Visuals pack folder. Required for imports."},"profileId":{"type":"string","minLength":1,"maxLength":100,"description":"Installed Visuals pack id for select; new portable profile name for import/export."}},"allOf":[{"if":{"properties":{"action":{"enum":["start_feature_cost","start_feature_costs","set_feature_enabled","start_setting_comparison"]}},"required":["action"]},"then":{"properties":{"featureShortName":{"not":{"enum":["WeatherPicker","PerformanceOverlay","Screenshot","UnifiedWater"]}}}}}]},"outputSchema":{"type":"object","description":"Visuals actions also return packs with compatible, status, summary, errors, unavailable and notices. Invalid UTF-8 in output strings is replaced with U+FFFD; valid UTF-8 output is unchanged. Comparison commands return queued/requestId. comparison_status publishes lastRequest acceptance, phase, restoration, captured presets, interval source-frame ranges and counts, and frame/GPU/CPU baseline/change/delta/uncertainty in milliseconds; invalid statistics are null.","properties":{"packs":{"type":"array","items":{"type":"object","properties":{"compatible":{"type":"boolean"},"status":{"type":"string","enum":["compatible","unavailable","invalid"]},"summary":{"type":"string"},"errors":{"type":"array","items":{"type":"string"}},"unavailable":{"type":"array","items":{"type":"string"}},"notices":{"type":"array","items":{"type":"string"}}}}},"history":{"type":"array","items":{"type":"object"}},"busy":{"type":"boolean"},"first":{"type":["object","null"]},"second":{"type":["object","null"]},"comparison":{"type":"object"},"unsaved":{"type":"integer"},"folder":{"type":"string"},"message":{"type":"string"}}}})json";
		devBench->RegisterTool(
			"communityshaders.performance_tuning",
			descriptor,
			&ToolHandler,
			nullptr);
		g_registered.store(true, std::memory_order_release);
		logger::info(
			"PerformanceTuningDevBenchBridge: registered communityshaders.performance_tuning with devbench build {}",
			devBench->GetBuildNumber());
	}

	bool IsBuilt() { return true; }
	bool IsRegistered() { return g_registered.load(std::memory_order_acquire); }
}

#else

namespace PerformanceTuningDevBenchBridge
{
	void Install() {}
	bool IsBuilt() { return false; }
	bool IsRegistered() { return false; }
}

#endif
