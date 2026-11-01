#include "AdvancedSettingsRenderer.h"
#include "I18n/I18n.h"
#include "Menu/SettingsPage.h"
#include "SettingsActions.h"
#include "Utils/WinApi.h"

#include <algorithm>
#include <cfloat>
#include <format>
#include <imgui.h>
#include <imgui_stdlib.h>
#include <thread>

#include "CSEditor/EditorWindow.h"
#include "FeatureIssues.h"
#include "Features/PerformanceOverlay/ABTesting/ABTesting.h"
#include "Features/RenderDoc.h"
#include "Fonts.h"
#include "Globals.h"
#include "Menu.h"
#include "ShaderCache.h"
#include "State.h"
#include "Util.h"
#include "Utils/Format.h"
#include "Utils/UI.h"

void AdvancedSettingsRenderer::RenderAdvancedSettings(
	const std::function<void()>& drawDisableAtBootSettings)
{
	MenuUI::SettingsPage page("Advanced", {
											  { "startup", T("menu.advanced.startup", "Startup"), T("menu.advanced.choose_which_features_load_when_the_game_starts", "Choose which features load when the game starts."), T("menu.advanced.features_loaded_at_startup", "Features loaded at startup"), true, true, T("menu.advanced.choose_startup_and_compilation", "Choose startup and compilation") },
											  { "shaders", T("menu.settings.tab_shaders", "Shaders"), T("menu.advanced.refine_compilation_and_shader_replacement", "Refine compilation and shader replacement."), T("menu.advanced.compilation_and_replacement", "Compilation and replacement"), true, true, nullptr },
											  { "diagnostics", T("menu.advanced.diagnostics", "Diagnostics"), T("menu.advanced.choose_logging_and_inspect_runtime_state", "Choose logging and inspect runtime state."), T("menu.advanced.logs_and_runtime_state", "Logs and runtime state"), true, false, T("menu.advanced.inspect_and_record", "Inspect and record") },
											  { "capture", T("menu.advanced.capture", "Capture"), T("menu.advanced.choose_frame_capture_and_recording_options", "Choose frame capture and recording options."), T("menu.advanced.frame_capture_and_storage", "Frame capture and storage"), true, true, nullptr },
											  { "testing", T("menu.advanced.tab_testing", "Testing"), T("menu.advanced.compare_controlled_settings_and_test_scenes", "Compare controlled settings and test scenes."), T("menu.advanced.controlled_scene_comparisons", "Controlled scene comparisons"), true, true, T("menu.advanced.validate_your_setup", "Validate your setup") },
										  });
	if (page.Is("startup"))
		RenderDisableAtBootSection(drawDisableAtBootSettings);
	if (page.Is("shaders"))
		RenderShadersSection();
	if (page.Is("diagnostics"))
		RenderDiagnosticsSection();
	if (page.Is("capture")) {
		if (MenuUI::DetailNote(T("menu.advanced.renderdoc_has_its_own_feature_page_for_capture_setup_storage_and_recorded_files", "RenderDoc has its own feature page for capture setup, storage and recorded files."), T("menu.advanced.open_renderdoc", "Open RenderDoc")))
			globals::menu->SelectFeatureMenu(globals::features::renderDoc.GetShortName());
	}
	if (page.Is("testing"))
		RenderTestingSection();
}

// -----------------------------------------------------------------------------
// Shaders tab
// -----------------------------------------------------------------------------

void AdvancedSettingsRenderer::RenderShadersSection()
{
	RenderShaderCompileFlags();

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	RenderShaderThreading();

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	RenderShaderCacheControls();

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	RenderShaderReplacementTable();

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	RenderShaderCompileStatistics();
	RenderCompileTraceExport();
}

void AdvancedSettingsRenderer::RenderShaderCompileFlags()
{
	auto shaderCache = globals::shaderCache;

	Util::DrawSectionHeader(T("menu.advanced.compile_flags", "Compile Flags"));
	const char* blockReason = MenuUI::GetGlobalSettingsActionBlockReason(MenuUI::GlobalSettingsAction::ClearShaderCache);
	if (blockReason)
		MenuUI::DetailNote(blockReason);
	ImGui::BeginDisabled(blockReason != nullptr);
	const auto endDisabled = SKSE::stl::scope_exit([] { ImGui::EndDisabled(); });

	// Keep drafts private until application; a hidden field abandons its draft.
	static std::string shaderDefines;
	static std::string appliedDefines;
	static bool editingDefines = false;
	static uint32_t lastDefinesFrame = 0;
	const auto frame = globals::state->frameCount;
	if (!editingDefines || (frame != lastDefinesFrame && frame != lastDefinesFrame + 1u)) {
		shaderDefines = globals::state->GetShaderDefinesSnapshot()->canonicalText;
		appliedDefines = shaderDefines;
	}
	lastDefinesFrame = frame;
	const bool applyDefines = Util::Widgets::InputText(T("menu.advanced.shader_defines", "Shader Defines"), &shaderDefines, ImGuiInputTextFlags_EnterReturnsTrue);
	editingDefines = ImGui::IsItemActive();
	if ((applyDefines || ImGui::IsItemDeactivatedAfterEdit()) &&
		!ImGui::IsKeyPressed(ImGuiKey_Escape, false) && shaderDefines != appliedDefines) {
		const auto previousDefines = globals::state->GetShaderDefinesSnapshot();
		if (shaderDefines != previousDefines->canonicalText) {
			globals::state->SetDefines(shaderDefines);
			if (globals::state->GetShaderDefinesSnapshot()->canonicalText != previousDefines->canonicalText)
				shaderCache->Clear();
		}
		appliedDefines = shaderDefines;
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(T("menu.advanced.semicolon_separated_shader_defines_leave_empty_to_clear_press_enter_or_leave_the_fiel", "Semicolon-separated shader defines; leave empty to clear. Press Enter or leave the field to apply. Closing this page discards unapplied edits. Compute shaders need a restart to recompile."));
	}

	// Half-precision (partial precision) shader compile flag
	bool partialPrecision = globals::state->enablePartialPrecision.load(std::memory_order_relaxed);
	if (Util::Widgets::Checkbox(T("menu.advanced.half_precision", "Half Precision (Partial Precision)"), &partialPrecision)) {
		globals::state->enablePartialPrecision.store(partialPrecision, std::memory_order_relaxed);
		// Force a recompile so the flag actually takes effect on subsequent shader builds.
		shaderCache->Clear();
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			T("menu.advanced.request_partial_precision_with_d3dcompile_partial_precision_image_quality_and_perform", "Request partial precision with D3DCOMPILE_PARTIAL_PRECISION. Image quality and performance depend on the shader and GPU. Changing this clears the cache; shaders recompile as needed."));
	}

	// Avoid flow control compiler flag (transient — not saved to config because the
	// right setting depends on the current scene, not the user).
	bool avoidFlowControl = globals::state->enableAvoidFlowControl.load(std::memory_order_relaxed);
	if (Util::Widgets::Checkbox(T("menu.advanced.avoid_flow_control", "Avoid Flow Control"), &avoidFlowControl)) {
		globals::state->enableAvoidFlowControl.store(avoidFlowControl, std::memory_order_relaxed);
		// Force a recompile so the flag actually takes effect on subsequent shader builds.
		shaderCache->Clear();
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			T("menu.advanced.ask_the_compiler_to_avoid_flow_control_instructions_with_d3dcompile_avoid_flow_contro", "Ask the compiler to avoid flow-control instructions with D3DCOMPILE_AVOID_FLOW_CONTROL. May help short branches but add work when skipped branches are evaluated. Resets each launch. Changing this clears the cache; shaders recompile as needed."));
	}
}

