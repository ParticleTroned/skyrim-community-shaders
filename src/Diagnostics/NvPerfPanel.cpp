#include "Diagnostics/NvPerf.h"

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "I18n/I18n.h"
#	include <algorithm>
#	include <cmath>
#	include <imgui.h>
#	include <imgui_stdlib.h>
#	include <sstream>

namespace NvPerf
{
	void DrawSettings()
	{
		if (!ImGui::CollapsingHeader(T("diagnostics.nvperf.nvidia_hardware_counters_d3d11", "NVIDIA hardware counters (D3D11)")))
			return;
		ImGui::TextWrapped(T("diagnostics.nvperf.developer_capture_of_shader_and_memory_bottle_46f23402", "Developer capture of shader and memory bottlenecks. Uses live frames; keep the scene stable. D3D12 neural rendering and frame-generation queues are not measured."));
		Probe();
		const auto device = GetDeviceInfo();
		if (!device.built) {
			ImGui::TextDisabled(T("diagnostics.nvperf.hardware_counters_are_not_included_in_this_build", "Hardware counters are not included in this build."));
			return;
		}
		if (!device.probed) {
			ImGui::TextDisabled(T("diagnostics.nvperf.waiting_for_the_render_thread_to_probe_this_gpu", "Waiting for the render thread to probe this GPU."));
			return;
		}
		if (!device.gpuSupported) {
			ImGui::TextWrapped(T("diagnostics.nvperf.unavailable_s", "Unavailable: %s"), device.error.c_str());
			return;
		}
		ImGui::Text("%s (%s)", device.deviceName.c_str(), device.chipName.c_str());
		static std::vector<char> selected;
		static std::string filters;
		static std::string error;
		static int samples = 1;
		static int nesting = 1;
		static int delay = 0;
		const auto defaults = DefaultMetrics();
		if (selected.size() != defaults.size())
			selected.assign(defaults.size(), 1);
		if (ImGui::TreeNode(T("diagnostics.nvperf.metrics", "Metrics"))) {
			const SKSE::stl::scope_exit closeTree([] { ImGui::TreePop(); });
			for (std::size_t index = 0; index < defaults.size(); ++index) {
				bool checked = selected[index] != 0;
				if (ImGui::Checkbox(FormatMetric(defaults[index]).c_str(), &checked))
					selected[index] = checked;
			}
		}
		ImGui::InputTextWithHint(T("diagnostics.nvperf.range_filters", "Range filters"), T("diagnostics.nvperf.comma_separated_trailing_matches_a_prefix", "Comma separated; trailing * matches a prefix"), &filters);
		ImGui::SliderInt(T("diagnostics.nvperf.samples", "Samples"), &samples, 1, static_cast<int>(MaxSamples), "%d", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderInt(T("diagnostics.nvperf.nesting_levels", "Nesting levels"), &nesting, 1, MaxNestingLevels, "%d", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderInt(T("diagnostics.nvperf.delay_frames", "Delay (frames)"), &delay, 0, MaxDelayFrames, "%d", ImGuiSliderFlags_AlwaysClamp);
		const auto status = GetStatus();
		const bool busy = status.state == CaptureState::Waiting || status.state == CaptureState::Capturing || status.state == CaptureState::Decoding;
		if (busy) {
			ImGui::Text(T("diagnostics.nvperf.capture_llu_sample_u_u_submitted_pass_zu_zu", "Capture %llu: sample %u/%u, submitted pass %zu/%zu"), static_cast<unsigned long long>(status.captureId), status.sample, status.samples, status.pass, status.passes);
			if (ImGui::Button(T("diagnostics.nvperf.cancel_hardware_capture", "Cancel hardware capture")))
				CancelCapture(status.captureId);
		} else if (ImGui::Button(T("diagnostics.nvperf.capture_hardware_counters", "Capture hardware counters"))) {
			error.clear();
			CaptureConfiguration config;
			for (std::size_t index = 0; index < defaults.size(); ++index)
				if (selected[index])
					config.metrics.push_back(defaults[index]);
			if (config.metrics.empty())
				error = T("diagnostics.nvperf.select_at_least_one_metric", "Select at least one metric.");
			if (filters.size() > MaxFilters * (MaxNameLength + 1))
				error = T("diagnostics.nvperf.range_filter_list_exceeds_its_limit", "Range filter list exceeds its limit.");
			if (error.empty()) {
				std::istringstream stream(filters);
				for (std::string filter; std::getline(stream, filter, ',');) {
					const auto first = filter.find_first_not_of(" \t");
					if (first == std::string::npos)
						continue;
					config.ranges.push_back(filter.substr(first, filter.find_last_not_of(" \t") - first + 1));
				}
				config.samples = static_cast<std::uint32_t>(samples);
				config.nestingLevels = static_cast<std::uint16_t>(nesting);
				config.delayFrames = static_cast<std::uint32_t>(delay);
				std::uint64_t id;
				RequestCapture(config, id, error);
			}
		}
		if (!error.empty())
			ImGui::TextWrapped("%s", error.c_str());
		if (!status.error.empty())
			ImGui::TextWrapped("%s", status.error.c_str());
		const auto result = LatestResult();
		if (!result)
			return;
		ImGui::Separator();
		ImGui::Text(T("diagnostics.nvperf.capture_llu_s", "Capture %llu: %s"), static_cast<unsigned long long>(result->id), result->success ? T("diagnostics.nvperf.complete", "complete") : T("diagnostics.nvperf.incomplete", "incomplete"));
		if (!result->error.empty())
			ImGui::TextWrapped("%s", result->error.c_str());
		if (ImGui::BeginTable("##NvPerfSummary", static_cast<int>(result->metrics.size()) + 1,
				ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg,
				ImVec2(0, 220))) {
			const SKSE::stl::scope_exit closeTable([] { ImGui::EndTable(); });
			ImGui::TableSetupColumn(T("diagnostics.nvperf.range", "Range"));
			for (const auto& metric : result->metrics) ImGui::TableSetupColumn(MetricLabel(metric).c_str());
			ImGui::TableHeadersRow();
			for (const auto& range : result->summary) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(range.name.c_str());
				for (double value : range.values) {
					ImGui::TableNextColumn();
					if (std::isfinite(value))
						ImGui::Text("%.4g", value);
					else
						ImGui::TextDisabled(T("diagnostics.nvperf.unavailable", "unavailable"));
				}
			}
		}
		ImGui::TextWrapped(T("diagnostics.nvperf.duration_counters_use_nanoseconds_rate_units_8c0f9128", "Duration counters use nanoseconds; rate units are shown in the column labels."));
		for (const auto& metric : result->unsupportedMetrics) ImGui::Text(T("diagnostics.nvperf.unsupported_s", "Unsupported: %s"), FormatMetric(metric).c_str());
		if (result->csvWritten) {
			ImGui::TextWrapped(T("diagnostics.nvperf.csv_s", "CSV: %s"), result->csvPath.string().c_str());
			if (ImGui::Button(T("diagnostics.nvperf.copy_csv_path", "Copy CSV path")))
				ImGui::SetClipboardText(result->csvPath.string().c_str());
		}
		if (!result->exportError.empty())
			ImGui::TextWrapped(T("diagnostics.nvperf.csv_export_failed_s", "CSV export failed: %s"), result->exportError.c_str());
	}
}
#endif
