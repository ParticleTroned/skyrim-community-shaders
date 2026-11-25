#include "PerformanceResultsUI.h"
#include "PerformanceTuningRenderer.h"
#include "SettingsPage.h"
#include "Utils/FileSystem.h"
#include "Utils/StringUtils.h"
#include "Utils/UI.h"
#include "Utils/WinApi.h"

namespace MenuUI::PerformanceResultsPage
{
	PerformanceResults::Store& GetStore()
	{
		static PerformanceResults::Store store(Util::PathHelpers::GetLogPath().parent_path(), [](const auto& path, std::string_view contents, bool overwrite) {
				Util::SetCurrentThreadCooperativeBackgroundPriority();
				std::string error;
				if (!Util::FileHelpers::WriteTextFileAtomic(path, contents, error, false, overwrite))
					throw std::runtime_error(error); }, Util::ChooseExportFolder);
		return store;
	}

	namespace
	{
		std::string Label(const nlohmann::json& record)
		{
			const auto kind = record.at("kind").get<std::string>();
			const char* title = kind == "quick_scan" ? "Quick scan" : kind == "feature_batch" ? "All features" :
			                                                      kind == "upscaling_sweep"   ? "Upscaling" :
			                                                                                    "Feature comparison";
			return record.at("started").get<std::string>() + "  " + title + "  (" + record.at("status").get<std::string>() + ")";
		}