void AdvancedSettingsRenderer::RenderShaderThreading()
{
	auto shaderCache = globals::shaderCache;

	Util::DrawSectionHeader(T("menu.advanced.threading", "Threading"));

	// The pool is deliberately capped at the responsive startup default. Do not
	// offer values above its actual worker count: they cannot increase throughput
	// and previously made persisted settings misleading.
	const int32_t poolThreads = static_cast<int32_t>(shaderCache->compilationPool.get_thread_count());
	const int32_t maxThreads = std::max(1, poolThreads);

	// Snap the persisted values back into the valid range — a stale config can
	// otherwise leave compilationThreadCount above maxThreads, which would
	// render the slider in an out-of-range state.
	shaderCache->compilationThreadCount = std::clamp(shaderCache->compilationThreadCount, 1, maxThreads);
	shaderCache->backgroundCompilationThreadCount = std::clamp(shaderCache->backgroundCompilationThreadCount, 1, maxThreads);

	Util::Widgets::SliderInt(T("menu.advanced.compiler_threads", "Compiler Threads"), &shaderCache->compilationThreadCount, 1, maxThreads, "%d", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			T("menu.advanced.startup_compile_threads_default_75_of_logical_cpus_including_e_cores_at_cooperative_o", "Startup compile threads. Default: 75%% of logical CPUs, including E-cores, at cooperative OS priority to leave system headroom. Higher compiles faster but may reduce responsiveness."));
	}
	Util::Widgets::SliderInt(T("menu.advanced.background_compiler_threads", "Background Compiler Threads"), &shaderCache->backgroundCompilationThreadCount, 1, maxThreads, "%d", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			T("menu.advanced.gameplay_compile_threads_default_half_the_performance_cores_to_protect_render_thread_", "Gameplay compile threads. Default: half the performance cores to protect render-thread time; workers resume normal game priority after startup. Higher compiles faster but may stutter."));
	}

	auto& menuSettings = globals::menu->GetSettings();
	Util::Widgets::Checkbox(T("menu.advanced.background_compile_on_boot", "Background Compile on Boot"), &menuSettings.BackgroundShaderCompilationOnBoot);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			T("menu.advanced.open_the_menu_immediately_compile_shaders_in_the_background_at_startup_skips_waiting_", "Open the menu immediately; compile shaders in the background at startup. Skips waiting, not compilation. Applies next launch; default off."));
	}

	Util::Widgets::Checkbox(T("menu.advanced.hide_background_compilation_at_info", "Hide Background Compilation at Info Level"), &menuSettings.HideBackgroundCompilationAtInfoLevel);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T("menu.advanced.hide_background_compilation_at_info_tooltip", "Hides the routine background-compilation progress overlay only while Log Level is info. Blocking compilation and important diagnostics remain visible. Other log levels keep the progress overlay visible."));
	}
}

void AdvancedSettingsRenderer::RenderShaderCacheControls()
{
	auto shaderCache = globals::shaderCache;

	Util::DrawSectionHeader(T("menu.advanced.cache_file_watcher", "Cache & File Watcher"));

	// File Watcher option
	bool useFileWatcher = shaderCache->UseFileWatcher();
	if (Util::Widgets::Checkbox(T("menu.advanced.enable_file_watcher", "Enable File Watcher"), &useFileWatcher)) {
		shaderCache->SetFileWatcher(useFileWatcher);
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			T("menu.advanced.automatically_recompile_shaders_on_file_change_intended_for_development", "Automatically recompile shaders on file change. Intended for development."));
	}

	// Dump Shaders option
	bool useDump = shaderCache->IsDump();
	if (Util::Widgets::Checkbox(T("menu.advanced.dump_shaders", "Dump Shaders"), &useDump)) {
		shaderCache->SetDump(useDump);
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(T("menu.advanced.dump_shaders_at_startup_for_reverse_engineering_unneeded_for_normal_use", "Dump shaders at startup for reverse engineering. Unneeded for normal use."));
	}

	MenuUI::DrawGlobalSettingsButton(MenuUI::GlobalSettingsAction::ClearShaderCache, T("menu.advanced.clear_shader_cache", "Clear Shader Cache"), { -1, 0 });
	if (auto count = shaderCache->GetLastScopedClearCount(); count > 0) {
		ImGui::TextDisabled(
			T("menu.advanced_settings_renderer.last_smart_clear_zu_shader_s_1f_ms", "Last smart clear: %zu shader(s) (%.1f ms)"),
			count,
			shaderCache->GetLastScopedClearMs());
	}
}

