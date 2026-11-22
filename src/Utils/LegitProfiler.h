// Based on LegitProfiler by Raikiri (https://github.com/Raikiri/LegitProfiler)
// MIT License - modified to remove glm dependency, using ImVec2 directly.
#pragma once

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace legit
{
	namespace Colors
	{
#define RGBA_LE(col) (((col & 0xff000000) >> (3 * 8)) + ((col & 0x00ff0000) >> (1 * 8)) + ((col & 0x0000ff00) << (1 * 8)) + ((col & 0x000000ff) << (3 * 8)))
		const static uint32_t turqoise = RGBA_LE(0x1abc9cffu);
		const static uint32_t greenSea = RGBA_LE(0x16a085ffu);
		const static uint32_t emerald = RGBA_LE(0x2ecc71ffu);
		const static uint32_t nephritis = RGBA_LE(0x27ae60ffu);
		const static uint32_t peterRiver = RGBA_LE(0x3498dbffu);
		const static uint32_t belizeHole = RGBA_LE(0x2980b9ffu);
		const static uint32_t amethyst = RGBA_LE(0x9b59b6ffu);
		const static uint32_t wisteria = RGBA_LE(0x8e44adffu);
		const static uint32_t sunFlower = RGBA_LE(0xf1c40fffu);
		const static uint32_t orange = RGBA_LE(0xf39c12ffu);
		const static uint32_t carrot = RGBA_LE(0xe67e22ffu);
		const static uint32_t pumpkin = RGBA_LE(0xd35400ffu);
		const static uint32_t alizarin = RGBA_LE(0xe74c3cffu);
		const static uint32_t pomegranate = RGBA_LE(0xc0392bffu);
		const static uint32_t clouds = RGBA_LE(0xecf0f1ffu);
		const static uint32_t silver = RGBA_LE(0xbdc3c7ffu);
		const static uint32_t imguiText = RGBA_LE(0xF2F5FAFFu);
#undef RGBA_LE
	}

	struct ProfilerTask
	{
		double startTime;
		double endTime;
		std::string name;
		std::string displayName;
		uint32_t color;
		double GetLength() { return endTime - startTime; }
	};
}

namespace ImGuiUtils
{
	class ProfilerGraph
	{
	public:
		static constexpr float kLegendTextScale = 1.0f;
		// Spare graph height changes row spacing, not the body-sized labels.
		static constexpr float kMaximumLegendTextScale = kLegendTextScale;
		static constexpr float kLegendRowSpacing = 4.0f;
		static constexpr float kLegendBottomMargin = 3.0f;

		/** Row height for readable legend labels at the current UI font size. */
		static float GetLegendRowHeight(float uiScale)
		{
			return std::max(10.0f * uiScale, ImGui::GetFontSize() * kLegendTextScale) + kLegendRowSpacing * uiScale;
		}

		/** Preferred legend text size within the full graph height and stable pass slots. */
		float GetLegendTextScale(float height, float uiScale) const
		{
			return GetLegendLayout(height, uiScale).textScale;
		}

		int frameWidth;
		int frameSpacing;
		bool useColoredLegendText;

		ProfilerGraph(size_t framesCount)
		{
			frames.resize(framesCount);
			for (auto& frame : frames)
				frame.tasks.reserve(100);
			frameWidth = 3;
			frameSpacing = 1;
			useColoredLegendText = true;
		}

