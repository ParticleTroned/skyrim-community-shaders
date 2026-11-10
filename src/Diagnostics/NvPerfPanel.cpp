#include "Diagnostics/NvPerf.h"

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "Utils/StringUtils.h"

#	include <algorithm>
#	include <cmath>
#	include <imgui.h>
#	include <imgui_stdlib.h>
#	include <sstream>

namespace NvPerf
{
	void DrawSettings()
	{
		if (!ImGui::CollapsingHeader("NVIDIA hardware counters (D3D11)"))
			return;
		ImGui::TextWrapped("Developer capture of shader and memory bottlenecks. Uses live frames; keep the scene stable. D3D12 NR eye queues are not measured and parallel-eye scheduling is unchanged.");
		Probe();
		const auto device = GetDeviceInfo();
		if (!device.built) {
			ImGui::TextDisabled("Hardware counters are not included in this build.");
			return;
		}
		if (!device.probed) {
			ImGui::TextDisabled("Waiting for the render thread to probe this GPU.");
			return;
		}
		if (!device.gpuSupported) {
			ImGui::TextWrapped("Unavailable: %s", device.error.c_str());
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
		if (ImGui::TreeNode("Metrics")) {
			const SKSE::stl::scope_exit closeTree([] { ImGui::TreePop(); });
			for (std::size_t index = 0; index < defaults.size(); ++index) {
				bool checked = selected[index] != 0;
				if (ImGui::Checkbox(FormatMetric(defaults[index]).c_str(), &checked))
					selected[index] = checked;
			}
		}
		ImGui::InputTextWithHint("Range filters", "Comma separated; trailing * matches a prefix", &filters);
		ImGui::SliderInt("Samples", &samples, 1, static_cast<int>(MaxSamples), "%d", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderInt("Nesting levels", &nesting, 1, MaxNestingLevels, "%d", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderInt("Delay (frames)", &delay, 0, MaxDelayFrames, "%d", ImGuiSliderFlags_AlwaysClamp);
		const auto status = GetStatus();
		const bool busy = status.state == CaptureState::Waiting || status.state == CaptureState::Capturing || status.state == CaptureState::Decoding;
		if (busy) {
			ImGui::Text("Capture %llu: sample %u/%u, submitted pass %zu/%zu", static_cast<unsigned long long>(status.captureId), status.sample, status.samples, status.pass, status.passes);
			if (ImGui::Button("Cancel hardware capture"))
				CancelCapture(status.captureId);
		} else if (ImGui::Button("Capture hardware counters")) {
			error.clear();
			CaptureConfiguration config;
			for (std::size_t index = 0; index < defaults.size(); ++index)
				if (selected[index])
					config.metrics.push_back(defaults[index]);
			if (config.metrics.empty())
				error = "Select at least one metric.";
			if (filters.size() > MaxFilters * (MaxNameLength + 1))
				error = "Range filter list exceeds its limit.";
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
		ImGui::Text("Capture %llu: %s", static_cast<unsigned long long>(result->id), result->success ? "complete" : "incomplete");
		if (!result->error.empty())
			ImGui::TextWrapped("%s", result->error.c_str());
		if (ImGui::BeginTable("##NvPerfSummary", static_cast<int>(result->metrics.size()) + 1,
				ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg,
				ImVec2(0, 220))) {
			const SKSE::stl::scope_exit closeTable([] { ImGui::EndTable(); });
			ImGui::TableSetupColumn("Range");
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
						ImGui::TextDisabled("unavailable");
				}
			}
		}
		ImGui::TextWrapped("Duration counters use nanoseconds; rate units are shown in the column labels.");
		for (const auto& metric : result->unsupportedMetrics) ImGui::Text("Unsupported: %s", FormatMetric(metric).c_str());
		if (result->csvWritten) {
			const auto pathText = Util::PathToUtf8(result->csvPath);
			ImGui::TextWrapped("CSV: %s", pathText.c_str());
			if (ImGui::Button("Copy CSV path"))
				ImGui::SetClipboardText(pathText.c_str());
		}
		if (!result->exportError.empty())
			ImGui::TextWrapped("CSV export failed: %s", result->exportError.c_str());
	}
}
#endif