void AdvancedSettingsRenderer::RenderShaderReplacementTable()
{
	auto state = globals::state;

	Util::DrawSectionHeader(T("menu.advanced.replace_original_shaders", "Replace Original Shaders"));

	if (ImGui::BeginTable("##ReplaceToggles", 3, ImGuiTableFlags_SizingStretchSame)) {
		globals::state->ForEachShaderTypeWithIndex([&](auto type, int classIndex) {
			ImGui::TableNextColumn();

			if (!(SIE::ShaderCache::IsSupportedShader(type) || state->IsDeveloperMode())) {
				ImGui::BeginDisabled();
				Util::Widgets::Checkbox(std::format("{}", magic_enum::enum_name(type)).c_str(), &state->enabledClasses[classIndex]);
				ImGui::EndDisabled();
			} else
				Util::Widgets::Checkbox(std::format("{}", magic_enum::enum_name(type)).c_str(), &state->enabledClasses[classIndex]);
		});
		if (state->IsDeveloperMode()) {
			Util::Widgets::Checkbox(T("menu.advanced.vertex", "Vertex"), &state->enableVShaders);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					T("menu.advanced.replace_vertex_shaders_for_the_types_above_off_uses_vanilla_shaders_for_developer_com", "Replace vertex shaders for the types above. Off uses vanilla shaders for developer comparisons."));
			}

			Util::Widgets::Checkbox(T("menu.advanced.pixel", "Pixel"), &state->enablePShaders);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					T("menu.advanced.replace_pixel_shaders_for_the_types_above_off_uses_vanilla_shaders_for_developer_comp", "Replace pixel shaders for the types above. Off uses vanilla shaders for developer comparisons."));
			}

			Util::Widgets::Checkbox(T("menu.advanced.compute", "Compute"), &state->enableCShaders);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					T("menu.advanced.replace_compute_shaders_for_the_types_above_off_uses_vanilla_shaders_for_developer_co", "Replace compute shaders for the types above. Off uses vanilla shaders for developer comparisons."));
			}
		}
		ImGui::EndTable();
	}
}