		void LoadFrameData(const legit::ProfilerTask* tasks, size_t count)
		{
			auto& currFrame = frames[currFrameIndex];
			currFrame.tasks.resize(0);
			currFrame.totalTime = 0.0f;
			for (size_t taskIndex = 0; taskIndex < count; taskIndex++) {
				if (taskIndex == 0)
					currFrame.tasks.push_back(tasks[taskIndex]);
				else {
					if (tasks[taskIndex - 1].color != tasks[taskIndex].color || tasks[taskIndex - 1].name != tasks[taskIndex].name)
						currFrame.tasks.push_back(tasks[taskIndex]);
					else
						currFrame.tasks.back().endTime = tasks[taskIndex].endTime;
				}
				currFrame.totalTime += float(tasks[taskIndex].endTime - tasks[taskIndex].startTime);
			}
			currFrame.taskStatsIndex.resize(currFrame.tasks.size());

			for (size_t taskIndex = 0; taskIndex < currFrame.tasks.size(); taskIndex++) {
				auto& task = currFrame.tasks[taskIndex];
				auto it = taskNameToStatsIndex.find(task.name);
				if (it == taskNameToStatsIndex.end()) {
					taskNameToStatsIndex[task.name] = taskStats.size();
					// A pass keeps its legend slot when timings or execution order change.
					taskStats.push_back({ taskStats.size(), size_t(-1) });
				}
				currFrame.taskStatsIndex[taskIndex] = taskNameToStatsIndex[task.name];
			}
			{
				float recentMax = 0.0f;
				size_t lookback = std::min(frames.size(), size_t(120));
				for (size_t i = 0; i < lookback; i++) {
					size_t idx = (currFrameIndex + frames.size() - 1 - i) % frames.size();
					recentMax = std::max(recentMax, frames[idx].totalTime);
				}
				if (peakFrameTime <= 0.0f)
					peakFrameTime = recentMax;
				else {
					float rate = (recentMax < peakFrameTime) ? 0.02f : 0.01f;
					peakFrameTime += (recentMax - peakFrameTime) * rate;
				}
			}
			currFrameIndex = (currFrameIndex + 1) % frames.size();
		}

		float GetTotalTaskTime(int frameIndexOffset)
		{
			return frames[GetCurrFrameIndex(frameIndexOffset)].totalTime;
		}

		float GetPeakFrameTime() const { return peakFrameTime; }

		/** Draw already-averaged pass timings without averaging their legend values again. */
		void RenderTimings(float graphWidth, float legendWidth, float height, int frameIndexOffset, float maxFrameTime, float uiScale = 1.0f, float legendTextScale = kLegendTextScale)
		{
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			const ImVec2 widgetPos = ImGui::GetCursorScreenPos();
			RenderGraph(drawList, widgetPos, ImVec2(graphWidth, height), frameIndexOffset, maxFrameTime, uiScale);
			if (legendWidth > 0.0f)
				RenderLegend(drawList, ImVec2(widgetPos.x + graphWidth, widgetPos.y), ImVec2(legendWidth, height), frameIndexOffset, maxFrameTime, uiScale, legendTextScale);
			ImGui::Dummy(ImVec2(graphWidth + legendWidth, height));
		}

	private:
		struct LegendLayout
		{
			size_t count = 0;
			float rowHeight = 0;
			float textScale = kLegendTextScale;
		};

		LegendLayout GetLegendLayout(float height, float uiScale) const
		{
			const float spacing = kLegendRowSpacing * uiScale;
			const float available = std::max(0.0f, height - 2 * kLegendBottomMargin * uiScale);
			const auto capacity = static_cast<size_t>((available + spacing) / GetLegendRowHeight(uiScale));
			const auto count = std::min(taskStats.size(), capacity);
			if (count == 0)
				return {};
			const float rowHeight = (available + spacing) / static_cast<float>(count);
			const float textScale = std::clamp((rowHeight - spacing) / ImGui::GetFontSize(), kLegendTextScale, kMaximumLegendTextScale);
			return { count, rowHeight, textScale };
		}

		size_t GetCurrFrameIndex(size_t frameIndexOffset) const
		{
			return (currFrameIndex - frameIndexOffset - 1 + 2 * frames.size()) % frames.size();
		}