		void SelectRun(const char* label, const PerformanceResults::View& view, bool comparison)
		{
			const auto& selected = comparison ? view.second : view.first;
			if (!ImGui::BeginCombo(label, selected ? Label(*selected).c_str() : "Select a saved run"))
				return;
			if (comparison && ImGui::Selectable("None", !selected))
				GetStore().Load({}, true);
			for (const auto& row : *view.history) {
				const auto id = row.at("id").get<std::string>();
				ImGui::PushID(id.c_str());
				if (ImGui::Selectable(Label(row).c_str(), selected && selected->at("id") == id))
					GetStore().Load(id, comparison);
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}

		void ContextLine(const nlohmann::json& context, const char* key, const char* label)
		{
			if (!context.contains(key))
				return;
			const auto& value = context.at(key);
			const auto text = value.is_string() ? value.get<std::string>() : value.dump();
			ImGui::TextWrapped("%s: %s", label, text.c_str());
		}

		void DrawContext(const nlohmann::json& record)
		{
			const auto& context = record.at("context");
			ContextLine(context, "runtime", "Runtime");
			ContextLine(context, "gameVersion", "Game version");
			ContextLine(context, "gpu", "Graphics card");
			ContextLine(context, "cpu", "Processor");
			ContextLine(context, "resolution", "Resolution");
			ImGui::TextWrapped("Scene: %s", PerformanceResults::SceneLabel(context).c_str());
			ContextLine(context, "gameHour", "Game hour");
			ContextLine(record, "elapsedSeconds", "Duration (seconds)");
			if (context.contains("build") && context.at("build").is_object()) {
				ContextLine(context.at("build"), "buildIdShort", "CSX build");
				ContextLine(context.at("build"), "sourceCommit", "Source commit");
				ContextLine(context.at("build"), "sourceDirty", "Modified source");
			}
			if (record.contains("fpsVR")) {
				ContextLine(record.at("fpsVR"), "status", "fpsVR");
				ContextLine(record.at("fpsVR"), "reason", "fpsVR note");
			}
		}

		void Number(const nlohmann::json& value)
		{
			if (value.is_number())
				ImGui::Text("%.3f", value.get<double>());
			else
				ImGui::TextDisabled("--");
		}

		void DrawResults(const PerformanceResults::View& view)
		{
			if (!view.first)
				return;
			const auto& record = *view.first;
			const bool quick = record.at("kind") == "quick_scan";
			SectionHeading(quick ? "Scan results" : "Measured cost", quick ?
																		 "Whole-frame CSX metrics and instrumented feature work are separate rows. Standard error uses only this run's repeat scans." :
																		 "Current minus Off/None, with standard error. FPS reports the loss relative to Off/None. Independent feature costs do not add up to a total.");
			if (record.contains("failure") && !record.at("failure").is_null())
				PageNotice(record.at("failure").get<std::string>(), NoticeKind::Error);
			static std::shared_ptr<const nlohmann::json> previousFirst, previousSecond;
			static nlohmann::json compared, rows;
			if (previousFirst != view.first || previousSecond != view.second) {
				previousFirst = view.first;
				previousSecond = view.second;
				compared = view.second ? PerformanceResults::Compare(record, *view.second) : nlohmann::json{};
				rows = view.second ? compared.at("rows") : PerformanceResults::SummaryMetrics(record);
			}
			if (view.second && (!compared.at("compatible").get<bool>() || !compared.at("contextMatches").get<bool>() || !compared.at("settingsMatch").get<bool>()))
				PageNotice("Saved runs use different settings or conditions.", NoticeKind::Pending,
					"Check the context before interpreting differences. Different runtimes, units or timing definitions are not subtracted. A difference is not a new significance test.");
			if (ImGui::BeginTable("##SavedPerformanceResults", view.second ? 6 : 5, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
				const SKSE::stl::scope_exit endTable([] { ImGui::EndTable(); });
				ImGui::TableSetupColumn("Feature / case", ImGuiTableColumnFlags_WidthStretch, 2);
				ImGui::TableSetupColumn("Metric");
				ImGui::TableSetupColumn(view.second ? "First" : "Value");
				ImGui::TableSetupColumn(view.second ? "Second" : "Std. error");
				if (view.second)
					ImGui::TableSetupColumn("Difference");
				ImGui::TableSetupColumn("Unit");
				ImGui::TableHeadersRow();
				for (const auto& row : rows) {
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextWrapped("%s", row.at("label").get_ref<const std::string&>().c_str());
					if (auto tooltip = Util::HoverTooltipWrapper()) {
						if (row.contains("failure") && !row["failure"].is_null())
							ImGui::TextWrapped("%s", row["failure"].get_ref<const std::string&>().c_str());
						ContextLine(row, "relativeTo", "Compared with");
						ContextLine(row, "current", "Current mean");
						ContextLine(row, "comparison", "Comparison mean");
						ContextLine(row, "missingSampleCount", "Missing samples");
						ContextLine(row, "pValue", "p-value");
					}
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(row.at("metric").get_ref<const std::string&>().c_str());
					ImGui::TableNextColumn();
					Number(row.value(view.second ? "first" : "value", nlohmann::json(nullptr)));
					ImGui::TableNextColumn();
					Number(row.value(view.second ? "second" : "standardError", nlohmann::json(nullptr)));
					if (view.second) {
						ImGui::TableNextColumn();
						Number(row.at("difference"));
					}
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(row.at("unit").get_ref<const std::string&>().c_str());
				}
			}
			if (ImGui::TreeNode("Run context")) {
				DrawContext(record);
				if (view.second) {
					SectionHeading("Second run");
					DrawContext(*view.second);
				}
				ImGui::TreePop();
			}
		}
	}

	void Draw()
	{
		auto& store = GetStore();
		static bool requested = false;
		if (!requested) {
			store.Refresh();
			requested = true;
		}
		const auto view = store.Snapshot();
		if (!view.error.empty())
			PageNotice(view.error, NoticeKind::Error);
		else if (!view.message.empty())
			PageNotice(view.message);
		if (view.busy)
			PageNotice(view.message == "Choose an export folder on the desktop." ? view.message : "Results task in progress.", NoticeKind::Pending);
		SectionHeading("Saved results", "Runs are kept across game restarts. HTML comparisons use only the current game session; older runs can be opened individually. Export opens a desktop folder picker; the default is SKSE/CSX/Performance. Reports are generated after measurements finish.");
		{
			const auto disabled = Util::DisableGuard(view.busy || PerformanceTuningRenderer::HasActiveMeasurements());
			SelectRun("Run", view, false);
			SelectRun("Compare with", view, true);
			static int format = 0;
			const char* formats[] = { "HTML + CSV", "HTML", "CSV" };
			ChoiceSetting("Export format", &format, formats, 3);
			{
				const auto noSelection = Util::DisableGuard(!view.first);
				if (ImGui::Button("Export..."))
					store.Export(view.first->at("id"), view.second ? view.second->at("id").get<std::string>() : "", format == 0 ? "both" : format == 1 ? "html" :
																																						 "csv");
			}
			ImGui::SameLine();
			if (ImGui::Button("Open folder")) {
				std::string error;
				if (!Util::OpenInShell(store.Folder(), error))
					PageNotice(error, NoticeKind::Error);
			}
			ImGui::SameLine();
			if (ImGui::Button("Refresh"))
				store.Refresh();
			if (view.unsaved && ImGui::Button("Retry saving results"))
				store.Retry();
			if (view.sessionRuns) {
				if (ImGui::Button("Open session HTML")) {
					std::string error;
					if (!Util::OpenInShell(std::filesystem::u8path(view.sessionReport), error))
						PageNotice(error, NoticeKind::Error);
				}
				ImGui::SameLine();
				if (ImGui::Button("Export session HTML..."))
					store.Export({}, {}, "html", {}, true);
				ImGui::TextWrapped("%zu measurements from this game session. Each run retains its own settings and scene details.", view.sessionRuns);
			}
		}
		if (view.history->empty() && !view.busy)
			ImGui::TextDisabled("No saved measurements yet.");
		DrawResults(view);
	}

	nlohmann::json Action(const nlohmann::json& args)
	{
		auto& store = GetStore();
		const auto action = args.at("action").get<std::string>();
		if (action != "results_status" && (PerformanceTuningRenderer::HasActiveMeasurements() || store.Busy()))
			return { { "accepted", false }, { "error", "measurement_or_results_busy" } };
		if (action == "refresh_results")
			store.Refresh();
		else if (action == "load_result")
			store.Load(args.at("resultId").get<std::string>(), args.value("comparison", false));
		else if (action == "export_result")
			store.Export(args.at("resultId").get<std::string>(), args.value("comparisonId", std::string{}),
				args.value("format", std::string("both")), std::filesystem::u8path(args.value("directory", std::string{})));
		else if (action == "retry_results")
			store.Retry();
		else if (action == "export_session")
			store.Export({}, {}, "html", std::filesystem::u8path(args.value("directory", std::string{})), true);
		const auto view = store.Snapshot();
		nlohmann::json status{ { "accepted", true }, { "busy", view.busy }, { "history", *view.history }, { "error", view.error },
			{ "message", view.message }, { "unsaved", view.unsaved }, { "folder", Util::PathToUtf8(store.Folder()) },
			{ "sessionRuns", view.sessionRuns }, { "sessionReport", view.sessionReport },
			{ "first", view.first ? *view.first : nlohmann::json(nullptr) }, { "second", view.second ? *view.second : nlohmann::json(nullptr) } };
		if (view.first && view.second)
			status["comparison"] = PerformanceResults::Compare(*view.first, *view.second);
		return status;
	}
}