void AdvancedSettingsRenderer::RenderShaderCompileStatistics()
{
	auto shaderCache = globals::shaderCache;

	if (!ImGui::TreeNodeEx(T("menu.advanced.statistics", "Statistics"))) {
		return;
	}

	ImGui::Text(T("menu.advanced.shader_compiler_s", "Shader Compiler : %s"), shaderCache->GetShaderStatsString().c_str());

	// Derived parallelism metrics are computed lazily on demand and only shown
	// once compilation has completed to avoid per-frame analysis while compiling.
	if (!shaderCache->IsCompiling()) {
		auto parallelism = shaderCache->GetParallelismStats();
		if (parallelism.has_value()) {
			const auto& p = parallelism.value();
			ImGui::Spacing();
			ImGui::TextDisabled(T("menu.advanced.parallelism_header", "Parallelism (derived from %zu compiled tasks)"), p.sampleCount);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(T("menu.advanced.parallelism_tooltip_1", "Computed lazily from the last completed build."));
				ImGui::Text(T("menu.advanced.parallelism_tooltip_2", "Only evaluated when this Statistics section is open."));
			}
			ImGui::Text(T("menu.advanced.work_metric", "Work (W, sum of task wall times): %s"), Util::FormatDuration(p.workMs).c_str());
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(T("menu.advanced.work_tooltip_1", "Total compile work: sum of all per-shader wall-clock compile times."));
				ImGui::Text(T("menu.advanced.work_tooltip_2", "This is not CPU time; it is accumulated task elapsed time."));
				ImGui::Text(T("menu.advanced.work_tooltip_3", "Equivalent serial time on one worker if overhead stayed the same."));
			}
			ImGui::Text(T("menu.advanced.span_metric", "Span (S, longest): %s"), Util::FormatDuration(p.spanMs).c_str());
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(T("menu.advanced.span_tooltip_1", "Critical-path lower bound, approximated by the single slowest shader."));
				ImGui::Text(T("menu.advanced.span_tooltip_2", "Even infinite cores cannot finish faster than this."));
			}
			ImGui::Text(T("menu.advanced.makespan_metric", "Makespan (T_p): %s"), Util::FormatDuration(p.makespanMs).c_str());
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(T("menu.advanced.makespan_tooltip", "Observed wall-clock duration for the full shader build."));
			}
			ImGui::Text(T("menu.advanced.queue_wait_metric", "Queue wait (avg/max): %s / %s"),
				Util::FormatDuration(p.avgQueueWaitMs).c_str(),
				Util::FormatDuration(p.maxQueueWaitMs).c_str());
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(T("menu.advanced.queue_wait_tooltip_1", "Time spent waiting in the ready queue before a worker started compilation."));
				ImGui::Text(T("menu.advanced.queue_wait_tooltip_2", "Useful for identifying scheduler-induced delay separate from compile cost."));
			}
			ImGui::Text(T("menu.advanced.avg_parallelism_metric", "Average parallelism (W/S): %.2fx"), p.avgParallelism);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(T("menu.advanced.avg_parallelism_tooltip_1", "Average useful concurrency in this workload."));
				ImGui::Text(T("menu.advanced.avg_parallelism_tooltip_2", "Roughly the worker count where adding more cores gives diminishing returns."));
			}
			ImGui::Text(T("menu.advanced.infinite_core_efficiency_metric", "Infinite-core efficiency (S/T_p): %.1f%%"), 100.0 * p.infiniteCoreEfficiency);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(T("menu.advanced.infinite_core_efficiency_tooltip_1", "How close runtime is to the infinite-core lower bound."));
				ImGui::Text(T("menu.advanced.infinite_core_efficiency_tooltip_2", "100%% means T_p == S."));
			}
			ImGui::Text(T("menu.advanced.infinite_core_gap_metric", "Infinite-core gap: %.1f%%"), p.infiniteCoreGapPercent);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(T("menu.advanced.infinite_core_gap_tooltip_1", "Distance from ideal infinite-core time."));
				ImGui::Text(T("menu.advanced.infinite_core_gap_tooltip_2", "Defined as 100 * (1 - S / T_p). Lower is better."));
			}

			ImGui::Spacing();
			ImGui::TextDisabled(T("menu.advanced.infinite_core_efficiency", "Infinite-core efficiency"));
			float efficiency = static_cast<float>(std::clamp(p.infiniteCoreEfficiency, 0.0, 1.0));
			const double efficiencyPercent = 100.0 * p.infiniteCoreEfficiency;
			const auto progress = std::vformat(T("menu.advanced.efficiency_progress", "{:.1f}% efficient / {:.1f}% gap"), std::make_format_args(efficiencyPercent, p.infiniteCoreGapPercent));
			ImGui::ProgressBar(efficiency, ImVec2(-1.0f, 0.0f), progress.c_str());

			ImGui::Spacing();
			ImGui::TextDisabled(T("menu.advanced.relative_durations", "Relative durations (normalized)"));
			double maxMs = std::max({ p.workMs, p.spanMs, p.makespanMs, 1.0 });
			auto drawRelativeBar = [maxMs](const char* label, double value) {
				float ratio = static_cast<float>(std::clamp(value / maxMs, 0.0, 1.0));
				ImGui::TextUnformatted(label);
				ImGui::SameLine();
				const auto duration = Util::FormatDuration(value);
				const double percent = 100.0 * ratio;
				const auto progress = std::vformat(T("menu.advanced.relative_bar_format", "{} ({:.1f}%)"), std::make_format_args(duration, percent));
				ImGui::ProgressBar(ratio, ImVec2(-1.0f, 0.0f), progress.c_str());
			};
			drawRelativeBar(T("menu.advanced.span_label", "Span (S)"), p.spanMs);
			drawRelativeBar(T("menu.advanced.makespan_label", "Makespan (T_p)"), p.makespanMs);
			drawRelativeBar(T("menu.advanced.work_label", "Work (W)"), p.workMs);
		}
	}

	// Top-3 slowest shaders from the last build
	auto topSlow = shaderCache->GetTopSlowTasks(3);
	if (!topSlow.empty()) {
		ImGui::Spacing();
		ImGui::TextDisabled(T("menu.advanced.top_slowest_shaders", "Top %zu Slowest Shaders (last build)"), topSlow.size());
		for (size_t i = 0; i < topSlow.size(); ++i) {
			const auto& rec = topSlow[i];
			ImGui::Text(T("menu.advanced.shader_slow_entry", "#%zu  %s  (weight %d)"), i + 1,
				Util::FormatDuration(rec.elapsedMs).c_str(), rec.priority);
			ImGui::SameLine();
			ImGui::TextDisabled("%s", rec.key.c_str());
			if (ImGui::IsItemHovered()) {
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("%s", rec.key.c_str());
				}
			}
			// Allow copying the full key with a right-click
			if (ImGui::BeginPopupContextItem(std::format("##slowcopy{}", i).c_str())) {
				if (ImGui::MenuItem(T("menu.advanced.copy_key", "Copy key"))) {
					ImGui::SetClipboardText(rec.key.c_str());
				}
				ImGui::EndPopup();
			}
		}
	}

	if (ImGui::TreeNodeEx(T("menu.advanced.all_compiled_tasks", "All Compiled Tasks"), ImGuiTreeNodeFlags_DefaultOpen)) {
		using SlowTaskRecord = SIE::CompilationSet::SlowTaskRecord;

		static std::vector<SlowTaskRecord> cachedRows;
		static int64_t cachedResetQpc = -1;
		const int64_t resetQpc = shaderCache->GetLastResetQpc();
		if (resetQpc != cachedResetQpc || shaderCache->IsCompiling()) {
			cachedResetQpc = resetQpc;
			cachedRows = shaderCache->GetAllTaskRecords();
		}

		static char taskFilterText[256] = "";
		static int taskSearchColumn = 0;
		static size_t taskSortColumn = 7;
		static bool taskSortAscending = false;

		auto queuePercent = [](const SlowTaskRecord& a_record) {
			const double total = a_record.queueWaitMs + a_record.elapsedMs;
			return total > 0.0 ? 100.0 * a_record.queueWaitMs / total : 0.0;
		};

		const int64_t qpcFrequency = shaderCache->GetQpcFrequency();
		auto completedSinceStartMs = [resetQpc, qpcFrequency](const SlowTaskRecord& a_record) {
			const double delay = qpcFrequency > 0 ?
			                         static_cast<double>(a_record.startQpc - resetQpc) * 1000.0 / static_cast<double>(qpcFrequency) :
			                         0.0;
			return delay + a_record.elapsedMs;
		};

		std::vector<Util::TableColumnConfig<SlowTaskRecord>> columns = {
			{ T("menu.advanced.column_key", "Key"), T("menu.advanced.shader_file_class_and_active_defines", "Shader file, class, and active defines"), [](const SlowTaskRecord& a_record) {
				 return a_record.key;
			 },
				/*truncate=*/true, /*widthWeight=*/4.0f },
			{ T("menu.advanced.elapsed", "Elapsed"), T("menu.advanced.wall_clock_compile_time_for_this_task", "Wall-clock compile time for this task"), [](const SlowTaskRecord& a_record) {
				 return Util::FormatDuration(a_record.elapsedMs);
			 } },
			{ T("menu.advanced.queue_wait", "Queue Wait"), T("menu.advanced.time_waiting_for_a_free_worker", "Time waiting for a free worker"), [](const SlowTaskRecord& a_record) {
				 return Util::FormatDuration(a_record.queueWaitMs);
			 } },
			{ T("menu.advanced.queue", "Queue %"), T("menu.advanced.queue_wait_as_a_share_of_wait_plus_compile_time", "Queue wait as a share of wait plus compile time"), [queuePercent](const SlowTaskRecord& a_record) {
				 return Util::FormatPercent(static_cast<float>(queuePercent(a_record)));
			 } },
			{ T("menu.advanced.weight", "Weight"), T("menu.advanced.estimated_compile_cost_scheduling_priority", "Estimated compile-cost scheduling priority"), [](const SlowTaskRecord& a_record) {
				 return std::to_string(a_record.priority);
			 } },
			{ T("menu.advanced.defines", "Defines"), T("menu.advanced.active_define_permutations", "Active define permutations"), [](const SlowTaskRecord& a_record) {
				 return std::to_string(a_record.defineCount);
			 } },
			{ T("menu.advanced.source_kb", "Source KB"), T("menu.advanced.hlsl_source_file_size_at_compile_time", "HLSL source file size at compile time"), [](const SlowTaskRecord& a_record) {
				 return std::format("{:.1f}", static_cast<double>(a_record.sourceSizeBytes) / 1024.0);
			 } },
			{ T("menu.advanced.completed", "Completed"), T("menu.advanced.time_from_build_start_until_task_completion", "Time from build start until task completion"), [completedSinceStartMs](const SlowTaskRecord& a_record) {
				 return Util::FormatDuration(completedSinceStartMs(a_record));
			 } }
		};

		std::vector<std::function<bool(const SlowTaskRecord&, const SlowTaskRecord&, bool)>> sorters = {
			[](const SlowTaskRecord& a, const SlowTaskRecord& b, bool ascending) { return ascending ? a.key < b.key : a.key > b.key; },
			[](const SlowTaskRecord& a, const SlowTaskRecord& b, bool ascending) { return ascending ? a.elapsedMs < b.elapsedMs : a.elapsedMs > b.elapsedMs; },
			[](const SlowTaskRecord& a, const SlowTaskRecord& b, bool ascending) { return ascending ? a.queueWaitMs < b.queueWaitMs : a.queueWaitMs > b.queueWaitMs; },
			[queuePercent](const SlowTaskRecord& a, const SlowTaskRecord& b, bool ascending) {
				return ascending ? queuePercent(a) < queuePercent(b) : queuePercent(a) > queuePercent(b);
			},
			[](const SlowTaskRecord& a, const SlowTaskRecord& b, bool ascending) { return ascending ? a.priority < b.priority : a.priority > b.priority; },
			[](const SlowTaskRecord& a, const SlowTaskRecord& b, bool ascending) { return ascending ? a.defineCount < b.defineCount : a.defineCount > b.defineCount; },
			[](const SlowTaskRecord& a, const SlowTaskRecord& b, bool ascending) { return ascending ? a.sourceSizeBytes < b.sourceSizeBytes : a.sourceSizeBytes > b.sourceSizeBytes; },
			[completedSinceStartMs](const SlowTaskRecord& a, const SlowTaskRecord& b, bool ascending) {
				return ascending ? completedSinceStartMs(a) < completedSinceStartMs(b) : completedSinceStartMs(a) > completedSinceStartMs(b);
			}
		};

		auto getFilterableFields = [queuePercent, completedSinceStartMs](const SlowTaskRecord& a_record) -> std::vector<std::string> {
			return {
				a_record.key,
				Util::FormatDuration(a_record.elapsedMs),
				Util::FormatDuration(a_record.queueWaitMs),
				Util::FormatPercent(static_cast<float>(queuePercent(a_record))),
				std::to_string(a_record.priority),
				std::to_string(a_record.defineCount),
				std::format("{:.1f}", static_cast<double>(a_record.sourceSizeBytes) / 1024.0),
				Util::FormatDuration(completedSinceStartMs(a_record))
			};
		};

		Util::TableFilterState<SlowTaskRecord> filterState(getFilterableFields);
		filterState.filterText = taskFilterText;
		filterState.searchColumn = taskSearchColumn;
		std::vector<Util::TableInputEvent<SlowTaskRecord>> inputEvents = {
			{ Util::TableInputEventType::ContextMenu,
				[](const SlowTaskRecord& a_record) { ImGui::SetClipboardText(a_record.key.c_str()); },
				T("menu.advanced.copy_key", "Copy key"), 1 }
		};

		Util::ShowInteractiveTable<SlowTaskRecord>(
			"##AllCompiledTasksTable",
			columns,
			cachedRows,
			taskSortColumn,
			taskSortAscending,
			sorters,
			filterState,
			inputEvents);

		strncpy_s(taskFilterText, filterState.filterText.c_str(), sizeof(taskFilterText) - 1);
		taskFilterText[sizeof(taskFilterText) - 1] = '\0';
		taskSearchColumn = filterState.searchColumn;
		ImGui::TreePop();
	}

	ImGui::TreePop();

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();
}