		void RenderGraph(ImDrawList* drawList, ImVec2 graphPos, ImVec2 graphSize, size_t frameIndexOffset, float maxFrameTime, float uiScale)
		{
			ImVec4 borderColor = ImGui::GetStyleColorVec4(ImGuiCol_Border);
			borderColor.w *= 0.85f;
			Rect(drawList, graphPos, ImVec2(graphPos.x + graphSize.x, graphPos.y + graphSize.y), ImGui::GetColorU32(borderColor), false);
			const float scaledFrameSpacing = std::max(1.0f, static_cast<float>(frameSpacing) * uiScale);
			const float historyWidth = std::max(0.0f, graphSize.x - 2.0f * uiScale);
			if (frames.empty() || historyWidth <= 0.0f)
				return;
			// Stretch the retained history across the panel without changing its sample count.
			const float scaledFrameWidth = std::max(std::max(1.0f, static_cast<float>(frameWidth) * uiScale),
				(historyWidth + scaledFrameSpacing) / static_cast<float>(frames.size()) - scaledFrameSpacing);
			const float heightThreshold = uiScale;

			for (size_t frameNumber = 0; frameNumber < frames.size(); frameNumber++) {
				size_t frameIndex = GetCurrFrameIndex(frameIndexOffset + frameNumber);

				const float frameRight = graphPos.x + graphSize.x - uiScale - (scaledFrameWidth + scaledFrameSpacing) * float(frameNumber);
				if (frameRight <= graphPos.x + uiScale)
					break;
				ImVec2 framePos = ImVec2(std::max(graphPos.x + uiScale, frameRight - scaledFrameWidth), graphPos.y + graphSize.y - uiScale);
				ImVec2 taskPos = framePos;
				auto& frame = frames[frameIndex];
				for (const auto& task : frame.tasks) {
					float taskStartHeight = (float(task.startTime) / maxFrameTime) * graphSize.y;
					float taskEndHeight = (float(task.endTime) / maxFrameTime) * graphSize.y;
					if (std::abs(taskEndHeight - taskStartHeight) > heightThreshold)
						Rect(drawList, ImVec2(taskPos.x, taskPos.y - taskStartHeight), ImVec2(frameRight, taskPos.y - taskEndHeight), task.color, true);
				}
			}
		}

		void RenderLegend(ImDrawList* drawList, ImVec2 legendPos, ImVec2 legendSize, size_t frameIndexOffset, float maxFrameTime, float uiScale, float textScale)
		{
			const auto layout = GetLegendLayout(legendSize.y, uiScale);
			if (layout.count == 0)
				return;
			textScale = std::clamp(textScale, kLegendTextScale, layout.textScale);
			float markerLeftRectMargin = 3.0f * uiScale;
			float markerLeftRectWidth = 5.0f * uiScale;
			float markerMidWidth = 18.0f * uiScale;
			float markerRightRectWidth = 8.0f * uiScale;
			float markerRigthRectMargin = kLegendBottomMargin * uiScale;
			const float textHeight = ImGui::GetFontSize() * textScale;
			float markerRightRectSpacing = kLegendRowSpacing * uiScale;
			float markerRightRectHeight = layout.rowHeight - markerRightRectSpacing;
			ImVec2 textMargin = ImVec2(5.0f * uiScale, (markerRightRectHeight - textHeight) * 0.5f);
			ImVec4 legendTextColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
			legendTextColor.w *= 0.78f;
			const ImU32 mutedTextColor = ImGui::GetColorU32(legendTextColor);
			const float timeColumnWidth = std::max(48.0f * uiScale, CalcTextWidth("000.00ms", textScale) + textMargin.x);

			auto& currFrame = frames[GetCurrFrameIndex(frameIndexOffset)];

			for (auto& taskStat : taskStats)
				taskStat.onScreenIndex = size_t(-1);

			const size_t tasksToShow = layout.count;
			for (size_t taskIndex = 0; taskIndex < currFrame.tasks.size(); taskIndex++) {
				if (taskIndex >= currFrame.taskStatsIndex.size())
					continue;

				auto& task = currFrame.tasks[taskIndex];
				auto& stat = taskStats[currFrame.taskStatsIndex[taskIndex]];

				if (stat.legendIndex >= tasksToShow)
					continue;

				if (stat.onScreenIndex == size_t(-1))
					stat.onScreenIndex = stat.legendIndex;
				else
					continue;

				float taskStartHeight = (float(task.startTime) / maxFrameTime) * legendSize.y;
				float taskEndHeight = (float(task.endTime) / maxFrameTime) * legendSize.y;

				ImVec2 markerLeftRectMin = ImVec2(legendPos.x + markerLeftRectMargin, legendPos.y + legendSize.y);
				ImVec2 markerLeftRectMax = ImVec2(markerLeftRectMin.x + markerLeftRectWidth, markerLeftRectMin.y);
				markerLeftRectMin.y -= taskStartHeight;
				markerLeftRectMax.y -= taskEndHeight;

				ImVec2 markerRightRectMin = ImVec2(legendPos.x + markerLeftRectMargin + markerLeftRectWidth + markerMidWidth, legendPos.y + legendSize.y - markerRigthRectMargin - (markerRightRectHeight + markerRightRectSpacing) * float(stat.onScreenIndex));
				ImVec2 markerRightRectMax = ImVec2(markerRightRectMin.x + markerRightRectWidth, markerRightRectMin.y - markerRightRectHeight);
				RenderTaskMarker(drawList, markerLeftRectMin, markerLeftRectMax, markerRightRectMin, markerRightRectMax, task.color);

				uint32_t textColor = useColoredLegendText ? task.color : mutedTextColor;

				const double taskTime = task.endTime - task.startTime;
				const float taskTimeMs = float(taskTime * 1000.0);
				std::ostringstream timeText;
				timeText.precision(2);
				timeText << std::fixed << taskTimeMs << "ms";
				const std::string& label = task.displayName.empty() ? task.name : task.displayName;

				Text(drawList, ImVec2(markerRightRectMax.x + textMargin.x, markerRightRectMax.y + textMargin.y), textColor, timeText.str().c_str(), textScale);
				Text(drawList, ImVec2(markerRightRectMax.x + textMargin.x + timeColumnWidth, markerRightRectMax.y + textMargin.y), textColor, label.c_str(), textScale);
			}
		}