// -----------------------------------------------------------------------------
// Diagnostics tab
// -----------------------------------------------------------------------------

void AdvancedSettingsRenderer::RenderDiagnosticsSection()
{
	RenderLoggingControls();

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	RenderRuntimeDebugControls();

	// Shader blocking only meaningful in developer mode (matches prior behavior).
	if (globals::state->IsDeveloperMode()) {
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		RenderShaderBlockingPanel();
	}
}

void AdvancedSettingsRenderer::RenderCompileTraceExport()
{
	Util::DrawSectionHeader(T("menu.advanced.compile_trace_export", "Compile Trace Export"));
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(T("menu.advanced.writes_every_compiled_task_from_the_current_build_to_chrome_trace_json", "Writes every compiled task from the current build to Chrome Trace JSON."));
		ImGui::Text(T("menu.advanced.open_the_result_in_perfetto_to_compare_compile_cost_with_queue_contention", "Open the result in Perfetto to compare compile cost with queue contention."));
	}

	auto shaderCache = globals::shaderCache;
	const auto logPath = Util::PathHelpers::GetLogPath();
	const bool canExport = !shaderCache->IsCompiling() && !logPath.empty();
	static bool lastExportOk = false;
	static std::string lastExportPath;

	ImGui::BeginDisabled(!canExport);
	if (ImGui::Button(T("menu.advanced.export_trace_perfetto", "Export Trace (Perfetto)"), { -1, 0 }) && !shaderCache->IsCompiling()) {
		const auto path = logPath.parent_path() / "compile-trace.json";
		lastExportOk = shaderCache->ExportCompileTrace(path);
		lastExportPath = path.string();
	}
	ImGui::EndDisabled();
	if (shaderCache->IsCompiling()) {
		Util::AddTooltip(
			T("menu.advanced.wait_for_the_current_build_to_finish_before_exporting", "Wait for the current build to finish before exporting."),
			ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled);
	} else if (logPath.empty()) {
		Util::AddTooltip(
			T("menu.advanced.the_community_shaders_log_directory_is_unavailable", "The Community Shaders log directory is unavailable."),
			ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled);
	}

	if (!lastExportPath.empty()) {
		if (lastExportOk) {
			ImGui::TextColored({ 0.4f, 0.9f, 0.4f, 1.0f }, T("menu.advanced.exported_s", "Exported: %s"), lastExportPath.c_str());
		} else {
			ImGui::TextColored({ 0.9f, 0.4f, 0.4f, 1.0f }, T("menu.advanced.export_failed_check_csx_log", "Export failed; check CSX.log."));
		}
	}
}

void AdvancedSettingsRenderer::RenderLoggingControls()
{
	Util::DrawSectionHeader(T("menu.advanced.tab_logging", "Logging"));

	// Log Level selection. Resync from state every frame so external changes
	// (config reload, console command, another caller of SetLogLevel) don't
	// leave the combo displaying a stale selection.
	spdlog::level::level_enum logLevel = globals::state->GetLogLevel();
	const char* items[] = {
		T("menu.advanced.log_level_trace", "trace"),
		T("menu.advanced.log_level_debug", "debug"),
		T("menu.advanced.log_level_info", "info"),
		T("menu.advanced.log_level_warn", "warn"),
		T("menu.advanced.log_level_err", "err"),
		T("menu.advanced.log_level_critical", "critical"),
		T("menu.advanced.log_level_off", "off")
	};
	int item_current = static_cast<int>(logLevel);
	if (Util::Widgets::Combo(T("menu.advanced.log_level", "Log Level"), &item_current, items, IM_ARRAYSIZE(items))) {
		globals::state->SetLogLevel(static_cast<spdlog::level::level_enum>(item_current));
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(T("menu.advanced.log_level_tooltip", "Log level. Trace is most verbose. Default is info."));
	}

	ImGui::Columns(2, nullptr, false);

	// Dump Ini Settings button
	if (ImGui::Button(T("menu.advanced.dump_ini_settings", "Dump Ini Settings"), { -1, 0 })) {
		Util::DumpSettingsOptions();
	}

	ImGui::NextColumn();

	static MenuUI::ActionFeedback feedback;
	// Open Logs button
	std::filesystem::path logPath = Util::PathHelpers::GetLogPath();
	if (!logPath.empty() && ImGui::Button(T("menu.advanced.open_logs", "Open Logs"), { -1, 0 })) {
		feedback.error = !Util::OpenInShell(logPath, feedback.message);
	}

	ImGui::Columns(1);
	feedback.Draw();
}

void AdvancedSettingsRenderer::RenderRuntimeDebugControls()
{
	Util::DrawSectionHeader(T("menu.advanced.runtime_debug", "Runtime Debug"));

	// Frame annotations toggle
	Util::Widgets::Checkbox(T("menu.advanced.frame_annotations", "Frame Annotations"), &globals::state->frameAnnotations);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(T("menu.advanced.frame_annotations_tooltip", "Enable detailed frame annotations for debugging render passes and draw calls."));
	}

	// Debug addresses section
	if (ImGui::TreeNodeEx(T("menu.advanced.addresses", "Addresses"))) {
		auto Renderer = globals::game::renderer;
		auto BSShaderAccumulator = *globals::game::currentAccumulator.get();
		auto RendererShadowState = globals::game::shadowState;
		ADDRESS_NODE(Renderer)
		ADDRESS_NODE(BSShaderAccumulator)
		ADDRESS_NODE(RendererShadowState)
		ImGui::TreePop();
	}
}