		static void Rect(ImDrawList* drawList, ImVec2 minPoint, ImVec2 maxPoint, uint32_t col, bool filled = true)
		{
			if (filled)
				drawList->AddRectFilled(minPoint, maxPoint, col);
			else
				drawList->AddRect(minPoint, maxPoint, col);
		}

		static void Text(ImDrawList* drawList, ImVec2 point, uint32_t col, const char* text, float scale = 1.0f)
		{
			ImFont* font = ImGui::GetFont();
			const float fontSize = ImGui::GetFontSize() * scale;
			drawList->AddText(font, fontSize, point, col, text);
		}

		static float CalcTextWidth(const char* text, float scale = 1.0f)
		{
			ImFont* font = ImGui::GetFont();
			const float fontSize = ImGui::GetFontSize() * scale;
			return font->CalcTextSizeA(fontSize, 1000000.0f, 0.0f, text).x;
		}

		static void RenderTaskMarker(ImDrawList* drawList, ImVec2 leftMinPoint, ImVec2 leftMaxPoint, ImVec2 rightMinPoint, ImVec2 rightMaxPoint, uint32_t col)
		{
			Rect(drawList, leftMinPoint, leftMaxPoint, col, true);
			Rect(drawList, rightMinPoint, rightMaxPoint, col, true);
			std::array<ImVec2, 4> points = {
				ImVec2(leftMaxPoint.x, leftMinPoint.y),
				ImVec2(leftMaxPoint.x, leftMaxPoint.y),
				ImVec2(rightMinPoint.x, rightMaxPoint.y),
				ImVec2(rightMinPoint.x, rightMinPoint.y)
			};
			drawList->AddConvexPolyFilled(points.data(), int(points.size()), col);
		}

		struct FrameData
		{
			std::vector<legit::ProfilerTask> tasks;
			std::vector<size_t> taskStatsIndex;
			float totalTime;
		};

		struct TaskStats
		{
			size_t legendIndex;
			size_t onScreenIndex;
		};

		std::vector<TaskStats> taskStats;
		std::map<std::string, size_t> taskNameToStatsIndex;
		std::vector<FrameData> frames;
		size_t currFrameIndex = 0;
		float peakFrameTime = 0.0f;
	};
}