void AdvancedSettingsRenderer::RenderShaderBlockingPanel()
{
	auto shaderCache = globals::shaderCache;

	Util::DrawSectionHeader(T("menu.advanced.shader_blocking", "Shader Blocking"));

	// Show blocked shader status as a regular section
	if (!shaderCache->blockedKey.empty()) {
		// Create a visually distinct box for the blocked shader info with rounded corners and border
		const float scale = Util::GetUIScale();
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f * scale);
		ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, ImGui::GetStyle().WindowBorderSize);
		ImVec4 blockedBgColor = Util::Colors::GetError();
		blockedBgColor.w = 0.15f;  // Semi-transparent background
		ImGui::PushStyleColor(ImGuiCol_ChildBg, blockedBgColor);
		const auto restoreStyle = SKSE::stl::scope_exit([] {
			ImGui::PopStyleColor();
			ImGui::PopStyleVar(2);
		});

		const float maxHeight = std::max(1.0f, ImGui::GetContentRegionAvail().y * 0.3f);
		ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, maxHeight));
		const bool childVisible = ImGui::BeginChild("##BlockedShaderInfo", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
		const auto endChild = SKSE::stl::scope_exit([] { ImGui::EndChild(); });
		if (childVisible) {
			Util::Text::Error(T("menu.advanced.shader_blocking_active", "Shader Blocking Active"));
			ImGui::SameLine();
			if (ImGui::Button(T("menu.advanced.stop_blocking", "Stop Blocking##Section"))) {
				shaderCache->DisableShaderBlocking();
			}

			ImGui::Text(T("menu.advanced.blocked_shader", "Blocked: %s"), shaderCache->blockedKey.c_str());

			// Try to get more details from active shaders
			auto activeShaders = shaderCache->GetActiveShaders();
			for (const auto& shader : activeShaders) {
				if (shader.key == shaderCache->blockedKey) {
					ImGui::Text(T("menu.advanced.shader_type_label", "Type: %s"), magic_enum::enum_name(shader.shaderType).data());
					ImGui::Text(T("menu.advanced.shader_class_label", "Class: %s"), magic_enum::enum_name(shader.shaderClass).data());
					ImGui::Text(T("menu.advanced.shader_descriptor", "Descriptor: 0x%X"), shader.descriptor);

					// Add button to copy shader info to clipboard
					ImGui::PushID(shader.key.c_str());
					if (ImGui::Button(T("menu.advanced.copy_info_blockedshader", "Copy Info##BlockedShader"))) {
						std::string diskPathStr;
						diskPathStr.reserve(shader.diskPath.size());
						for (wchar_t wc : shader.diskPath) {
							diskPathStr += static_cast<char>(wc);
						}

						const auto shaderType = magic_enum::enum_name(shader.shaderType);
						const auto shaderClass = magic_enum::enum_name(shader.shaderClass);
						std::string fullInfo = std::vformat(T("menu.advanced.shader_info_clipboard", "Type: {}\nClass: {}\nDescriptor: 0x{:X}\nKey: {}\nCache Path: {}"),
							std::make_format_args(shaderType, shaderClass, shader.descriptor, shader.key, diskPathStr));
						ImGui::SetClipboardText(fullInfo.c_str());
					}
					ImGui::PopID();
					if (ImGui::IsItemHovered()) {
						if (auto _tt = Util::HoverTooltipWrapper()) {
							ImGui::Text(T("menu.advanced.copy_info_tooltip", "Copy complete shader information including cache path to clipboard"));
						}
					}

					break;
				}
			}
		}
	}

	// Blocking hotkeys + enable toggle
	{
		auto menu = globals::menu;
		auto& menuSettings = menu->GetSettings();
		auto& themeSettings = menuSettings.Theme;

		if (Util::Widgets::Checkbox(T("menu.advanced.enable_shader_blocking", "Enable Shader Blocking"), &menuSettings.EnableShaderBlocking)) {
			// Setting saved automatically on next save
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(T("menu.advanced.enable_shader_blocking_tooltip", "Enables hotkeys to cycle through and block individual shaders for debugging purposes."));
		}

		if (menuSettings.EnableShaderBlocking) {
			ImGui::Indent();

			// Shader Block Previous Key
			if (menu->settingShaderBlockPrevKey) {
				ImGui::Text(T("menu.advanced.press_key_shader_block_prev", "Press any key for Shader Block Previous..."));
			} else {
				ImGui::AlignTextToFramePadding();
				ImGui::Text(T("menu.advanced.block_previous", "Block Previous:"));
				ImGui::SameLine();
				ImGui::AlignTextToFramePadding();
				ImGui::TextColored(themeSettings.StatusPalette.CurrentHotkey, "%s",
					Util::Input::KeyIdToString(menuSettings.ShaderBlockPrevKey).c_str());
				ImGui::SameLine();
				if (ImGui::Button(T("menu.advanced.change_shader_block_prev", "Change##ShaderBlockPrev"))) {
					menu->settingShaderBlockPrevKey = true;
				}
			}

			// Shader Block Next Key
			if (menu->settingShaderBlockNextKey) {
				ImGui::Text(T("menu.advanced.press_key_shader_block_next", "Press any key for Shader Block Next..."));
			} else {
				ImGui::AlignTextToFramePadding();
				ImGui::Text(T("menu.advanced.block_next", "Block Next:"));
				ImGui::SameLine();
				ImGui::AlignTextToFramePadding();
				ImGui::TextColored(themeSettings.StatusPalette.CurrentHotkey, "%s",
					Util::Input::KeyIdToString(menuSettings.ShaderBlockNextKey).c_str());
				ImGui::SameLine();
				if (ImGui::Button(T("menu.advanced.change_shader_block_next", "Change##ShaderBlockNext"))) {
					menu->settingShaderBlockNextKey = true;
				}
			}

			ImGui::Unindent();
		}
	}

	// Active shaders list — rendered inline; the parent panel already says
	// "Shader Blocking", so a nested CollapsingHeader was redundant noise.
	{
		ImGui::Spacing();
		Util::DrawSectionHeader(T("menu.advanced.active_shaders_used_recently", "Active Shaders (Used Recently)"));
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				T("menu.advanced.recently_used_shaders_entries_expire_after_about_one_second_enable_shader_blocking_ab", "Recently used shaders; entries expire after about one second. Enable Shader Blocking above to cycle/block them with hotkeys."));
		}

		// Get fresh active shaders data for accurate count and table
		auto activeShaders = shaderCache->GetActiveShaders();
		uint32_t totalDrawCalls = 0;
		for (const auto& shader : activeShaders) {
			totalDrawCalls += shader.drawCalls;
		}

		// Static variables to maintain table filter state
		static char filterText[256] = "";
		static int searchColumn = 0;        // 0 = All Columns, 1 = Type, 2 = Class, 3 = Descriptor, 4 = Draw Calls, 5 = Key
		static size_t sortColumn = 4;       // Default sort by Frame % (draw calls)
		static bool sortAscending = false;  // Descending by default (highest usage first)		// Create shader rows for the table utility (simplified - no filter data needed)
		struct ShaderRow
		{
			SIE::ShaderCache::ActiveShaderInfo shader;
			uint32_t totalDrawCalls;
		};

		std::vector<ShaderRow> shaderRows;
		for (const auto& shader : activeShaders) {
			shaderRows.push_back({ shader, totalDrawCalls });
		}

		// Build column configurations
		std::vector<Util::TableColumnConfig<ShaderRow>> columns = {
			{ T("menu.advanced.column_type", "Type"), T("menu.advanced.column_type_tooltip", "Shader type"), [](const ShaderRow& row) {
				 return std::string(magic_enum::enum_name(row.shader.shaderType));
			 } },
			{ T("menu.advanced.column_class", "Class"), T("menu.advanced.column_class_tooltip", "Shader class"), [](const ShaderRow& row) {
				 return std::string(magic_enum::enum_name(row.shader.shaderClass));
			 } },
			{ T("menu.advanced.column_descriptor", "Descriptor"), T("menu.advanced.column_descriptor_tooltip", "Shader descriptor"), [](const ShaderRow& row) {
				 return std::format("0x{:X}", row.shader.descriptor);
			 } },
			{ T("menu.advanced.column_frame_pct", "Frame %"), T("menu.advanced.column_frame_pct_tooltip", "Percentage of draw calls this frame"), [](const ShaderRow& row) {
				 float percentage = Util::CalculatePercentage(static_cast<float>(row.shader.drawCalls), static_cast<float>(row.totalDrawCalls));
				 return Util::FormatPercent(percentage);
			 } },
			{ T("menu.advanced.column_key", "Key"), T("menu.advanced.column_key_tooltip", "Shader key"), [](const ShaderRow& row) {
				 return row.shader.key;
			 } }
		};

		// Row click callbacks
		auto onRowLeftClick = [shaderCache](const ShaderRow& row) {
			if (row.shader.key == shaderCache->blockedKey) {
				shaderCache->DisableShaderBlocking();
			} else {
				// Block this shader - use IterateShaderBlock to find and block it
				// Or set blockedKey directly (simpler for click-to-block)
				shaderCache->blockedKey = row.shader.key;
				logger::info("Blocking shader: {}", row.shader.key);
			}
		};

		auto onRowRightClick = [shaderCache](const ShaderRow& row) {
			std::string diskPathStr;
			diskPathStr.reserve(row.shader.diskPath.size());
			for (wchar_t wc : row.shader.diskPath) {
				diskPathStr += static_cast<char>(wc);
			}

			const auto shaderType = magic_enum::enum_name(row.shader.shaderType);
			const auto shaderClass = magic_enum::enum_name(row.shader.shaderClass);
			std::string fullInfo = std::vformat(T("menu.advanced.shader_info_clipboard", "Type: {}\nClass: {}\nDescriptor: 0x{:X}\nKey: {}\nCache Path: {}"),
				std::make_format_args(shaderType, shaderClass, row.shader.descriptor, row.shader.key, diskPathStr));
			ImGui::SetClipboardText(fullInfo.c_str());
		};
		auto getRowTooltip = [shaderCache](const ShaderRow& row) {
			const auto* clickAction = (row.shader.key == shaderCache->blockedKey) ? T("menu.advanced.click_to_unblock", "Left-click to unblock this shader") : T("menu.advanced.click_to_block", "Left-click to block this shader");
			const auto shaderType = magic_enum::enum_name(row.shader.shaderType);
			const auto shaderClass = magic_enum::enum_name(row.shader.shaderClass);
			return std::vformat(T("menu.advanced.shader_row_tooltip", "Type: {}\nClass: {}\nDescriptor: 0x{:X}\nKey: {}\n\n{}"),
				std::make_format_args(shaderType, shaderClass, row.shader.descriptor, row.shader.key, clickAction));
		};

		// Define function to extract filterable fields (for TableFilterState)
		auto getFilterableFields = [](const ShaderRow& row) -> std::vector<std::string> {
			return {
				std::string(magic_enum::enum_name(row.shader.shaderType)),                                                                         // Type
				std::string(magic_enum::enum_name(row.shader.shaderClass)),                                                                        // Class
				std::format("0x{:X}", row.shader.descriptor),                                                                                      // Descriptor
				Util::FormatPercent(Util::CalculatePercentage(static_cast<float>(row.shader.drawCalls), static_cast<float>(row.totalDrawCalls))),  // Frame %
				row.shader.key                                                                                                                     // Key
			};
		};

		// Define sorting comparators (customSorts parameter)
		std::vector<std::function<bool(const ShaderRow&, const ShaderRow&, bool)>> sorters = {
			// Type - string sort
			[](const ShaderRow& a, const ShaderRow& b, bool ascending) {
				std::string aVal = std::string(magic_enum::enum_name(a.shader.shaderType));
				std::string bVal = std::string(magic_enum::enum_name(b.shader.shaderType));
				return ascending ? (aVal < bVal) : (aVal > bVal);
			},
			// Class - string sort
			[](const ShaderRow& a, const ShaderRow& b, bool ascending) {
				std::string aVal = std::string(magic_enum::enum_name(a.shader.shaderClass));
				std::string bVal = std::string(magic_enum::enum_name(b.shader.shaderClass));
				return ascending ? (aVal < bVal) : (aVal > bVal);
			},
			// Descriptor - numeric sort
			[](const ShaderRow& a, const ShaderRow& b, bool ascending) {
				return ascending ? (a.shader.descriptor < b.shader.descriptor) : (a.shader.descriptor > b.shader.descriptor);
			},
			// Frame % - numeric sort
			[](const ShaderRow& a, const ShaderRow& b, bool ascending) {
				float aPercent = Util::CalculatePercentage(static_cast<float>(a.shader.drawCalls), static_cast<float>(a.totalDrawCalls));
				float bPercent = Util::CalculatePercentage(static_cast<float>(b.shader.drawCalls), static_cast<float>(b.totalDrawCalls));
				return ascending ? (aPercent < bPercent) : (aPercent > bPercent);
			},
			// Key - string sort
			[](const ShaderRow& a, const ShaderRow& b, bool ascending) {
				return ascending ? (a.shader.key < b.shader.key) : (a.shader.key > b.shader.key);
			}
		};

		// Create filter state
		Util::TableFilterState<ShaderRow> filterState(getFilterableFields);

		// Initialize filter state from existing variables
		filterState.filterText = std::string(filterText, filterText + strlen(filterText));
		filterState.searchColumn = searchColumn;

		// Define input events for row interactions
		std::vector<Util::TableInputEvent<ShaderRow>> inputEvents = {
			// Left-click to block/unblock shader
			{ Util::TableInputEventType::MouseClick, onRowLeftClick, "", 0 },
			// Right-click context menu for copying info
			{ Util::TableInputEventType::ContextMenu, onRowRightClick, T("menu.advanced.copy_info", "Copy Info"), 1 }
		};

		// Render the table with all configurations
		Util::ShowInteractiveTable<ShaderRow>(
			"##ActiveShadersTable",
			columns,
			shaderRows,
			sortColumn,
			sortAscending,
			sorters,
			filterState,
			inputEvents,
			getRowTooltip);

		// Update static variables with modified filter state
		strncpy_s(filterText, filterState.filterText.c_str(), sizeof(filterText) - 1);
		filterText[sizeof(filterText) - 1] = '\0';
		searchColumn = filterState.searchColumn;
	}
}

// -----------------------------------------------------------------------------
// Disable at Boot tab
// -----------------------------------------------------------------------------

void AdvancedSettingsRenderer::RenderDisableAtBootSection(const std::function<void()>& drawDisableAtBootSettings)
{
	drawDisableAtBootSettings();
}

// -----------------------------------------------------------------------------
// Testing tab
// -----------------------------------------------------------------------------

void AdvancedSettingsRenderer::RenderTestingSection()
{
	// A/B Testing settings
	auto* abTestingManager = ABTestingManager::GetSingleton();
	abTestingManager->DrawSettingsUI();

	// Developer Mode Testing UI + scene-prep button (previously on the "Developer" tab)
	if (globals::state->IsDeveloperMode()) {
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		FeatureIssues::Test::DrawDeveloperModeTestingUI();

		ImGui::Spacing();
		// Test Conditions button - runs a set of console commands to prepare the player for testing
		if (ImGui::Button(T("menu.advanced.test_conditions", "Test Conditions"), { -1, 0 })) {
			if (auto ui = RE::UI::GetSingleton(); ui && !ui->menuStack.empty() && RE::PlayerCharacter::GetSingleton()) {
				RE::Console::ExecuteCommand("player.setav speedmult 1000");
				RE::Console::ExecuteCommand("tgm");
				RE::Console::ExecuteCommand("tcl");
				EditorWindow::GetSingleton()->PauseTime();
				RE::Console::ExecuteCommand("set gamehour to 12");
				RE::Console::ExecuteCommand("coc whiterun");
				RE::Console::ExecuteCommand("fw 81a");
			}
		}
	}
}
